#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>
#include <vector>

class VectorClock {
public:
    VectorClock() = default;
    explicit VectorClock(const std::string& node_id);

    // Increment this node's counter. Returns the new value.
    uint64_t tick();

    // Merge another vector clock (take max of each entry).
    void merge(const VectorClock& other);

    // Comparison
    bool happens_before(const VectorClock& other) const;
    bool is_concurrent(const VectorClock& other) const;

    // Get this node's current counter value
    uint64_t get_local_time() const;

    // Serialization
    std::vector<uint8_t> serialize() const;
    static VectorClock deserialize(const uint8_t* data, size_t len);

    const std::unordered_map<std::string, uint64_t>& get_clock() const;
    const std::string& get_node_id() const { return node_id_; }

private:
    std::string node_id_;
    std::unordered_map<std::string, uint64_t> clock_;
};
