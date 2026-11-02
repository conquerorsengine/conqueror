#pragma once

#include <string>

namespace Conqueror::Editor::TileAssetInspector
{
    // Tile / Rule Tile / Animated Tile (.cqtile) ve Tile Palette (.cqpalette) varlıklarının Inspector'ı
    void DrawTile(const std::string& path);
    void DrawPalette(const std::string& path);
}
