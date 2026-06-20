#pragma once
#include "Core/Base/Base.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API CellularAutomata {
    public:
        // Conway's Game of Life evaluation step
        static std::vector<int> ConwayStep(const std::vector<int>& grid, int width, int height);
        
        // Langton's Ant step
        static void LangtonAntStep(std::vector<int>& grid, int width, int height, int& antX, int& antY, int& antDir);
    };
}
