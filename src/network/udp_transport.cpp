#include "udp_transport.h"
#include "../util/logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

UDPTransport::UDPTransport(const std::string& bind_host, uint16_t bind_port) : port_(bind_port) {
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        throw std::runtime_error("Failed to create UDP socket");
    }

    int opt = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(bind_port);
    inet_pton(AF_INET, bind_host.c_str(), &addr.sin_addr);

    if (bind(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sockfd_);
        throw std::runtime_error("Failed to bind UDP socket on port " + std::to_string(bind_port));
    }
}

UDPTransport::~UDPTransport() {
    if (sockfd_ >= 0) close(sockfd_);
}

bool UDPTransport::send_to(const std::string& host, uint16_t port, const uint8_t* data, size_t len) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) {
        LOG_WARN("DNS resolution failed for " + host);
        return false;
    }

    ssize_t sent = sendto(sockfd_, data, len, 0, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    return sent == static_cast<ssize_t>(len);
}

std::vector<uint8_t> UDPTransport::recv_from(std::string& sender_host, uint16_t& sender_port, int timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::vector<uint8_t> buf(65535);
    struct sockaddr_in sender_addr{};
    socklen_t addr_len = sizeof(sender_addr);

    ssize_t n = recvfrom(sockfd_, buf.data(), buf.size(), 0,
                         reinterpret_cast<struct sockaddr*>(&sender_addr), &addr_len);
    if (n <= 0) return {};

    buf.resize(static_cast<size_t>(n));

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sender_addr.sin_addr, ip, sizeof(ip));
    sender_host = ip;
    sender_port = ntohs(sender_addr.sin_port);

    return buf;
}
