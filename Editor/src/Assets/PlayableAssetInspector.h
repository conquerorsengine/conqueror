#pragma once

#include <string>

namespace Conqueror::Editor::PlayableAssetInspector
{
    // Playable Asset (.cqplayable): davranış betiği ve alan değerleri (Unity Playable Asset)
    bool IsPlayable(const std::string& path);
    void Draw(const std::string& path);
}
