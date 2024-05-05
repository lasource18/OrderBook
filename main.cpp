#include "OrderBook.h"

int main()
{
    OrderBook orderbook;
    const OrderId orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
    std::cout << "Size after inserting one order: " << orderbook.Size() << std::endl;

    orderbook.CancelOrder(orderId);
    std::cout << "Size removing the order: " << orderbook.Size() << std::endl;

    return 0;
}