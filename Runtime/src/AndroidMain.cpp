// Android giriş noktası (NativeActivity): APK içindeki oyun dosyaları ilk açılışta uygulamanın veri klasörüne
// çıkarılır, çalışma dizini oraya alınır ve masaüstündeki oyuncu aynen çalıştırılır.
#ifdef __ANDROID__

#include "Platform/Android/AndroidPlatform.h"

#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <cstdio>
#include <cstring>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

int CQ_RuntimeMain(int argc, char** argv);

namespace
{
    // Conqueror Player (264): "Aç" ile gelen .cqspace (dosya yöneticisi, indirmeler, paylaşım) ContentResolver'dan önbelleğe kopyalanır;
    // kopyalanan paketin yolu (paket değilse boş)
    std::string CopyOpenedPackage(android_app* app, const std::string& cacheDir)
    {
        JNIEnv* env = nullptr;
        JavaVM* vm = app->activity->vm;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)
            return {};
        std::string result;
        jobject activity = app->activity->clazz;
        jclass activityClass = env->GetObjectClass(activity);
        jobject intent = env->CallObjectMethod(activity, env->GetMethodID(activityClass, "getIntent", "()Landroid/content/Intent;"));
        jobject uri = intent ? env->CallObjectMethod(intent, env->GetMethodID(env->GetObjectClass(intent), "getData", "()Landroid/net/Uri;")) : nullptr;
        if (uri)
        {
            jobject resolver = env->CallObjectMethod(activity, env->GetMethodID(activityClass, "getContentResolver", "()Landroid/content/ContentResolver;"));
            jobject in = env->CallObjectMethod(resolver, env->GetMethodID(env->GetObjectClass(resolver), "openInputStream",
                                                                           "(Landroid/net/Uri;)Ljava/io/InputStream;"), uri);
            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
                in = nullptr;
            }
            if (in)
            {
                jclass inClass = env->GetObjectClass(in);
                jmethodID read = env->GetMethodID(inClass, "read", "([B)I");
                jbyteArray buffer = env->NewByteArray(1 << 16);
                const std::string dest = cacheDir + "/opened.cqspace";
                FILE* out = std::fopen(dest.c_str(), "wb");
                char header[4] = {};
                size_t total = 0;
                while (out)
                {
                    const jint n = env->CallIntMethod(in, read, buffer);
                    if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
                    if (n < 0)
                        break;
                    jbyte chunk[1 << 16];
                    env->GetByteArrayRegion(buffer, 0, n, chunk);
                    if (total < 4)
                        std::memcpy(header + total, chunk, (size_t)std::min<jint>(n, (jint)(4 - total)));
                    std::fwrite(chunk, 1, (size_t)n, out);
                    total += (size_t)n;
                }
                if (out)
                    std::fclose(out);
                env->CallVoidMethod(in, env->GetMethodID(inClass, "close", "()V"));
                if (env->ExceptionCheck())
                    env->ExceptionClear();
                // Kap biçimi: AssetBundle ("CQAB" başlığı)
                if (total > 4 && std::memcmp(header, "CQAB", 4) == 0)
                    result = dest;
                else
                    __android_log_print(ANDROID_LOG_ERROR, "Conqueror", "The opened file is not a Conqueror game (.cqspace)");
            }
        }
        vm->DetachCurrentThread();
        return result;
    }
}

void android_main(android_app* app)
{
    Conqueror::Android::SetApp(app);

    // Uygulamanın yazılabilir klasörleri: motorun POSIX yolları (PlayerPrefs, günlük, gölgelendirici önbelleği, geçici
    // dosyalar) HOME / XDG / TMPDIR'e bakar; NativeActivity bunları tanımlamaz
    const std::string files = Conqueror::Android::FilesDirectory();
    const std::string cache = files.substr(0, files.find_last_of('/')) + "/cache";
    mkdir(cache.c_str(), 0700);
    setenv("HOME", files.c_str(), 1);
    setenv("XDG_DATA_HOME", files.c_str(), 1);
    setenv("XDG_CONFIG_HOME", files.c_str(), 1);
    setenv("XDG_CACHE_HOME", cache.c_str(), 1);
    setenv("TMPDIR", cache.c_str(), 1);

    // Conqueror Player: dışarıdan açılan .cqspace oyunu
    const std::string opened = CopyOpenedPackage(app, cache);
    const std::string game = files + "/game";
    if (!opened.empty())
    {
        std::string program = "conqueror";
        std::string package = opened;
        char* argv[] = { program.data(), package.data(), nullptr };
        chdir(files.c_str());
        CQ_RuntimeMain(2, argv);
        ANativeActivity_finish(app->activity);
        std::exit(0);
    }
    if (!Conqueror::Android::ExtractAssets(game))
        __android_log_print(ANDROID_LOG_ERROR, "Conqueror", "Game files could not be extracted to %s", game.c_str());
    if (chdir(game.c_str()) != 0)
        __android_log_print(ANDROID_LOG_ERROR, "Conqueror", "chdir(%s) failed", game.c_str());

    std::string program = "conqueror";
    std::string project = game;
    char* argv[] = { program.data(), project.data(), nullptr };
    CQ_RuntimeMain(2, argv);

    // Oyun kapandı: etkinliği bitir ve süreci sonlandır (Android süreci yeniden kullanırsa motorun statik durumu
    // temiz başlamaz)
    ANativeActivity_finish(app->activity);
    std::exit(0);
}

#endif
