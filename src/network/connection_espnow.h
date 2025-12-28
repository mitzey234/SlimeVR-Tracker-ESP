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
#ifndef SLIMEVR_NETWORK_CONNECTION_ESPNOW_H_
#define SLIMEVR_NETWORK_CONNECTION_ESPNOW_H_

#include <Arduino.h>
#include <WiFiUdp.h>

#include <optional>

#include "../configuration/SensorConfig.h"
#include "featureflags.h"
#include "globals.h"
#include "packets.h"
#include "quat.h"
#include "sensors/sensor.h"
#include "wifihandler.h"

namespace SlimeVR::Network {

#define MUST_TRANSFER_BOOL(b) \
	if (!(b))                 \
		return false;

#define MUST(b) \
	if (!(b))   \
		return;

class ConnectionESPNOW {
public:
	ConnectionESPNOW() {
#ifdef SERVER_IP
		m_ServerHost.fromString(SERVER_IP);
#endif

#ifdef SERVER_PORT
		m_ServerPort = SERVER_PORT;
#endif
	}

	void searchForServer();
	void update();
	void reset();
	bool isConnected() const { return m_Connected; }
	void setTrackerRate(uint32_t rateHz) { m_TrackerRateHz = rateHz; }

	// Bundle support disabled for ESP-NOW - not compatible with binary packet protocol
	bool beginBundle() { return false; }
	bool endBundle() { return false; }

	// New binary packet system for ESP-NOW
	void sendPacket0_DeviceInfo(); // Every 100ms
	void sendPacket1_QuatAccel(const Quat& quat, const Vector3& accel); // On orientation change
	void sendPacket3_Status(); // Every second
	void sendPacket4_QuatMag(const Quat& quat, const Vector3& mag); // When mag data available (~200ms max)

	// Legacy compatibility methods (deprecated - use packet methods above)
	// PACKET_ACCEL 4
	void sendSensorAcceleration(uint8_t sensorId, Vector3 vector);

	// PACKET_BATTERY_LEVEL 12
	void sendBatteryLevel(float batteryVoltage, float batteryPercentage);

	// PACKET_TAP 13
	void sendSensorTap(uint8_t sensorId, uint8_t value);

	// PACKET_ERROR 14
	void sendSensorError(uint8_t sensorId, uint8_t error);

	// PACKET_ROTATION_DATA 17
	void sendRotationData(
		uint8_t sensorId,
		Quat* const quaternion,
		uint8_t dataType,
		uint8_t accuracyInfo
	);

	// PACKET_MAGNETOMETER_ACCURACY 18
	void sendMagnetometerAccuracy(uint8_t sensorId, float accuracyInfo);

	// PACKET_SIGNAL_STRENGTH 19
	void sendSignalStrength(uint8_t signalStrength);

	// PACKET_TEMPERATURE 20
	void sendTemperature(uint8_t sensorId, float temperature);

	// PACKET_FEATURE_FLAGS 22
	void sendFeatureFlags();

	// PACKET_FLEX_DATA 26
	void sendFlexData(uint8_t sensorId, float flexLevel);

#if ENABLE_INSPECTION
	void sendInspectionRawIMUData(
		uint8_t sensorId,
		int16_t rX,
		int16_t rY,
		int16_t rZ,
		uint8_t rA,
		int16_t aX,
		int16_t aY,
		int16_t aZ,
		uint8_t aA,
		int16_t mX,
		int16_t mY,
		int16_t mZ,
		uint8_t mA
	);
	void sendInspectionRawIMUData(
		uint8_t sensorId,
		float rX,
		float rY,
		float rZ,
		uint8_t rA,
		float aX,
		float aY,
		float aZ,
		uint8_t aA,
		float mX,
		float mY,
		float mZ,
		uint8_t mA
	);
#endif

	const ServerFeatures& getServerFeatureFlags() { return m_ServerFeatures; }

private:
	void updateSensorState(std::vector<std::unique_ptr<::Sensor>>& sensors);
	void maybeRequestFeatureFlags();
	bool isSensorStateUpdated(int i, std::unique_ptr<::Sensor>& sensor);

	bool beginPacket();
	bool endPacket();

	size_t write(const uint8_t* buffer, size_t size);
	size_t write(uint8_t byte);

	bool sendPacketType(SendPacketType type);
	bool sendPacketNumber();
	bool sendFloat(float f);
	bool sendByte(uint8_t c);
	bool sendShort(uint16_t i);
	bool sendInt(uint32_t i);
	bool sendLong(uint64_t l);
	bool sendBytes(const uint8_t* c, size_t length);
	bool sendShortString(const char* str);
	bool sendLongString(const char* str);

	template <typename Packet>
	bool sendPacket(
		SendPacketType type,
		Packet packet,
		std::optional<uint64_t> packetNumberOverride = std::nullopt
	) {
		MUST_TRANSFER_BOOL(beginPacket());
		MUST_TRANSFER_BOOL(sendPacketType(type));
		if (packetNumberOverride) {
			MUST_TRANSFER_BOOL(sendLong(*packetNumberOverride));
		} else {
			MUST_TRANSFER_BOOL(sendPacketNumber());
		}

		MUST_TRANSFER_BOOL(
			sendBytes(reinterpret_cast<uint8_t*>(&packet), sizeof(Packet))
		);

		return endPacket();
	}

	template <typename Callback>
	bool sendPacketCallback(
		SendPacketType type,
		Callback bodyCallback,
		std::optional<uint64_t> packetNumberOverride = std::nullopt
	) {
		MUST_TRANSFER_BOOL(beginPacket());
		MUST_TRANSFER_BOOL(sendPacketType(type));
		if (packetNumberOverride) {
			MUST_TRANSFER_BOOL(sendLong(*packetNumberOverride));
		} else {
			MUST_TRANSFER_BOOL(sendPacketNumber());
		}

		MUST_TRANSFER_BOOL(bodyCallback());

		return endPacket();
	}

	int getWriteError();

	void returnLastPacket(int len);

	// PACKET_HEARTBEAT 0
	void sendHeartbeat();

	// PACKET_HANDSHAKE 3
	void sendTrackerDiscovery();

	// PACKET_SENSOR_INFO 15
	void sendSensorInfo(::Sensor& sensor);

	void sendAcknowledgeConfigChange(uint8_t sensorId, SensorToggles configType);

	bool m_Connected = true;
	SlimeVR::Logging::Logger m_Logger = SlimeVR::Logging::Logger("UDPConnection");

	WiFiUDP m_UDP;
	unsigned char m_Packet[128];  // buffer for incoming packets
	uint64_t m_PacketNumber = 0;

	int m_ServerPort = 6969;
	IPAddress m_ServerHost = IPAddress(255, 255, 255, 255);
	unsigned long m_LastConnectionAttemptTimestamp;
	unsigned long m_LastPacketTimestamp;

	SensorStatus m_AckedSensorState[MAX_SENSORS_COUNT] = {SensorStatus::SENSOR_OFFLINE};
	SlimeVR::Configuration::SensorConfigBits m_AckedSensorConfigData[MAX_SENSORS_COUNT]
		= {};
	bool m_AckedSensorCalibration[MAX_SENSORS_COUNT] = {false};
	unsigned long m_LastSensorInfoPacketTimestamp = 0;
	unsigned long m_ErrorSendingWaitUntilTimestamp = 0;

	uint8_t m_FeatureFlagsRequestAttempts = 0;
	unsigned long m_FeatureFlagsRequestTimestamp = millis();
	ServerFeatures m_ServerFeatures{};

	bool m_IsBundle = false;
	uint16_t m_BundlePacketPosition = 0;
	uint16_t m_BundlePacketInnerCount = 0;

	unsigned char m_Buf[8];

	uint8_t peer_addr[6];

	// Data capture from legacy stub methods - reused in binary packets
	Quat m_LastQuat[MAX_SENSORS_COUNT] = {Quat(1, 0, 0, 0)};
	Vector3 m_LastAccel[MAX_SENSORS_COUNT] = {Vector3(0, 0, 0)};
	float m_LastTemperature[MAX_SENSORS_COUNT] = {0.0f};
	uint8_t m_LastError[MAX_SENSORS_COUNT] = {0};
	uint8_t m_LastAccuracy[MAX_SENSORS_COUNT] = {0};

	bool m_HasQuatData[MAX_SENSORS_COUNT] = {false};
	bool m_HasAccelData[MAX_SENSORS_COUNT] = {false};
	bool m_HasTemperature[MAX_SENSORS_COUNT] = {false};
	bool m_HasNewData[MAX_SENSORS_COUNT] = {false};

	uint32_t m_TrackerRateHz = 100; // Default 100Hz polling rate
};

}  // namespace SlimeVR::Network

#endif  // SLIMEVR_NETWORK_CONNECTION_ESPNOW_H_
