#pragma once

#include "Core/Base/Base.h"
#include "CQSVM.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Conqueror::CQS
{
    // Bir betik sınıfının denetçide (Inspector) gösterilen alanı
    struct CQSScriptField
    {
        std::string Name;
        std::string TypeName;
        Value       DefaultValue;        // yalnızca ilkel türler (null/bool/int/float)
        std::string DefaultString;       // metin varsayılanı (geçici VM'den kopyalanır)
        bool        IsStringDefault = false;
        bool        HasDefault = false;
        std::vector<std::string> Attributes;
    };

    // ConquerorScript çalışma zamanı: motorun CQS ile konuştuğu tek arayüz.
    class CQ_API CQSEngine
    {
    public:
        static void Init();
        static void Shutdown();

        // Oyun modu başlangıcında/sonunda: temiz bir VM (statik durum oturumlar arasında taşınmaz)
        static void ResetRuntime();

        // Betik dosyasını yükle (aynı VM'de aynı içerik tekrar çalıştırılmaz)
        static bool LoadScript(const std::string& path, std::string* error = nullptr);
        static bool ExecuteFile(const std::string& path) { return LoadScript(path); }
        // Oyun sırasında kaydedilen betiği yeniden yükle: çalışan örnekler yeni sınıfa geçer, alan değerleri adla korunur
        static int HotReload(const std::string& path, std::string* error = nullptr);
        static bool ExecuteString(const std::string& source, const std::string& name = "<string>");
        // Konsol: ifade/deyim değerlendir; sonuç metin olarak
        static bool Evaluate(const std::string& source, std::string& output);

        // Sınıf örneği (sabitlenmiş); className boşsa dosyadaki ilk script sınıfı
        static void* Instantiate(const std::string& className, uint32_t entityHandle);
        static void  DestroyInstance(void* instance);
        static bool  HasMethod(void* instance, const std::string& method);
        // Eski arayüz: tek float argüman (ts)
        static void  Invoke(void* instance, const std::string& methodName, float ts, void* scene = nullptr);
        static bool  InvokeMethod(void* instance, const std::string& methodName, const Value* args, int argc, Value* result = nullptr);
        static bool  SetField(void* instance, const std::string& field, const Value& value);
        static bool  GetField(void* instance, const std::string& field, Value& value);

        // Kare sonu: gerekirse çöp toplama
        static void EndFrame();

        // Dosyadaki sınıflar ve denetçi alanları (çalıştırmadan derleyerek)
        static bool GetScriptClasses(const std::string& path, std::vector<std::string>& classes, std::string* error = nullptr);
        static bool GetScriptFields(const std::string& path, const std::string& className, std::vector<CQSScriptField>& fields, std::string* error = nullptr);
        // Kaynağı yalnızca derle (sözdizimi kontrolü / editör)
        static bool CheckSyntax(const std::string& source, const std::string& name, std::vector<std::string>& errors,
                                std::vector<std::string>* warnings = nullptr);

        static CQSVM* GetVM() { return s_VM.get(); }
        static void*  GetActiveScene();
        static std::string GetFirstScriptClass(const std::string& path);

        // Bildirilen betik hatalarının toplam sayısı ve sonuncusu (derleme + çalışma; otomasyon testleri)
        static uint64_t GetErrorCount();
        static std::string GetLastError();

        static void SetJitEnabled(bool enabled);
        static bool IsJitEnabled() { return s_JitEnabled; }

    private:
        static void CreateVM();
        static void OnScriptError(const std::string& message);

        static std::unique_ptr<CQSVM> s_VM;
        static inline std::unordered_map<std::string, std::string> s_LoadedHashes;    // yol -> içerik özeti (bu VM)
        static inline std::unordered_map<std::string, std::vector<std::string>> s_FileClasses;
        static inline std::unordered_map<void*, uint32_t> s_Pins;
        static inline std::unordered_map<std::string, double> s_LastErrorTime;
        static inline bool s_JitEnabled = true;
        static void CountError(const std::string& message);
    };
}
