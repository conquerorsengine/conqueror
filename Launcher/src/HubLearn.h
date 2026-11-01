#pragma once

#include <string>
#include <vector>

namespace Conqueror::Launcher
{
    struct LearnGuide
    {
        const char* Title;
        const char* Icon;
        const char* Summary;
        const char* Body;   // basit işaretleme: "# başlık", "- madde", "```" kod bloğu, boş satır
    };

    const std::vector<LearnGuide>& GetGuides();
    // İşaretlenmiş metni ImGui ile çiz
    void DrawMarkup(const char* text);
    // nativebridge betik API başvurusu (kategori + arama)
    void DrawApiReference(std::string& search);
}
