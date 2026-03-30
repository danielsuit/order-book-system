#include "test_framework.h"
#include "../src/crdt/crdt_orderbook.h"

TEST(crdt_local_add) {
    CRDTOrderBook book("node-a");
    std::string oid = book.local_add_order(Side::BUY, 100.0, 50);
    ASSERT_FALSE(oid.empty());

    auto snap = book.get_snapshot(5);
    ASSERT_EQ(snap.bids.size(), static_cast<size_t>(1));
    ASSERT_EQ(snap.bids[0].total_quantity, 50);
}

TEST(crdt_local_cancel) {
    CRDTOrderBook book("node-a");
    std::string oid = book.local_add_order(Side::SELL, 100.0, 30);
    ASSERT_TRUE(book.local_cancel_order(oid));
    ASSERT_FALSE(book.local_cancel_order(oid));

    auto snap = book.get_snapshot(5);
    ASSERT_TRUE(snap.asks.empty());
}

TEST(crdt_local_market_order) {
    CRDTOrderBook book("node-a");
    book.local_add_order(Side::SELL, 100.0, 50);
    book.local_add_order(Side::SELL, 101.0, 30);

    auto fills = book.local_market_order(Side::BUY, 60);
    ASSERT_EQ(fills.size(), static_cast<size_t>(2));
    ASSERT_EQ(fills[0].quantity, 50);
    ASSERT_EQ(fills[1].quantity, 10);
}

TEST(crdt_remote_operation_add) {
    CRDTOrderBook book_a("node-a");
    CRDTOrderBook book_b("node-b");

    book_a.local_add_order(Side::BUY, 100.0, 50);

    auto ops = book_a.get_operation_log();
    ASSERT_EQ(ops.size(), static_cast<size_t>(1));

    bool applied = book_b.apply_remote_operation(ops[0]);
    ASSERT_TRUE(applied);

    auto snap = book_b.get_snapshot(5);
    ASSERT_EQ(snap.bids.size(), static_cast<size_t>(1));
}

TEST(crdt_deduplication) {
    CRDTOrderBook book_a("node-a");
    CRDTOrderBook book_b("node-b");

    book_a.local_add_order(Side::BUY, 100.0, 50);
    auto ops = book_a.get_operation_log();

    ASSERT_TRUE(book_b.apply_remote_operation(ops[0]));
    ASSERT_FALSE(book_b.apply_remote_operation(ops[0]));
}

TEST(crdt_two_node_convergence) {
    CRDTOrderBook book_a("node-a");
    CRDTOrderBook book_b("node-b");

    book_a.local_add_order(Side::BUY, 100.0, 50);
    book_b.local_add_order(Side::SELL, 101.0, 30);

    auto ops_a = book_a.get_operation_log();
    auto ops_b = book_b.get_operation_log();

    for (auto& op : ops_a) book_b.apply_remote_operation(op);
    for (auto& op : ops_b) book_a.apply_remote_operation(op);

    auto snap_a = book_a.get_snapshot(5);
    auto snap_b = book_b.get_snapshot(5);

    ASSERT_EQ(snap_a.bids.size(), snap_b.bids.size());
    ASSERT_EQ(snap_a.asks.size(), snap_b.asks.size());
    ASSERT_EQ(snap_a.bids[0].total_quantity, snap_b.bids[0].total_quantity);
    ASSERT_EQ(snap_a.asks[0].total_quantity, snap_b.asks[0].total_quantity);
}

TEST(crdt_op_log_grows) {
    CRDTOrderBook book("node-a");
    book.local_add_order(Side::BUY, 100.0, 10);
    book.local_add_order(Side::SELL, 101.0, 20);
    ASSERT_EQ(book.get_op_log_size(), static_cast<size_t>(2));
}

TEST(crdt_clock_advances) {
    CRDTOrderBook book("node-a");
    book.local_add_order(Side::BUY, 100.0, 10);
    auto c1 = book.get_clock().get_local_time();
    book.local_add_order(Side::SELL, 101.0, 20);
    auto c2 = book.get_clock().get_local_time();
    ASSERT_GT(c2, c1);
}
