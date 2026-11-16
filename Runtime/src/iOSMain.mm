// iOS / iPadOS giriş noktası: UIKit uygulaması. Oyun dosyaları paketin içinden (conqueror.app/game) yazılabilir uygulama klasörüne
// kopyalanır (Android'deki APK çıkarma gibi), çalışma dizini oraya alınır ve masaüstündeki oyuncu kurulur; kareleri UIKit'in ekran
// tazelemesi (CADisplayLink) sürer. Uygulamayı Tools/iOS/package_ios.py paketler (simülatör ya da aygıt).
#include "Core/Base/Base.h"

#if defined(CQ_PLATFORM_IOS)

#include "Core/Application.h"
#include "Core/Logging/Log.h"
#include "Core/Project/GamePackage.h"
#include "Platform/iOS/iOSPlatform.h"

#import <UIKit/UIKit.h>

#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

@interface CQAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation CQAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    // Motorun POSIX yolları (PlayerPrefs, günlük, gölgelendirici önbelleği, geçici dosyalar) HOME / XDG / TMPDIR'e bakar:
    // uygulama kum havuzunun Library klasörleri
    const std::string library = Conqueror::iOS::LibraryDirectory();
    const std::string support = library + "/Application Support";
    const std::string caches = Conqueror::iOS::CachesDirectory();
    mkdir(support.c_str(), 0700);
    mkdir(caches.c_str(), 0700);
    setenv("XDG_DATA_HOME", support.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (library + "/Preferences").c_str(), 1);
    setenv("XDG_CACHE_HOME", caches.c_str(), 1);

    const std::string game = Conqueror::iOS::PrepareGameFolder();
    if (game.empty() || chdir(game.c_str()) != 0)
        NSLog(@"[Conqueror] The game files were not found in the app bundle (game/)");

    Conqueror::Log::Init();
    CQ_CORE_INFO("Starting Conqueror Runtime (iOS)...");
    static std::string program = "conqueror";
    static std::string project = game;
    static char* argv[] = { program.data(), project.data(), nullptr };
    Conqueror::Application* app = Conqueror::CreateApplication({ 2, argv });
    Conqueror::iOS::RunFrameLoop(app);
    return YES;
}

// .cqspace (Dosyalar uygulaması, AirDrop, "Open in"): uygulamanın önbelleğine kopyalanır, oyuncu açılış ekranından açar (264)
- (BOOL)application:(UIApplication*)application openURL:(NSURL*)url options:(NSDictionary<UIApplicationOpenURLOptionsKey, id>*)options
{
    if (![[url.pathExtension lowercaseString] isEqualToString:@"cqspace"])
        return NO;
    const BOOL scoped = [url startAccessingSecurityScopedResource];
    NSString* caches = [NSString stringWithUTF8String:Conqueror::iOS::CachesDirectory().c_str()];
    NSString* dest = [caches stringByAppendingPathComponent:[url lastPathComponent]];
    [[NSFileManager defaultManager] removeItemAtPath:dest error:nil];
    NSError* error = nil;
    const BOOL ok = [[NSFileManager defaultManager] copyItemAtURL:url toURL:[NSURL fileURLWithPath:dest] error:&error];
    if (scoped)
        [url stopAccessingSecurityScopedResource];
    if (ok)
        Conqueror::GamePackage::RequestOpen(dest.UTF8String);
    else
        NSLog(@"[Conqueror] Could not open %@: %@", url, error);
    return ok;
}

@end

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([CQAppDelegate class]));
    }
}

#endif
