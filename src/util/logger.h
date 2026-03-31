#pragma once
#include <string>
#include <iostream>
#include <chrono>
#include <mutex>
#include <sstream>

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void set_node_id(const std::string& node_id) { node_id_ = node_id; }
    void set_level(LogLevel level) { level_ = level; }

    void log(LogLevel level, const std::string& msg) {
        if (level < level_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();

        std::cerr << "{\"ts\":" << millis
                  << ",\"node\":\"" << node_id_
                  << "\",\"level\":\"" << level_str(level)
                  << "\",\"msg\":\"" << msg << "\"}\n";
    }

    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void warn(const std::string& msg) { log(LogLevel::WARN, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }

private:
    Logger() = default;
    static const char* level_str(LogLevel l) {
        switch (l) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARN: return "WARN";
            case LogLevel::ERROR: return "ERROR";
        }
        return "UNKNOWN";
    }

    std::string node_id_ = "unknown";
    LogLevel level_ = LogLevel::INFO;
    std::mutex mutex_;
};

#define LOG_INFO(msg) Logger::instance().info(msg)
#define LOG_WARN(msg) Logger::instance().warn(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)
#define LOG_DEBUG(msg) Logger::instance().debug(msg)
