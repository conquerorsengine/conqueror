#pragma once

#include "Scripting/ScriptRuntime.h"

namespace Conqueror::CQS
{
    // ConquerorScript'in ScriptRuntime uygulaması (CQSEngine üzerinde ince katman)
    class CQSRuntime final : public Scripting::ScriptRuntime
    {
    public:
        const char* GetName() const override { return "ConquerorScript"; }
        const char* GetLanguageId() const override { return "cqs"; }
        std::vector<std::string> GetExtensions() const override { return { ".cqs" }; }

        void OnPlayStart() override;
        void OnPlayStop() override;
        void EndFrame() override;

        bool LoadScript(const std::string& path, std::string* error) override;
        std::string GetDefaultClass(const std::string& path) override;
        void* CreateInstance(const std::string& path, const std::string& className, uint32_t entity) override;
        void DestroyInstance(void* instance) override;
        bool HasMethod(void* instance, const std::string& method) override;
        bool Invoke(void* instance, const std::string& method, const Value* args, int argc) override;
        bool InvokeWithResult(void* instance, const std::string& method, const Value* args, int argc, Value* result) override;
        bool SetField(void* instance, const std::string& field, const Scripting::ScriptValue& value) override;
        bool GetField(void* instance, const std::string& field, Scripting::ScriptValue& value, std::string* display) override;
        int  HotReload(const std::string& path, std::string* error) override;

        bool GetClasses(const std::string& path, std::vector<std::string>& classes, std::string* error) override;
        bool GetFields(const std::string& path, const std::string& className, std::vector<Scripting::ScriptFieldInfo>& fields, std::string* error) override;
        bool CheckSyntax(const std::string& source, const std::string& name, std::vector<std::string>& errors, std::vector<std::string>* warnings) override;
        std::string GetNewScriptTemplate(const std::string& className) const override;
    };
}
