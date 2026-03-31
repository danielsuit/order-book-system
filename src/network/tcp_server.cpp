#include "tcp_server.h"
#include "../util/logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <thread>

TCPServer::TCPServer(const std::string& bind_host, uint16_t bind_port, RequestHandler handler)
    : port_(bind_port), handler_(std::move(handler)) {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) throw std::runtime_error("Failed to create TCP socket");

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(bind_port);
    inet_pton(AF_INET, bind_host.c_str(), &addr.sin_addr);

    if (bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(server_fd_);
        throw std::runtime_error("Failed to bind TCP socket on port " + std::to_string(bind_port));
    }

    if (listen(server_fd_, 16) < 0) {
        close(server_fd_);
        throw std::runtime_error("Failed to listen on TCP socket");
    }
}

TCPServer::~TCPServer() {
    stop();
    if (server_fd_ >= 0) close(server_fd_);
}

void TCPServer::start() {
    running_ = true;
    accept_thread_ = std::thread(&TCPServer::accept_loop, this);
    LOG_INFO("TCP server started on port " + std::to_string(port_));
}

void TCPServer::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
    }
    if (accept_thread_.joinable()) accept_thread_.join();
}

void TCPServer::accept_loop() {
    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        // Set a timeout so we can check running_ periodically
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(server_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int client_fd = accept(server_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
        if (client_fd < 0) continue;

        std::thread(&TCPServer::handle_client, this, client_fd).detach();
    }
}

void TCPServer::handle_client(int client_fd) {
    std::string request;
    char buf[4096];

    // Read until newline
    while (true) {
        ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        request.append(buf, static_cast<size_t>(n));
        if (request.find('\n') != std::string::npos) break;
    }

    // Strip trailing newline/whitespace
    while (!request.empty() && (request.back() == '\n' || request.back() == '\r')) {
        request.pop_back();
    }

    if (!request.empty()) {
        std::string response = handler_(request);
        response += "\n";
        // Use MSG_NOSIGNAL where available, otherwise just send
#ifdef MSG_NOSIGNAL
        send(client_fd, response.c_str(), response.size(), MSG_NOSIGNAL);
#else
        send(client_fd, response.c_str(), response.size(), 0);
#endif
    }

    close(client_fd);
}
