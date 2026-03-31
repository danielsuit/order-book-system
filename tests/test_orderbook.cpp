#include "test_framework.h"
#include "../src/orderbook/orderbook.h"

TEST(orderbook_add_and_best_bid) {
    OrderBook book;
    Order o;
    o.order_id = "o1"; o.node_id = "n1"; o.side = Side::BUY;
    o.price = 100.0; o.quantity = 50; o.remaining = 50; o.timestamp = 1;
    book.add_order(o);

    auto best = book.get_best_bid();
    ASSERT_TRUE(best.has_value());
    ASSERT_EQ(best->price, 100.0);
    ASSERT_EQ(best->total_quantity, 50);
}

TEST(orderbook_add_and_best_ask) {
    OrderBook book;
    Order o;
    o.order_id = "o1"; o.node_id = "n1"; o.side = Side::SELL;
    o.price = 150.0; o.quantity = 30; o.remaining = 30; o.timestamp = 1;
    book.add_order(o);

    auto best = book.get_best_ask();
    ASSERT_TRUE(best.has_value());
    ASSERT_EQ(best->price, 150.0);
    ASSERT_EQ(best->total_quantity, 30);
}

TEST(orderbook_multiple_bids_sorted) {
    OrderBook book;
    Order o1; o1.order_id="o1"; o1.node_id="n1"; o1.side=Side::BUY;
    o1.price=100.0; o1.quantity=10; o1.remaining=10; o1.timestamp=1;
    Order o2; o2.order_id="o2"; o2.node_id="n1"; o2.side=Side::BUY;
    o2.price=105.0; o2.quantity=20; o2.remaining=20; o2.timestamp=2;
    book.add_order(o1);
    book.add_order(o2);

    auto best = book.get_best_bid();
    ASSERT_TRUE(best.has_value());
    ASSERT_EQ(best->price, 105.0);
}

TEST(orderbook_cancel_order) {
    OrderBook book;
    Order o; o.order_id="o1"; o.node_id="n1"; o.side=Side::BUY;
    o.price=100.0; o.quantity=10; o.remaining=10; o.timestamp=1;
    book.add_order(o);

    ASSERT_TRUE(book.cancel_order("o1"));
    ASSERT_FALSE(book.get_best_bid().has_value());
    ASSERT_FALSE(book.cancel_order("o1"));
    ASSERT_FALSE(book.cancel_order("nonexistent"));
}

TEST(orderbook_match_market_buy) {
    OrderBook book;
    Order a1; a1.order_id="a1"; a1.node_id="n1"; a1.side=Side::SELL;
    a1.price=100.0; a1.quantity=50; a1.remaining=50; a1.timestamp=1;
    Order a2; a2.order_id="a2"; a2.node_id="n1"; a2.side=Side::SELL;
    a2.price=101.0; a2.quantity=50; a2.remaining=50; a2.timestamp=2;
    book.add_order(a1);
    book.add_order(a2);

    auto fills = book.match_market_order(Side::BUY, 70);
    ASSERT_EQ(fills.size(), static_cast<size_t>(2));
    ASSERT_EQ(fills[0].price, 100.0);
    ASSERT_EQ(fills[0].quantity, 50);
    ASSERT_EQ(fills[1].price, 101.0);
    ASSERT_EQ(fills[1].quantity, 20);

    auto best = book.get_best_ask();
    ASSERT_TRUE(best.has_value());
    ASSERT_EQ(best->price, 101.0);
    ASSERT_EQ(best->total_quantity, 30);
}

TEST(orderbook_match_market_sell) {
    OrderBook book;
    Order b1; b1.order_id="b1"; b1.node_id="n1"; b1.side=Side::BUY;
    b1.price=100.0; b1.quantity=30; b1.remaining=30; b1.timestamp=1;
    Order b2; b2.order_id="b2"; b2.node_id="n1"; b2.side=Side::BUY;
    b2.price=99.0; b2.quantity=30; b2.remaining=30; b2.timestamp=2;
    book.add_order(b1);
    book.add_order(b2);

    auto fills = book.match_market_order(Side::SELL, 40);
    ASSERT_EQ(fills.size(), static_cast<size_t>(2));
    ASSERT_EQ(fills[0].price, 100.0);
    ASSERT_EQ(fills[0].quantity, 30);
    ASSERT_EQ(fills[1].price, 99.0);
    ASSERT_EQ(fills[1].quantity, 10);
}

TEST(orderbook_snapshot) {
    OrderBook book;
    Order b; b.order_id="b1"; b.node_id="n1"; b.side=Side::BUY;
    b.price=100.0; b.quantity=10; b.remaining=10; b.timestamp=1;
    Order a; a.order_id="a1"; a.node_id="n1"; a.side=Side::SELL;
    a.price=101.0; a.quantity=20; a.remaining=20; a.timestamp=2;
    book.add_order(b);
    book.add_order(a);

    auto snap = book.get_snapshot(5);
    ASSERT_EQ(snap.bids.size(), static_cast<size_t>(1));
    ASSERT_EQ(snap.asks.size(), static_cast<size_t>(1));
    ASSERT_EQ(snap.bids[0].price, 100.0);
    ASSERT_EQ(snap.asks[0].price, 101.0);
}

TEST(orderbook_has_order) {
    OrderBook book;
    Order o; o.order_id="o1"; o.node_id="n1"; o.side=Side::BUY;
    o.price=100.0; o.quantity=10; o.remaining=10; o.timestamp=1;
    book.add_order(o);
    ASSERT_TRUE(book.has_order("o1"));
    ASSERT_FALSE(book.has_order("o2"));
}

TEST(orderbook_replicated_insert) {
    OrderBook book;
    Order o; o.order_id="o1"; o.node_id="n1"; o.side=Side::SELL;
    o.price=100.0; o.quantity=10; o.remaining=10; o.timestamp=1;
    book.insert_replicated_order(o);
    ASSERT_TRUE(book.has_order("o1"));

    book.insert_replicated_order(o);
    auto best = book.get_best_ask();
    ASSERT_EQ(best->total_quantity, 10);
}

TEST(orderbook_replicated_cancel) {
    OrderBook book;
    Order o; o.order_id="o1"; o.node_id="n1"; o.side=Side::BUY;
    o.price=100.0; o.quantity=10; o.remaining=10; o.timestamp=1;
    book.insert_replicated_order(o);
    book.apply_replicated_cancel("o1");
    ASSERT_FALSE(book.get_best_bid().has_value());
}

TEST(orderbook_get_all_orders) {
    OrderBook book;
    Order o1; o1.order_id="o1"; o1.node_id="n1"; o1.side=Side::BUY;
    o1.price=100.0; o1.quantity=10; o1.remaining=10; o1.timestamp=1;
    Order o2; o2.order_id="o2"; o2.node_id="n1"; o2.side=Side::SELL;
    o2.price=101.0; o2.quantity=20; o2.remaining=20; o2.timestamp=2;
    book.add_order(o1);
    book.add_order(o2);

    auto all = book.get_all_orders();
    ASSERT_EQ(all.size(), static_cast<size_t>(2));
}

TEST(orderbook_fifo_priority) {
    OrderBook book;
    Order o1; o1.order_id="o1"; o1.node_id="n1"; o1.side=Side::SELL;
    o1.price=100.0; o1.quantity=10; o1.remaining=10; o1.timestamp=1;
    Order o2; o2.order_id="o2"; o2.node_id="n1"; o2.side=Side::SELL;
    o2.price=100.0; o2.quantity=10; o2.remaining=10; o2.timestamp=2;
    book.add_order(o1);
    book.add_order(o2);

    auto fills = book.match_market_order(Side::BUY, 10);
    ASSERT_EQ(fills.size(), static_cast<size_t>(1));
    ASSERT_EQ(fills[0].order_id, std::string("o1"));
}
