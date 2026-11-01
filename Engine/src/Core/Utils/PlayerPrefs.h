#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>

namespace Conqueror
{
    // Unity PlayerPrefs karşılığı: kullanıcı başına kalıcı anahtar/değer deposu (yüksek skor, ayarlar...).
    // Dosya: <GetPersistentDataPath()>/prefs.yaml. Değişiklikler Save() çağrıldığında, oyun durduğunda
    // ve uygulama kapanırken yazılır (atomik: geçici dosya + yeniden adlandırma).
    class CQ_API PlayerPrefs
    {
    public:
        static void SetInt(const std::string& key, int64_t value);
        static void SetFloat(const std::string& key, double value);
        static void SetString(const std::string& key, const std::string& value);

        static int64_t GetInt(const std::string& key, int64_t defaultValue = 0);
        static double GetFloat(const std::string& key, double defaultValue = 0.0);
        static std::string GetString(const std::string& key, const std::string& defaultValue = "");

        static bool HasKey(const std::string& key);
        static void DeleteKey(const std::string& key);
        static void DeleteAll();

        // Değişiklik varsa diske yaz; başarı (değişiklik yoksa true)
        static bool Save();

        // Oyun verisi klasörü: <kullanıcı veri dizini>/<Organization>/<Product> (yoksa oluşturulur).
        // Linux: $XDG_DATA_HOME ya da ~/.local/share, Windows: %APPDATA%, macOS: ~/Library/Application Support
        static std::string GetPersistentDataPath();
    };
}
