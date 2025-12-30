#pragma once

#include "logging/Logger.h"
#ifdef ESP8266
#include <espnow.h>
#else
#include <esp_now.h>
#endif

namespace SlimeVR {
class ESPNow {
public:
	enum class GatewayStatus {
		NotSetup = 0,
		SearchingForGateway,
		Connecting,
		Pairing,
		Connected,
		Failed
	};

	enum class ESPNowMessageTypes : uint8_t {
		PAIRING_REQUEST = 0, //When the tracker is trying to pair with a gateway
		PAIRING_RESPONSE = 1, //When the gateway is responding to a pairing request
		HANDSHAKE_REQUEST = 2, //When the tracker is trying to handshake with a gateway
		HANDSHAKE_RESPONSE = 3, //When the gateway is responding to a handshake
		HEARTBEAT_ECHO = 4, //Regular heartbeat message to keep the connection alive
		HEARTBEAT_RESPONSE = 5, //Response to the heartbeat message
		TRACKER_DATA = 6, //Regular tracker data packet
		PAIRING_ANNOUNCEMENT = 7, //When the gateway is announcing its presence for pairing
		UNPAIR = 8, //When the tracker is unpairing from the gateway
		TRACKER_RATE = 9 //When the gateway is setting the tracker polling rate
	};

	static unsigned int &getChannel();

	static ESPNow &getInstance();

	static ESPNow instance;

	ESPNow() = default;

#if ESP8266
	static void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len);
#else
	static void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len);
	esp_now_rate_config_t rate_config;
#endif

	void HandlePairingAnnouncement(uint8_t * mac, uint8_t *data, uint8_t len);

	void SendPairingRequest();

	void HandleHeartbeatEcho(uint8_t * mac, uint8_t *data, uint8_t len);

	void HandlePairingResponse(uint8_t * mac, uint8_t *data, uint8_t len);

	void SendHandshakeRequest();

	void HandleHandshakeResponse(uint8_t * mac, uint8_t *data, uint8_t len);

	void HandleUnpair(uint8_t * mac, uint8_t *data, uint8_t len);

	void HandleTrackerRate(uint8_t * mac, uint8_t *data, uint8_t len);

	unsigned long LastPairingRequestTime = 0;

	unsigned long LastHandshakeRequestTime = 0;

	unsigned long PairingStartTime = 0;

	unsigned long LastPacketSendTime = 0;

	unsigned long LastChannelSwitchTime = 0;

	unsigned int channel = 0;

	// Heartbeat tracking
	unsigned long LastHeartbeatSendTime = 0;
	unsigned long HeartbeatSentTimestamp = 0;
	bool WaitingForHeartbeatResponse = false;
	uint8_t MissedHeartbeats = 0;
	uint16_t HeartbeatSequenceNumber = 0;

	void SendHeartbeat();
	void HandleHeartbeatResponse(uint8_t * mac, uint8_t *data, uint8_t len);

	void setUp();

	void upkeep();

	[[nodiscard]] bool isConnected() const;

	GatewayStatus state = GatewayStatus::NotSetup;

	bool initialized = false;

	uint8_t securityCode[8];

	uint8_t gatewayAddress[6] = {0};
	bool hasGatewayAddress = false;
	uint8_t trackerId = 0;

	const uint8_t* getGateway();

	const uint8_t* getSecurityCode();

	void Connect();

	void Pairing();

	void setState (GatewayStatus newState);

	SlimeVR::Logging::Logger espnowHandlerLogger{"ESPNowHandler"};
};
}  // namespace SlimeVR
