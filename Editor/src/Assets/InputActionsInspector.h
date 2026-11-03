#pragma once

#include <string>

namespace Conqueror::Editor::InputActionsInspector
{
    // Input Actions varlığı (.cqinputactions) Inspector'da: Edit Asset, Project-wide Actions ataması, kontrol şemaları,
    // haritalar ve eylemlerin özeti (tür, kontrol, bağlama sayısı, etkileşimler)
    void Draw(const std::string& path);
}
