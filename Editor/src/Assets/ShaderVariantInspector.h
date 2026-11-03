#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Conqueror::Editor::ShaderVariantInspector
{
    // Shader Variant Collection (.cqshadervariants): ön derlenecek malzemeler / shader'lar, Warm Up, Preloaded Shaders'a ekle
    void Draw(const std::string& path);
    // "Add Shaders Used In Scene" için açık sahnenin malzeme ve shader dosyaları (EditorLayer bağlar)
    void SetSceneProvider(std::function<std::vector<std::string>()> provider);
}
