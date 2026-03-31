#pragma once
#include <string>
#include <vector>
#include <cstdint>

class UDPTransport {
public:
    UDPTransport(const std::string& bind_host, uint16_t bind_port);
    ~UDPTransport();

    bool send_to(const std::string& host, uint16_t port, const uint8_t* data, size_t len);
    std::vector<uint8_t> recv_from(std::string& sender_host, uint16_t& sender_port, int timeout_ms = 1000);

    uint16_t get_port() const { return port_; }

private:
    int sockfd_ = -1;
    uint16_t port_;
};
