#pragma once

#include "Core/Project/Project.h"

#include <string>

// Sürüm denetimi iç yardımcıları (VersionControl.cpp ve VersionControlOps.cpp ortak): kabuk komutu çalıştırma, tırnaklama, p4 komut
// öneki, geçici dosya. Editör dışından kullanılmaz.
namespace Conqueror::Editor::VersionControl::Detail
{
    // Kabuk için tek argüman (POSIX: tek tırnak, Windows: çift tırnak)
    std::string Quote(const std::string& s);
    // Komutu dir klasöründe çalıştırır: stdin boş (/dev/null: git / p4 parola sormak için beklemez), GIT_TERMINAL_PROMPT=0;
    // stdout + stderr birlikte output'a. Çıkış kodu (-1: başlatılamadı)
    int Run(const std::string& dir, const std::string& command, std::string& output);
    // Run gibi; stderr ayrı (çıktısı ayrıştırılan komutlar: git log, p4 -ztag ...)
    int RunSplit(const std::string& dir, const std::string& command, std::string& out, std::string& err);
    // p4 [-p sunucu] [-u kullanıcı] [-c çalışma alanı] (proje göreli p4 yolu projeye göre çözülür)
    std::string P4Command(const VersionControlSettings& s, const std::string& projectDir);
    // Geçici dosya (yol + içerik); nesne yok olunca silinir
    struct TempFile
    {
        std::string Path;
        explicit TempFile(const std::string& content);
        ~TempFile();
        TempFile(const TempFile&) = delete;
        TempFile& operator=(const TempFile&) = delete;
    };
    std::string Trim(std::string s);
}
