#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "../crdt/crdt_orderbook.h"

enum class MessageType : uint8_t {
    GOSSIP_PUSH       = 0x01,
    GOSSIP_PULL       = 0x02,
    GOSSIP_PUSH_PULL  = 0x03,
    HEARTBEAT         = 0x04,
    ANTI_ENTROPY_REQ  = 0x05,
    ANTI_ENTROPY_RES  = 0x06,
};

struct Message {
    MessageType type;
    std::string sender_id;
    VectorClock sender_clock;
    std::vector<Operation> operations;

    std::vector<uint8_t> serialize() const;
    static Message deserialize(const uint8_t* data, size_t len);
};
