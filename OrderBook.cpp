#include "OrderBook.h"

#include <ctime>
#include <numeric>
#include <chrono>

// Remove GoodForDay orders from the order book at the close
void OrderBook::PruneGoodForDayOrders()
{
    using namespace std::chrono;
    const auto end = hours(16);

    while(true)
    {
        const auto now = system_clock::now();
        const auto now_c = system_clock::to_time_t(now);
        std::tm now_parts;
        localtime_r(&now_c, &now_parts);

        if(now_parts.tm_hour >= end.count())
            now_parts.tm_mday += 1;

        now_parts.tm_hour = end.count();
        now_parts.tm_min = 0;
        now_parts.tm_sec = 0;

        auto next = system_clock::from_time_t(mktime(&now_parts));
        auto till = next - now + milliseconds(100);

        {
            std::unique_lock ordersLock{ ordersMutex_ }; // Acquire a unique lock to protect shared data

            // Check if the shutdown flag is set or wait for the specified time
            if(shutdown_.load(std::memory_order_acquire) ||
                shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout)
                return;
        }

        OrderIds orderIds;

        {
            std::scoped_lock ordersLock{ ordersMutex_ };

            for (const auto& [_, entry]: orders_)
            {
                const auto& [order, _] = entry;

                if(order->GetOrderType() != OrderType::GoodForDay)
                    continue;
                
                orderIds.push_back(order->GetOrderId());
            }
        }

        CancelOrders(orderIds);
    }
}

void OrderBook::CancelOrders(OrderIds orderIds)
{
    std::scoped_lock ordersLock{ ordersMutex_ };

    for (const auto& orderId : orderIds)
    {
        CancelOrderInternal(orderId);
    }
}

void OrderBook::CancelOrderInternal(OrderId orderId)
{
    if(!orders_.contains(orderId))
        return;
    
    const auto& [order, iterator] = orders_.at(orderId);
    orders_.erase(orderId);

    if(order->GetSide() == Side::Sell)
    {
        auto price = order->GetPrice();
        auto& orders = asks_.at(price);
        orders.erase(iterator);

        if(orders.empty())
            asks_.erase(price);
    }
    else
    {
        auto price = order->GetPrice();
        auto& orders = bids_.at(price);
        orders.erase(iterator);

        if(orders.empty())
            bids_.erase(price);
    }

    OnOrderCancelled(order);
}

bool OrderBook::CanMatch(Side side, Price price) const
{
    if(side == Side::Buy)
    {
        if(asks_.empty())
            return false;
        
        const auto& [bestAsk, _] = *asks_.begin();

        return price >= bestAsk;
    }
    else
    {
        if(bids_.empty())
            return false;
        
        const auto& [bestBid, _] = *bids_.begin();

        return price <= bestBid;
    }
}

Trades OrderBook::MatchOrders()
{
    Trades trades;
    trades.reserve(orders_.size());

    while(true)
    {
        if (bids_.empty() || asks_.empty())
            break;

        auto& [bidPrice, bids] = *bids_.begin();
        auto& [askPrice, asks] = *asks_.begin();

        if(bidPrice < askPrice)
            break;

        while(bids.size() && asks.size())
        {
            auto& bid = bids.front();
            auto& ask = asks.front();

            Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

            bid->Fill(quantity);
            ask->Fill(quantity);

            if(bid->IsFilled())
            {
                bids.pop_front();
                orders_.erase(bid->GetOrderId());
            }

            if(ask->IsFilled())
            {
                asks.pop_front();
                orders_.erase(ask->GetOrderId());
            }

            if (bids.empty())
                bids_.erase(bidPrice);

            if (asks.empty())
                asks_.erase(askPrice);
            
            trades.push_back(Trade {
                TradeInfo { bid->GetOrderId(), bid->GetPrice(), quantity },
                TradeInfo { ask->GetOrderId(), ask->GetPrice(), quantity }
            });

            OnOrderMatched(bid->GetPrice(), quantity, bid->IsFilled());
            OnOrderMatched(ask->GetPrice(), quantity, ask->IsFilled());
        }
    }

    if(!bids_.empty())
    {
        auto& [_, bids] = *bids_.begin();
        auto& order = bids.front();

        if(order->GetOrderType() == OrderType::FillOrKill)
            CancelOrder(order->GetOrderId());
    }

    if(!asks_.empty())
    {
        auto& [_, asks] = *asks_.begin();
        auto& order = asks.front();

        if(order->GetOrderType() == OrderType::FillAndKill)
            CancelOrder(order->GetOrderId());
    }

    return trades;
}

Trades OrderBook::AddOrder(OrderPointer order)
{
    std::scoped_lock ordersLock{ ordersMutex_ };
    
    if(orders_.contains(order->GetOrderId()))
        return {};

        if(order->GetOrderType() == OrderType::Market)
        {
            if(order->GetSide() == Side::Buy && !asks_.empty())
            {
                const auto& [worstAsk, _] = *asks_.rbegin();
                order->ToGoodTillCancel(worstAsk);
            }
            else if(order->GetSide() == Side::Sell && !bids_.empty())
            {
                const auto& [worstBid, _] = *bids_.rbegin();
                order->ToGoodTillCancel(worstBid);
            }
            else
                return {};
        }
    
    if(order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
        return {};
    
    if(order->GetOrderType() == OrderType::FillOrKill && !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()))
        return {};
    
    OrderPointers::iterator iterator;

    if(order->GetSide() == Side::Buy)
    {
        auto& orders = bids_[order->GetPrice()];
        orders.push_back(order);
        iterator = std::next(orders.begin(), orders.size() - 1);
    }
    else
    {
        auto& orders = asks_[order->GetPrice()];
        orders.push_back(order);
        iterator = std::next(orders.begin(), orders.size() - 1);
    }

    orders_.insert({ order->GetOrderId(), OrderEntry { order, iterator }});

    OnOrderAdded(order);

    return MatchOrders();
}

Trades OrderBook::ModifyOrder(OrderModify order)
{
	OrderType orderType;

	{
		std::scoped_lock ordersLock{ ordersMutex_ };

		if (!orders_.contains(order.GetOrderId()))
			return { };

		const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
		orderType = existingOrder->GetOrderType();
	}

	CancelOrder(order.GetOrderId());
	return AddOrder(order.ToOrderPointer(orderType));
}

void OrderBook::CancelOrder(OrderId orderId)
{
    std::scoped_lock ordersLock{ ordersMutex_ };
    CancelOrderInternal(orderId);
}

void OrderBook::OnOrderCancelled(OrderPointer order)
{
    UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
}

void OrderBook::OnOrderAdded(OrderPointer order)
{
    UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(), LevelData::Action::Add);
}

void OrderBook::OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled)
{
    UpdateLevelData(price, quantity, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
}

void OrderBook::UpdateLevelData(Price price, Quantity quantity, LevelData::Action action)
{
    auto& data = data_[price];

    data.count_ += action == LevelData::Action::Remove ? -1 : action == LevelData::Action::Add ? 1 : 0;

    if(action == LevelData::Action::Remove || action == LevelData::Action::Match)
    {
        data.quantity_ -= quantity;
    }
    else
    {
        data.quantity_ += quantity;
    }

    if(data.count_ == 0)
        data_.erase(price);
}

// Helps determine if FillOrKill order can be fully filled
bool OrderBook::CanFullyFill(Side side, Price price, Quantity quantity) const
{
    if(!CanMatch(side, price))
        return false;
    
    std::optional<Price> threshold;

    if(side == Side::Buy)
    {
        const auto [askPrice, _] = *asks_.begin();
        threshold = askPrice;
    }
    else
    {
        const auto [bidPrice, _] = *bids_.begin();
        threshold = bidPrice;
    }

    for(const auto& [levelPrice, levelData]: data_)
    {
        if(threshold.has_value() &&
        (side == Side::Buy && threshold.value() > levelPrice) ||
        (side == Side::Sell && threshold.value() < levelPrice))
            continue;

        if((side == Side::Buy && levelPrice > price) ||
        (side == Side::Sell && levelPrice < price))
            continue;

        if(quantity <= levelData.quantity_)
            return true;
        
        quantity -= levelData.quantity_;
    }

    return false;
}

std::size_t OrderBook::Size() const { return orders_.size(); }

// Get the quantities at each price level
OrderBookLevelInfos OrderBook::GetOrderInfos() const
{
    LevelInfos bidsInfo, asksInfo;
    bidsInfo.reserve(orders_.size());
    asksInfo.reserve(orders_.size());

    auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
    {
        return LevelInfo { price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
            [](Quantity runningSum, const OrderPointer& order)
            { return runningSum + order->GetRemainingQuantity(); }) };
    };

    for(const auto& [price, orders] : bids_)
        bidsInfo.push_back(CreateLevelInfos(price, orders));

    for(const auto& [price, orders] : asks_)
        asksInfo.push_back(CreateLevelInfos(price, orders));

    return OrderBookLevelInfos { bidsInfo, asksInfo };
}

OrderBook::OrderBook() : ordersPruneThread_{ [this] { PruneGoodForDayOrders();}} {}

OrderBook::~OrderBook()
{
    shutdown_.store(true, std::memory_order_release);
    shutdownConditionVariable_.notify_one();
    ordersPruneThread_.join();
}