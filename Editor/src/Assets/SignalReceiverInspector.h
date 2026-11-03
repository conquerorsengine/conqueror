#pragma once

#include "Scene/Entity.h"

#include <string>

namespace Conqueror::Editor::SignalReceiverInspector
{
    // Signal Receiver bileşeni: tepkiler (Signal varlığı -> hedef nesne metodu)
    void Draw(Entity entity);
    // Signal varlığı (.cqsignal)
    void DrawAsset(const std::string& path);
}
