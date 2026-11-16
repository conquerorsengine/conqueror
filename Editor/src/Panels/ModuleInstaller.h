#pragma once

#include <string>

// Build Settings > platform: o platformun Hub modülü (hazır derlenmiş oyuncu) bu editöre kurulu mu; değilse "Install" düğmesi
// GitHub Releases'tan bu editör sürümünün modülünü indirip kurar (Hub'ın Installs > Add modules ile aynı iş) (47)
namespace Conqueror::Editor::ModuleInstaller
{
    // true: modülün hazır oyuncusu kurulu (Build araç zinciri istemez)
    bool Draw(const std::string& platform);
    // Testler: kurulumu başlat / bitti mi / durum metni
    bool Start(const std::string& platform, std::string& error);
    bool Busy();
    std::string Status();
    // module_install start <platform> | busy | expect_status <metin> | expect_installed <platform>
    void RegisterAutomation();
}
