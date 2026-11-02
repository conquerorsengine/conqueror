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
    const std::vector<std::string>& ModelExtensions();
    const std::vector<std::string>& AudioExtensions();
    const std::vector<std::string>& FontExtensions();
    // "*.png;*.jpg" / ".png;.jpg" -> { ".png", ".jpg" } (boş = her dosya)
    std::vector<std::string> ParseFilter(const std::string& filter);
    // Uzantılara göre tür adı ("Texture", "Material", "Audio Clip" ...)
    std::string TypeNameFor(const std::vector<std::string>& extensions);

    // Unity nesne alanı yanındaki ◎ düğmesi: tıklanınca pencereyi açar, seçim yapılınca true (picked "" = None)
    bool Button(const char* id, const std::string& typeName, const std::vector<std::string>& extensions, const std::string& current,
                std::string& picked);
}
