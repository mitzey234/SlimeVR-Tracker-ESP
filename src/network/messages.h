#pragma once

#include <cstdint>
#include "espnowhandler.h"

namespace SlimeVR {
struct ESPNowPairingAnnouncementMessage {
    ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::PAIRING_ANNOUNCEMENT;
	uint8_t channel;
    uint8_t securityBytes[8];
} __attribute__((packed));

struct ESPNowPairingMessage {
    ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::PAIRING_REQUEST;
    uint8_t securityBytes[8];
} __attribute__((packed));

struct ESPNowPairingAckMessage {
    ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::PAIRING_RESPONSE;
} __attribute__((packed));

struct ESPNowConnectionMessage {
    ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::HANDSHAKE_REQUEST;
    uint8_t securityBytes[8];
} __attribute__((packed));

struct ESPNowConnectionAckMessage {
    ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::HANDSHAKE_RESPONSE;
	uint8_t channel;
    uint8_t trackerId;
} __attribute__((packed));

struct ESPNowPacketMessage {
    ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::TRACKER_DATA;
	uint8_t len;
    uint8_t data[240]; //Probably correct size
} __attribute__((packed));

struct ESPNowHeartbeatEchoMessage {
    ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::HEARTBEAT_ECHO;
	uint16_t sequenceNumber;
} __attribute__((packed));

struct ESPNowHeartbeatResponseMessage {
    ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::HEARTBEAT_RESPONSE;
	uint16_t sequenceNumber;
} __attribute__((packed));

struct ESPNowUnpairMessage {
	ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::UNPAIR;
	uint8_t securityBytes[8];
} __attribute__((packed));

struct ESPNowTrackerRateMessage {
	ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::TRACKER_RATE;
	uint32_t rateHz; // Requested polling rate in Hz
} __attribute__((packed));

struct ESPNowEnterOtaModeMessage {
	ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::ENTER_OTA_MODE;
	uint8_t securityBytes[8];
    uint8_t ota_auth[16];
    long ota_portNum;
    uint8_t ota_ip[4];
	char ssid[33];
    char password[65];
} __attribute__((packed));

struct ESPNowEnterOtaAckMessage {
    ESPNow::ESPNowMessageTypes header = ESPNow::ESPNowMessageTypes::ENTER_OTA_ACK;
} __attribute__((packed));

struct ESPNowMessageBase {
    ESPNow::ESPNowMessageTypes header;
} __attribute__((packed));

union ESPNowMessage {
    ESPNowMessageBase base;
    ESPNowPairingMessage pairing;
    ESPNowPairingAckMessage pairingAck;
    ESPNowConnectionMessage connection;
    ESPNowPacketMessage packet;
    ESPNowPairingAnnouncementMessage pairingAnnouncement;
    ESPNowConnectionAckMessage connectionAck;
    ESPNowHeartbeatEchoMessage heartbeatEcho;
    ESPNowHeartbeatResponseMessage heartbeatResponse;
    ESPNowTrackerRateMessage trackerRate;
    ESPNowEnterOtaModeMessage enterOtaMode;
    ESPNowEnterOtaAckMessage enterOtaAck;
};
}  // namespace SlimeVR
