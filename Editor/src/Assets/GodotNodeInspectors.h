#pragma once

#include "Scene/Entity.h"

namespace Conqueror::Editor::GodotNodeInspectors
{
    // Resource Preloader: ad + varlık listesi
    void DrawResourcePreloader(Entity entity);
    // Shader Globals Override: koleksiyonun parametreleri ve geçersiz kılan değerler
    void DrawShaderGlobalsOverride(Entity entity);
}
