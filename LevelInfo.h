#pragma once

#include "Usings.h"

// Quantity available at a specific price level
struct LevelInfo
{
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;