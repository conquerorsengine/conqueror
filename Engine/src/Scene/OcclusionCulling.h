#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// Occlusion Culling (Unity): pişirilmiş görünürlük. Görünüm hacmi (Occlusion Area, yoksa statik nesnelerin kapsadığı alan)
// hücrelere bölünür; her hücreden statik nesnelerin hangilerinin görülebildiği GPU'da kimlik görüntüleriyle bulunur. Oyunda kamera
// hangi hücredeyse oradan görünmeyen statik nesneler çizilmez (gölgeleri kalır). Occlusion Portal (kapı) kapalıyken arkası da gizlenir.
namespace Conqueror
{
    class Scene;

    struct OcclusionAreaComponent
    {
        glm::vec3 Center{ 0.0f };
        glm::vec3 Size{ 10.0f, 5.0f, 10.0f };
        bool IsViewVolume = true;             // kamera bu hacimde dolaşabilir (hücreler burada)
    };

    struct OcclusionPortalComponent
    {
        bool Open = true;                     // kapalıyken arkasındakiler gizlenir (oyunda kapı açılınca Open = true)
        glm::vec3 Center{ 0.0f };
        glm::vec3 Size{ 1.0f, 2.0f, 0.2f };
    };

    struct OcclusionBakeSettings
    {
        float CellSize = 3.0f;                // m
        int Resolution = 64;                  // yön başına piksel (6 yön)
        int SamplesPerCell = 5;               // 1: merkez, 5: merkez + 4 köşe, 9: + üst köşeler
    };

    struct OcclusionData
    {
        glm::vec3 Origin{ 0.0f };
        glm::vec3 CellSize{ 3.0f };
        glm::ivec3 Dims{ 0 };
        std::vector<uint64_t> Renderers;              // statik nesneler (sıra -> UUID)
        std::vector<uint64_t> Portals;                // portal UUID'leri (en çok 32)
        std::vector<int32_t> CellIndex;               // hücre -> Cells dizini (-1: görünüm hacmi dışı)
        struct Cell
        {
            std::vector<uint32_t> Visible;            // görünen nesne sıraları
            std::vector<uint32_t> PortalMask;         // her biri için: kapanınca gizleyen portallar (bit)
        };
        std::vector<Cell> Cells;
        float BakeSeconds = 0.0f;

        CQ_API std::string Serialize() const;
        CQ_API static std::shared_ptr<OcclusionData> Deserialize(const std::string& data);
    };

    // Pişirme işi: ana iş parçacığında (GL) karelere bölünerek yürür, düzenleyici donmaz
    class CQ_API OcclusionBaker
    {
    public:
        OcclusionBaker();
        ~OcclusionBaker();
        bool Begin(Scene& scene, const OcclusionBakeSettings& settings, std::string& error);
        // budgetMs kadar çalışır; bitince true
        bool Step(float budgetMs);
        void Cancel();
        bool IsRunning() const;
        float Progress() const;
        std::shared_ptr<OcclusionData> Result() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m;
    };

    namespace OcclusionCulling
    {
        // Kameranın bulunduğu hücreden görünmeyen statik nesneler (pişirme yoksa / kamera hücrelerin dışındaysa boş)
        CQ_API void ComputeHidden(Scene& scene, const glm::vec3& cameraPosition, std::unordered_set<uint32_t>& hidden);
        // Seçiliyken / görselleştirmede: hücreler ve portallar
        CQ_API void DrawGizmo(Scene& scene, uint32_t entity);
        CQ_API void DrawCells(Scene& scene, const glm::vec3& cameraPosition);
    }

    void RegisterOcclusionComponents();
}
