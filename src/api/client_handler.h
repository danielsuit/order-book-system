#pragma once
#include <string>
#include "../crdt/crdt_orderbook.h"

class ClientHandler {
public:
    static std::string handle(const std::string& request, CRDTOrderBook& book);
};
