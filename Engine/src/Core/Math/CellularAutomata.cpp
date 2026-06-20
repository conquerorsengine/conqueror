#include "CellularAutomata.h"

namespace Conqueror::Math {
    std::vector<int> CellularAutomata::ConwayStep(const std::vector<int>& grid, int width, int height) {
        std::vector<int> nextGrid = grid;
        auto getCell = [&](int x, int y) {
            if (x < 0 || x >= width || y < 0 || y >= height) return 0;
            return grid[y * width + x];
        };

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int neighbors = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        neighbors += getCell(x + dx, y + dy);
                    }
                }

                int idx = y * width + x;
                if (grid[idx] == 1) {
                    if (neighbors < 2 || neighbors > 3) nextGrid[idx] = 0;
                } else {
                    if (neighbors == 3) nextGrid[idx] = 1;
                }
            }
        }
        return nextGrid;
    }

    void CellularAutomata::LangtonAntStep(std::vector<int>& grid, int width, int height, int& antX, int& antY, int& antDir) {
        if (antX < 0 || antX >= width || antY < 0 || antY >= height) return;
        
        int idx = antY * width + antX;
        int color = grid[idx];
        
        if (color == 0) {
            antDir = (antDir + 1) % 4; // Turn right
            grid[idx] = 1;
        } else {
            antDir = (antDir + 3) % 4; // Turn left
            grid[idx] = 0;
        }

        if (antDir == 0) antY--; // Up
        else if (antDir == 1) antX++; // Right
        else if (antDir == 2) antY++; // Down
        else if (antDir == 3) antX--; // Left
    }
}
