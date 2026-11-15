#include "MaterialPreview.h"

#include "Core/Math/Math.h"
#include "Renderer/RenderTargetRegistry.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "Scene/CameraRenderTarget.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "UI/EditorUI.h"

#include <glad/glad.h>
#include <imgui.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace Conqueror::Editor::MaterialPreview
{
    namespace
    {
        constexpr int kSize = 256;
        constexpr size_t kSlots = 8;   // aynı anda çizilebilen önizleme (büyük önizleme + Inspector'daki küçük küreler)

        // Her yuva kendi küçük sahnesi: küre (ya da küp / silindir / düzlem), güneş ışığı ve hedef dokuya çizen kamera. Sahne başına
        // kare başına bir çizim olduğundan (CameraRenderTargets) her malzeme kendi yuvasında
        struct Slot
        {
            std::shared_ptr<Scene> Preview;
            entt::entity Shape = entt::null;
            std::string Key;
            std::string Target;
            uint64_t LastUsed = 0;
        };

        struct State
        {
            std::vector<Slot> Slots;
            uint64_t Tick = 0;
            int ShapeType = 0;              // büyük önizleme: 0 küre, 1 küp, 2 silindir, 3 düzlem
            float Yaw = -25.0f, Pitch = 12.0f;
            uint32_t Framebuffer = 0;       // son büyük önizlemenin çıktısı (testler)
            int Renders = 0;
        };

        // Kapanışta yok edilmez (GL bağlamı o sırada gitmiş olur)
        State& S()
        {
            static auto& s = *new State();
            return s;
        }

        MeshType ShapeMesh(int type)
        {
            switch (type)
            {
                case 1: return MeshType::Cube;
                case 2: return MeshType::Cylinder;
                case 3: return MeshType::Plane;
                default: return MeshType::Sphere;
            }
        }

        void BuildScene(Slot& slot)
        {
            slot.Preview = std::make_shared<Scene>();
            Scene& scene = *slot.Preview;

            Entity shape = scene.CreateEntity("Preview Shape");
            shape.AddComponent<MeshRendererComponent>().Type = MeshType::Sphere;
            slot.Shape = (entt::entity)shape;

            // Unity önizlemesi gibi: sol üst önden anahtar ışık, gölge yok (tek nesne)
            Entity sun = scene.CreateEntity("Preview Light");
            auto& light = sun.AddComponent<DirectionalLightComponent>();
            light.Color = glm::vec3(1.0f);
            light.Intensity = 1.2f;
            light.CastShadows = false;
            sun.GetComponent<TransformComponent>().Rotation = Math::EulerForForward(glm::vec3(0.55f, -0.65f, -0.55f));

            Entity cam = scene.CreateEntity("Preview Camera");
            auto& cc = cam.AddComponent<CameraComponent>();
            cc.Camera.SetPerspective(glm::radians(30.0f), 0.05f, 50.0f);
            cc.Background = CameraComponent::BackgroundMode::SolidColor;
            cc.BackgroundColor = { 0.19f, 0.20f, 0.23f, 1.0f };
            cam.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 2.45f };   // kamera -Z'ye bakar
            auto& rt = cam.AddComponent<RenderTargetComponent>();
            rt.Name = slot.Target;
            rt.Width = kSize;
            rt.Height = kSize;
            rt.UpdateMode = RenderTargetUpdate::Always;
        }

        // Anahtar için yuva (yoksa en uzun süredir kullanılmayan yuva bu malzemeye geçer)
        Slot& Acquire(const std::string& key)
        {
            State& s = S();
            ++s.Tick;
            for (auto& slot : s.Slots)
                if (slot.Key == key)
                {
                    slot.LastUsed = s.Tick;
                    return slot;
                }
            Slot* use = nullptr;
            if (s.Slots.size() < kSlots)
            {
                s.Slots.emplace_back();
                use = &s.Slots.back();
                use->Target = "__MaterialPreview" + std::to_string(s.Slots.size() - 1);
                BuildScene(*use);
            }
            else
                use = &*std::min_element(s.Slots.begin(), s.Slots.end(), [](const Slot& a, const Slot& b) { return a.LastUsed < b.LastUsed; });
            use->Key = key;
            use->LastUsed = s.Tick;
            return *use;
        }

        // Yuvayı bu malzeme ve pozla çizer; çıktı dokusu (yoksa nullptr)
        std::shared_ptr<RenderTargetTexture> Render(Slot& slot, const std::shared_ptr<Material>& material, const std::string& path, int shapeType,
                                                    float yaw, float pitch)
        {
            Scene& scene = *slot.Preview;
            Entity shape{ slot.Shape, &scene };
            auto& mr = shape.GetComponent<MeshRendererComponent>();
            mr.MaterialPath = path;
            mr.MaterialInstance = material;   // paylaşılan örnek: Inspector'daki değişiklik aynı karede görünür
            mr.Type = ShapeMesh(shapeType);
            auto& tc = shape.GetComponent<TransformComponent>();
            tc.Rotation = glm::radians(glm::vec3(shapeType == 3 ? pitch + 60.0f : pitch, yaw, 0.0f));   // düzlem kameraya eğik bakar
            tc.Scale = shapeType == 1 ? glm::vec3(0.72f) : shapeType == 2 ? glm::vec3(0.62f) : glm::vec3(1.0f);
            scene.UpdateWorldTransforms();
            CameraRenderTargets::Render(scene);
            auto target = RenderTargetRegistry::Acquire(slot.Target);
            return target && target->GetRendererID() ? target : nullptr;
        }

        // Yalnız testler sorduğunda okunur (her karede okumak GPU'yu bekletir)
        void ReadMean(uint32_t framebuffer, float mean[3])
        {
            std::vector<unsigned char> px((size_t)kSize * kSize * 4);
            GLint previous = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
            glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous);
            // Yalnız ortadaki bölge (şekil): arka plan ortalamayı boğmasın
            double sum[3] = { 0, 0, 0 };
            int n = 0;
            for (int y = kSize * 3 / 8; y < kSize * 5 / 8; ++y)
                for (int x = kSize * 3 / 8; x < kSize * 5 / 8; ++x, ++n)
                    for (int c = 0; c < 3; ++c)
                        sum[c] += px[((size_t)y * kSize + x) * 4 + c];
            for (int c = 0; c < 3; ++c)
                mean[c] = n ? (float)(sum[c] / n / 255.0) : 0.0f;
        }
    }

    void Draw(const std::string& materialPath)
    {
        State& s = S();
        auto material = MaterialAsset::Load(materialPath);
        if (!material)
            return;
        Slot& slot = Acquire("asset:" + materialPath);
        auto target = Render(slot, material, materialPath, s.ShapeType, s.Yaw, s.Pitch);

        UI::SubHeader("Preview");
        static const char* kShapes[] = { "Sphere", "Cube", "Cylinder", "Plane" };
        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo("##PreviewShape", &s.ShapeType, kShapes, IM_ARRAYSIZE(kShapes));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Preview shape");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset"))
        {
            s.Yaw = -25.0f;
            s.Pitch = 12.0f;
        }
        if (!target)
            return;
        const float side = std::clamp(ImGui::GetContentRegionAvail().x, 64.0f, 320.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - side) * 0.5f));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##MaterialPreview", ImVec2(side, side));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            s.Yaw += d.x * 0.6f;
            s.Pitch = std::clamp(s.Pitch + d.y * 0.6f, -89.0f, 89.0f);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
            ImGui::SetTooltip("Drag to rotate");
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)(uint64_t)target->GetRendererID(), at, ImVec2(at.x + side, at.y + side),
                                             ImVec2(0, 1), ImVec2(1, 0));
        s.Framebuffer = target->GetFramebuffer();
        ++s.Renders;
    }

    bool Thumbnail(const std::shared_ptr<Material>& material, const std::string& key, float size)
    {
        if (!material)
            return false;
        Slot& slot = Acquire("thumb:" + key);
        auto target = Render(slot, material, key.rfind("instance:", 0) == 0 ? std::string() : key, 0, -25.0f, 12.0f);
        if (!target)
            return false;
        ImGui::Image((ImTextureID)(uint64_t)target->GetRendererID(), ImVec2(size, size), ImVec2(0, 1), ImVec2(1, 0));
        return true;
    }

    bool LastStats(float rgb[3], int& renders)
    {
        const State& s = S();
        renders = s.Renders;
        rgb[0] = rgb[1] = rgb[2] = 0.0f;
        if (!s.Renders || !s.Framebuffer)
            return false;
        ReadMean(s.Framebuffer, rgb);
        return true;
    }
}
