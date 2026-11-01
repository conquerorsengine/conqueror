// Android giriş noktası (NativeActivity): APK içindeki oyun dosyaları ilk açılışta uygulamanın veri klasörüne
// çıkarılır, çalışma dizini oraya alınır ve masaüstündeki oyuncu aynen çalıştırılır.
#ifdef __ANDROID__

#include "Platform/Android/AndroidPlatform.h"

#include <android/log.h>
#include <android_native_app_glue.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

int CQ_RuntimeMain(int argc, char** argv);

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

    const std::string game = files + "/game";
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
