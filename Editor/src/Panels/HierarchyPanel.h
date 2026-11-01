#pragma once

#include "Scene/Scene.h"
#include "Scene/Entity.h"

#include <imgui.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace Conqueror::Editor
{
    class HierarchyPanel
    {
    public:
        HierarchyPanel(const std::shared_ptr<Scene>& scene);
        ~HierarchyPanel() = default;

        void OnImGuiRender();
        void SetContext(const std::shared_ptr<Scene>& scene);
        void SetSelectionChangedCallback(std::function<void(Entity)> callback) { m_SelectionChangedCallback = callback; }
        // Çift tık / "Frame" menüsü: görünümde kadraja al
        void SetFrameCallback(std::function<void(Entity)> callback) { m_FrameCallback = callback; }
        void SetSceneName(const std::string& name) { m_SceneName = name; }

        Entity GetSelectedEntity() const { return m_SelectionContext; }
        void SetSelectedEntity(Entity entity);
        void StartRename(Entity entity);

        // Oluşturma menüsü (Hierarchy "+" düğmesi, sağ tık ve GameObject menüsü paylaşır).
        // parent geçerliyse yeni nesne onun çocuğu olur. Oluşturulan varlığı döndürür.
        static Entity DrawCreateMenuItems(Scene* scene, Entity parent);

        // Bileşen ikonları (Hierarchy ve Inspector ortak)
        struct ComponentIcon
        {
            const char* Icon{};
            ImVec4 Color;
            const char* Name{};
        };
        static ComponentIcon GetPrimaryIcon(Entity entity);
        static void GetComponentIcons(Entity entity, std::vector<ComponentIcon>& out);

    private:
        void DrawToolbar();
        void DrawEntityNode(Entity entity, int depth);
        void DrawFilteredList();
        void DrawEntityContextMenu(Entity entity, bool& deleted);
        void HandleRowClick(Entity entity);
        void Select(Entity entity);
        bool IsDescendant(Entity parent, Entity potentialDescendant);
        bool MatchesFilter(Entity entity) const;
        std::vector<Entity> GetRootEntities() const;

    private:
        std::shared_ptr<Scene> m_Context;
        Entity m_SelectionContext;
        std::function<void(Entity)> m_SelectionChangedCallback;
        std::function<void(Entity)> m_FrameCallback;
        std::string m_SceneName = "Scene";

        // Arama
        char m_Filter[128] = "";

        // Shift ile aralık seçimi: önceki karedeki görünür sıra
        std::vector<Entity> m_VisibleOrder;
        std::vector<Entity> m_VisibleOrderBuilding;
        Entity m_RangeAnchor;

        // Seçili varlığa kaydır (viewport'tan seçilince)
        Entity m_ScrollTo;
        Entity m_LastSelected;
        std::unordered_set<uint32_t> m_ForceOpen;

        // Rename state
        bool m_IsRenaming = false;
        // Revert, varlığı yok edip yeniden oluşturur: menü çizimi bitince uygulanır
        Entity m_PendingPrefabRevert;
        Entity m_RenameEntity;
        char m_RenameBuffer[256] = "";
        int m_RenameFrame = 0;
    };
}
