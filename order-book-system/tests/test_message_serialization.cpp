#include "test_framework.h"
#include "../src/gossip/message.h"

TEST(message_serialize_heartbeat) {
    Message msg;
    msg.type = MessageType::HEARTBEAT;
    msg.sender_id = "node-a";
    msg.sender_clock = VectorClock("node-a");
    msg.sender_clock.tick();

    auto bytes = msg.serialize();
    ASSERT_TRUE(bytes.size() > 0);

    auto msg2 = Message::deserialize(bytes.data(), bytes.size());
    ASSERT_EQ(static_cast<uint8_t>(msg2.type), static_cast<uint8_t>(MessageType::HEARTBEAT));
    ASSERT_EQ(msg2.sender_id, std::string("node-a"));
    ASSERT_TRUE(msg2.operations.empty());
}

TEST(message_serialize_with_operations) {
    Message msg;
    msg.type = MessageType::GOSSIP_PUSH;
    msg.sender_id = "node-b";
    msg.sender_clock = VectorClock("node-b");
    msg.sender_clock.tick();
    msg.sender_clock.tick();

    Operation op;
    op.type = OpType::ADD_ORDER;
    op.order.order_id = "order-123";
    op.order.node_id = "node-b";
    op.order.side = Side::BUY;
    op.order.price = 150.25;
    op.order.quantity = 100;
    op.order.remaining = 100;
    op.order.timestamp = 2;
    op.order.is_cancelled = false;
    op.origin_node = "node-b";
    op.clock = msg.sender_clock;
    op.op_id = "op-456";

    msg.operations.push_back(op);

    auto bytes = msg.serialize();
    auto msg2 = Message::deserialize(bytes.data(), bytes.size());

    ASSERT_EQ(static_cast<uint8_t>(msg2.type), static_cast<uint8_t>(MessageType::GOSSIP_PUSH));
    ASSERT_EQ(msg2.sender_id, std::string("node-b"));
    ASSERT_EQ(msg2.operations.size(), static_cast<size_t>(1));

    auto& op2 = msg2.operations[0];
    ASSERT_EQ(static_cast<uint8_t>(op2.type), static_cast<uint8_t>(OpType::ADD_ORDER));
    ASSERT_EQ(op2.order.order_id, std::string("order-123"));
    ASSERT_EQ(static_cast<uint8_t>(op2.order.side), static_cast<uint8_t>(Side::BUY));
    ASSERT_TRUE(op2.order.price > 150.24 && op2.order.price < 150.26);
    ASSERT_EQ(op2.order.quantity, 100);
    ASSERT_EQ(op2.order.remaining, 100);
    ASSERT_EQ(op2.order.timestamp, static_cast<uint64_t>(2));
    ASSERT_FALSE(op2.order.is_cancelled);
    ASSERT_EQ(op2.op_id, std::string("op-456"));
}

TEST(message_serialize_cancel_operation) {
    Message msg;
    msg.type = MessageType::GOSSIP_PUSH;
    msg.sender_id = "node-c";
    msg.sender_clock = VectorClock("node-c");

    Operation op;
    op.type = OpType::CANCEL_ORDER;
    op.cancel_order_id = "order-789";
    op.origin_node = "node-c";
    op.clock = msg.sender_clock;
    op.op_id = "op-cancel-1";

    msg.operations.push_back(op);

    auto bytes = msg.serialize();
    auto msg2 = Message::deserialize(bytes.data(), bytes.size());

    ASSERT_EQ(msg2.operations.size(), static_cast<size_t>(1));
    ASSERT_EQ(static_cast<uint8_t>(msg2.operations[0].type), static_cast<uint8_t>(OpType::CANCEL_ORDER));
    ASSERT_EQ(msg2.operations[0].cancel_order_id, std::string("order-789"));
}

TEST(message_serialize_multiple_operations) {
    Message msg;
    msg.type = MessageType::ANTI_ENTROPY_RES;
    msg.sender_id = "node-a";
    msg.sender_clock = VectorClock("node-a");

    for (int i = 0; i < 5; ++i) {
        Operation op;
        op.type = OpType::ADD_ORDER;
        op.order.order_id = "order-" + std::to_string(i);
        op.order.node_id = "node-a";
        op.order.side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        op.order.price = 100.0 + i;
        op.order.quantity = 10 * (i + 1);
        op.order.remaining = 10 * (i + 1);
        op.order.timestamp = static_cast<uint64_t>(i + 1);
        op.origin_node = "node-a";
        op.clock = msg.sender_clock;
        op.op_id = "op-" + std::to_string(i);
        msg.operations.push_back(op);
    }

    auto bytes = msg.serialize();
    auto msg2 = Message::deserialize(bytes.data(), bytes.size());

    ASSERT_EQ(msg2.operations.size(), static_cast<size_t>(5));
    for (size_t i = 0; i < 5; ++i) {
        ASSERT_EQ(msg2.operations[i].order.order_id,
                  std::string("order-" + std::to_string(i)));
    }
}
