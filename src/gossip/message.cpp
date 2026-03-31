#include "message.h"
#include <cstring>
#include <arpa/inet.h>
#include <stdexcept>

// Helper functions for serialization
namespace {

void write_u8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

void write_u16(std::vector<uint8_t>& buf, uint16_t v) {
    uint16_t n = htons(v);
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&n),
               reinterpret_cast<uint8_t*>(&n) + 2);
}

void write_u32(std::vector<uint8_t>& buf, uint32_t v) {
    uint32_t n = htonl(v);
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&n),
               reinterpret_cast<uint8_t*>(&n) + 4);
}

void write_u64(std::vector<uint8_t>& buf, uint64_t v) {
    write_u32(buf, static_cast<uint32_t>(v >> 32));
    write_u32(buf, static_cast<uint32_t>(v & 0xFFFFFFFF));
}

void write_string(std::vector<uint8_t>& buf, const std::string& s) {
    write_u16(buf, static_cast<uint16_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

void write_double(std::vector<uint8_t>& buf, double v) {
    int64_t iv = static_cast<int64_t>(v * 10000.0);
    write_u64(buf, static_cast<uint64_t>(iv));
}

void write_order(std::vector<uint8_t>& buf, const Order& o) {
    write_string(buf, o.order_id);
    write_string(buf, o.node_id);
    write_u8(buf, static_cast<uint8_t>(o.side));
    write_double(buf, o.price);
    write_u64(buf, static_cast<uint64_t>(o.quantity));
    write_u64(buf, static_cast<uint64_t>(o.remaining));
    write_u64(buf, o.timestamp);
    write_u8(buf, o.is_cancelled ? 1 : 0);
}

void write_operation(std::vector<uint8_t>& buf, const Operation& op) {
    write_u8(buf, static_cast<uint8_t>(op.type));
    write_order(buf, op.order);
    write_string(buf, op.cancel_order_id);
    write_string(buf, op.origin_node);

    auto clock_bytes = op.clock.serialize();
    write_u32(buf, static_cast<uint32_t>(clock_bytes.size()));
    buf.insert(buf.end(), clock_bytes.begin(), clock_bytes.end());

    write_string(buf, op.op_id);
}

class Reader {
public:
    Reader(const uint8_t* data, size_t len) : data_(data), len_(len), pos_(0) {}

    uint8_t read_u8() {
        check(1);
        return data_[pos_++];
    }

    uint16_t read_u16() {
        check(2);
        uint16_t v;
        std::memcpy(&v, data_ + pos_, 2);
        pos_ += 2;
        return ntohs(v);
    }

    uint32_t read_u32() {
        check(4);
        uint32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        pos_ += 4;
        return ntohl(v);
    }

    uint64_t read_u64() {
        uint32_t high = read_u32();
        uint32_t low = read_u32();
        return (static_cast<uint64_t>(high) << 32) | low;
    }

    std::string read_string() {
        uint16_t len = read_u16();
        check(len);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return s;
    }

    double read_double() {
        int64_t iv = static_cast<int64_t>(read_u64());
        return static_cast<double>(iv) / 10000.0;
    }

    Order read_order() {
        Order o;
        o.order_id = read_string();
        o.node_id = read_string();
        o.side = static_cast<Side>(read_u8());
        o.price = read_double();
        o.quantity = static_cast<int64_t>(read_u64());
        o.remaining = static_cast<int64_t>(read_u64());
        o.timestamp = read_u64();
        o.is_cancelled = read_u8() != 0;
        return o;
    }

    Operation read_operation() {
        Operation op;
        op.type = static_cast<OpType>(read_u8());
        op.order = read_order();
        op.cancel_order_id = read_string();
        op.origin_node = read_string();

        uint32_t clock_len = read_u32();
        check(clock_len);
        op.clock = VectorClock::deserialize(data_ + pos_, clock_len);
        pos_ += clock_len;

        op.op_id = read_string();
        return op;
    }

    size_t pos() const { return pos_; }

private:
    void check(size_t need) {
        if (pos_ + need > len_) throw std::runtime_error("Message deserialization: buffer overrun");
    }
    const uint8_t* data_;
    size_t len_;
    size_t pos_;
};

} // anonymous namespace

std::vector<uint8_t> Message::serialize() const {
    std::vector<uint8_t> buf;

    // msg_type (1 byte)
    write_u8(buf, static_cast<uint8_t>(type));

    // placeholder for msg_len (4 bytes) — fill in at the end
    size_t len_pos = buf.size();
    write_u32(buf, 0);

    // sender_id (length-prefixed string, padded/truncated to fit)
    write_string(buf, sender_id);

    // sender vector clock
    auto clock_bytes = sender_clock.serialize();
    write_u32(buf, static_cast<uint32_t>(clock_bytes.size()));
    buf.insert(buf.end(), clock_bytes.begin(), clock_bytes.end());

    // operations
    write_u32(buf, static_cast<uint32_t>(operations.size()));
    for (auto& op : operations) {
        write_operation(buf, op);
    }

    // Fill in msg_len
    uint32_t total_len = htonl(static_cast<uint32_t>(buf.size()));
    std::memcpy(buf.data() + len_pos, &total_len, 4);

    return buf;
}

Message Message::deserialize(const uint8_t* data, size_t len) {
    Reader r(data, len);
    Message msg;

    msg.type = static_cast<MessageType>(r.read_u8());
    /*uint32_t msg_len =*/ r.read_u32(); // total length, not needed for reading

    msg.sender_id = r.read_string();

    uint32_t clock_len = r.read_u32();
    // We need to read clock_len bytes for the vector clock
    msg.sender_clock = VectorClock::deserialize(data + r.pos(), clock_len);
    // Advance reader past clock bytes manually by reading them
    for (uint32_t i = 0; i < clock_len; ++i) r.read_u8();

    uint32_t op_count = r.read_u32();
    for (uint32_t i = 0; i < op_count; ++i) {
        msg.operations.push_back(r.read_operation());
    }

    return msg;
}
