#pragma once

#include "Core/Base/Base.h"

#include <string>
#include <vector>

// Unreal Gameplay Tags karşılığı: noktalı hiyerarşik etiketler ("Status.Burning", "Ability.Fire.Ball").
// Üst etiket sorgusu alt etiketleri de kapsar: "Status" sorgusu "Status.Burning" taşıyan nesneyle eşleşir.
// Oyun etiketinden (Tag alanı, tek değer) farklı olarak bir nesne istediği kadar gameplay tag taşır.
namespace Conqueror
{
    class Scene;
    class Entity;

    struct GameplayTagsComponent
    {
        // Inspector: virgül ya da satır sonuyla ayrılmış etiketler
        std::string Tags;

        // Çalışma zamanı: ayrıştırılmış liste (Tags metni değişince yeniden kurulur)
        std::vector<std::string> Parsed;
        std::string ParsedFrom;
    };

    namespace GameplayTags
    {
        // "A.B" etiketi "A" sorgusuyla eşleşir (exact=false), "A.B" ile her zaman eşleşir
        CQ_API bool Matches(const std::string& owned, const std::string& query, bool exact = false);
        CQ_API bool IsValidTag(const std::string& tag);
        CQ_API std::string Normalize(const std::string& tag);   // boşlukları ve fazla noktaları temizle

        CQ_API const std::vector<std::string>& Get(Entity entity);
        CQ_API bool Has(Entity entity, const std::string& query, bool exact = false);
        CQ_API bool HasAny(Entity entity, const std::vector<std::string>& queries, bool exact = false);
        CQ_API bool HasAll(Entity entity, const std::vector<std::string>& queries, bool exact = false);
        CQ_API bool Add(Entity entity, const std::string& tag);
        CQ_API bool Remove(Entity entity, const std::string& tag, bool removeChildren = false);
        CQ_API std::vector<uint32_t> FindEntities(Scene& scene, const std::string& query, bool exact = false);

        // Ek etiket kaynakları (ör. Ability System'in efektlerden gelen etiketleri) sorgulara katılır
        using ExtraTagSource = std::vector<std::string> (*)(Entity entity);
        CQ_API void SetExtraSource(ExtraTagSource source);
        CQ_API std::vector<std::string> GetAll(Entity entity);   // bileşen + ek kaynak
    }

    void RegisterGameplayTagComponents();
}
