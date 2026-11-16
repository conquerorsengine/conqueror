#include "UIBinding.h"

#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "UI/UIComponents.h"
#include "UI/UISystem.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<UIBindingComponent>(Entity, UIBindingComponent&) {}

    namespace UIBindings
    {
        namespace
        {
            std::string Lower(std::string s)
            {
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
                return s;
            }

            bool SameName(const std::string& a, const std::string& b) { return Lower(a) == Lower(b); }

            bool ToBool(const std::string& v)
            {
                const std::string l = Lower(v);
                return l == "true" || l == "1" || l == "yes" || (std::atof(v.c_str()) != 0.0f && l != "false");
            }

            glm::vec4 ToColor(const std::string& v, const glm::vec4& fallback)
            {
                glm::vec4 c = fallback;
                std::string s = v;
                for (char& ch : s)
                    if (!(std::isdigit((unsigned char)ch) || ch == '.' || ch == '-' || ch == 'e'))
                        ch = ' ';
                const int n = std::sscanf(s.c_str(), "%f %f %f %f", &c.r, &c.g, &c.b, &c.a);
                return n >= 3 ? c : fallback;
            }
        }

        bool Resolve(Scene& scene, const std::string& source, std::string& value, std::string* error)
        {
            // <Varlık>.<Bileşen>.<Alan>; varlık adında nokta olabilir: son iki parça bileşen ve alan
            const size_t last = source.rfind('.');
            const size_t mid = last == std::string::npos ? std::string::npos : source.rfind('.', last - 1);
            if (mid == std::string::npos)
            {
                if (error) *error = "expected Entity.Component.Field";
                return false;
            }
            const std::string entityName = source.substr(0, mid), compName = source.substr(mid + 1, last - mid - 1), fieldName = source.substr(last + 1);
            Entity e;
            for (auto h : scene.m_Registry.view<TagComponent>())
                if (scene.m_Registry.get<TagComponent>(h).Tag == entityName)
                {
                    e = Entity{ h, &scene };
                    break;
                }
            if (!e)
            {
                if (error) *error = "no object named '" + entityName + "'";
                return false;
            }
            for (const ComponentInfo& info : ComponentRegistry::All())
            {
                if (!(SameName(info.Name, compName) || SameName(info.TypeName, compName) || SameName(info.ScriptName, compName) ||
                      SameName(info.TypeName, compName + "Component")))
                    continue;
                if (!info.Has || !info.Has(e) || !info.Get)
                    continue;
                void* component = info.Get(e);
                for (const ComponentField& f : info.Fields)
                    if (SameName(f.Name, fieldName) || SameName(f.Label, fieldName))
                    {
                        value = ComponentRegistry::FieldToString(f, component);
                        return true;
                    }
                if (error) *error = info.Name + " has no field '" + fieldName + "'";
                return false;
            }
            if (error) *error = entityName + " has no component '" + compName + "'";
            return false;
        }

        void Update(Scene& scene)
        {
            auto view = scene.m_Registry.view<UIBindingComponent>();
            for (auto h : view)
            {
                const auto& b = view.get<UIBindingComponent>(h);
                Entity e{ h, &scene };
                std::string v;
                if (!b.Text.empty() && e.HasComponent<UITextComponent>() && Resolve(scene, b.Text, v))
                {
                    std::string text = b.Format.empty() ? std::string("{0}") : b.Format;
                    const size_t at = text.find("{0}");
                    text = at == std::string::npos ? text + v : text.replace(at, 3, v);
                    e.GetComponent<UITextComponent>().Text = text;
                }
                if (!b.Value.empty() && Resolve(scene, b.Value, v))
                {
                    if (e.HasComponent<UISliderComponent>())
                        e.GetComponent<UISliderComponent>().Value = (float)std::atof(v.c_str());
                    if (e.HasComponent<UIScrollbarComponent>())
                        e.GetComponent<UIScrollbarComponent>().Value = (float)std::atof(v.c_str());
                }
                if (!b.Fill.empty() && e.HasComponent<UIImageComponent>() && Resolve(scene, b.Fill, v))
                    e.GetComponent<UIImageComponent>().FillAmount = std::clamp((float)std::atof(v.c_str()), 0.0f, 1.0f);
                if (!b.Color.empty() && Resolve(scene, b.Color, v))
                {
                    if (e.HasComponent<UIImageComponent>())
                        e.GetComponent<UIImageComponent>().Color = ToColor(v, e.GetComponent<UIImageComponent>().Color);
                    if (e.HasComponent<UITextComponent>())
                        e.GetComponent<UITextComponent>().Color = ToColor(v, e.GetComponent<UITextComponent>().Color);
                }
                if (!b.Active.empty() && Resolve(scene, b.Active, v))
                {
                    if (!e.HasComponent<ActiveComponent>())
                        e.AddComponent<ActiveComponent>();
                    e.GetComponent<ActiveComponent>().Active = ToBool(v);
                }
                if (!b.Interactable.empty() && Resolve(scene, b.Interactable, v))
                    if (UISelectableState* s = UISystem::GetSelectableState(e))
                        s->Interactable = ToBool(v);
            }
        }

        void RegisterComponents()
        {
            using B = UIBindingComponent;
            const char* tip = "Source: Entity.Component.Field, e.g. Player.Health.Current (Inspector names work). Empty: not bound.";
            ComponentDef<B>("UI Binding", "UIBindingComponent", "UI", ICON_FA_LINK)
                .Describe("Property Binding (Unreal UMG Bind): this UI element's text, value, fill, color, visibility or interactable state follows "
                          "another object's component field every frame, without a script.")
                .Field("Text", "Text", &B::Text).Tooltip(tip)
                .Field("Format", "Text Format", &B::Format).Tooltip("{0} is replaced by the value, e.g. \"HP: {0}\"")
                .Field("Value", "Value", &B::Value).Tooltip(tip)
                .Field("Fill", "Fill Amount", &B::Fill).Tooltip(tip)
                .Field("Color", "Color", &B::Color).Tooltip(tip)
                .Field("Active", "Visible", &B::Active).Tooltip(tip)
                .Field("Interactable", "Interactable", &B::Interactable).Tooltip(tip);
        }
    }
}
