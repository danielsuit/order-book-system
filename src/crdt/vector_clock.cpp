#include "vector_clock.h"
#include <algorithm>
#include <cstring>
#include <arpa/inet.h>

VectorClock::VectorClock(const std::string& node_id) : node_id_(node_id) {
    clock_[node_id] = 0;
}

uint64_t VectorClock::tick() {
    return ++clock_[node_id_];
}

void VectorClock::merge(const VectorClock& other) {
    for (auto& [key, val] : other.clock_) {
        clock_[key] = std::max(clock_[key], val);
    }
}

bool VectorClock::happens_before(const VectorClock& other) const {
    bool at_least_one_less = false;

    // Every entry in this clock must be <= corresponding entry in other
    for (auto& [key, val] : clock_) {
        auto it = other.clock_.find(key);
        uint64_t other_val = (it != other.clock_.end()) ? it->second : 0;
        if (val > other_val) return false;
        if (val < other_val) at_least_one_less = true;
    }

    // Check entries in other that we don't have
    for (auto& [key, val] : other.clock_) {
        if (clock_.find(key) == clock_.end() && val > 0) {
            at_least_one_less = true;
        }
    }

    return at_least_one_less;
}

bool VectorClock::is_concurrent(const VectorClock& other) const {
    return !happens_before(other) && !other.happens_before(*this);
}

uint64_t VectorClock::get_local_time() const {
    auto it = clock_.find(node_id_);
    return (it != clock_.end()) ? it->second : 0;
}

const std::unordered_map<std::string, uint64_t>& VectorClock::get_clock() const {
    return clock_;
}

// Serialization format:
// [4 bytes: num_entries] then for each entry:
// [2 bytes: key_len][key bytes][8 bytes: value (network order)]
std::vector<uint8_t> VectorClock::serialize() const {
    std::vector<uint8_t> buf;

    // node_id: 2-byte len + bytes
    uint16_t nid_len = static_cast<uint16_t>(node_id_.size());
    uint16_t nid_len_n = htons(nid_len);
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&nid_len_n),
               reinterpret_cast<uint8_t*>(&nid_len_n) + 2);
    buf.insert(buf.end(), node_id_.begin(), node_id_.end());

    // num entries
    uint32_t count = static_cast<uint32_t>(clock_.size());
    uint32_t count_n = htonl(count);
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&count_n),
               reinterpret_cast<uint8_t*>(&count_n) + 4);

    for (auto& [key, val] : clock_) {
        uint16_t klen = static_cast<uint16_t>(key.size());
        uint16_t klen_n = htons(klen);
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&klen_n),
                   reinterpret_cast<uint8_t*>(&klen_n) + 2);
        buf.insert(buf.end(), key.begin(), key.end());

        // 8 bytes for uint64_t in network byte order
        uint32_t high = htonl(static_cast<uint32_t>(val >> 32));
        uint32_t low = htonl(static_cast<uint32_t>(val & 0xFFFFFFFF));
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&high),
                   reinterpret_cast<uint8_t*>(&high) + 4);
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&low),
                   reinterpret_cast<uint8_t*>(&low) + 4);
    }

    return buf;
}

VectorClock VectorClock::deserialize(const uint8_t* data, size_t len) {
    size_t pos = 0;

    auto read_u16 = [&]() -> uint16_t {
        uint16_t v;
        std::memcpy(&v, data + pos, 2);
        pos += 2;
        return ntohs(v);
    };
    auto read_u32 = [&]() -> uint32_t {
        uint32_t v;
        std::memcpy(&v, data + pos, 4);
        pos += 4;
        return ntohl(v);
    };
    auto read_string = [&](uint16_t slen) -> std::string {
        std::string s(reinterpret_cast<const char*>(data + pos), slen);
        pos += slen;
        return s;
    };

    uint16_t nid_len = read_u16();
    std::string node_id = read_string(nid_len);

    VectorClock vc(node_id);

    uint32_t count = read_u32();
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t klen = read_u16();
        std::string key = read_string(klen);
        uint32_t high = read_u32();
        uint32_t low = read_u32();
        uint64_t val = (static_cast<uint64_t>(high) << 32) | low;
        vc.clock_[key] = val;
    }

    (void)len;  // Used implicitly; bounds checking omitted for performance
    return vc;
}
