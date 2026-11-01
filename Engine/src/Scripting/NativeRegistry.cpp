#include "NativeRegistry.h"
#include "ConquerorScript/CQSNativeBridge.h"
#include "ConquerorScript/CQSVM.h"

namespace Conqueror::Scripting
{
    NativeRegistry& NativeRegistry::Get()
    {
        static NativeRegistry* s_Registry = [] {
            auto* r = new NativeRegistry();
            CQS::CQSNativeBridge::RegisterGlobals(r);
            return r;
        }();
        return *s_Registry;
    }

    void NativeRegistry::DefineNative(const std::string& name, CQS::NativeFn function, int arity)
    {
        auto it = m_Index.find(name);
        if (it != m_Index.end())
        {
            m_Functions[it->second].Function = std::move(function);
            m_Functions[it->second].Arity = arity;
            return;
        }
        m_Index[name] = m_Functions.size();
        m_Functions.push_back({ name, arity, std::move(function) });
    }

    void NativeRegistry::DefineGlobal(const std::string& name, CQS::Value value)
    {
        // Nesne sabitleri (Key, Mouse, Math tabloları) tüm VM'lerde paylaşılır: GC'den koru
        CQS::CQSHeap::Get().AddPersistentRoot(value);
        for (auto& [n, v] : m_Constants)
        {
            if (n == name)
            {
                v = value;
                return;
            }
        }
        m_Constants.emplace_back(name, value);
    }

    const NativeFunctionInfo* NativeRegistry::Find(const std::string& name) const
    {
        auto it = m_Index.find(name);
        return it == m_Index.end() ? nullptr : &m_Functions[it->second];
    }

    void NativeRegistry::BindTo(CQS::CQSVM* vm) const
    {
        for (const auto& f : m_Functions)
            vm->DefineNative(f.Name, f.Function, f.Arity);
        for (const auto& [name, value] : m_Constants)
            vm->DefineGlobal(name, value);
    }
}
