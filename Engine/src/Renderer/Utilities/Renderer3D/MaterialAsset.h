#pragma once

#include "Core/Base/Base.h"

#include <memory>
#include <string>

namespace Conqueror
{
    class Material;

    // Malzeme varlığı (Unity .mat / Unreal Material): taban rengi ve dokular, metalik / pürüzlülük, normal, ışıma,
    // render modu ve Shader Graph tek bir .cqmat dosyasında. Mesh Renderer yalnız bu dosyaya başvurur; aynı dosyayı
    // kullanan tüm nesneler tek malzeme nesnesini paylaşır (değişince hepsi güncellenir).
    namespace MaterialAsset
    {
        constexpr const char* Extension = ".cqmat";

        CQ_API bool IsMaterialFile(const std::string& path);
        // Önbellekli yükleme: aynı dosya için aynı nesne (bulunamazsa nullptr)
        CQ_API std::shared_ptr<Material> Load(const std::string& path);
        // Diske yaz; nesne bu yolun önbellek kaydı olur
        CQ_API bool Save(const std::shared_ptr<Material>& material, const std::string& path);
        // Yeni malzeme dosyası (isteğe bağlı bir malzemenin kopyasıyla); oluşturulan nesne
        CQ_API std::shared_ptr<Material> Create(const std::string& path, const Material* from = nullptr);
        // Malzemenin dosyası (önbellekte değilse boş)
        CQ_API std::string PathOf(const Material* material);
        // Diskte değişen malzeme dosyalarını yerinde yeniden yükler (editör çağırır); değişen sayısı
        CQ_API int CheckFileChanges();

        // Dosya taşındı / yeniden adlandırıldı: önbellek anahtarı ve varyantların üst yolu güncellenir
        CQ_API void OnFileMoved(const std::string& from, const std::string& to);

        // Motorla gelen salt okunur paket (Packages/Core): yeni 3D nesnelerin varsayılan malzemesi. Dama deseni nesneye yapışık
        // (Unity gibi taşıyınca / döndürünce nesneyle gider), kareler ölçekte de 1 m kalır (nesne uzayında metre izdüşümü)
        CQ_API const std::string& DefaultGridPath();
        CQ_API std::shared_ptr<Material> DefaultGrid();
        // Dünyaya sabit dama (Unreal WorldGridMaterial): eski sahnelerin varsayılanı; malzemesiz / eski biçimli nesneler bununla
        // yüklenir, görünümleri değişmez
        CQ_API const std::string& DefaultGridWorldPath();
        CQ_API std::shared_ptr<Material> DefaultGridWorld();
        // Motor kaynaklarındaki dosyalar (Packages) düzenlenemez
        CQ_API bool IsReadOnly(const std::string& path);

        // YAML metni (sahneye gömülü malzeme ve dosya aynı biçimi kullanır)
        CQ_API std::string ToYaml(const Material& material);
        CQ_API bool FromYaml(const std::string& text, Material& material, std::string* error = nullptr);
    }
}
