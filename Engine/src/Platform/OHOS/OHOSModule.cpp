// HarmonyOS NAPI modülü: editör / oyuncu .so'su ArkTS'ten "import engine from 'lib<ad>.so'" ile ve XComponent libraryname ile
// yüklenir. Dışa aktarılan işlevler (Tools/HarmonyOS/template/entry/src/main/ets/pages/Index.ets kullanır):
//   setup(resourceManager, filesDir, cacheDir, tempDir)  uygulama klasörleri ve HAP kaynak yöneticisi (rawfile)
//   start(args: string[])                                 motor iş parçacığını başlatır (bir kez)
//   setHostCallback((kind: string, payload: string) => void)  motorun ArkTS'ten istekleri (imleç, seçici, adres, başlık, kapanış)
//   pickerResult(id: number, paths: string[], error: string)  belge seçicinin sonucu
//   insertText(text: string)                              giriş yönteminden metin
//   requestClose() / destroy()                            pencere kapatılmak isteniyor / uygulama sonlanıyor
// Motor iş parçacığı HAP'teki rawfile içeriğini (cq_files.txt listesi) filesDir/<içerik klasörü>'ne çıkarır (sürüm değişmediyse
// atlar), HOME / XDG / TMPDIR'i uygulama klasörlerine yönlendirir ve hedefin main gövdesini çalıştırır.
#if defined(__OHOS__)

#include "OHOSInternal.h"
#include "OHOSPlatform.h"

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <hilog/log.h>
#include <napi/native_api.h>
#include <rawfile/raw_file.h>
#include <rawfile/raw_file_manager.h>

#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    constexpr unsigned int kLogDomain = 0x3C51;

    struct Module
    {
        napi_module Napi{};
        std::string Name;
        std::string ContentFolder;
        Conqueror::OHOS::MainFunction Main = nullptr;

        std::mutex Lock;
        std::string FilesDir = "/data/storage/el2/base/files";   // setup() gelmezse uygulama kum havuzunun varsayılanları
        std::string CacheDir = "/data/storage/el2/base/cache";
        std::string TempDir = "/data/storage/el2/base/temp";
        std::string ContentDir;
        std::vector<std::string> Args;
        NativeResourceManager* Resources = nullptr;
        napi_threadsafe_function Host = nullptr;
        bool Started = false;
        bool Quitting = false;

        // Belge seçici isteği (motor iş parçacığı bekler)
        std::condition_variable PickWake;
        int PickSerial = 0;
        int PickDoneId = -1;
        std::vector<std::string> PickPaths;
        std::string PickError;
    };

    Module& M()
    {
        static Module m;
        return m;
    }

    void Log(LogLevel level, const std::string& msg)
    {
        OH_LOG_Print(LOG_APP, level, kLogDomain, "Conqueror", "%{public}s", msg.c_str());
    }

    std::string JsonEscape(const std::string& in)
    {
        std::string out;
        out.reserve(in.size() + 8);
        for (char c : in)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if ((unsigned char)c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(unsigned char)c);
                        out += buf;
                    }
                    else
                        out += c;
            }
        }
        return out;
    }

    // ── NAPI yardımcıları
    std::string GetString(napi_env env, napi_value value)
    {
        size_t length = 0;
        if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok)
            return {};
        std::string out(length, '\0');
        napi_get_value_string_utf8(env, value, out.data(), length + 1, &length);
        out.resize(length);
        return out;
    }

    std::vector<std::string> GetStringArray(napi_env env, napi_value value)
    {
        std::vector<std::string> out;
        bool isArray = false;
        if (napi_is_array(env, value, &isArray) != napi_ok || !isArray)
            return out;
        uint32_t count = 0;
        napi_get_array_length(env, value, &count);
        for (uint32_t i = 0; i < count; ++i)
        {
            napi_value item = nullptr;
            if (napi_get_element(env, value, i, &item) == napi_ok)
                out.push_back(GetString(env, item));
        }
        return out;
    }

    napi_value Undefined(napi_env env)
    {
        napi_value u = nullptr;
        napi_get_undefined(env, &u);
        return u;
    }

    template<size_t N>
    size_t GetArgs(napi_env env, napi_callback_info info, napi_value (&args)[N])
    {
        size_t argc = N;
        for (auto& a : args)
            a = nullptr;
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
        return argc;
    }

    // ── HAP rawfile içeriğini çıkarma
    bool ReadRawFile(NativeResourceManager* mgr, const std::string& name, std::string& out)
    {
        RawFile64* file = OH_ResourceManager_OpenRawFile64(mgr, name.c_str());
        if (!file)
            return false;
        const int64_t size = OH_ResourceManager_GetRawFileSize64(file);
        out.resize(size > 0 ? (size_t)size : 0);
        const int64_t read = size > 0 ? OH_ResourceManager_ReadRawFile64(file, out.data(), size) : 0;
        OH_ResourceManager_CloseRawFile64(file);
        return read == size;
    }

    bool ExtractRawFiles(const std::string& destination)
    {
        NativeResourceManager* mgr = nullptr;
        {
            std::lock_guard<std::mutex> lock(M().Lock);
            mgr = M().Resources;
        }
        if (!mgr)
        {
            Log(LOG_ERROR, "setup() was not called with the resource manager: HAP content cannot be extracted");
            return false;
        }
        std::string manifest;
        if (!ReadRawFile(mgr, "cq_files.txt", manifest))
        {
            Log(LOG_ERROR, "cq_files.txt missing in the HAP rawfile folder (package with Tools/HarmonyOS/package_hap.py)");
            return false;
        }
        const std::string version = manifest.substr(0, manifest.find('\n'));
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(destination, ec);
        {
            std::ifstream stamp(destination + "/.cq_content_version");
            std::string have;
            std::getline(stamp, have);
            if (!version.empty() && have == version)
                return true;
        }
        int extracted = 0, failed = 0;
        size_t pos = manifest.find('\n');
        while (pos != std::string::npos && pos + 1 < manifest.size())
        {
            const size_t end = manifest.find('\n', pos + 1);
            std::string rel = manifest.substr(pos + 1, (end == std::string::npos ? manifest.size() : end) - pos - 1);
            pos = end;
            if (!rel.empty() && rel.back() == '\r')
                rel.pop_back();
            if (rel.empty() || rel.find("..") != std::string::npos)
                continue;
            RawFile64* file = OH_ResourceManager_OpenRawFile64(mgr, rel.c_str());
            if (!file)
            {
                ++failed;
                continue;
            }
            const fs::path out = fs::path(destination) / rel;
            fs::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            static thread_local std::vector<char> buffer(256 * 1024);
            int64_t n = 0;
            while ((n = OH_ResourceManager_ReadRawFile64(file, buffer.data(), (int64_t)buffer.size())) > 0)
                f.write(buffer.data(), (std::streamsize)n);
            OH_ResourceManager_CloseRawFile64(file);
            f.good() ? ++extracted : ++failed;
        }
        std::ofstream(destination + "/.cq_content_version") << version << "\n";
        Log(failed ? LOG_WARN : LOG_INFO, "Extracted " + std::to_string(extracted) + " files to " + destination +
                                             (failed ? " (" + std::to_string(failed) + " failed)" : std::string()));
        return true;
    }

    // ── Motor iş parçacığı
    void EngineThread()
    {
        Module& m = M();
        std::string files, cache, temp;
        std::vector<std::string> args;
        {
            std::lock_guard<std::mutex> lock(m.Lock);
            files = m.FilesDir;
            cache = m.CacheDir;
            temp = m.TempDir.empty() ? m.CacheDir : m.TempDir;
            args = m.Args;
        }
        mkdir(files.c_str(), 0700);
        mkdir(cache.c_str(), 0700);
        mkdir(temp.c_str(), 0700);
        // Motorun POSIX yolları (PlayerPrefs, günlük, gölgelendirici önbelleği, geçici dosyalar) HOME / XDG / TMPDIR'e bakar;
        // HAP uygulamasında bunlar tanımsız ya da yazılamaz
        setenv("HOME", files.c_str(), 1);
        setenv("XDG_DATA_HOME", files.c_str(), 1);
        setenv("XDG_CONFIG_HOME", files.c_str(), 1);
        setenv("XDG_CACHE_HOME", cache.c_str(), 1);
        setenv("TMPDIR", temp.c_str(), 1);

        const std::string content = files + "/" + m.ContentFolder;
        if (!ExtractRawFiles(content))
            Log(LOG_ERROR, "HAP content could not be extracted to " + content);
        {
            std::lock_guard<std::mutex> lock(m.Lock);
            m.ContentDir = content;
        }
        // Motorun "yürütülebilir dosya klasörü" (kaynak arama, oyun klasörü): çıkarılan içerik (SelfExecutable.h)
        const std::string program = content + "/" + m.Name;
        setenv("CQ_OHOS_EXECUTABLE", program.c_str(), 1);
        if (chdir(content.c_str()) != 0)
            Log(LOG_ERROR, "chdir(" + content + ") failed");

        std::vector<std::string> storage;
        storage.push_back(program);
        for (const auto& a : args)
            storage.push_back(a);
        std::vector<char*> argv;
        for (auto& a : storage)
            argv.push_back(a.data());
        argv.push_back(nullptr);

        Log(LOG_INFO, "Starting " + m.Name + " (" + std::to_string(storage.size() - 1) + " arguments)");
        const int rc = m.Main ? m.Main((int)storage.size(), argv.data()) : -1;
        Log(LOG_INFO, m.Name + " finished (" + std::to_string(rc) + ")");

        Conqueror::OHOS::Detail::EngineFinished();
        {
            std::lock_guard<std::mutex> lock(m.Lock);
            m.Quitting = true;
        }
        m.PickWake.notify_all();
        // Uygulamayı kapat (ArkTS: context.terminateSelf)
        Conqueror::OHOS::PostHostRequest("quit", std::to_string(rc));
    }

    // ── Dışa aktarılan işlevler (ArkTS ana iş parçacığı)
    napi_value Setup(napi_env env, napi_callback_info info)
    {
        napi_value args[4];
        const size_t argc = GetArgs(env, info, args);
        Module& m = M();
        std::lock_guard<std::mutex> lock(m.Lock);
        if (argc > 0 && args[0] && !m.Resources)
            m.Resources = OH_ResourceManager_InitNativeResourceManager(env, args[0]);
        if (argc > 1 && args[1]) if (auto v = GetString(env, args[1]); !v.empty()) m.FilesDir = v;
        if (argc > 2 && args[2]) if (auto v = GetString(env, args[2]); !v.empty()) m.CacheDir = v;
        if (argc > 3 && args[3]) if (auto v = GetString(env, args[3]); !v.empty()) m.TempDir = v;
        return Undefined(env);
    }

    napi_value Start(napi_env env, napi_callback_info info)
    {
        napi_value args[1];
        const size_t argc = GetArgs(env, info, args);
        Module& m = M();
        {
            std::lock_guard<std::mutex> lock(m.Lock);
            if (m.Started)
                return Undefined(env);
            m.Started = true;
            if (argc > 0 && args[0])
                m.Args = GetStringArray(env, args[0]);
        }
        // Motor kendi iş parçacığında: ArkTS ana iş parçacığı uzun işlerde (proje yükleme, gölgelendirici derleme) donmasın.
        // Masaüstündeki ana iş parçacığı gibi büyük yığın
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 16u * 1024u * 1024u);
        pthread_t thread;
        const int rc = pthread_create(&thread, &attr, [](void*) -> void* { EngineThread(); return nullptr; }, nullptr);
        pthread_attr_destroy(&attr);
        if (rc == 0)
            pthread_detach(thread);
        else
            Log(LOG_ERROR, "Engine thread could not be started");
        return Undefined(env);
    }

    void CallHost(napi_env env, napi_value callback, void*, void* data)
    {
        auto* request = static_cast<std::pair<std::string, std::string>*>(data);
        if (env && callback && request)
        {
            napi_value argv[2] = {};
            napi_create_string_utf8(env, request->first.c_str(), request->first.size(), &argv[0]);
            napi_create_string_utf8(env, request->second.c_str(), request->second.size(), &argv[1]);
            napi_call_function(env, Undefined(env), callback, 2, argv, nullptr);
        }
        delete request;
    }

    napi_value SetHostCallback(napi_env env, napi_callback_info info)
    {
        napi_value args[1];
        GetArgs(env, info, args);
        napi_valuetype type = napi_undefined;
        if (!args[0] || napi_typeof(env, args[0], &type) != napi_ok || type != napi_function)
            return Undefined(env);
        napi_value name = nullptr;
        napi_create_string_utf8(env, "ConquerorHost", NAPI_AUTO_LENGTH, &name);
        napi_threadsafe_function tsfn = nullptr;
        if (napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr, &CallHost, &tsfn) != napi_ok)
        {
            Log(LOG_ERROR, "napi_create_threadsafe_function failed");
            return Undefined(env);
        }
        Module& m = M();
        napi_threadsafe_function previous = nullptr;
        {
            std::lock_guard<std::mutex> lock(m.Lock);
            previous = m.Host;
            m.Host = tsfn;
        }
        if (previous)
            napi_release_threadsafe_function(previous, napi_tsfn_release);
        return Undefined(env);
    }

    napi_value PickerResult(napi_env env, napi_callback_info info)
    {
        napi_value args[3];
        const size_t argc = GetArgs(env, info, args);
        int32_t id = -1;
        if (argc > 0 && args[0])
            napi_get_value_int32(env, args[0], &id);
        Module& m = M();
        {
            std::lock_guard<std::mutex> lock(m.Lock);
            m.PickPaths = argc > 1 && args[1] ? GetStringArray(env, args[1]) : std::vector<std::string>{};
            m.PickError = argc > 2 && args[2] ? GetString(env, args[2]) : std::string();
            m.PickDoneId = id;
        }
        m.PickWake.notify_all();
        return Undefined(env);
    }

    napi_value InsertText(napi_env env, napi_callback_info info)
    {
        napi_value args[1];
        if (GetArgs(env, info, args) > 0 && args[0])
            Conqueror::OHOS::Detail::InsertText(GetString(env, args[0]));
        return Undefined(env);
    }

    napi_value RequestClose(napi_env env, napi_callback_info)
    {
        Conqueror::OHOS::Detail::RequestClose();
        return Undefined(env);
    }

    napi_value Destroy(napi_env env, napi_callback_info)
    {
        Module& m = M();
        {
            std::lock_guard<std::mutex> lock(m.Lock);
            m.Quitting = true;
        }
        m.PickWake.notify_all();
        Conqueror::OHOS::Detail::RequestDestroy();
        return Undefined(env);
    }

    napi_value Init(napi_env env, napi_value exports)
    {
        napi_property_descriptor desc[] = {
            { "setup", nullptr, Setup, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "start", nullptr, Start, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "setHostCallback", nullptr, SetHostCallback, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "pickerResult", nullptr, PickerResult, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "insertText", nullptr, InsertText, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "requestClose", nullptr, RequestClose, nullptr, nullptr, nullptr, napi_default, nullptr },
            { "destroy", nullptr, Destroy, nullptr, nullptr, nullptr, napi_default, nullptr },
        };
        napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

        // XComponent bu kitaplığı libraryname ile yüklediyse yerel bileşen nesnesi exports üzerindedir
        napi_value component = nullptr;
        napi_valuetype type = napi_undefined;
        if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &component) == napi_ok && component &&
            napi_typeof(env, component, &type) == napi_ok && type == napi_object)
        {
            OH_NativeXComponent* native = nullptr;
            if (napi_unwrap(env, component, reinterpret_cast<void**>(&native)) == napi_ok && native)
                Conqueror::OHOS::Detail::AttachXComponent(native);
        }
        return exports;
    }
}

namespace Conqueror::OHOS
{
    void RegisterModule(const char* moduleName, MainFunction main, const char* contentFolder)
    {
        Module& m = M();
        m.Name = moduleName ? moduleName : "conqueror";
        m.ContentFolder = contentFolder && *contentFolder ? contentFolder : "content";
        m.Main = main;
        m.Napi.nm_version = 1;
        m.Napi.nm_flags = 0;
        m.Napi.nm_filename = nullptr;
        m.Napi.nm_register_func = Init;
        m.Napi.nm_modname = m.Name.c_str();
        m.Napi.nm_priv = nullptr;
        napi_module_register(&m.Napi);
    }

    std::string FilesDirectory()
    {
        std::lock_guard<std::mutex> lock(M().Lock);
        return M().FilesDir;
    }

    std::string CacheDirectory()
    {
        std::lock_guard<std::mutex> lock(M().Lock);
        return M().CacheDir;
    }

    std::string ContentDirectory()
    {
        std::lock_guard<std::mutex> lock(M().Lock);
        return M().ContentDir;
    }

    const std::vector<std::string>& StartArguments()
    {
        return M().Args;   // start() motor iş parçacığından önce yazar, sonra değişmez
    }

    bool PostHostRequest(const std::string& kind, const std::string& payload)
    {
        napi_threadsafe_function host = nullptr;
        {
            std::lock_guard<std::mutex> lock(M().Lock);
            host = M().Host;
        }
        if (!host)
            return false;
        auto* request = new std::pair<std::string, std::string>(kind, payload);
        if (napi_call_threadsafe_function(host, request, napi_tsfn_nonblocking) != napi_ok)
        {
            delete request;
            return false;
        }
        return true;
    }

    bool ShowPicker(PickerMode mode, const std::vector<std::string>& extensions, const std::string& defaultPath,
                    const std::string& defaultName, std::vector<std::string>& outPaths, std::string& error)
    {
        Module& m = M();
        int id = 0;
        {
            std::lock_guard<std::mutex> lock(m.Lock);
            if (m.Quitting)
                return false;
            id = ++m.PickSerial;
        }
        const char* modeName = mode == PickerMode::OpenFiles ? "openMultiple" : mode == PickerMode::SaveFile ? "save"
                             : mode == PickerMode::PickFolder ? "folder" : "open";
        std::string json = "{\"id\":" + std::to_string(id) + ",\"mode\":\"" + modeName + "\",\"extensions\":[";
        for (size_t i = 0; i < extensions.size(); ++i)
            json += (i ? ",\"" : "\"") + JsonEscape(extensions[i]) + "\"";
        json += "],\"defaultPath\":\"" + JsonEscape(defaultPath) + "\",\"defaultName\":\"" + JsonEscape(defaultName) + "\"}";
        if (!PostHostRequest("pick", json))
        {
            error = "The system file picker is not available (HarmonyOS host callback not set)";
            return false;
        }
        std::unique_lock<std::mutex> lock(m.Lock);
        m.PickWake.wait(lock, [&] { return m.PickDoneId == id || m.Quitting; });
        if (m.PickDoneId != id)
            return false;
        outPaths = m.PickPaths;
        error = m.PickError;
        return error.empty() && !outPaths.empty();
    }

    bool OpenURL(const std::string& url)
    {
        return !url.empty() && PostHostRequest("openUrl", url);
    }
}

#endif
