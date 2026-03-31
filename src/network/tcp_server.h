#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>

class TCPServer {
public:
    using RequestHandler = std::function<std::string(const std::string&)>;

    TCPServer(const std::string& bind_host, uint16_t bind_port, RequestHandler handler);
    ~TCPServer();

    void start();
    void stop();

private:
    void accept_loop();
    void handle_client(int client_fd);

    int server_fd_ = -1;
    uint16_t port_;
    RequestHandler handler_;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
};
