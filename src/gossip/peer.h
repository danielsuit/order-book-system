#pragma once
#include <string>
#include <chrono>
#include <cstdint>

struct PeerInfo {
    std::string node_id;
    std::string host;
    uint16_t port = 0;
    bool is_alive = true;
    std::chrono::steady_clock::time_point last_heard = std::chrono::steady_clock::now();
};
