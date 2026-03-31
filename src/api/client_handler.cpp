#include "client_handler.h"
#include "../quantum/julia_bridge.h"
#include "../quantum/quantum_matcher.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

std::string ClientHandler::handle(const std::string& request, CRDTOrderBook& book) {
    std::istringstream ss(request);
    std::string cmd;
    ss >> cmd;
    cmd = to_upper(cmd);

    if (cmd == "ADD") {
        std::string side_str;
        double price;
        int64_t qty;
        ss >> side_str >> price >> qty;
        side_str = to_upper(side_str);

        Side side;
        if (side_str == "BUY") side = Side::BUY;
        else if (side_str == "SELL") side = Side::SELL;
        else return "ERROR invalid_side";

        if (price <= 0 || qty <= 0) return "ERROR invalid_params";

        std::string oid = book.local_add_order(side, price, qty);
        return "OK " + oid;

    } else if (cmd == "CANCEL") {
        std::string order_id;
        ss >> order_id;
        if (order_id.empty()) return "ERROR missing_order_id";
        bool ok = book.local_cancel_order(order_id);
        return ok ? "OK" : "ERROR order_not_found";

    } else if (cmd == "MARKET") {
        std::string side_str;
        int64_t qty;
        ss >> side_str >> qty;
        side_str = to_upper(side_str);

        Side side;
        if (side_str == "BUY") side = Side::BUY;
        else if (side_str == "SELL") side = Side::SELL;
        else return "ERROR invalid_side";

        if (qty <= 0) return "ERROR invalid_quantity";

        auto fills = book.local_market_order(side, qty);
        std::ostringstream out;
        out << "FILLED " << fills.size();
        for (auto& f : fills) {
            out << "\n" << std::fixed << std::setprecision(4) << f.price << " " << f.quantity;
        }
        return out.str();

    } else if (cmd == "BOOK") {
        int depth = 10;
        ss >> depth;
        if (depth <= 0) depth = 10;

        auto snap = book.get_snapshot(depth);
        std::ostringstream out;
        out << std::fixed << std::setprecision(4);
        out << "BIDS";
        for (auto& level : snap.bids) {
            out << "\n" << level.price << " " << level.total_quantity;
        }
        out << "\nASKS";
        for (auto& level : snap.asks) {
            out << "\n" << level.price << " " << level.total_quantity;
        }
        return out.str();

    } else if (cmd == "STATUS") {
        auto clock = book.get_clock();
        std::ostringstream out;
        out << "NODE " << book.get_node_id()
            << "\nORDERS " << book.get_order_count()
            << "\nOP_LOG " << book.get_op_log_size()
            << "\nCLOCK";
        for (auto& [k, v] : clock.get_clock()) {
            out << " " << k << "=" << v;
        }
        return out.str();

    } else if (cmd == "PEERS") {
        // This needs access to gossip engine — handled externally
        return "ERROR peers_not_available_here";

    } else if (cmd == "METRICS") {
        auto clock = book.get_clock();
        std::ostringstream out;
        out << "# HELP convergence_orders_total Total active orders\n"
            << "# TYPE convergence_orders_total gauge\n"
            << "convergence_orders_total{node=\"" << book.get_node_id() << "\"} "
            << book.get_order_count() << "\n"
            << "# HELP convergence_op_log_size Operation log size\n"
            << "# TYPE convergence_op_log_size gauge\n"
            << "convergence_op_log_size{node=\"" << book.get_node_id() << "\"} "
            << book.get_op_log_size() << "\n";

        auto snap = book.get_snapshot(1000);
        out << "# HELP convergence_book_depth Number of price levels\n"
            << "# TYPE convergence_book_depth gauge\n"
            << "convergence_book_depth{node=\"" << book.get_node_id() << "\",side=\"bid\"} "
            << snap.bids.size() << "\n"
            << "convergence_book_depth{node=\"" << book.get_node_id() << "\",side=\"ask\"} "
            << snap.asks.size() << "\n";
        return out.str();
    }

    } else if (cmd == "QMATCH") {
        // Quantum-optimized batch matching (does NOT execute — preview only)
        if (!JuliaBridge::instance().is_initialized()) {
            return "ERROR quantum_not_available";
        }

        auto all_orders = book.get_snapshot(1000);
        // We need the actual orders, not just price levels
        auto fills = QuantumMatcher::optimize_batch(
            *reinterpret_cast<const OrderBook*>(&book));  // Access underlying book

        std::ostringstream out;
        out << std::fixed << std::setprecision(4);
        out << "QMATCHES " << fills.size();
        double total_surplus = 0.0;
        for (auto& f : fills) {
            out << "\n" << f.bid_order_id << " " << f.ask_order_id
                << " " << f.exec_price << " " << f.quantity
                << " surplus=" << f.surplus;
            total_surplus += f.surplus * f.quantity;
        }
        out << "\nTOTAL_SURPLUS " << total_surplus;
        return out.str();

    } else if (cmd == "QCOMPARE") {
        // Compare quantum vs classical matching
        if (!JuliaBridge::instance().is_initialized()) {
            return "ERROR quantum_not_available";
        }

        auto result = QuantumMatcher::compare(
            *reinterpret_cast<const OrderBook*>(&book));

        std::ostringstream out;
        out << std::fixed << std::setprecision(4);
        out << "QUANTUM_MATCHES " << result.quantum_fills.size()
            << " SURPLUS " << result.quantum_surplus;
        for (auto& f : result.quantum_fills) {
            out << "\n  " << f.bid_order_id << " x " << f.ask_order_id
                << " @ " << f.exec_price << " qty=" << f.quantity;
        }
        out << "\nCLASSICAL_MATCHES " << result.classical_fills.size()
            << " SURPLUS " << result.classical_surplus;
        for (auto& f : result.classical_fills) {
            out << "\n  " << f.bid_order_id << " x " << f.ask_order_id
                << " @ " << f.exec_price << " qty=" << f.quantity;
        }
        double improvement = 0.0;
        if (result.classical_surplus > 0) {
            improvement = (result.quantum_surplus - result.classical_surplus) /
                          result.classical_surplus * 100.0;
        }
        out << "\nIMPROVEMENT " << improvement << "%";
        return out.str();

    } else if (cmd == "QSTATUS") {
        // Quantum subsystem status
        bool initialized = JuliaBridge::instance().is_initialized();
        std::ostringstream out;
        out << "QUANTUM " << (initialized ? "ENABLED" : "DISABLED");
        if (initialized) {
            out << "\nBRIDGE julia_yao";
            out << "\nCAPABILITIES qaoa_matching quantum_rng quantum_scheduling";
        }
        return out.str();
    }

    return "ERROR unknown_command";
}
