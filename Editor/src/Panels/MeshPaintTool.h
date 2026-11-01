#pragma once

#include "Panels/ViewportPanel.h"
#include "Scene/Entity.h"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor
{
    // Mesh Paint modu (Unreal "Mesh Paint"): seçili nesnenin Vertex Paint bileşeni denetçisinden açılır;
    // viewport'ta sürükleyerek köşe renkleri boyanır (Shift: silgi = beyaz), her darbe tek adımda geri alınır.
    class MeshPaintTool
    {
    public:
        static MeshPaintTool& Get();

        void SetSceneProvider(std::function<std::shared_ptr<Scene>()> provider) { m_Scene = std::move(provider); }
        // Vertex Paint bileşeninin denetçi ek alanı (fırça ayarları, Boya / Doldur / Temizle)
        void DrawInspector(Entity entity);
        // ViewportPanel araç kancası: true = fare bu karede boyamaya gitti
        bool OnViewportTool(const ViewportToolContext& ctx);

        bool IsActive() const { return m_Active; }
        void SetActive(bool active) { m_Active = active; }

        glm::vec4 Color{ 1.0f, 0.2f, 0.15f, 1.0f };
        float Radius = 0.5f;
        float Strength = 0.6f;
        float Falloff = 0.5f;
        bool PaintR = true, PaintG = true, PaintB = true, PaintA = false;

    private:
        void BeginStroke(Entity entity);
        void EndStroke(Entity entity);

        std::function<std::shared_ptr<Scene>()> m_Scene;
        bool m_Active = false;
        bool m_Stroking = false;
        std::string m_StrokeBefore;
        uint64_t m_StrokeEntity = 0;
    };
}
