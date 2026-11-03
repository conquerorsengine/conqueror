#pragma once

#include <functional>
#include <string>

namespace Conqueror::Editor::CubemapInspector
{
    // .cqcubemap Inspector'ı (Unity Legacy Cubemap): kaynak (altı yüz / panorama), yüzler çapraz düzende küçük
    // resimlerle (bırak / ◎ seç), panorama ve çözünürlük, "Use as Skybox".
    void Draw(const std::string& path);
    // EditorLayer bağlar: açık sahnenin gökyüzü bu küp olur
    inline std::function<void(const std::string&)> s_UseAsSkybox;
}
