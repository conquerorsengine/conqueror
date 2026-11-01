#pragma once

#include "Core/Base/Base.h"

#include <filesystem>
#include <string>

namespace Conqueror
{
    class Scene;

    // Native Script bileşeninin .cpp kaynakları (ScriptableEntity sınıfları, motorun C++ API'si): motor kaynağı
    // sistemin C++ derleyicisiyle paylaşımlı kitaplığa kendisi derler; kullanıcı DLL derlemez. Kaynak değişince Play'de
    // yeniden derlenir. Derlenmiş oyunda kitaplık oyunun NativeScripts/ klasöründen yüklenir (derleyici gerekmez).
    namespace NativeScriptCompiler
    {
        struct Result
        {
            bool Success = false;
            bool Rebuilt = false;
            std::filesystem::path Library;
            std::string Log;
        };

        // .cpp / .cc / .cxx
        CQ_API bool IsSource(const std::string& path);
        // Editörde derlenen kitaplık: <proje>/Library/NativeScripts/lib<ad>.so|.dll|.dylib
        CQ_API std::filesystem::path CompiledLibrary(const std::filesystem::path& source);
        // Derlenmiş oyundaki hazır kitaplık: <oyun>/NativeScripts/lib<ad>.so|.dll|.dylib
        CQ_API std::filesystem::path PackagedLibrary(const std::filesystem::path& projectDir, const std::filesystem::path& source);
        // Kitaplık yoksa ya da kaynaktan eskiyse derler (force: her durumda)
        CQ_API Result Compile(const std::filesystem::path& source, bool force = false);
        // Yüklenecek kitaplık: oyunda hazır olanı, editörde derlenmişi (gerekirse derleyerek)
        CQ_API Result Resolve(const std::filesystem::path& source);
        // Play başında: sahnedeki .cpp kaynaklı Native Script'ler derlenir (eskiyse), modül yeniden yüklenir, sınıf bağlanır
        CQ_API void PrepareScene(Scene& scene);
        // Bulunan derleyici (boşsa yok) ve motor başlıkları kökü
        CQ_API std::string FindCompiler();
        CQ_API std::filesystem::path EngineSourceRoot();
    }
}
