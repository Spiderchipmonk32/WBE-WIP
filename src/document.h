#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Everything the user creates on top of the generated world.
// (Filled in by the nations, cities, trade, history and lore features.)
struct Document {
    int version = 1;
};
