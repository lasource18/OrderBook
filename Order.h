#pragma once

#include <list>
#include <exception>
#include <format>

#include "OrderType.h"
#include "Side.h"
#include "Usings.h"
#include "Constants.h"

class Order
{
    public:
        Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
            : orderType_ { orderType },
            orderId_ { orderId },
            side_ { side },
            price_ { price },
            initialQuantity { quantity },
            remainingQuantity { quantity }
        { }

        Order(OrderId orderId, Side side, Quantity quantity)
            : Order(OrderType::Market, orderId, side, Constants::InvalidPrice, quantity)
        { }

        OrderType GetOrderType() const { return orderType_; }
        OrderId GetOrderId() const { return orderId_; }
        Side GetSide() const { return side_; }
        Price GetPrice() const { return price_; }
        Quantity GetInitialQuantity() const { return initialQuantity; }
        Quantity GetRemainingQuantity() const { return remainingQuantity; }
        Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
        bool IsFilled() const { return GetRemainingQuantity() == 0; }
        void Fill(Quantity quantity)
        {
            if (quantity > GetRemainingQuantity())
                throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity.", GetOrderId()));
            remainingQuantity -= quantity;
        };
        void ToGoodTillCancel(Price price)
        {
            if(GetOrderType() != OrderType::Market)
                throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity.", GetOrderId()));
            
            if(!std::isfinite(price))
                throw std::logic_error(std::format("Order ({}) must be a tradeable price.", GetOrderId()));

            price_ = price;
            orderType_ = OrderType::GoodTillCancel;
        }

    private:
        OrderType orderType_;
        OrderId orderId_;
        Side side_;
        Price price_;
        Quantity initialQuantity;
        Quantity remainingQuantity;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

