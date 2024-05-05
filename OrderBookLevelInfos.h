#pragma once

#include "LevelInfo.h"

// Using two vectors of LevelInfos structs to keep tracks of the quantities/prices for the bids/asks
class OrderBookLevelInfos
{
    public:
        OrderBookLevelInfos(const LevelInfos& bids, const LevelInfos& asks) 
            : bids_ { bids }, 
            asks_ { asks }
        {

        }

        const LevelInfos& GetBids() const { return bids_; };
        const LevelInfos& GetAsks() const { return asks_; };

    private:
        LevelInfos bids_;
        LevelInfos asks_;
};
