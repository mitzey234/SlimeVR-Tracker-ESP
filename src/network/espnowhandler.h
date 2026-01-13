#pragma once

#include "logging/Logger.h"
#ifdef ESP8266
#include <espnow.h>
#else
#include <esp_now.h>
#endif

#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2ARGS(mac) mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]

#if ALLOW_14_WIFI_CHANNELS
#define MAX_WIFI_CHANNEL_ARRAY 4
#define MAX_WIFI_CHANNEL 14
#else
#define MAX_WIFI_CHANNEL_ARRAY 3
#define MAX_WIFI_CHANNEL 11
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
		Failed,
		OTAUpdate
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
		TRACKER_RATE = 9, //When the gateway is setting the tracker polling rate
		ENTER_OTA_MODE = 10, // When the gateway is instructing the tracker to enter OTA update mode
        ENTER_OTA_ACK = 11 // Acknowledgment from tracker to gateway to enter OTA update mode
	};

	unsigned int channels[5] = {2, 5, 8, 11, 14}; //Channels to scan for gateway

	static unsigned int getChannel();

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

	void HandleOTAMessage(uint8_t * mac, uint8_t *data, uint8_t len);

	void SendOTAAck();

	void SendOTARequest();

	unsigned long LastPairingRequestTime = 0;

	unsigned long LastHandshakeRequestTime = 0;

	unsigned long PairingStartTime = 0;

	unsigned long LastPacketSendTime = 0;

	unsigned long LastChannelSwitchTime = 0;

	unsigned long LastTestDataSendTime = 0;

	unsigned int channelIndex = 0;

	// Heartbeat tracking
	unsigned long LastHeartbeatSendTime = 0;
	unsigned long HeartbeatSentTimestamp = 0;
	bool WaitingForHeartbeatResponse = false;
	uint8_t MissedHeartbeats = 0;
	uint16_t HeartbeatSequenceNumber = 0;

	uint16_t LastGatewayHeartbeatSequenceNumber = 0;

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

	void incrementChannel();

	void singleIncrementChannel();

	void singleIncrementChannel(bool reverse);

	void setChannel (uint8_t channel);

	uint8_t addPeer(uint8_t peerMac[6], bool defaultConfig);

	uint8_t addPeer(uint8_t peerMac[6]);

    bool deletePeer(uint8_t peerMac[6]);

	SlimeVR::Logging::Logger espnowHandlerLogger{"ESPNowHandler"};

	// Send queue for rate limiting
	struct PendingMessage {
		uint8_t peerMac[6];
		uint8_t data[128];
		size_t dataLen = 0;
		bool ephemeral = false;
		bool isHeartbeat = false;
	};
	static constexpr size_t maxQueueSize = 64;
	PendingMessage sendQueue[maxQueueSize];
	size_t queueHead = 0;
	size_t queueTail = 0;
	int queueSize() const {
		return queueHead == queueTail ? 0 : (queueHead > queueTail ? ((maxQueueSize - queueHead) + queueTail) : (queueTail - queueHead));
	}
	unsigned long lastSendTime = 0;
	static constexpr unsigned long sendRateLimit = 5;

	void queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen, bool isHeartbeat, bool ephemeral);
	void queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen, bool isHeartbeat);
	void queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen);
	void processSendQueue();

	uint8_t ota_auth[16];
	long ota_portNum;
	uint8_t ota_ip[4];

	char ssid[33];
    char password[65];
};
}  // namespace SlimeVR
