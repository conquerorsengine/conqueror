// HarmonyOS giriş noktası (editör): libConquerorEditor.so yüklenirken NAPI modülü "ConquerorEditor" kaydedilir (ArkTS:
// import engine from 'libConquerorEditor.so', XComponent libraryname: 'ConquerorEditor'). HAP'teki motor kaynakları
// filesDir/editor'e çıkarılır ve editör masaüstündeki main gövdesiyle (CQ_EditorMain) motor iş parçacığında çalışır.
// HAP uygulaması tek süreçtir, başlatıcı (hub) yoktur: proje verilmezse (ArkTS start() argümanı) ilk açılışta örnek 3B proje
// filesDir/Projects/My Project'e kopyalanır ve o açılır; başka projeler Dosya menüsünden sistem belge seçicisiyle açılır.
#include "Core/Base/Base.h"

#if defined(CQ_PLATFORM_OHOS)

#include "Platform/OHOS/OHOSPlatform.h"

#include <filesystem>
#include <string>
#include <vector>

int CQ_EditorMain(int argc, char** argv);

namespace
{
    std::string DefaultProject()
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path target = fs::path(Conqueror::OHOS::FilesDirectory()) / "Projects" / "My Project";
        for (const auto& entry : fs::directory_iterator(target, ec))
            if (entry.path().extension() == ".cqproj")
                return target.string();
        // Çalışma klasörü çıkarılan motor içeriğidir (Resources/DefaultProjects/3d: hub'ın "3D" şablonu)
        const fs::path source = fs::current_path(ec) / "Resources" / "DefaultProjects" / "3d";
        fs::create_directories(target, ec);
        fs::copy(source, target, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
        return ec ? std::string() : target.string();
    }

    int EditorEntry(int argc, char** argv)
    {
        std::vector<char*> args(argv, argv + argc);
        std::string project;
        if (argc < 2)
        {
            project = DefaultProject();
            if (!project.empty())
                args.push_back(project.data());
        }
        const int count = (int)args.size();
        args.push_back(nullptr);
        return CQ_EditorMain(count, args.data());
    }
}

extern "C" __attribute__((constructor)) void CQ_RegisterEditorModule(void)
{
    Conqueror::OHOS::RegisterModule("ConquerorEditor", &EditorEntry, "editor");
}

#endif
