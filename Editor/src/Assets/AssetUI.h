#pragma once

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

// Varlık Inspector'ları için ortak alanlar (std::string metin kutusu, Unity tarzı nesne yuvası)
namespace Conqueror::Editor::AssetUI
{
    bool InputString(const char* id, std::string& value, ImGuiInputTextFlags flags = 0);
    bool InputMultiline(const char* id, std::string& value, const ImVec2& size, ImGuiInputTextFlags flags = 0);

    // Proje göreli yol <-> mutlak yol
    std::filesystem::path Resolve(const std::string& projectRelative);
    std::string ToProjectRelative(const std::filesystem::path& absolute);

    // Nesne yuvası: ad kutusu (Content Browser'dan bırakılabilir) + ◎ seçici. value proje göreli yol; değişince true.
    bool AssetSlot(const char* id, const std::string& typeName, const std::vector<std::string>& extensions, std::string& value);

    // İki sütunlu özellik tablosu satırı başlangıcı (etiket solda, değer sağda; çağıran değeri çizer)
    bool BeginProperties(const char* id, float labelWidth = 150.0f);
    void Row(const char* label);
    void EndProperties();
}
