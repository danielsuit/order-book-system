#include "uuid.h"
#include <random>
#include <sstream>
#include <iomanip>

std::string generate_uuid() {
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    uint32_t a = dist(gen);
    uint16_t b = static_cast<uint16_t>(dist(gen));
    uint16_t c = static_cast<uint16_t>((dist(gen) & 0x0FFF) | 0x4000); // Version 4
    uint16_t d = static_cast<uint16_t>((dist(gen) & 0x3FFF) | 0x8000); // Variant 1
    uint16_t e1 = static_cast<uint16_t>(dist(gen));
    uint32_t e2 = dist(gen);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << a << "-"
       << std::setw(4) << b << "-"
       << std::setw(4) << c << "-"
       << std::setw(4) << d << "-"
       << std::setw(4) << e1
       << std::setw(8) << e2;
    return ss.str();
}
