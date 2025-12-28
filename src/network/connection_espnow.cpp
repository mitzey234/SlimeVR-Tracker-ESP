/*
	SlimeVR Code is placed under the MIT license
	Copyright (c) 2023 SlimeVR Contributors

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.
*/

#include "connection_espnow.h"
#include "messages.h"

#if ESP8266
#include <espnow.h>
#else
#include <esp_now.h>
#endif

#include "GlobalVars.h"
#include "logging/Logger.h"
#include "packets.h"
#include "batterymonitor.h"

#define TIMEOUT 3000UL

// Magnetometer status constants for Packet 0
#define SVR_MAG_STATUS_NOT_SUPPORTED 0  // No magnetometer hardware
#define SVR_MAG_STATUS_DISABLED 1       // Magnetometer present but disabled
#define SVR_MAG_STATUS_ENABLED 2        // Magnetometer present and enabled

// Fixed-point conversion macros
#define TO_FIXED_15(x) ((int16_t)((x) * 32767.0f))  // ±1.0 range
#define TO_FIXED_10(x) ((int16_t)((x) * 1023.0f))   // ±32G range
#define TO_FIXED_7(x) ((int16_t)((x) * 127.0f))     // ±256m/s² range
#define SATURATE_UINT10(x) ((uint16_t)(((x) > 1023) ? 1023 : (x)))
#define SATURATE_UINT11(x) ((uint16_t)(((x) > 2047) ? 2047 : (x)))

namespace SlimeVR::Network {

// Helper function to encode battery level
// Bits 0-6: Battery percentage (0-100%)
// Bit 7: Battery availability flag (0x80)
// Returns 0 if no battery, or (0x80 | percentage) if battery present
static uint8_t encodeBatteryLevel(float level, bool batteryPresent) {
	if (!batteryPresent) {
		return 0; // No battery detected
	}
	uint8_t percentage = (uint8_t)(level * 100.0f);
	if (percentage > 100) percentage = 100;
	return 0x80 | percentage; // Battery present flag + percentage
}

// Helper function to encode battery voltage
// Range: 2.45V to 5.05V in 10mV increments (0-255)
// Formula: (voltage_mV / 10) - 245
static uint8_t encodeBatteryVoltage(float voltage) {
	int voltage_mV = (int)(voltage * 1000.0f);
	int encoded = (voltage_mV / 10) - 245;
	if (encoded < 0) encoded = 0;
	if (encoded > 255) encoded = 255;
	return (uint8_t)encoded;
}

bool ConnectionESPNOW::beginPacket() {
	memset(m_Packet, 0, sizeof(m_Packet));
	m_BundlePacketPosition = 0;
	return true;
}

bool ConnectionESPNOW::endPacket() {
	if (m_BundlePacketPosition == 0 || m_BundlePacketPosition > 16) {
		return false;
	}

	ESPNowPacketMessage espNowMessage;
	memcpy(espNowMessage.data, m_Packet, m_BundlePacketPosition);
	espNowMessage.len = m_BundlePacketPosition;

	auto result = esp_now_send(espNow.getInstance().gatewayAddress, (uint8_t*)&espNowMessage, 2 + espNowMessage.len);
#if ESP8266
	if (result != ERR_OK) {
#else
	if (result != ESP_OK) {
#endif
		Serial.printf("[ESPNOW] Error sending packet: %d\n", result);
		m_ErrorSendingWaitUntilTimestamp = millis() + 500; // Wait 500ms before next send attempt
		return false;
	}
	m_BundlePacketPosition = 0;
	return true;
}

size_t ConnectionESPNOW::write(uint8_t byte) { return write(&byte, 1); }

int ConnectionESPNOW::getWriteError() {
	return 0;
}

// Legacy packet method stubs - capture data for binary packet transmission
void ConnectionESPNOW::sendRotationData(uint8_t sensorId, Quat* const quaternion, uint8_t dataType, uint8_t accuracyInfo) {
	// Store the latest rotation data - will be sent in next update cycle via Packet 1 or 2
	if (quaternion) {
		m_LastQuat[sensorId] = *quaternion;
		m_HasQuatData[sensorId] = true;
		m_LastAccuracy[sensorId] = accuracyInfo;
		m_HasNewData[sensorId] = true; // Sensor detected movement/change
	}
}

void ConnectionESPNOW::sendSensorAcceleration(uint8_t sensorId, Vector3 vector) {
	// Store the latest acceleration data
	m_LastAccel[sensorId] = vector;
	m_HasAccelData[sensorId] = true;
}

void ConnectionESPNOW::sendSensorError(uint8_t sensorId, uint8_t error) {
	// Errors will be reflected in status packets
	m_LastError[sensorId] = error;
}

void ConnectionESPNOW::sendSensorTap(uint8_t sensorId, uint8_t value) {
	// Tap events not yet supported in binary protocol
	// TODO: Add tap event support if needed
}

void ConnectionESPNOW::sendTemperature(uint8_t sensorId, float temperature) {
	// Temperature is sent in Packet 0 and Packet 2
	m_LastTemperature[sensorId] = temperature;
	m_HasTemperature[sensorId] = true;
}

void ConnectionESPNOW::sendFlexData(uint8_t sensorId, float flexLevel) {
	// Flex data not yet supported in binary protocol
	// TODO: Add flex sensor support if needed
}

void ConnectionESPNOW::sendBatteryLevel(float voltage, float level) {
	// Battery data is sent in Packet 0 every 100ms and Packet 2 every 5s
	// Already handled by battery monitor directly, no need to cache
}

void ConnectionESPNOW::reset() {
	std::fill(m_AckedSensorState, m_AckedSensorState+MAX_SENSORS_COUNT, SensorStatus::SENSOR_OFFLINE);
	//statusManager.setStatus(SlimeVR::Status::SERVER_CONNECTING, true);
}

void ConnectionESPNOW::update() {
	auto nowMs = millis();
	auto nowUs = micros();

	// Static timing variables for packet scheduling
	static unsigned long lastPacket0Time = 0;
	static unsigned long lastPacket1Time = 0;
	static unsigned long lastPacket3Time = 0;
	static unsigned long lastPacket4Time = 0;
	static int primarySensorId = -1; // ID of the sensor we're using for data

	auto& sensors = sensorManager.getSensors();

	// Find and stick with first working sensor
	if (primarySensorId == -1 || primarySensorId >= (int)sensors.size() ||
	    !sensors[primarySensorId] || sensors[primarySensorId]->getSensorState() != SensorStatus::SENSOR_OK) {
		// Need to find a working sensor
		primarySensorId = -1;
		for (size_t i = 0; i < sensors.size(); i++) {
			if (sensors[i] && sensors[i]->getSensorState() == SensorStatus::SENSOR_OK) {
				primarySensorId = i;
				break;
			}
		}
	}

	// If no working sensor found, skip data packets
	if (primarySensorId == -1) {
		return;
	}

	// Packet 0: Device info every 250ms
	if (nowMs - lastPacket0Time >= 250) {
		sendPacket0_DeviceInfo();
		lastPacket0Time = nowMs;
	}

	// Packet 3: Status updates every 1 second
	if (nowMs - lastPacket3Time >= 1000) {
		sendPacket3_Status();
		lastPacket3Time = nowMs;
	}

	// Check if primary sensor has new data
	if (m_HasNewData[primarySensorId] && m_HasQuatData[primarySensorId] && m_HasAccelData[primarySensorId]) {
		// Packet 1: Full precision quat + accel (rate limited)
		uint32_t minIntervalUs = 1000000 / m_TrackerRateHz;
		if (nowUs - lastPacket1Time >= minIntervalUs && m_ErrorSendingWaitUntilTimestamp <= nowMs) {
			sendPacket1_QuatAccel(m_LastQuat[primarySensorId], m_LastAccel[primarySensorId]);
			lastPacket1Time = nowUs;
			m_HasNewData[primarySensorId] = false;
		}
	}

	// Packet 4: Magnetometer data (if available, max ~200ms interval)
	if (sensors[primarySensorId]->getAttachedMagnetometer() != nullptr && (nowMs - lastPacket4Time >= 200)) {
		// For now, skip mag data as we need proper magnetometer access
		// TODO: Add proper magnetometer data access
		// Vector3 mag = sensor->getMagnetometerData();
		// sendPacket4_QuatMag(m_LastQuat[primarySensorId], mag);
		lastPacket4Time = nowMs;
	}
}

// Packet 0: Device info (sent every 250ms)
void ConnectionESPNOW::sendPacket0_DeviceInfo() {
	beginPacket();
	m_Packet[0] = 0; // packet type
	m_Packet[1] = espNow.getInstance().trackerId; // tracker_id assigned by gateway

	// Battery encoding
	float battVoltage = battery.getVoltage();
	float battLevel = battery.getLevel();
	bool hasBattery = (battVoltage > 0);
	m_Packet[2] = encodeBatteryLevel(battLevel, hasBattery);
	m_Packet[3] = encodeBatteryVoltage(battVoltage);

	// Get sensors reference for temperature and magnetometer checks
	auto& sensors = sensorManager.getSensors();

	// Temperature from primary sensor (if available)
	uint8_t sensor_temp = 0;
	if (!sensors.empty() && sensors[0] && m_HasTemperature[0]) {
		// Temperature encoding: ((temp - 25) * 2 + 128.5)
		// Range: -38.5°C to +88.5°C, 0.5°C resolution
		// 0 = no data, 1-255 = valid range
		float temp_celsius = m_LastTemperature[0];
		float temp_encoded_f = ((temp_celsius - 25.0f) * 2.0f + 128.5f);
		int temp_encoded = (int)temp_encoded_f;
		if (temp_encoded < 1) temp_encoded = 1;
		if (temp_encoded > 255) temp_encoded = 255;
		sensor_temp = (uint8_t)temp_encoded;
	}
	m_Packet[4] = sensor_temp;

	m_Packet[5] = BOARD; // board_id
	m_Packet[6] = HARDWARE_MCU; // mcu_id
	m_Packet[7] = 0; // reserved
	m_Packet[8] = (uint8_t)sensorManager.getSensorType(0); // imu_id

	// Magnetometer status - check if magnetometer is attached
	uint8_t mag_status = SVR_MAG_STATUS_NOT_SUPPORTED;
	if (!sensors.empty() && sensors[0]) {
		const char* magType = sensors[0]->getAttachedMagnetometer();
		if (magType != nullptr) {
			// Magnetometer hardware present - assume enabled (default behavior)
			mag_status = SVR_MAG_STATUS_ENABLED;
		}
	}
	m_Packet[9] = mag_status;

	// Firmware date and version - compute once at compile time
	static uint16_t fw_date = []() {
		// Parse __DATE__ macro: "MMM DD YYYY" (e.g., "Dec 27 2025")
		static const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
		char month_str[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
		int build_month = ((strstr(month_names, month_str) - month_names) / 3) + 1;
		int build_day = (__DATE__[4] == ' ' ? __DATE__[5] - '0' : (__DATE__[4] - '0') * 10 + (__DATE__[5] - '0'));
		int build_year = ((__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 + (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0'));
		return ((build_year - 2020) & 127) << 9 | (build_month & 15) << 5 | (build_day & 31);
	}();

	static uint8_t fw_version[3] = {0};
	static bool fw_version_parsed = false;
	if (!fw_version_parsed) {
		sscanf(FIRMWARE_VERSION, "%hhu.%hhu.%hhu", &fw_version[0], &fw_version[1], &fw_version[2]);
		fw_version_parsed = true;
	}

	m_Packet[10] = (fw_date >> 0) & 0xFF;
	m_Packet[11] = (fw_date >> 8) & 0xFF;
	m_Packet[12] = fw_version[0];
	m_Packet[13] = fw_version[1];
	m_Packet[14] = fw_version[2];

	m_Packet[15] = 0; // rssi (filled by receiver)

	m_BundlePacketPosition = 16;
	endPacket();
}

// Packet 1: Full precision quaternion and accel (sent on orientation change)
void ConnectionESPNOW::sendPacket1_QuatAccel(const Quat& quat, const Vector3& accel) {
	beginPacket();
	m_Packet[0] = 1; // packet type
	m_Packet[1] = espNow.getInstance().trackerId;

	uint16_t* buf = (uint16_t*)&m_Packet[2];
	buf[0] = TO_FIXED_15(quat.x); // qx
	buf[1] = TO_FIXED_15(quat.y); // qy
	buf[2] = TO_FIXED_15(quat.z); // qz
	buf[3] = TO_FIXED_15(quat.w); // qw
	buf[4] = TO_FIXED_7(accel.x); // ax (±256m/s²)
	buf[5] = TO_FIXED_7(accel.y); // ay
	buf[6] = TO_FIXED_7(accel.z); // az

	m_BundlePacketPosition = 16;
	endPacket();
}

// Packet 3: Status (sent every second)
void ConnectionESPNOW::sendPacket3_Status() {
	beginPacket();
	m_Packet[0] = 3; // packet type
	m_Packet[1] = espNow.getInstance().trackerId;
	m_Packet[2] = m_Connected ? 1 : 0; // server status
	m_Packet[3] = statusManager.getStatus(); // tracker status
	m_Packet[15] = 0; // rssi (filled by receiver)

	m_BundlePacketPosition = 16;
	endPacket();
}

// Packet 4: Full precision quat and magnetometer (sent when mag data available, ~200ms max)
void ConnectionESPNOW::sendPacket4_QuatMag(const Quat& quat, const Vector3& mag) {
	beginPacket();
	m_Packet[0] = 4; // packet type
	m_Packet[1] = espNow.getInstance().trackerId;

	uint16_t* buf = (uint16_t*)&m_Packet[2];
	buf[0] = TO_FIXED_15(quat.x); // qx
	buf[1] = TO_FIXED_15(quat.y); // qy
	buf[2] = TO_FIXED_15(quat.z); // qz
	buf[3] = TO_FIXED_15(quat.w); // qw
	buf[4] = TO_FIXED_10(mag.x); // mx (±32G)
	buf[5] = TO_FIXED_10(mag.y); // my
	buf[6] = TO_FIXED_10(mag.z); // mz

	m_BundlePacketPosition = 16;
	endPacket();
}

}  // namespace SlimeVR::Network
