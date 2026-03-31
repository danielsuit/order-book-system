#pragma once
#include <deque>
#include <vector>
#include "order.h"

struct PriceLevel {
    double price = 0.0;
    int64_t total_quantity = 0;
    std::deque<Order*> orders;  // FIFO queue, price-time priority
};

struct BookSnapshot {
    std::vector<PriceLevel> bids;  // Sorted descending by price
    std::vector<PriceLevel> asks;  // Sorted ascending by price
};
