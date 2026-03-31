#include "crdt_orderbook.h"
#include "../util/uuid.h"

CRDTOrderBook::CRDTOrderBook(const std::string& node_id)
    : node_id_(node_id), clock_(node_id) {}

std::string CRDTOrderBook::local_add_order(Side side, double price, int64_t quantity) {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t ts = clock_.tick();
    std::string oid = generate_uuid();

    Order order;
    order.order_id = oid;
    order.node_id = node_id_;
    order.side = side;
    order.price = price;
    order.quantity = quantity;
    order.remaining = quantity;
    order.timestamp = ts;

    book_.add_order(order);

    Operation op;
    op.type = OpType::ADD_ORDER;
    op.order = order;
    op.origin_node = node_id_;
    op.clock = clock_;
    op.op_id = generate_uuid();

    op_log_.push_back(op);
    seen_ops_.insert(op.op_id);

    return oid;
}

bool CRDTOrderBook::local_cancel_order(const std::string& order_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!book_.has_order(order_id)) return false;

    clock_.tick();
    bool cancelled = book_.cancel_order(order_id);
    if (!cancelled) return false;

    Operation op;
    op.type = OpType::CANCEL_ORDER;
    op.cancel_order_id = order_id;
    op.origin_node = node_id_;
    op.clock = clock_;
    op.op_id = generate_uuid();

    op_log_.push_back(op);
    seen_ops_.insert(op.op_id);

    return true;
}

std::vector<Fill> CRDTOrderBook::local_market_order(Side side, int64_t quantity) {
    std::lock_guard<std::mutex> lock(mutex_);

    clock_.tick();
    auto fills = book_.match_market_order(side, quantity);

    // Generate cancel ops for fully filled orders so other nodes learn about them
    for (auto& fill : fills) {
        if (!book_.has_order(fill.order_id)) {
            Operation op;
            op.type = OpType::CANCEL_ORDER;
            op.cancel_order_id = fill.order_id;
            op.origin_node = node_id_;
            op.clock = clock_;
            op.op_id = generate_uuid();

            op_log_.push_back(op);
            seen_ops_.insert(op.op_id);
        }
    }

    return fills;
}

bool CRDTOrderBook::apply_remote_operation(const Operation& op) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (seen_ops_.count(op.op_id)) return false;

    clock_.merge(op.clock);

    if (op.type == OpType::ADD_ORDER) {
        book_.insert_replicated_order(op.order);
    } else if (op.type == OpType::CANCEL_ORDER) {
        book_.apply_replicated_cancel(op.cancel_order_id);
    }

    op_log_.push_back(op);
    seen_ops_.insert(op.op_id);

    return true;
}

std::vector<Operation> CRDTOrderBook::get_operation_log() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return op_log_;
}

std::vector<Operation> CRDTOrderBook::get_operations_since(const VectorClock& peer_clock) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Operation> result;

    for (auto& op : op_log_) {
        // If the peer's clock doesn't dominate this op's clock, they might be missing it
        if (!op.clock.happens_before(peer_clock) && !(op.clock.get_clock() == peer_clock.get_clock())) {
            result.push_back(op);
        }
    }

    return result;
}

VectorClock CRDTOrderBook::get_clock() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clock_;
}

BookSnapshot CRDTOrderBook::get_snapshot(int depth) const {
    return book_.get_snapshot(depth);
}

std::optional<PriceLevel> CRDTOrderBook::get_best_bid() const {
    return book_.get_best_bid();
}

std::optional<PriceLevel> CRDTOrderBook::get_best_ask() const {
    return book_.get_best_ask();
}

size_t CRDTOrderBook::get_op_log_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return op_log_.size();
}

size_t CRDTOrderBook::get_order_count() const {
    return book_.get_all_orders().size();
}
