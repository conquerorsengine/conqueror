#pragma once

#include "Core/Base/Base.h"
#include "ConquerorScript/CQSValue.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror::CQS
{
    class CQSVM;
}

namespace Conqueror::Scripting
{
    struct NativeFunctionInfo
    {
        std::string   Name;
        int           Arity = 0;      // -1: değişken
        CQS::NativeFn Function;
    };

    // "nativebridge" modülü: motorun betiklere açtığı tüm fonksiyon ve sabitler.
    // Tek bir kayıt tablosu; her dil (CQS, Lua, Python, C#, Java, C++) aynı tabloya bağlanır.
    class CQ_API NativeRegistry
    {
    public:
        static constexpr const char* ModuleName = "nativebridge";

        // İlk çağrıda motor fonksiyonlarını kaydeder
        static NativeRegistry& Get();

        void DefineNative(const std::string& name, CQS::NativeFn function, int arity);
        void DefineGlobal(const std::string& name, CQS::Value value);

        const std::vector<NativeFunctionInfo>& GetFunctions() const { return m_Functions; }
        const std::vector<std::pair<std::string, CQS::Value>>& GetConstants() const { return m_Constants; }
        const NativeFunctionInfo* Find(const std::string& name) const;

        // Tüm fonksiyon/sabitleri bir CQS VM'ine global olarak bağla
        void BindTo(CQS::CQSVM* vm) const;

    private:
        NativeRegistry() = default;

        std::vector<NativeFunctionInfo> m_Functions;
        std::unordered_map<std::string, size_t> m_Index;
        std::vector<std::pair<std::string, CQS::Value>> m_Constants;
    };
}
