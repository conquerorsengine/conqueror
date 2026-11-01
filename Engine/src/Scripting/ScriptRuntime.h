#pragma once

#include "Core/Base/Base.h"
#include "Scripting/ScriptTypes.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror::Scripting
{
    // Bir betik dilinin motor tarafı. Sahne, Inspector ve Kod Düzenleyici dilden bağımsız olarak bu arayüzü kullanır.
    // Tüm diller motor API'sine aynı "nativebridge" kayıt tablosu üzerinden erişir; argümanlar CQS::Value olarak taşınır.
    class CQ_API ScriptRuntime
    {
    public:
        virtual ~ScriptRuntime() = default;

        virtual const char* GetName() const = 0;                   // "ConquerorScript", "Luau", "Python" ...
        virtual const char* GetLanguageId() const = 0;             // "cqs", "luau", "py", "cs", "java", "cpp"
        virtual std::vector<std::string> GetExtensions() const = 0; // ".cqs" ...
        virtual bool IsAvailable() const { return true; }
        virtual std::string GetUnavailableReason() const { return ""; }

        // Oyun modu yaşam döngüsü
        virtual void OnPlayStart() {}
        virtual void OnPlayStop() {}
        virtual void EndFrame() {}

        virtual bool LoadScript(const std::string& path, std::string* error) = 0;
        virtual std::string GetDefaultClass(const std::string& path) = 0;
        virtual void* CreateInstance(const std::string& path, const std::string& className, uint32_t entity) = 0;
        virtual void DestroyInstance(void* instance) = 0;
        virtual bool HasMethod(void* instance, const std::string& method) = 0;
        virtual bool Invoke(void* instance, const std::string& method, const CQS::Value* args, int argc) = 0;
        // Dönüş değerini de isteyen çağrı (InvokeMethod); desteklemeyen diller null döndürür
        virtual bool InvokeWithResult(void* instance, const std::string& method, const CQS::Value* args, int argc, CQS::Value* result)
        {
            if (result)
                *result = CQS::Value::MakeNull();
            return Invoke(instance, method, args, argc);
        }
        virtual bool SetField(void* instance, const std::string& field, const ScriptValue& value) = 0;
        virtual bool GetField(void* instance, const std::string& field, ScriptValue& value, std::string* display = nullptr) = 0;
        virtual int  HotReload(const std::string& path, std::string* error) { (void)path; (void)error; return 0; }

        // Çalıştırmadan inceleme (Inspector / Kod Düzenleyici)
        virtual bool GetClasses(const std::string& path, std::vector<std::string>& classes, std::string* error) = 0;
        virtual bool GetFields(const std::string& path, const std::string& className, std::vector<ScriptFieldInfo>& fields, std::string* error) = 0;
        virtual bool CheckSyntax(const std::string& source, const std::string& name, std::vector<std::string>& errors, std::vector<std::string>* warnings)
        {
            (void)source; (void)name; (void)errors; (void)warnings;
            return true;
        }
        // Arka planda süren sözdizimi denetimi var mı (düzenleyici sonucu almak için tekrar sorar)
        virtual bool HasPendingSyntaxCheck() const { return false; }
        virtual std::string GetNewScriptTemplate(const std::string& className) const = 0;
    };

    class CQ_API ScriptRuntimes
    {
    public:
        static void Init();
        static void Shutdown();

        static const std::vector<ScriptRuntime*>& All();
        static ScriptRuntime* ForPath(const std::string& path);
        static ScriptRuntime* ForLanguage(const std::string& languageId);
        static bool IsScriptFile(const std::string& path);

        static void PlayStart();
        static void PlayStop();
        static void EndFrame();

        // Oyun sırasında varlığı silinen betik örnekleri (OnDestroy çağrıldıktan sonra): aynı karede artık
        // çağrılmazlar (IsRetired) ve çağrı yığını boşken (kare sonu / oyun durdurma) FlushRetired ile yok edilirler.
        // Kendi varlığını Update içinde silen bir betiğin örneği böylece hâlâ çalışırken serbest bırakılmaz.
        static void RetireInstance(ScriptRuntime* runtime, void* instance);
        static bool IsRetired(void* instance);
        static void FlushRetired();

        // Betikler arası paylaşılan değerler (GlobalSet/GlobalGet): oyun oturumu boyunca, sahne geçişlerinde korunur
        static std::unordered_map<std::string, ScriptValue>& Globals();

        // CQS::Value <-> ScriptValue
        static CQS::Value ToValue(const ScriptValue& v);
        static ScriptValue FromValue(const CQS::Value& v, bool* representable = nullptr);
    };
}
