#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>
#include <vector>

// State Graph (.cqstategraph, Unity Visual Scripting State Graph / Unreal State Tree): bir durum makinesi; her durum
// kendi betiğini (.cqgraph ya da herhangi bir dilde betik) çalıştırır, geçişler süreye, tuşa, tetikleyiciye ya da
// değişkene bağlıdır. Script bileşenine betik gibi eklenir.
namespace Conqueror::Scripting
{
    class NativeRegistry;
}

namespace Conqueror::Scripting::StateGraphs
{
    constexpr const char* Extension = ".cqstategraph";
    constexpr const char* Kind = "StateGraph";

    // Tetikleyici gönder (bu varlığın çalışan tüm State Graph'ları); karşılayan geçiş varsa kuyruğa alınır
    CQ_API bool Trigger(uint32_t entity, const std::string& name);
    CQ_API bool SetVariable(uint32_t entity, const std::string& name, bool value);
    CQ_API std::string CurrentState(uint32_t entity);
    // Düzenleyici: bu dosyanın o an çalışan durum düğümleri
    CQ_API std::vector<int> ActiveNodes(const std::string& path);
    void RegisterNatives(NativeRegistry* vm);
}
