#include "GlobalVars.h"
#include "globals.h"
#if !ESP8266
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#endif

#include "network/espnowhandler.h"
#include "messages.h"

namespace SlimeVR {

// Define the static instance
ESPNow ESPNow::instance;

ESPNow &ESPNow::getInstance() {
    return instance;
}

unsigned int &ESPNow::getChannel() {
	return getInstance().channel;
}

void ESPNow::setUp() {
	Serial.println("[ESPNow] Setting up ESPNow");
	if (initialized) {
		Connect();
		return;
	}

	channel = 0;

	WiFi.mode(WIFI_STA);
#if !ESP8266
	esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11G);
	//esp_wifi_set_max_tx_power(WIFI_POWER_2dBm);
	WiFi.setChannel(channel);

	rate_config.phymode = WIFI_PHY_MODE_HT20;
    rate_config.rate = WIFI_PHY_RATE_MCS7_SGI;
    rate_config.ersu = false;
#else
	WiFi.setPhyMode(WIFI_PHY_MODE_11G);
	//WiFi.setOutputPower(19.5);
	wifi_set_channel(getChannel());
#endif

	auto startState = esp_now_init();
#if ESP8266
	if (startState == ERR_OK) {
#else
	if (startState == ESP_OK) {
#endif
		Serial.println("[ESPNow] Init Success");
	} else {
		Serial.println("[ESPNow] Init Failed: " + String(startState));
		setState(GatewayStatus::Failed);
		ESP.restart();
	}

#if ESP8266
	// ESP8266 requires setting self role before adding peers
	esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
#endif

	esp_now_register_recv_cb(OnDataRecv);
	hasGatewayAddress = false;
	LastHeartbeatSendTime = 0;
	HeartbeatSentTimestamp = 0;
	WaitingForHeartbeatResponse = false;
	LastPairingRequestTime = 0;
	LastHandshakeRequestTime = 0;
	PairingStartTime = 0;
	LastPacketSendTime = 0;
	MissedHeartbeats = 0;

	uint8_t macaddr[6];
	WiFi.macAddress(macaddr);

	Serial.printf(
		"[ESPNOW] address: %02x:%02x:%02x:%02x:%02x:%02x\n",
		macaddr[0],
		macaddr[1],
		macaddr[2],
		macaddr[3],
		macaddr[4],
		macaddr[5]
	);

#if ESP8266
#if POWERSAVING_MODE == POWER_SAVING_NONE
	WiFi.setSleepMode(WIFI_NONE_SLEEP);
#elif POWERSAVING_MODE == POWER_SAVING_MINIMUM
	WiFi.setSleepMode(WIFI_MODEM_SLEEP);
#elif POWERSAVING_MODE == POWER_SAVING_MODERATE
	WiFi.setSleepMode(WIFI_MODEM_SLEEP, 10);
#elif POWERSAVING_MODE == POWER_SAVING_MAXIMUM
	WiFi.setSleepMode(WIFI_LIGHT_SLEEP, 10);
#error "MAX POWER SAVING NOT WORKING YET, please disable!"
#endif
#else
#if POWERSAVING_MODE == POWER_SAVING_NONE
	WiFi.setSleep(WIFI_PS_NONE);
#elif POWERSAVING_MODE == POWER_SAVING_MINIMUM
	WiFi.setSleep(WIFI_PS_MIN_MODEM);
#elif POWERSAVING_MODE == POWER_SAVING_MODERATE \
	|| POWERSAVING_MODE == POWER_SAVING_MAXIMUM
	wifi_config_t conf;
	if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
		conf.sta.listen_interval = 10;
		esp_wifi_set_config(WIFI_IF_STA, &conf);
		WiFi.setSleep(WIFI_PS_MAX_MODEM);
		espnowHandlerLogger.info("Power saving enabled with listen interval %d", conf.sta.listen_interval);
	} else {
		espnowHandlerLogger.error("Unable to get WiFi config, power saving not enabled!");
	}
#endif
#endif
	Connect();
}

const uint8_t* ESPNow::getGateway() {
	return configuration.getESPNowGatewayAddress();
}

const uint8_t* ESPNow::getSecurityCode() {
	return configuration.getESPNowSecurityCode();
}

bool ESPNow::isConnected() const {
	return state == GatewayStatus::Connected && hasGatewayAddress;
}

#if ESP8266
void ESPNow::OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
#else
void ESPNow::OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
	// Adapt ESP32 parameters to ESP8266 format for existing handlers
	uint8_t mac_copy[6];
	memcpy(mac_copy, esp_now_info->src_addr, 6);
	uint8_t *mac = mac_copy;

	// Cast away const and convert int len to uint8_t for compatibility
	uint8_t *data = const_cast<uint8_t*>(incomingData);
	uint8_t len8 = (uint8_t)len;
#endif
	// Serial.printf(
	// 	"[ESPNOW] Received %d bytes from %02x:%02x:%02x:%02x:%02x:%02x: ",
	// 	len,
	// 	mac[0],
	// 	mac[1],
	// 	mac[2],
	// 	mac[3],
	// 	mac[4],
	// 	mac[5]
	// );
	// for (int i = 0; i < len; i++) {
	// 	Serial.printf("%02x", incomingData[i]);
	// 	if (i < len - 1) Serial.print(" ");
	// }
	// Serial.println();

#if ESP8266
	switch (incomingData[0]) {
		case static_cast<uint8_t>(ESPNowMessageTypes::PAIRING_ANNOUNCEMENT):
			instance.HandlePairingAnnouncement(mac, incomingData, len);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::PAIRING_RESPONSE):
			instance.HandlePairingResponse(mac, incomingData, len);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::HANDSHAKE_RESPONSE):
			instance.HandleHandshakeResponse(mac, incomingData, len);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::HEARTBEAT_ECHO):
			instance.HandleHeartbeatEcho(mac, incomingData, len);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::HEARTBEAT_RESPONSE):
			instance.HandleHeartbeatResponse(mac, incomingData, len);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::UNPAIR):
			instance.HandleUnpair(mac, incomingData, len);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::TRACKER_RATE):
			instance.HandleTrackerRate(mac, incomingData, len);
			break;
		default:
			Serial.println("[ESPNOW] Unknown message type received");
			break;
	}
#else
	switch (data[0]) {
		case static_cast<uint8_t>(ESPNowMessageTypes::PAIRING_ANNOUNCEMENT):
			instance.HandlePairingAnnouncement(mac, data, len8);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::PAIRING_RESPONSE):
			instance.HandlePairingResponse(mac, data, len8);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::HANDSHAKE_RESPONSE):
			instance.HandleHandshakeResponse(mac, data, len8);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::HEARTBEAT_ECHO):
			instance.HandleHeartbeatEcho(mac, data, len8);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::HEARTBEAT_RESPONSE):
			instance.HandleHeartbeatResponse(mac, data, len8);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::UNPAIR):
			instance.HandleUnpair(mac, data, len8);
			break;
		case static_cast<uint8_t>(ESPNowMessageTypes::TRACKER_RATE):
			instance.HandleTrackerRate(mac, data, len8);
			break;
		default:
			Serial.println("[ESPNOW] Unknown message type received");
			break;
	}
#endif
}

void ESPNow::HandlePairingAnnouncement(uint8_t * mac, uint8_t *data, uint8_t len) {
	// Handle pairing announcement logic here
	if (state != GatewayStatus::Pairing || hasGatewayAddress) return;

	if (len != sizeof(ESPNowPairingAnnouncementMessage)) {
		Serial.printf("[ESPNOW] Invalid pairing announcement message length: expected %d, got %d\n", sizeof(ESPNowPairingAnnouncementMessage), len);
		return;
	}
	Serial.println("[ESPNOW] Handling pairing announcement...");
	auto& message = *reinterpret_cast<ESPNowPairingAnnouncementMessage*>(data);
	Serial.printf(
		"[ESPNOW] Security bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
		message.securityBytes[0],
		message.securityBytes[1],
		message.securityBytes[2],
		message.securityBytes[3],
		message.securityBytes[4],
		message.securityBytes[5],
		message.securityBytes[6],
		message.securityBytes[7]
	);

	//save security code
	memcpy(securityCode, message.securityBytes, 8);
	memcpy(gatewayAddress, mac, 6);

	unsigned int announcedChannel = message.channel;
	Serial.printf("[ESPNOW] Found gateway %02x:%02x:%02x:%02x:%02x:%02x on channel: %d\n",
		gatewayAddress[0], gatewayAddress[1], gatewayAddress[2],
		gatewayAddress[3], gatewayAddress[4], gatewayAddress[5],
		announcedChannel
	);

	channel = announcedChannel;
#if !ESP8266
	WiFi.setChannel(announcedChannel);
#else
	wifi_set_channel(announcedChannel);
#endif

	// Add peer for communication
#if ESP8266
	auto addResult = esp_now_add_peer(gatewayAddress, ESP_NOW_ROLE_COMBO, announcedChannel, NULL, 0);
	if (addResult != ERR_OK) {
		Serial.printf("[ESPNOW] Failed to add peer: %d\n", addResult);
		return;
	}
#else
	// ESP32 requires esp_now_peer_info_t structure
	esp_now_peer_info_t peerInfo = {};
	memcpy(peerInfo.peer_addr, gatewayAddress, 6);
	peerInfo.channel = announcedChannel;  // Use announced channel
	peerInfo.encrypt = false;  // No encryption for now

	auto addResult = esp_now_add_peer(&peerInfo);
	if (addResult != ESP_OK) {
		Serial.printf("[ESPNOW] Failed to add peer: %d\n", addResult);
		return;
	} else {
		esp_now_set_peer_rate_config(peerInfo.peer_addr, &rate_config);
	}
#endif

	hasGatewayAddress = true;
	Serial.println("[ESPNOW] Attempting to pair with gateway...");
	PairingStartTime = millis();
}

void ESPNow::SendPairingRequest() {
	if (!hasGatewayAddress) {
		Serial.println("[ESPNOW] No gateway address set, cannot send pairing request");
		return;
	}
	ESPNowPairingMessage pairRequest;
	memcpy(pairRequest.securityBytes, securityCode, 8);
	auto result = esp_now_send(gatewayAddress, reinterpret_cast<uint8_t*>(&pairRequest), sizeof(ESPNowPairingMessage));
#if ESP8266
	if (result != ERR_OK) {
#else
	if (result != ESP_OK) {
#endif
		Serial.println("[ESPNOW] Error sending pairing request: " + String(result));
		return;
	}
	Serial.println("[ESPNOW] Pairing request sent to: " +
		String(gatewayAddress[0], HEX) + ":" +
		String(gatewayAddress[1], HEX) + ":" +
		String(gatewayAddress[2], HEX) + ":" +
		String(gatewayAddress[3], HEX) + ":" +
		String(gatewayAddress[4], HEX) + ":" +
		String(gatewayAddress[5], HEX)
	);
}

void ESPNow::HandlePairingResponse(uint8_t * mac, uint8_t *data, uint8_t len) {
	if (state != GatewayStatus::Pairing || !hasGatewayAddress) return;
	// Handle pairing response logic here
	Serial.println("[ESPNOW] Successfully paired with gateway, establishing connection...");

	// Save security code and gateway address to persistent storage
	configuration.setESPNowGateway(gatewayAddress, securityCode);
	Serial.printf(
		"[ESPNOW] Saved gateway %02x:%02x:%02x:%02x:%02x:%02x to configuration\n",
		gatewayAddress[0], gatewayAddress[1], gatewayAddress[2],
		gatewayAddress[3], gatewayAddress[4], gatewayAddress[5]
	);

	esp_now_del_peer(gatewayAddress);

	channel--;

	// For simplicity, assume pairing is always successful
	setState(GatewayStatus::Connecting);
}

void ESPNow::SendHandshakeRequest() {
	if (!hasGatewayAddress) {
		Serial.println("[ESPNOW] No gateway address set, cannot send handshake request");
		return;
	}
	ESPNowConnectionMessage handshakeRequest;
	memcpy(handshakeRequest.securityBytes, securityCode, 8);
	auto result = esp_now_send(gatewayAddress, reinterpret_cast<uint8_t*>(&handshakeRequest), sizeof(ESPNowConnectionMessage));
#if ESP8266
	if (result != ERR_OK) {
#else
	if (result != ESP_OK) {
#endif
		Serial.println("[ESPNOW] Error sending handshake request: " + String(result));
		return;
	}
}

void ESPNow::HandleHandshakeResponse(uint8_t * mac, uint8_t *data, uint8_t len) {
	if (state != GatewayStatus::Connecting || !hasGatewayAddress) return;
	// Handle handshake response logic here
	Serial.println("[ESPNOW] Handshake response received, connection established");

	auto& ackMessage = *reinterpret_cast<ESPNowConnectionAckMessage*>(data);
	Serial.printf("[ESPNOW] Assigned channel: %d and tracker ID: %d\n", ackMessage.channel, ackMessage.trackerId);

	// Store the assigned tracker ID
	trackerId = ackMessage.trackerId;

	// Initialize heartbeat system
	LastHeartbeatSendTime = 0;
	WaitingForHeartbeatResponse = false;
	MissedHeartbeats = 0;

	channel = ackMessage.channel;

#if !ESP8266
	WiFi.setChannel(channel);
#else
	wifi_set_channel(channel);
#endif

	setState(GatewayStatus::Connected);
}

void ESPNow::SendHeartbeat() {
	if (!hasGatewayAddress) {
		Serial.println("[ESPNOW] No gateway address set, cannot send heartbeat");
		return;
	}

	// Generate random 16-bit sequence number
	HeartbeatSequenceNumber = (uint16_t)random(0, 65536);
	ESPNowHeartbeatEchoMessage heartbeat;
	heartbeat.sequenceNumber = HeartbeatSequenceNumber;
	auto result = esp_now_send(gatewayAddress, reinterpret_cast<uint8_t*>(&heartbeat), sizeof(ESPNowHeartbeatEchoMessage));
#if ESP8266
	if (result != ERR_OK) {
#else
	if (result != ESP_OK) {
#endif
		Serial.printf("[ESPNOW] Error sending heartbeat: %d\n", result);
		return;
	}

	HeartbeatSentTimestamp = millis();
	WaitingForHeartbeatResponse = true;
}

void ESPNow::HandleHeartbeatResponse(uint8_t * mac, uint8_t *data, uint8_t len) {
	if (state != GatewayStatus::Connected || !hasGatewayAddress) return;
	if (!WaitingForHeartbeatResponse) {
		//Serial.println("[ESPNOW] Received unexpected heartbeat response");
		return;
	}

	if (len != sizeof(ESPNowHeartbeatResponseMessage)) {
		//Serial.printf("[ESPNOW] Invalid heartbeat response length: expected %d, got %d\n", sizeof(ESPNowHeartbeatResponseMessage), len);
		return;
	}

	auto& responseMessage = *reinterpret_cast<ESPNowHeartbeatResponseMessage*>(data);
	uint16_t receivedSeq = responseMessage.sequenceNumber;

	if (receivedSeq != HeartbeatSequenceNumber) {
		//Serial.printf("[ESPNOW] Heartbeat sequence mismatch: expected %u, got %u\n", HeartbeatSequenceNumber, receivedSeq);
		return;
	}

	unsigned long latency = millis() - HeartbeatSentTimestamp;
	Serial.printf("[ESPNOW] Heartbeat response received - Seq: %u, Latency: %lu ms\n", receivedSeq, latency);	WaitingForHeartbeatResponse = false;
	MissedHeartbeats = 0;
}

void ESPNow::HandleHeartbeatEcho(uint8_t * mac, uint8_t *data, uint8_t len) {
	if (state != GatewayStatus::Connected || !hasGatewayAddress || memcmp(mac, gatewayAddress, 6) != 0) return;

	if (len != sizeof(ESPNowHeartbeatEchoMessage)) {
		Serial.printf("[ESPNOW] Invalid heartbeat echo length: expected %d, got %d\n", sizeof(ESPNowHeartbeatEchoMessage), len);
		return;
	}

	MissedHeartbeats = 0;

	ESPNowHeartbeatResponseMessage heartbeatResponse;
	auto& echoMessage = *reinterpret_cast<ESPNowHeartbeatEchoMessage*>(data);
	heartbeatResponse.sequenceNumber = echoMessage.sequenceNumber;
	auto result = esp_now_send(mac, reinterpret_cast<uint8_t*>(&heartbeatResponse), sizeof(ESPNowHeartbeatResponseMessage));
#if ESP8266
	if (result != ERR_OK) {
#else
	if (result != ESP_OK) {
#endif
		Serial.println("[ESPNOW] Error sending heartbeat response: " + String(result));
		return;
	}
}

void ESPNow::HandleUnpair(uint8_t * mac, uint8_t *data, uint8_t len) {
	if (!hasGatewayAddress) return;

	if (len != sizeof(ESPNowUnpairMessage)) {
		Serial.printf("[ESPNOW] Invalid unpair message length: expected %d, got %d\n", sizeof(ESPNowUnpairMessage), len);
		return;
	}

	// Verify MAC address matches current gateway
	if (memcmp(mac, gatewayAddress, 6) != 0) {
		Serial.println("[ESPNOW] Unpair request from unknown address, ignoring");
		return;
	}

	auto& message = *reinterpret_cast<ESPNowUnpairMessage*>(data);

	// Verify security code matches
	if (memcmp(message.securityBytes, securityCode, 8) != 0) {
		Serial.println("[ESPNOW] Unpair request with invalid security code, ignoring");
		return;
	}

	Serial.println("[ESPNOW] Received valid unpair request from gateway");

	// Remove peer
	esp_now_del_peer(gatewayAddress);

	// Clear gateway address and security code from memory
	memset(gatewayAddress, 0, 6);
	memset(securityCode, 0, 8);
	hasGatewayAddress = false;

	configuration.clearESPNowGateway();

	// Clear from persistent storage
	configuration.setESPNowGateway(nullptr, nullptr);

	Serial.println("[ESPNOW] Unpaired from gateway, entering pairing mode");

	// Return to pairing mode to find new gateway
	Pairing();
}

void ESPNow::HandleTrackerRate(uint8_t * mac, uint8_t *data, uint8_t len) {
	if (!this->hasGatewayAddress) return;

	if (len != sizeof(ESPNowTrackerRateMessage)) {
		Serial.printf("[ESPNOW] Invalid tracker rate message length: expected %d, got %d\n", sizeof(ESPNowTrackerRateMessage), len);
		return;
	}

	// Verify MAC address matches current gateway
	if (memcmp(mac, this->gatewayAddress, 6) != 0) {
		Serial.println("[ESPNOW] Tracker rate request from unknown address, ignoring");
		return;
	}

	auto& message = *reinterpret_cast<ESPNowTrackerRateMessage*>(const_cast<uint8_t*>(data));

	Serial.printf("[ESPNOW] Received tracker rate request: %u Hz\n", message.rateHz);

	// Forward to network connection to update rate limiting
	networkConnection.setTrackerRate(message.rateHz);
}

void ESPNow::Connect () {
	setState(GatewayStatus::SearchingForGateway);
}

void ESPNow::Pairing() {
	hasGatewayAddress = false;
	setState(GatewayStatus::Pairing);
}

void ESPNow::setState (GatewayStatus newState) {
	if (state == newState) return;
	state = newState;
	switch (state) {
		case GatewayStatus::NotSetup:
			Serial.println("[ESPNow]: Not set up");
			break;
		case GatewayStatus::SearchingForGateway: {
			Serial.println("[ESPNow]: Searching for gateway");
			const uint8_t* gateway = getGateway();
			const uint8_t* security = getSecurityCode();
			if (gateway == nullptr || security == nullptr) {
				Serial.println("[ESPNow]: No gateway address found, entering pairing mode");
				Pairing();
				break;
			}
			Serial.println("[ESPNow]: Gateway address found, connecting...");
			memcpy(gatewayAddress, gateway, 6);
			memcpy(securityCode, security, 8);
			hasGatewayAddress = true;
			statusManager.setStatus(SlimeVR::Status::WIFI_CONNECTING, false);
			statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, false);
			setState(GatewayStatus::Connecting);
			break;
		}
		case GatewayStatus::Connecting:
			Serial.println("[ESPNow]: Connecting to gateway");
			statusManager.setStatus(SlimeVR::Status::WIFI_CONNECTING, true);
			statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, false);
#if ESP8266
			esp_now_add_peer(gatewayAddress, ESP_NOW_ROLE_COMBO, 0, NULL, 0);
#else
			{
				esp_now_peer_info_t peerInfo = {};
				memcpy(peerInfo.peer_addr, gatewayAddress, 6);
				peerInfo.channel = 0;
				peerInfo.encrypt = false;
				esp_now_add_peer(&peerInfo);
				esp_now_set_peer_rate_config(peerInfo.peer_addr, &rate_config);
			}
#endif
			break;
		case GatewayStatus::Pairing:
			if (!hasGatewayAddress) {
				Serial.println("[ESPNow]: Starting Pairing mode");
				esp_now_del_peer(gatewayAddress);
				std::fill_n(gatewayAddress, 6, 0);
				std::fill_n(securityCode, 8, 0);
				statusManager.setStatus(SlimeVR::Status::WIFI_CONNECTING, false);
				statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, true);
				PairingStartTime = millis();
			}
			break;
		case GatewayStatus::Connected:
			Serial.println("[ESPNow]: Connected to gateway");
			statusManager.setStatus(SlimeVR::Status::WIFI_CONNECTING, false);
			statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, false);
			break;
		case GatewayStatus::Failed:
			Serial.println("[ESPNow]: failed");
			break;
	}
}

void ESPNow::upkeep () {
	auto now = millis();
	switch (state) {
		case GatewayStatus::NotSetup:
			break;
		case GatewayStatus::SearchingForGateway: {
			break;
		}
		case GatewayStatus::Connecting:
			// Try to handshake with gateway
			if (hasGatewayAddress) {
				if (now - LastChannelSwitchTime >= 350) {
					LastChannelSwitchTime = now;
					if (channel == 0) {
						channel = 1;
					} else if (channel > 0 && channel < 11) {
						channel = channel + 1;
					} else if (channel >= 11) {
						channel = 1;
					}
#if !ESP8266
					WiFi.setChannel(channel);
#else
					wifi_set_channel(channel);
#endif
					Serial.printf("[ESPNow]: Connect gateway via channel %d\n", channel);
				}
				if (now - LastHandshakeRequestTime < 500) {
					//Don't spam requests
					break;
				}
				LastHandshakeRequestTime = now;
				SendHandshakeRequest();
			} else {
				setState(GatewayStatus::SearchingForGateway);
			}
			break;
		case GatewayStatus::Pairing:
			// Try to pair with gateway
			if (!hasGatewayAddress && (now - LastChannelSwitchTime) >= 350) {
				LastChannelSwitchTime = now;
				if (channel == 0) {
					channel = 1;
				} else if (channel > 0 && channel < 11) {
					channel = channel + 1;
				} else if (channel >= 11) {
					channel = 1;
				}
#if !ESP8266
				WiFi.setChannel(channel);
#else
				wifi_set_channel(channel);
#endif
				Serial.printf("[ESPNow]: Scanning channel %d for gateway\n", channel);
			}

			if (now - PairingStartTime > 60000) {
				Serial.println("[ESPNow]: Pairing timed out, restarting search for gateway");
				if (hasGatewayAddress) esp_now_del_peer(gatewayAddress);
				Connect();
			} else if (hasGatewayAddress) {
				if (now - LastPairingRequestTime < 500) {
					//Don't spam requests
					break;
				}
				LastPairingRequestTime = now;
				SendPairingRequest();
			}
			break;
		case GatewayStatus::Connected:
			// Maintain connection with heartbeat
			// Only run heartbeat logic once per second
			if (now - LastHeartbeatSendTime >= 1000) {
				// Check if we're waiting for a response
				if (WaitingForHeartbeatResponse) {
					// Check if timeout exceeded (1000ms)
					if (now - HeartbeatSentTimestamp >= 1000) {
						MissedHeartbeats++;
						//Serial.printf("[ESPNOW] Heartbeat timeout - Missed: %d/3\n", MissedHeartbeats);

						if (MissedHeartbeats >= 5) {
							Serial.println("[ESPNOW] Connection lost - 5 heartbeats missed");
							channel--;
							if (hasGatewayAddress) esp_now_del_peer(gatewayAddress);
							setState(GatewayStatus::Connecting);
							break;
						}

						WaitingForHeartbeatResponse = false;
					}
				}

				// Send heartbeat if not waiting for response
				if (!WaitingForHeartbeatResponse) {
					LastHeartbeatSendTime = now;
					SendHeartbeat();
				}
			}

			#if SENDTESTINGFRAMES
			// Send packet data at 100 packets per second (10ms interval)
			if (now - LastPacketSendTime >= 1000/200) {
				LastPacketSendTime = now;
				ESPNowPacketMessage packet;
				packet.len = 16;
				memcpy(packet.data, "Hello World!1234", 16); //Example data

				// Only send the actual used portion: header (1) + len (1) + actual data length
				size_t actualSize = 2 + packet.len;
				auto result = esp_now_send(gatewayAddress, reinterpret_cast<uint8_t*>(&packet), actualSize);
			#if ESP8266
				if (result != ERR_OK) {
			#else
				if (result != ESP_OK) {
			#endif
					//Serial.printf("[ESPNOW] ERR: %d\n", result);
				}
			}
#endif
			break;
		case GatewayStatus::Failed:
			// Handle failure
			break;
	}
}
}  // namespace SlimeVR
