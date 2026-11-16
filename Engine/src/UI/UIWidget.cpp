#include "UIWidget.h"

#include "Core/Logging/Log.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "UI/UISystem.h"

#include <IconsFontAwesome6.h>

#include <vector>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<UIWidgetComponent>(Entity, UIWidgetComponent&) {}

    namespace UIWidgets
    {
        void InstantiateAll(Scene& scene)
        {
            std::vector<entt::entity> hosts;
            for (auto e : scene.m_Registry.view<UIWidgetComponent>())
                hosts.push_back(e);
            for (auto h : hosts)
            {
                auto& w = scene.m_Registry.get<UIWidgetComponent>(h);
                if (w.Spawned || w.Widget.empty())
                    continue;
                w.Spawned = true;
                Entity host{ h, &scene };
                Entity root = PrefabSerializer::Instantiate(scene, w.Widget, host);
                if (!root)
                    CQ_CORE_ERROR("[UI Widget] {0}: cannot load {1}", host.GetComponent<TagComponent>().Tag, w.Widget);
            }
            scene.UpdateWorldTransforms();
        }

        bool CreateFile(const std::filesystem::path& path)
        {
            auto scene = std::make_shared<Scene>();
            Entity canvas = UISystem::CreateCanvas(*scene, path.stem().string());
            return canvas && PrefabSerializer::Serialize(canvas, path);
        }

        bool SaveCanvas(Entity canvas, const std::filesystem::path& path)
        {
            if (!canvas || !canvas.HasComponent<CanvasComponent>())
                return false;
            return PrefabSerializer::Serialize(canvas, path);
        }

        void RegisterComponents()
        {
            using W = UIWidgetComponent;
            ComponentDef<W>("UI Widget", "UIWidgetComponent", "UI", ICON_FA_OBJECT_GROUP)
                .Describe("Shows a UI Widget (.cqwidget: a HUD or menu designed in the UI Editor) when the game plays. The same widget can be used in "
                          "every scene; its elements are created under this object before scripts start, so scripts find them by name.")
                .Field("Widget", "Widget", &W::Widget, FieldKind::Asset).Filter(".cqwidget");
        }
    }
}
