#pragma once

#include "Core/Base/Base.h"

#include <string>
#include <vector>

// LuaQ: Conqueror'ın Lua tabanlı dili. Lua 5.4 + Luau söz diziminin üst kümesi; kaynak, satır numaraları
// korunarak Luau'ya indirgenir ve Luau VM'inde (gerekirse yerel kod üretimiyle) çalışır.
//
//  Lua 5.4'ten (Luau'da olmayan): bit işlemleri & | ~ << >> ~x, goto / ::etiket::, local x <const>, local f <close>,
//                                 onaltılık kayan sayılar (0x1p4)
//  Luau'dan: tür açıklamaları (silinir), continue, += ..= //=, if-ifadesi, `metin {ifade}`, @native
//  LuaQ eklentileri:
//    class Ad [extends Taban] ... end   (yöntemlerde örtük self, static, super, __tostring gibi metametotlar)
//    enum Renk { Kirmizi, Yesil = 5 }, const X = 1, match d case 1, 2 then ... case _ if k then ... else ... end
//    x => x * 2, (a, b) => do ... end, function f(a, b = 10), a?.b?.c(), a ?? b, a ??= b
//    defer ifade, local {ad, yas} = kisi, local [x, y] = nokta, f(...liste), {...a, ...b, k = 1}
//    async function / await (saniye, görev ya da fonksiyon), spawn, wait; != ! && || takma adları
namespace Conqueror::LuaQ
{
    struct Diagnostic
    {
        int Line = 0;
        int Column = 0;
        std::string Message;
    };

    struct CompileOptions
    {
        std::string ChunkName = "luaq";
        // Dosyada üst düzey return yoksa ve bu adda (ya da tek) bir sınıf varsa modül onu döndürür
        std::string AutoReturnClass;
    };

    // LuaQ kaynağını Luau kaynağına çevirir; hata varsa false ve teşhisler
    CQ_API bool Compile(const std::string& source, const CompileOptions& options, std::string& luauOut, std::vector<Diagnostic>& errors);

    // Çalışma zamanı kitaplığı (Luau kaynağı): __luaq tablosu, class/async/close/spread yardımcıları,
    // spawn / wait / isinstance / range genel fonksiyonları ve Lua 5.4 uyumluluk eklentileri
    CQ_API const char* Prelude();
}
