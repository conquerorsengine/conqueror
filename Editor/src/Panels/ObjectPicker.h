#pragma once

#include <functional>
#include <string>
#include <vector>

#include <imgui.h>

namespace Conqueror::Editor::ObjectPicker
{
    // Unity Object Picker ("Select Sprite" penceresi): arama, Assets / Packages / Scene sekmeleri, küçük resim
    // ızgarası, boyut kaydırıcısı, None. Tıklama seçer (yuva hemen güncellenir), çift tıklama seçip kapatır.
    //
    // Kullanım (yuva kodu):
    //   if (ImGui::Button("◎")) ObjectPicker::Open(ImGui::GetID("pick"), "Material", { ".cqmat" }, current);
    //   std::string picked; if (ObjectPicker::Consume(ImGui::GetID("pick"), picked)) current = picked; // "" = None
    void Open(ImGuiID owner, const std::string& typeName, const std::vector<std::string>& extensions, const std::string& current);
    bool Consume(ImGuiID owner, std::string& picked);
    bool IsOpenFor(ImGuiID owner);

    // Pencereyi çiz (EditorLayer her kare)
    void Draw();

    // "Scene" sekmesi: açık sahnenin kullandığı dosyalar (EditorLayer bağlar)
    void SetSceneProvider(std::function<std::vector<std::string>()> provider);

    // Yaygın uzantı kümeleri
    const std::vector<std::string>& ImageExtensions();
}
