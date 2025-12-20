//
//  order.cpp
//  order-book-system
//
//  Created by Daniel Suit on 12/17/25.
//

#include <string>

struct Order{
    float orderId;
    float price;
    float quantity;
    int side;
    float timestamp;
    std::string orderType;
    struct Order *nextOrder;
};

int orders(int x){
    return 0;
}

