#include "GlobalVars.h"
#include "globals.h"

#if !ESP8266
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#else
#include <ESP8266WiFi.h>
#endif

#include "network/espnowhandler.h"
#include "messages.h"

namespace SlimeVR {
	// Define the static instance
	ESPNow ESPNow::instance;

	ESPNow &ESPNow::getInstance() {
		return instance;
	}

	//Gets the actual current WiFi channel being used
	unsigned int ESPNow::getChannel() {
		if (getInstance().state == GatewayStatus::NotSetup) return getInstance().channels[0];
		return WiFi.channel();
	}

	void ESPNow::setUp() {
		Serial.println("[ESPNow] Setting up ESPNow");

		channelIndex = 0;

		WiFi.mode(WIFI_STA);
#if !ESP8266
		esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11G);
		//esp_wifi_set_max_tx_power(WIFI_POWER_2dBm);
		auto result = WiFi.setChannel(1);
		if (result != ESP_OK) {
			Serial.println("[ESPNow] Failed to set WiFi channel for init: " + String(result));
			return;
		}

		rate_config.phymode = WIFI_PHY_MODE_HT20;
		rate_config.rate = WIFI_PHY_RATE_MCS0_SGI;
		rate_config.ersu = false;
		rate_config.dcm = true;
#else
		WiFi.setPhyMode(WIFI_PHY_MODE_11N | WIFI_PHY_MODE_11G);
		wifi_set_user_fixed_rate(FIXED_RATE_MASK_ALL, RATE_11N_MCS0);
		wifi_set_user_limit_rate_mask(LIMIT_RATE_MASK_ALL);
		auto d = wifi_set_user_rate_limit(RC_LIMIT_11N, WIFI_STA, RATE_11N_MCS2, RATE_11N_MCS0);
		if (d != true) {
			Serial.println("[ESPNow] Failed to set WiFi rate limit for init: " + String(d));
			return;
		}
		//WiFi.setOutputPower(19.5);
		auto result = wifi_set_channel(1);
		if (result != true) {
			Serial.println("[ESPNow] Failed to set WiFi channel for init: " + String(result));
			return;
		}
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

		//Add the broadcast address as a peer to allow sending broadcast messages
		uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
		addPeer(broadcastAddress, true);

		Serial.printf(
			"[ESPNow] address: %02x:%02x:%02x:%02x:%02x:%02x\n",
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
		uint8_t *data = incomingData;
		uint8_t len8 = len;
#else
	void ESPNow::OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
		uint8_t mac_copy[6]; memcpy(mac_copy, esp_now_info->src_addr, 6);
		uint8_t *mac = mac_copy;
		uint8_t *data = const_cast<uint8_t*>(incomingData);
		uint8_t len8 = (uint8_t)len;
#endif

		//Serial.println("[ESPNow] Data received, length: " + String(len8));
		// Serial.print("[ESPNow] Data: ");
		// for (int i = 0; i < len8; i++) {
		// 	Serial.printf("%02x ", data[i]);
		// }
		// Serial.println();


		switch (data[0]) {
			case static_cast<uint8_t>(ESPNowMessageTypes::PAIRING_ANNOUNCEMENT): instance.HandlePairingAnnouncement(mac, data, len8); break;
			case static_cast<uint8_t>(ESPNowMessageTypes::PAIRING_RESPONSE): instance.HandlePairingResponse(mac, data, len8); break;
			case static_cast<uint8_t>(ESPNowMessageTypes::HANDSHAKE_RESPONSE): instance.HandleHandshakeResponse(mac, data, len8); break;
			case static_cast<uint8_t>(ESPNowMessageTypes::HEARTBEAT_ECHO): instance.HandleHeartbeatEcho(mac, data, len8); break;
			case static_cast<uint8_t>(ESPNowMessageTypes::HEARTBEAT_RESPONSE): instance.HandleHeartbeatResponse(mac, data, len8); break;
			case static_cast<uint8_t>(ESPNowMessageTypes::UNPAIR): instance.HandleUnpair(mac, data, len8); break;
			case static_cast<uint8_t>(ESPNowMessageTypes::TRACKER_RATE): instance.HandleTrackerRate(mac, data, len8); break;
			case static_cast<uint8_t>(ESPNowMessageTypes::ENTER_OTA_MODE): instance.HandleOTAMessage(mac, data, len8); break;
			default: /* Serial.println("[ESPNow] Unknown message type received");*/ break;
		}
	}

	void ESPNow::HandlePairingAnnouncement(uint8_t * mac, uint8_t *data, uint8_t len) {
		// Handle pairing announcement logic here
		if (state != GatewayStatus::Pairing || hasGatewayAddress) return;

		if (len != sizeof(ESPNowPairingAnnouncementMessage)) {
			Serial.printf("[ESPNow] Invalid pairing announcement message length: expected %d, got %d\n", sizeof(ESPNowPairingAnnouncementMessage), len);
			return;
		}
		Serial.println("[ESPNow] Handling pairing announcement...");
		auto& message = *reinterpret_cast<ESPNowPairingAnnouncementMessage*>(data);
		Serial.printf(
			"[ESPNow] Security bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
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
		Serial.printf("[ESPNow] Found gateway %02x:%02x:%02x:%02x:%02x:%02x on channel: %d\n",
			gatewayAddress[0], gatewayAddress[1], gatewayAddress[2],
			gatewayAddress[3], gatewayAddress[4], gatewayAddress[5],
			announcedChannel
		);

		setChannel(announcedChannel);
		hasGatewayAddress = true;
		Serial.println("[ESPNow] Attempting to pair with gateway...");
		PairingStartTime = millis();
	}

	void ESPNow::SendPairingRequest() {
		if (!hasGatewayAddress) {
			Serial.println("[ESPNow] No gateway address set, cannot send pairing request");
			return;
		}
		ESPNowPairingMessage pairRequest;
		memcpy(pairRequest.securityBytes, securityCode, 8);
		queueMessage(gatewayAddress, reinterpret_cast<uint8_t*>(&pairRequest), sizeof(ESPNowPairingMessage), false, true);
		Serial.println("[ESPNow] Pairing request sent to: " +
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
		Serial.println("[ESPNow] Successfully paired with gateway, establishing connection...");

		// Save security code and gateway address to persistent storage
		configuration.setESPNowGateway(gatewayAddress, securityCode);
		Serial.printf(
			"[ESPNow] Saved gateway %02x:%02x:%02x:%02x:%02x:%02x to configuration\n",
			gatewayAddress[0], gatewayAddress[1], gatewayAddress[2],
			gatewayAddress[3], gatewayAddress[4], gatewayAddress[5]
		);

		singleIncrementChannel(true);

		esp_now_del_peer(gatewayAddress);

		// For simplicity, assume pairing is always successful
		setState(GatewayStatus::Connecting);
	}

	void ESPNow::SendHandshakeRequest() {
		if (!hasGatewayAddress) {
			Serial.println("[ESPNow] No gateway address set, cannot send handshake request");
			return;
		}
		ESPNowConnectionMessage handshakeRequest;
		memcpy(handshakeRequest.securityBytes, securityCode, 8);
		//Serial.println("[ESPNow] Sending handshake request");
		uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
		queueMessage(broadcastAddress, reinterpret_cast<uint8_t*>(&handshakeRequest), sizeof(ESPNowConnectionMessage), false, true);
	}

	void ESPNow::HandleHandshakeResponse(uint8_t * mac, uint8_t *data, uint8_t len) {
		if (state != GatewayStatus::Connecting || !hasGatewayAddress) return;
		// Handle handshake response logic here
		//Serial.println("[ESPNow] Handshake response received, connection established");

		auto& ackMessage = *reinterpret_cast<ESPNowConnectionAckMessage*>(data);
		Serial.printf("[ESPNow] Assigned channel: %d and tracker ID: %d\n", ackMessage.channel, ackMessage.trackerId);

		// Store the assigned tracker ID
		trackerId = ackMessage.trackerId;

		// Initialize heartbeat system
		LastHeartbeatSendTime = 0;
		WaitingForHeartbeatResponse = false;
		MissedHeartbeats = 0;

#if !ESP8266
		WiFi.setChannel(ackMessage.channel);
#else
		wifi_set_channel(ackMessage.channel);
#endif

		setState(GatewayStatus::Connected);
	}

	void ESPNow::SendHeartbeat() {
		if (!hasGatewayAddress) {
			Serial.println("[ESPNow] No gateway address set, cannot send heartbeat");
			return;
		}

		// Generate random 16-bit sequence number
		HeartbeatSequenceNumber = (uint16_t)random(0, 65536);
		ESPNowHeartbeatEchoMessage heartbeat;
		heartbeat.sequenceNumber = HeartbeatSequenceNumber;
		//Serial.printf("[ESPNow] Sending heartbeat - Seq: %u\n", HeartbeatSequenceNumber);
		queueMessage(gatewayAddress, reinterpret_cast<uint8_t*>(&heartbeat), sizeof(ESPNowHeartbeatEchoMessage), true);

		HeartbeatSentTimestamp = millis();
		WaitingForHeartbeatResponse = true;
	}

	void ESPNow::HandleHeartbeatResponse(uint8_t * mac, uint8_t *data, uint8_t len) {
		if (state != GatewayStatus::Connected || !hasGatewayAddress) return;
		if (!WaitingForHeartbeatResponse) {
			//Serial.println("[ESPNow] Received unexpected heartbeat response");
			return;
		}

		if (len != sizeof(ESPNowHeartbeatResponseMessage)) {
			//Serial.printf("[ESPNow] Invalid heartbeat response length: expected %d, got %d\n", sizeof(ESPNowHeartbeatResponseMessage), len);
			return;
		}

		auto& responseMessage = *reinterpret_cast<ESPNowHeartbeatResponseMessage*>(data);
		uint16_t receivedSeq = responseMessage.sequenceNumber;

		if (receivedSeq != HeartbeatSequenceNumber) {
			//Serial.printf("[ESPNow] Heartbeat sequence mismatch: expected %u, got %u\n", HeartbeatSequenceNumber, receivedSeq);
			return;
		}

		unsigned long latency = millis() - HeartbeatSentTimestamp;
		Serial.printf("[ESPNow] Heartbeat response received - Seq: %u, Latency: %lu ms\n", receivedSeq, latency);	WaitingForHeartbeatResponse = false;
		MissedHeartbeats = 0;
	}

	void ESPNow::HandleHeartbeatEcho(uint8_t * mac, uint8_t *data, uint8_t len) {
		if (state != GatewayStatus::Connected || !hasGatewayAddress || memcmp(mac, gatewayAddress, 6) != 0) return;

		if (len != sizeof(ESPNowHeartbeatEchoMessage)) {
			Serial.printf("[ESPNow] Invalid heartbeat echo length: expected %d, got %d\n", sizeof(ESPNowHeartbeatEchoMessage), len);
			return;
		}

		MissedHeartbeats = 0;

		ESPNowHeartbeatResponseMessage heartbeatResponse;
		auto& echoMessage = *reinterpret_cast<ESPNowHeartbeatEchoMessage*>(data);
		if (LastGatewayHeartbeatSequenceNumber == echoMessage.sequenceNumber) {
			//Serial.printf("[ESPNow] Duplicate heartbeat echo received - Seq: %u, ignoring\n", echoMessage.sequenceNumber);
			return;
		}
		LastGatewayHeartbeatSequenceNumber = echoMessage.sequenceNumber;
		heartbeatResponse.sequenceNumber = echoMessage.sequenceNumber;
		//Serial.printf("[ESPNow] Heartbeat echo received - Seq: %u, sending response\n", echoMessage.sequenceNumber);
		queueMessage(mac, reinterpret_cast<uint8_t*>(&heartbeatResponse), sizeof(ESPNowHeartbeatResponseMessage));
		queueMessage(mac, reinterpret_cast<uint8_t*>(&heartbeatResponse), sizeof(ESPNowHeartbeatResponseMessage));
	}

	void ESPNow::HandleUnpair(uint8_t * mac, uint8_t *data, uint8_t len) {
		if (!hasGatewayAddress) return;

		if (len != sizeof(ESPNowUnpairMessage)) {
			Serial.printf("[ESPNow] Invalid unpair message length: expected %d, got %d\n", sizeof(ESPNowUnpairMessage), len);
			return;
		}

		// Verify MAC address matches current gateway
		if (memcmp(mac, gatewayAddress, 6) != 0) {
			Serial.println("[ESPNow] Unpair request from unknown address, ignoring");
			return;
		}

		auto& message = *reinterpret_cast<ESPNowUnpairMessage*>(data);

		// Verify security code matches
		if (memcmp(message.securityBytes, securityCode, 8) != 0) {
			Serial.println("[ESPNow] Unpair request with invalid security code, ignoring");
			return;
		}

		Serial.println("[ESPNow] Received valid unpair request from gateway");

		// Remove peer
		deletePeer(gatewayAddress);

		// Clear gateway address and security code from memory
		memset(gatewayAddress, 0, 6);
		memset(securityCode, 0, 8);
		hasGatewayAddress = false;

		configuration.clearESPNowGateway();

		// Clear from persistent storage
		configuration.setESPNowGateway(nullptr, nullptr);

		Serial.println("[ESPNow] Unpaired from gateway, entering pairing mode");

		//Clear message buffers
		queueHead = 0;
		queueTail = 0;

		// Return to pairing mode to find new gateway
		Pairing();
	}

	void ESPNow::HandleTrackerRate(uint8_t * mac, uint8_t *data, uint8_t len) {
		if (!this->hasGatewayAddress) return;

		if (len != sizeof(ESPNowTrackerRateMessage)) {
			Serial.printf("[ESPNow] Invalid tracker rate message length: expected %d, got %d\n", sizeof(ESPNowTrackerRateMessage), len);
			return;
		}

		// Verify MAC address matches current gateway
		if (memcmp(mac, this->gatewayAddress, 6) != 0) {
			Serial.println("[ESPNow] Tracker rate request from unknown address, ignoring");
			return;
		}

		auto& message = *reinterpret_cast<ESPNowTrackerRateMessage*>(const_cast<uint8_t*>(data));

		Serial.printf("[ESPNow] Received tracker rate request: %u Hz\n", message.rateHz);

		// Forward to network connection to update rate limiting
		networkConnection.setTrackerRate(message.rateHz);
	}

	void ESPNow::HandleOTAMessage(uint8_t * mac, uint8_t *data, uint8_t len) {
		// Handle OTA message logic here
		if (!this->hasGatewayAddress || memcmp(mac, this->gatewayAddress, 6) != 0) return;

		if (len != sizeof(ESPNowEnterOtaModeMessage)) {
			Serial.printf("[ESPNow] Invalid OTA mode message length: expected %d, got %d\n", sizeof(ESPNowEnterOtaModeMessage), len);
			return;
		}

		auto& message = *reinterpret_cast<ESPNowEnterOtaModeMessage*>(data);

		//Check security code
		if (memcmp(message.securityBytes, this->securityCode, 8) != 0) {
			Serial.println("[ESPNow] OTA mode request with invalid security code, ignoring");
			return;
		}

		mempcpy(ota_auth, message.ota_auth, 16);
		ota_portNum = message.ota_portNum;
		mempcpy(ota_ip, message.ota_ip, 4);
		memcpy(ssid, message.ssid, 33);
		memcpy(password, message.password, 65);

		Serial.printf("[ESPNow] Received valid OTA mode request: IP: %d.%d.%d.%d, Port: %ld SSID: %s\n", ota_ip[0], ota_ip[1], ota_ip[2], ota_ip[3], ota_portNum, ssid);

		//clear message buffers since we will be disconnecting anyways
		queueHead = 0;
		queueTail = 0;
		lastSendTime = 0;

		//Send Acknowledgment back to gateway
		SendOTAAck();
		lastSendTime = 0;
		SendOTAAck();
		lastSendTime = 0;
		SendOTAAck();

		setState(GatewayStatus::OTAUpdate);
	}

	void ESPNow::SendOTAAck() {
		if (!hasGatewayAddress) {
			Serial.println("[ESPNow] No gateway address set, cannot send heartbeat");
			return;
		}

		//Send OTA Ack message
		ESPNowEnterOtaAckMessage ackMessage;
		queueMessage(gatewayAddress, reinterpret_cast<uint8_t*>(&ackMessage), sizeof(ESPNowEnterOtaAckMessage));

		HeartbeatSentTimestamp = millis();
		WaitingForHeartbeatResponse = true;
	}

	// Sends a UDP datagram: 'OTAREQUEST' + raw ota_auth bytes to ota_ip:ota_portNum
	void ESPNow::SendOTARequest() {
		WiFiUDP udp;
		// Prefix must be 10 bytes: 'OTAREQUEST' (10 chars, no null terminator)
		const uint8_t prefix[10] = {'O','T','A','R','E','Q','U','E','S','T'};
		uint8_t buffer[10 + 16];
		memcpy(buffer, prefix, 10);
		memcpy(buffer + 10, ota_auth, 16);
		IPAddress ip(ota_ip[0], ota_ip[1], ota_ip[2], ota_ip[3]);
		udp.beginPacket(ip, ota_portNum);
		udp.write(buffer, sizeof(buffer));
		udp.endPacket();
		Serial.printf("[ESPNow] Sent OTA request to %d.%d.%d.%d:%ld\n", ota_ip[0], ota_ip[1], ota_ip[2], ota_ip[3], ota_portNum);
		for (int i = 0; i < 16; ++i) Serial.printf("%02x", ota_auth[i]);
		Serial.println();
	}

	// Queue a message for sending with rate limiting
	void ESPNow::queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen, bool isHeartbeat, bool ephemeral) {
		// Validate message data
		if (dataLen == 0 || dataLen > 128) {
			Serial.printf("[ESPNow] Invalid message size %zu for " MACSTR ", skipping\n", dataLen, MAC2ARGS(peerMac));
			return;
		}

		// Check if queue is full
		size_t nextTail = (queueTail + 1) % maxQueueSize;
		if (nextTail == queueHead) {
			// Calculate queue depth for diagnostic output
			size_t queueDepth = (queueTail >= queueHead) ? (queueTail - queueHead) : (maxQueueSize - queueHead + queueTail);
			Serial.printf("[ESPNow] Send queue full! Dropping message to " MACSTR " (queue: %zu/%zu, depth: %zu)\n", MAC2ARGS(peerMac), maxQueueSize, maxQueueSize, queueDepth);
			return;
		}

		// Add message to queue
		PendingMessage &msg = sendQueue[queueTail];
		memcpy(msg.peerMac, peerMac, 6);
		memcpy(msg.data, data, dataLen);
		msg.dataLen = dataLen;
		msg.ephemeral = ephemeral;
		msg.isHeartbeat = isHeartbeat;
		queueTail = nextTail;

		processSendQueue();
	}

	// Queue a message for sending with rate limiting
	void ESPNow::queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen, bool isHeartbeat) {
		queueMessage(peerMac, data, dataLen, isHeartbeat, false);
	}

	// Queue a message for sending with rate limiting
	void ESPNow::queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen) {
		queueMessage(peerMac, data, dataLen, false);
	}

	// Process queued messages with rate limiting
	void ESPNow::processSendQueue() {
		if (queueHead == queueTail) return; // Queue is empty

		unsigned long currentTime = micros();
		unsigned long deltaTime = currentTime - lastSendTime;
		//Serial.printf("[ESPNow] Processing send queue %lu\n", deltaTime);
		if (deltaTime >= sendRateLimit*1000UL) {
			PendingMessage &msg = sendQueue[queueHead];

			// Validate message data
			if (msg.dataLen == 0 || msg.dataLen > 128) {
				Serial.printf("[ESPNow] Invalid message size %zu for " MACSTR ", dropping\n", msg.dataLen, MAC2ARGS(msg.peerMac));
				queueHead = (queueHead + 1) % maxQueueSize;
				lastSendTime = currentTime;
				return;
			}

			// Ensure peer is added before sending
			if (!esp_now_is_peer_exist(msg.peerMac)) {
				auto addResult = addPeer(msg.peerMac);
#if ESP8266
				if (addResult != ERR_OK) {
#else
				if (addResult != ESP_OK) {
#endif
					Serial.printf("[ESPNow] Failed to add peer " MACSTR " for queued message, error: %d\n", MAC2ARGS(msg.peerMac), addResult);
					queueHead = (queueHead + 1) % maxQueueSize;
					lastSendTime = currentTime;
					return;
				}
			}

			auto result = esp_now_send(msg.peerMac, msg.data, msg.dataLen);

			if (msg.isHeartbeat) {
				LastHeartbeatSendTime = millis();
			}

			if (msg.ephemeral) {
				// Remove peer if message was ephemeral
				deletePeer(msg.peerMac);
			}

#if ESP8266
			if (result == ERR_OK) {
#else
			if (result == ESP_OK) {
#endif
				// Message sent successfully, remove from queue
				queueHead = (queueHead + 1) % maxQueueSize;
				lastSendTime = currentTime;
#if ESP8266
			} else if (result == ERR_MEM) {
#else
			} else if (result == ESP_ERR_ESPNOW_NO_MEM) {
#endif
				// ESP-NOW internal buffer is full - skip sending
				//Serial.printf("[ESPNow] buffer full, retrying message to " MACSTR ", error: %d\n", MAC2ARGS(msg.peerMac), result);
				queueHead = (queueHead + 1) % maxQueueSize;
				lastSendTime = currentTime;
			} else {
				// Other errors - log and drop the message
				Serial.printf("[ESPNow] Failed to send queued message to " MACSTR ", error: %d\n", MAC2ARGS(msg.peerMac), result);
				queueHead = (queueHead + 1) % maxQueueSize;
				lastSendTime = currentTime;
			}
		}
	}

	// Adds a ESP-Now peer with the given MAC address
	uint8_t ESPNow::addPeer(uint8_t peerMac[6], bool defaultConfig) {
		// Check if peer already exists
		if (esp_now_is_peer_exist(peerMac)) {
			Serial.printf("[ESPNow] Peer " MACSTR " already exists.\n", MAC2ARGS(peerMac));
#if ESP8266
			return ERR_OK; // Peer already exists, return success
#else
			return ESP_OK; // Peer already exists, return success
#endif
		}
#if ESP8266
		auto result = esp_now_add_peer(peerMac, ESP_NOW_ROLE_COMBO, 0, NULL, 0);
		if (result != ERR_OK) {
			Serial.printf("[ESPNow] Failed to add peer, error: %d\n", result);
			return result;
		}
#else
		esp_now_peer_info_t peerInfo = {};
		memcpy(peerInfo.peer_addr, peerMac, 6);
		peerInfo.channel = 0;
		peerInfo.encrypt = false;
		peerInfo.ifidx = WIFI_IF_STA;
		auto result = esp_now_add_peer(&peerInfo);
		if (result != ESP_OK) {
			Serial.printf("[ESPNow] Failed to add peer, error: %d\n", result);
			return result;
		} else if (!defaultConfig) {
			result = esp_now_set_peer_rate_config(peerInfo.peer_addr, &rate_config);
			if (result != ESP_OK) {
				Serial.printf("[ESPNow] Failed to set peer rate config, error: %d\n", result);
				return result;
			}
		}
#endif
		return result;
	}

	// Adds a ESP-Now peer with the given MAC address
	uint8_t ESPNow::addPeer(uint8_t peerMac[6]) {
		return addPeer(peerMac, false);
	}

	// Deletes a ESP-Now peer with the given MAC address
	bool ESPNow::deletePeer(uint8_t peerMac[6]) {
		if (!esp_now_is_peer_exist(peerMac)) {
			Serial.printf("Peer " MACSTR " does not exist.\n", MAC2ARGS(peerMac));
			return true; // Peer does not exist, return success
		}

		//Serial.printf("[ESPNow] Deleting peer " MACSTR "\n", MAC2ARGS(peerMac));
		auto result = esp_now_del_peer(peerMac);
#if !ESP8266
		auto compareAgainst = ESP_OK;
#else
		auto compareAgainst = ERR_OK;
#endif
		if (result != compareAgainst) {
			Serial.printf("[ESPNow] Failed to delete peer " MACSTR ", error: %d\n", MAC2ARGS(peerMac), result);
			return false;
		}
		return true;
	}

	void ESPNow::Connect () {
		setState(GatewayStatus::SearchingForGateway);
	}

	void ESPNow::Pairing() {
		if (hasGatewayAddress) {
			deletePeer(gatewayAddress);
			hasGatewayAddress = false;
		}
		setState(GatewayStatus::Pairing);
	}

	void ESPNow::setState (GatewayStatus newState) {
		if (state == newState) return;
		auto previousState = state;
		state = newState;

		if (previousState == GatewayStatus::OTAUpdate) statusManager.setStatus(SlimeVR::Status::UPDATING, false);

		switch (state) {
			case GatewayStatus::NotSetup:
				Serial.println("[ESPNow] Not set up");
				break;
			case GatewayStatus::SearchingForGateway: {
				Serial.println("[ESPNow] Searching for gateway");
				const uint8_t* gateway = getGateway();
				const uint8_t* security = getSecurityCode();
				if (gateway == nullptr || security == nullptr) {
					Serial.println("[ESPNow] No gateway address found, entering pairing mode");
					Pairing();
					break;
				}
				Serial.println("[ESPNow] Gateway address found, connecting...");
				memcpy(gatewayAddress, gateway, 6);
				memcpy(securityCode, security, 8);
				hasGatewayAddress = true;
				statusManager.setStatus(SlimeVR::Status::WIFI_CONNECTING, false);
				statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, false);
				setState(GatewayStatus::Connecting);
				break;
			}
			case GatewayStatus::Connecting:
				Serial.println("[ESPNow] Connecting to gateway");
				statusManager.setStatus(SlimeVR::Status::WIFI_CONNECTING, true);
				statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, false);
				break;
			case GatewayStatus::Pairing:
				if (!hasGatewayAddress) {
					Serial.println("[ESPNow] Starting Pairing mode");
					statusManager.setStatus(SlimeVR::Status::WIFI_CONNECTING, false);
					statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, true);
					PairingStartTime = millis();
				}
				break;
			case GatewayStatus::Connected:
				Serial.println("[ESPNow] Connected to gateway");
				statusManager.setStatus(SlimeVR::Status::WIFI_CONNECTING, false);
				statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, false);
				break;
			case GatewayStatus::Failed:
				Serial.println("[ESPNow] failed");
				break;
			case GatewayStatus::OTAUpdate:
				Serial.println("[ESPNow] Entering OTA Update mode");
				statusManager.setStatus(SlimeVR::Status::WIFI_CONNECTING, false);
				statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, false);
				statusManager.setStatus(SlimeVR::Status::UPDATING, true);
				LastHeartbeatSendTime = 0;
				HeartbeatSentTimestamp = 0;
				WaitingForHeartbeatResponse = false;
				break;
		}
	}

	void ESPNow::incrementChannel () {
		if (channelIndex < 0) {
			channelIndex = 0;
		} else if (channelIndex >= 0 && channelIndex < MAX_WIFI_CHANNEL_ARRAY) {
			channelIndex = channelIndex + 1;
		} else if (channelIndex >= MAX_WIFI_CHANNEL_ARRAY) {
			channelIndex = 0;
		}
#ifndef ESP8266
		auto result = WiFi.setChannel(channels[channelIndex]);
		if (result != ESP_OK) {
			Serial.println("[ESPNow] Failed to set WiFi channel: " + String(result));
			return;
		}
#else
		auto result = wifi_set_channel(channels[channelIndex]);
		if (result != true) {
			Serial.printf("[ESPNow] Failed to set WiFi channel %d: %d\n", channels[channelIndex], result);
			return;
		}
#endif
		//Serial.printf("[ESPNow] Switched to channel %d\n", getChannel());
	}

	void ESPNow::singleIncrementChannel (bool reverse) {
		int target = 0;
		int current = getChannel();
		if (reverse) {
			if (current <= 1) {
				target = MAX_WIFI_CHANNEL;
			} else if (current > 1 && current <= MAX_WIFI_CHANNEL) {
				target = current - 1;
			}
		} else {
			if (current <= 0) {
				target = 1;
			} else if (current >= 1 && current < MAX_WIFI_CHANNEL) {
				target = current + 1;
			} else if (current >= MAX_WIFI_CHANNEL) {
				target = 1;
			}
		}
#ifndef ESP8266
		auto result = WiFi.setChannel(target);
		if (result != ESP_OK) {
			Serial.printf("[ESPNow] Failed to set WiFi channel %d: %d\n", target, result);
			return;
		}
#else
		auto result = wifi_set_channel(target);
		if (result != true) {
			Serial.printf("[ESPNow] Failed to set WiFi channel %d: %d\n", target, result);
			return;
		}
#endif
		//Serial.printf("[ESPNow] Switched to channel %d\n", getChannel());
	}

	void ESPNow::singleIncrementChannel () {
		singleIncrementChannel(false);
	}

	void ESPNow::setChannel (uint8_t channel) {
#ifndef ESP8266
		auto result = WiFi.setChannel(channel);
		if (result != ESP_OK) {
			Serial.printf("[ESPNow] Failed to set WiFi channel %d: %d\n", channel, result);
			return;
		}
#else
		auto result = wifi_set_channel(channel);
		if (result != true) {
			Serial.printf("[ESPNow] Failed to set WiFi channel %d: %d\n", channel, result);
			return;
		}
#endif
		Serial.printf("[ESPNow] Switched to channel %d\n", getChannel());
	}

	void ESPNow::upkeep () {
		processSendQueue();

		auto now = millis();
		switch (state) {
			case GatewayStatus::NotSetup:
				break;
			case GatewayStatus::SearchingForGateway: {
				break;
			}
			case GatewayStatus::Connecting: {
				// Try to handshake with gateway
				static unsigned long connectStartTime = 0;
				static bool connectTimerStarted = false;
				if (!connectTimerStarted) {
					connectStartTime = millis();
					connectTimerStarted = true;
				}
				if (hasGatewayAddress) {
					if (now - LastChannelSwitchTime >= 300) {
						LastChannelSwitchTime = now;
						singleIncrementChannel();
						Serial.printf("[ESPNow] Connect gateway via channel %d\n", getChannel());
					}
					if (now - LastHandshakeRequestTime < 150) {
						//Don't spam requests
						break;
					}
					LastHandshakeRequestTime = now;
					SendHandshakeRequest();
					// Timeout: If connecting takes more than 60s, enter pairing mode
					if ((now - connectStartTime) > 60000) {
						Serial.println("[ESPNow] Connecting to gateway timed out, entering pairing mode");
						connectTimerStarted = false;
						Pairing();
						break;
					}
				} else {
					setState(GatewayStatus::SearchingForGateway);
					connectTimerStarted = false;
				}
				break;
			}
			case GatewayStatus::Pairing: {
				// Try to pair with gateway
				unsigned long pairingTimeout = 60000;
				// Reduce timeout to 10s if gateway/security stored in persistent memory
				if (getGateway() != nullptr && getSecurityCode() != nullptr) pairingTimeout = 10000;
				if (!hasGatewayAddress && (now - LastChannelSwitchTime) >= 400) {
					LastChannelSwitchTime = now;
					incrementChannel();
					Serial.printf("[ESPNow] Scanning channel %d for gateway\n", getChannel());
				}

				if (now - PairingStartTime > pairingTimeout) {
					Serial.println("[ESPNow] Pairing timed out, restarting search for gateway");
					if (hasGatewayAddress) {
						esp_now_del_peer(gatewayAddress);
						hasGatewayAddress = false;
					}
					Connect();
				} else if (hasGatewayAddress) {
					if (now - LastPairingRequestTime < 200) {
						//Don't spam requests
						break;
					}
					LastPairingRequestTime = now;
					SendPairingRequest();
				}
				break;
			}
			case GatewayStatus::Connected: {
				// Maintain connection with heartbeat
				// Only run heartbeat logic once per second
				if (now - LastHeartbeatSendTime >= 1000) {
					// Check if we're waiting for a response
					if (WaitingForHeartbeatResponse) {
							MissedHeartbeats++;
							//Serial.printf("[ESPNow] Heartbeat timeout - Missed: %d/3\n", MissedHeartbeats);
							if (MissedHeartbeats >= 5) {
								Serial.println("[ESPNow] Connection lost - 5 heartbeats missed");
								channelIndex--;
								if (hasGatewayAddress) esp_now_del_peer(gatewayAddress);
								setState(GatewayStatus::Connecting);
								break;
							}
							WaitingForHeartbeatResponse = false;
					}

					// Send heartbeat if not waiting for response
					if (!WaitingForHeartbeatResponse) {
						LastHeartbeatSendTime = now;
						SendHeartbeat();
					}
				}

#if SEND_TEST_DATA
				if (now - LastTestDataSendTime >= (1000 / TEST_DATA_RATE_HZ)) {
					LastTestDataSendTime = now;
					ESPNowPacketMessage testData;
					testData.len = 16;
					for (int i = 0; i < 16; ++i) testData.data[i] = i;
					queueMessage(gatewayAddress, reinterpret_cast<uint8_t*>(&testData), 2 + testData.len);
				}
#endif
				break;
			}
			case GatewayStatus::Failed:
				// Handle failure
				break;
			case GatewayStatus::OTAUpdate: {
				// WiFi connection state updater for OTA mode
				static unsigned long wifiConnectStart = 0;
				static bool wifiConnectStarted = false;
				if (!wifiConnectStarted) {
					wifiConnectStart = millis();
					wifiConnectStarted = true;
					return;
				}

				static bool connecting = false;
				if (!connecting) {
					WiFi.mode(WIFI_STA);
#if ESP32
					WiFi.setTxPower(WIFI_POWER_19_5dBm); // Set max power for better range
#else
					WiFi.setOutputPower(20.5); // Set max power for better range
#endif
					WiFi.begin(ssid, password);
					Serial.printf("[ESPNow] Connecting to %s\n", ssid);
					connecting = true;
					return;
				}

				if (WiFi.status() == WL_CONNECTED) {
					unsigned long elapsed = millis() - wifiConnectStart;
					if (elapsed > 60000) {
						Serial.println("[ESPNow] timed out waiting for OTA update");
						WiFi.disconnect(true);
						setState(GatewayStatus::NotSetup);
						WiFi.mode(WIFI_OFF); // Fully reset WiFi before re-init
						delay(100); // Give hardware time to reset
						setUp();
						wifiConnectStarted = false;
					}
					delay(150);
					SendOTARequest();
				} else {
					unsigned long elapsed = millis() - wifiConnectStart;
					if (elapsed > 30000) {
						Serial.println("[ESPNow] WiFi connection failed or timed out for OTA update");
						WiFi.disconnect(true);
						setState(GatewayStatus::NotSetup);
						WiFi.mode(WIFI_OFF); // Fully reset WiFi before re-init
						delay(100); // Give hardware time to reset
						setUp();
						wifiConnectStarted = false;
					} else {
						Serial.println("[ESPNow] Waiting for WiFi connection...");
						delay(1000); // Placeholder for OTA logic
					}
				}
				break;
			}
		}
	}

}  // namespace SlimeVR
