#include "UI/UIExtras.h"
#include "UISystem.h"
#include "Core/Serialization/SceneSerializer.h"
#include <unordered_map>
#include "UI/UIToolkit.h"
#include "Scene/AudioOcclusion.h"
#include "Scene/AssetRuntime.h"
#include "Core/Input/InputGlyphs.h"
#include "FontAtlas.h"
#include "RichText.h"
#include "UIComponents.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/SpriteSheet.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Input/Input.h"
#include "Core/Utils/Platform.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

namespace Conqueror
{
    namespace
    {
        constexpr float kWorldUnitsPerPixel = 0.1f; // sahne görünümünde canvas ölçeği (1080 px = 108 birim)

        std::vector<UIEvent> s_Events;
        std::vector<UIEvent> s_PendingEvents;
        bool s_PointerOverUI = false;
        // Ekran uzayı canvas'larının çizildiği görünümün kökü (maske kırpması pencere koordinatında)
        glm::ivec2 s_ScreenOrigin{ 0, 0 };

        // Odak / basılı öğe (sahne doğrulamalı)
        struct EntityRef
        {
            Scene* SceneRef = nullptr;
            entt::entity Handle = entt::null;
            Entity Get(Scene& scene) const
            {
                if (SceneRef != &scene || Handle == entt::null || !scene.m_Registry.valid(Handle))
                    return {};
                return Entity{ Handle, &scene };
            }
            void Set(Entity e) { SceneRef = e ? e.GetScene() : nullptr; Handle = e ? (entt::entity)e : entt::null; }
            void Clear() { SceneRef = nullptr; Handle = entt::null; }
        };
        EntityRef s_Pressed, s_Focused, s_OpenDropdown;
        // Event Trigger: imlecin üstünde / basılı olduğu öğe
        EntityRef s_TriggerHover, s_TriggerPressed;
        glm::vec2 s_TriggerPressPos{ 0.0f };
        // Klavye / gamepad gezinmesi
        EntityRef s_Selected;
        bool s_NavActive = false;
        glm::vec2 s_LastMouse{ -1.0f };
        glm::ivec2 s_NavHeld{ 0 };
        float s_NavRepeat = 0.0f;
        bool s_PadSubmitPrev = false, s_PadCancelPrev = false;

        std::shared_ptr<Texture2D> s_RoundSprite;   // 9-slice yuvarlak dikdörtgen
        std::shared_ptr<Texture2D> s_CircleSprite;
        constexpr float kRoundRadius = 16.0f;       // dokudaki köşe yarıçapı (piksel)

        std::shared_ptr<Texture2D> MakeShapeTexture(int size, float radius)
        {
            std::vector<uint8_t> px((size_t)size * size * 4);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    // Yuvarlak dikdörtgen işaretli mesafe (kenar yumuşatmalı)
                    glm::vec2 p((float)x + 0.5f, (float)y + 0.5f);
                    glm::vec2 c((float)size * 0.5f);
                    glm::vec2 q = glm::abs(p - c) - (c - glm::vec2(radius));
                    float d = glm::length(glm::max(q, glm::vec2(0.0f))) + std::min(std::max(q.x, q.y), 0.0f) - radius;
                    float a = std::clamp(0.5f - d, 0.0f, 1.0f);
                    size_t i = ((size_t)y * size + x) * 4;
                    px[i] = px[i + 1] = px[i + 2] = 255;
                    px[i + 3] = (uint8_t)std::lround(a * 255.0f);
                }
            auto tex = Texture2D::Create((uint32_t)size, (uint32_t)size);
            tex->SetData(px.data(), (uint32_t)px.size());
            return tex;
        }

        void EnsureSprites()
        {
            if (!s_RoundSprite)
                s_RoundSprite = MakeShapeTexture(64, kRoundRadius);
            if (!s_CircleSprite)
                s_CircleSprite = MakeShapeTexture(64, 32.0f);
        }

        // ------------------------------------------------------------ Yerleşim

        struct Item
        {
            mutable Entity E;
            glm::mat4 M{ 1.0f };    // öğe yereli (0..w,0..h) -> canvas birimi
            glm::vec2 Size{ 0.0f };
            glm::vec4 Clip{ 0.0f }; // canvas uzayı AABB (z<0: kırpma yok)
            int Depth = 0;
            float Alpha = 1.0f;       // Canvas Group saydamlığı (üstlerle çarpılmış)
            bool Interactable = true; // Canvas Group etkileşimi
            bool Blocks = true;       // Canvas Group: tıklamayı engeller mi
        };

        // Content Size Fitter için metnin tercih edilen boyutu (aşağıda tanımlı)
        glm::vec2 PreferredTextSize(const UITextComponent& t, float maxWidth);

        float ComputeScaleFactor(Entity canvas, const glm::vec2& screen)
        {
            if (canvas.HasComponent<CanvasComponent>() && canvas.GetComponent<CanvasComponent>().Mode == CanvasComponent::RenderMode::WorldSpace)
                return 1.0f;
            if (!canvas.HasComponent<CanvasScalerComponent>())
                return 1.0f;
            const auto& sc = canvas.GetComponent<CanvasScalerComponent>();
            switch (sc.ScaleMode)
            {
                case CanvasScalerComponent::UIScaleMode::ScaleWithScreenSize:
                {
                    glm::vec2 ref = glm::max(sc.ReferenceResolution, glm::vec2(1.0f));
                    float lw = std::log2(std::max(screen.x, 1.0f) / ref.x), lh = std::log2(std::max(screen.y, 1.0f) / ref.y);
                    return std::pow(2.0f, lw + (lh - lw) * std::clamp(sc.MatchWidthOrHeight, 0.0f, 1.0f));
                }
                case CanvasScalerComponent::UIScaleMode::ConstantPixelSize:
                default:
                    return std::max(sc.ScaleFactor, 0.01f);
            }
        }

        glm::vec2 CanvasSize(Entity canvas, const glm::vec2& screen)
        {
            if (canvas.GetComponent<CanvasComponent>().Mode == CanvasComponent::RenderMode::WorldSpace)
            {
                if (canvas.HasComponent<CanvasScalerComponent>())
                    return canvas.GetComponent<CanvasScalerComponent>().ReferenceResolution;
                return glm::vec2(1920.0f, 1080.0f);
            }
            return screen / ComputeScaleFactor(canvas, screen);
        }

        glm::mat4 LocalMatrix(const glm::vec2& pivotPos, const RectTransformComponent& rt, const glm::vec2& size)
        {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(pivotPos, 0.0f));
            if (rt.Rotation != 0.0f)
                m = glm::rotate(m, glm::radians(rt.Rotation), glm::vec3(0, 0, 1));
            if (rt.Scale != glm::vec2(1.0f))
                m = glm::scale(m, glm::vec3(rt.Scale, 1.0f));
            return glm::translate(m, glm::vec3(-size * rt.Pivot, 0.0f));
        }

        glm::vec4 IntersectClip(const glm::vec4& a, const glm::vec4& b)
        {
            if (a.z < 0.0f) return b;
            if (b.z < 0.0f) return a;
            float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
            float x1 = std::min(a.x + a.z, b.x + b.z), y1 = std::min(a.y + a.w, b.y + b.w);
            return glm::vec4(x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0));
        }

        glm::vec4 TransformedAABB(const glm::mat4& m, const glm::vec2& size)
        {
            glm::vec2 c[4] = { glm::vec2(m * glm::vec4(0, 0, 0, 1)), glm::vec2(m * glm::vec4(size.x, 0, 0, 1)),
                               glm::vec2(m * glm::vec4(size.x, size.y, 0, 1)), glm::vec2(m * glm::vec4(0, size.y, 0, 1)) };
            glm::vec2 mn = glm::min(glm::min(c[0], c[1]), glm::min(c[2], c[3])), mx = glm::max(glm::max(c[0], c[1]), glm::max(c[2], c[3]));
            return glm::vec4(mn, mx - mn);
        }

        void LayoutRecursive(Scene& scene, Entity parent, const glm::mat4& parentMatrix, const glm::vec2& parentSize, const glm::vec4& clip,
                             int depth, std::vector<Item>& items, float groupAlpha, bool groupInteractable, bool groupBlocks)
        {
            std::vector<Entity> children;
            for (auto c : parent.GetChildren())
                if (c.HasComponent<RectTransformComponent>() && scene.IsEntityActive(c))
                    children.push_back(c);
            if (children.empty())
                return;
            auto ignored = [](Entity c) {
                return c.HasComponent<UILayoutElementComponent>() && c.GetComponent<UILayoutElementComponent>().IgnoreLayout;
            };

            // Yerleşim grubu: çocukların dikdörtgenlerini doğrudan belirler
            std::vector<glm::vec4> forced(children.size(), glm::vec4(-1.0f));
            if (parent.HasComponent<UILayoutGroupComponent>())
            {
                auto& lg = parent.GetComponent<UILayoutGroupComponent>();
                const glm::vec4 pad = lg.Padding; // sol, sağ, üst, alt
                const float availW = std::max(0.0f, parentSize.x - pad.x - pad.y);
                const float availH = std::max(0.0f, parentSize.y - pad.z - pad.w);
                // Layout Element "Ignore Layout" olanlar kendi çapalarıyla yerleşir
                std::vector<size_t> laid;
                for (size_t i = 0; i < children.size(); ++i)
                    if (!ignored(children[i]))
                        laid.push_back(i);
                const int n = (int)laid.size();
                const int alignCol = (int)lg.ChildAlignment % 3, alignRow = (int)lg.ChildAlignment / 3;
                glm::vec2 contentSize(0.0f);
                if (lg.Type == UILayoutType::Grid)
                {
                    int cols = lg.Constraint > 0 ? lg.Constraint : std::max(1, (int)std::floor((availW + lg.GridSpacing.x) / (lg.CellSize.x + lg.GridSpacing.x)));
                    int rows = (n + cols - 1) / cols;
                    float gridW = cols * lg.CellSize.x + (cols - 1) * lg.GridSpacing.x;
                    float gridH = rows * lg.CellSize.y + (rows - 1) * lg.GridSpacing.y;
                    float startX = pad.x + (availW - gridW) * (alignCol * 0.5f);
                    float startTop = parentSize.y - pad.z - (availH - gridH) * (alignRow * 0.5f);
                    for (int i = 0; i < n; ++i)
                    {
                        int cx = i % cols, cy = i / cols;
                        forced[laid[(size_t)i]] = glm::vec4(startX + cx * (lg.CellSize.x + lg.GridSpacing.x),
                                                            startTop - (cy + 1) * lg.CellSize.y - cy * lg.GridSpacing.y, lg.CellSize.x, lg.CellSize.y);
                    }
                    contentSize = glm::vec2(gridW + pad.x + pad.y, gridH + pad.z + pad.w);
                }
                else
                {
                    const bool vertical = lg.Type == UILayoutType::Vertical;
                    float mainAvail = vertical ? availH : availW;
                    float total = 0.0f;
                    std::vector<float> mainSizes((size_t)n), mins((size_t)n, 0.0f), flex((size_t)n, 0.0f);
                    float flexTotal = 0.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        Entity c = children[laid[(size_t)i]];
                        const auto& rt = c.GetComponent<RectTransformComponent>();
                        float sz = vertical ? rt.SizeDelta.y : rt.SizeDelta.x;
                        if (c.HasComponent<UILayoutElementComponent>())
                        {
                            const auto& le = c.GetComponent<UILayoutElementComponent>();
                            float pref = vertical ? le.PreferredHeight : le.PreferredWidth;
                            float mn = vertical ? le.MinHeight : le.MinWidth;
                            float fl = vertical ? le.FlexibleHeight : le.FlexibleWidth;
                            if (pref >= 0.0f) sz = pref;
                            if (mn >= 0.0f) { mins[(size_t)i] = mn; sz = std::max(sz, mn); }
                            if (fl > 0.0f) { flex[(size_t)i] = fl; flexTotal += fl; }
                        }
                        mainSizes[(size_t)i] = sz;
                        total += sz;
                    }
                    total += lg.Spacing * std::max(0, n - 1);
                    bool forceExpand = vertical ? lg.ChildForceExpandHeight : lg.ChildForceExpandWidth;
                    bool controlMain = vertical ? lg.ControlChildHeight : lg.ControlChildWidth;
                    if (controlMain && flexTotal > 0.0f && mainAvail > total)
                    {
                        // Esnek öğeler kalan alanı ağırlıklarına göre paylaşır
                        float extra = mainAvail - total;
                        for (int i = 0; i < n; ++i)
                            mainSizes[(size_t)i] += extra * flex[(size_t)i] / flexTotal;
                        total = mainAvail;
                    }
                    else if (forceExpand && controlMain && n > 0)
                    {
                        float each = (mainAvail - lg.Spacing * (n - 1)) / n;
                        for (int i = 0; i < n; ++i) mainSizes[(size_t)i] = std::max(mins[(size_t)i], each);
                        total = mainAvail;
                    }
                    float offsetMain = (mainAvail - total) * ((vertical ? alignRow : alignCol) * 0.5f);
                    float cursor = 0.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        const auto& rt = children[laid[(size_t)i]].GetComponent<RectTransformComponent>();
                        bool controlCross = vertical ? lg.ControlChildWidth : lg.ControlChildHeight;
                        float cross = controlCross ? (vertical ? availW : availH) : (vertical ? rt.SizeDelta.x : rt.SizeDelta.y);
                        float crossAvail = vertical ? availW : availH;
                        float crossOffset = (crossAvail - cross) * ((vertical ? alignCol : alignRow) * 0.5f);
                        float m = mainSizes[(size_t)i];
                        if (vertical)
                            forced[laid[(size_t)i]] = glm::vec4(pad.x + crossOffset, parentSize.y - pad.z - offsetMain - cursor - m, cross, m);
                        else
                            forced[laid[(size_t)i]] = glm::vec4(pad.x + offsetMain + cursor, pad.w + (availH - cross) - crossOffset, m, cross);
                        cursor += m + lg.Spacing;
                    }
                    contentSize = vertical ? glm::vec2(parentSize.x, total + pad.z + pad.w) : glm::vec2(total + pad.x + pad.y, parentSize.y);
                }
                // İçeriğe sığdır: üstün SizeDelta'sını güncelle (ScrollRect içeriği için)
                if (lg.FitContent && parent.HasComponent<RectTransformComponent>())
                {
                    auto& prt = parent.GetComponent<RectTransformComponent>();
                    if (lg.Type != UILayoutType::Horizontal) prt.SizeDelta.y = contentSize.y;
                    if (lg.Type != UILayoutType::Vertical) prt.SizeDelta.x = contentSize.x;
                }
            }

            // ScrollRect: ilk çocuk içerik; kaydırma ofseti + kırpma
            glm::mat4 childBase = parentMatrix;
            glm::vec4 childClip = clip;
            UIScrollRectComponent* scroll = parent.HasComponent<UIScrollRectComponent>() ? &parent.GetComponent<UIScrollRectComponent>() : nullptr;
            if (scroll && scroll->Mask)
                childClip = IntersectClip(clip, TransformedAABB(parentMatrix, parentSize));
            // Rect Mask 2D / Mask: çocuklar bu dikdörtgene kırpılır
            if (parent.HasComponent<UIRectMask2DComponent>())
            {
                const glm::vec4 pad = parent.GetComponent<UIRectMask2DComponent>().Padding;   // sol, sağ, üst, alt
                glm::mat4 inner = glm::translate(parentMatrix, glm::vec3(pad.x, pad.w, 0.0f));
                glm::vec2 innerSize = glm::max(parentSize - glm::vec2(pad.x + pad.y, pad.z + pad.w), glm::vec2(0.0f));
                childClip = IntersectClip(childClip, TransformedAABB(inner, innerSize));
            }
            if (parent.HasComponent<UIMaskComponent>())
                childClip = IntersectClip(childClip, TransformedAABB(parentMatrix, parentSize));

            for (size_t i = 0; i < children.size(); ++i)
            {
                Entity child = children[i];
                auto& rt = child.GetComponent<RectTransformComponent>();
                glm::vec2 size, pivotPos;
                if (forced[i].z >= 0.0f)
                {
                    size = glm::vec2(forced[i].z, forced[i].w);
                    pivotPos = glm::vec2(forced[i].x, forced[i].y) + size * rt.Pivot;
                }
                else
                {
                    // Content Size Fitter: metnin tercih edilen boyutu SizeDelta'yı belirler (çapalar ayrık olmalı)
                    if (child.HasComponent<UIContentSizeFitterComponent>() && child.HasComponent<UITextComponent>())
                    {
                        const auto& fit = child.GetComponent<UIContentSizeFitterComponent>();
                        const auto& txt = child.GetComponent<UITextComponent>();
                        float maxW = fit.HorizontalFit == UIFitMode::Unconstrained ? rt.SizeDelta.x : 1.0e6f;
                        glm::vec2 pref = PreferredTextSize(txt, maxW);
                        if (fit.HorizontalFit != UIFitMode::Unconstrained) rt.SizeDelta.x = std::ceil(pref.x);
                        if (fit.VerticalFit != UIFitMode::Unconstrained) rt.SizeDelta.y = std::ceil(pref.y);
                    }
                    glm::vec2 aMin = parentSize * rt.AnchorMin, aMax = parentSize * rt.AnchorMax;
                    size = glm::max((aMax - aMin) + rt.SizeDelta, glm::vec2(0.0f));
                    pivotPos = aMin + (aMax - aMin) * rt.Pivot + rt.AnchoredPosition;
                    // Aspect Ratio Fitter
                    if (child.HasComponent<UIAspectRatioFitterComponent>())
                    {
                        const auto& ar = child.GetComponent<UIAspectRatioFitterComponent>();
                        const float ratio = std::max(ar.AspectRatio, 1e-4f);
                        switch (ar.Mode)
                        {
                            case UIAspectMode::WidthControlsHeight: size.y = size.x / ratio; break;
                            case UIAspectMode::HeightControlsWidth: size.x = size.y * ratio; break;
                            case UIAspectMode::FitInParent:
                            case UIAspectMode::EnvelopeParent:
                            {
                                bool fitW = parentSize.x / std::max(parentSize.y, 1e-4f) < ratio;
                                if (ar.Mode == UIAspectMode::EnvelopeParent) fitW = !fitW;
                                size = fitW ? glm::vec2(parentSize.x, parentSize.x / ratio) : glm::vec2(parentSize.y * ratio, parentSize.y);
                                pivotPos = parentSize * 0.5f + (size * (rt.Pivot - 0.5f));
                                break;
                            }
                            case UIAspectMode::None: break;
                        }
                    }
                }
                glm::mat4 base = childBase;
                if (scroll && i == 0)
                {
                    scroll->ContentSize = size;
                    glm::vec2 maxOffset = glm::max(size - parentSize, glm::vec2(0.0f));
                    scroll->ContentOffset = glm::clamp(scroll->ContentOffset, glm::vec2(-maxOffset.x, 0.0f), glm::vec2(0.0f, maxOffset.y));
                    if (!scroll->Horizontal) scroll->ContentOffset.x = 0.0f;
                    if (!scroll->Vertical) scroll->ContentOffset.y = 0.0f;
                    scroll->NormalizedPosition = glm::vec2(maxOffset.x > 0.0f ? -scroll->ContentOffset.x / maxOffset.x : 0.0f,
                                                           maxOffset.y > 0.0f ? 1.0f - scroll->ContentOffset.y / maxOffset.y : 1.0f);
                    base = glm::translate(base, glm::vec3(scroll->ContentOffset, 0.0f));
                }
                glm::mat4 m = base * LocalMatrix(pivotPos, rt, size);
                rt.CanvasMatrix = m;
                rt.ComputedRect = glm::vec4(pivotPos - size * rt.Pivot, size);
                // Canvas Group: saydamlık ve etkileşim alt ağaca geçer
                float alpha = groupAlpha;
                bool interactable = groupInteractable, blocks = groupBlocks;
                if (child.HasComponent<UICanvasGroupComponent>())
                {
                    const auto& cg = child.GetComponent<UICanvasGroupComponent>();
                    if (cg.IgnoreParentGroups) { alpha = 1.0f; interactable = true; blocks = true; }
                    alpha *= std::clamp(cg.Alpha, 0.0f, 1.0f);
                    interactable = interactable && cg.Interactable;
                    blocks = blocks && cg.BlocksRaycasts;
                }
                Item item{ child, m, size, childClip, depth };
                item.Alpha = alpha;
                item.Interactable = interactable;
                item.Blocks = blocks;
                items.push_back(item);
                LayoutRecursive(scene, child, m, size, childClip, depth + 1, items, alpha, interactable, blocks);
            }
        }

        std::vector<Entity> SortedCanvases(Scene& scene)
        {
            std::vector<Entity> canvases;
            auto view = scene.m_Registry.view<CanvasComponent>();
            for (auto e : view)
                if (scene.IsEntityActive(e))
                    canvases.push_back(Entity{ e, &scene });
            std::stable_sort(canvases.begin(), canvases.end(), [](Entity a, Entity b) {
                return a.GetComponent<CanvasComponent>().SortOrder < b.GetComponent<CanvasComponent>().SortOrder;
            });
            return canvases;
        }

        void BuildItems(Scene& scene, Entity canvas, const glm::vec2& canvasSize, std::vector<Item>& items)
        {
            items.clear();
            float alpha = 1.0f;
            bool interactable = true, blocks = true;
            if (canvas.HasComponent<UICanvasGroupComponent>())
            {
                const auto& cg = canvas.GetComponent<UICanvasGroupComponent>();
                alpha = std::clamp(cg.Alpha, 0.0f, 1.0f);
                interactable = cg.Interactable;
                blocks = cg.BlocksRaycasts;
            }
            LayoutRecursive(scene, canvas, glm::mat4(1.0f), canvasSize, glm::vec4(0, 0, -1, -1), 0, items, alpha, interactable, blocks);
        }

        // ------------------------------------------------------------ Çizim yardımcıları

        // Çizilen öğenin Canvas Group saydamlığı (tüm UI çizimleri bununla çarpılır)
        float s_DrawAlpha = 1.0f;

        void DrawCorners(const glm::vec3* p, const glm::vec2* uv, const std::shared_ptr<Texture2D>& tex, const glm::vec4& color, float tiling = 1.0f)
        {
            glm::vec4 c = color;
            c.a *= s_DrawAlpha;
            Renderer2D::DrawQuadCorners(p, uv, tex, c, tiling);
        }

        void Quad(const glm::mat4& m, glm::vec2 mn, glm::vec2 mx, const std::shared_ptr<Texture2D>& tex, glm::vec2 uv0, glm::vec2 uv1, const glm::vec4& color, float tiling = 1.0f)
        {
            glm::vec3 p[4] = { glm::vec3(m * glm::vec4(mn.x, mn.y, 0, 1)), glm::vec3(m * glm::vec4(mx.x, mn.y, 0, 1)),
                               glm::vec3(m * glm::vec4(mx.x, mx.y, 0, 1)), glm::vec3(m * glm::vec4(mn.x, mx.y, 0, 1)) };
            glm::vec2 uv[4] = { { uv0.x, uv0.y }, { uv1.x, uv0.y }, { uv1.x, uv1.y }, { uv0.x, uv1.y } };
            DrawCorners(p, uv, tex ? tex : Renderer2D::GetWhiteTexture(), color, tiling);
        }

        // Metin dörtgeni: bit eşlem yazı tipinde (Custom Font) SDF değil, renk geçişi varsa köşe başına renk
        bool s_BitmapFont = false;
        const glm::vec4* s_TextGradient = nullptr;

        void TextQuad(const glm::mat4& m, glm::vec2 mn, glm::vec2 mx, const std::shared_ptr<Texture2D>& tex, glm::vec2 uv0, glm::vec2 uv1, const glm::vec4& color,
                      float skew, float baseline, const glm::vec4* gradient)
        {
            auto P = [&](float x, float y) { return glm::vec3(m * glm::vec4(x + skew * (y - baseline), y, 0, 1)); };
            glm::vec3 p[4] = { P(mn.x, mn.y), P(mx.x, mn.y), P(mx.x, mx.y), P(mn.x, mx.y) };
            glm::vec2 uv[4] = { { uv0.x, uv0.y }, { uv1.x, uv0.y }, { uv1.x, uv1.y }, { uv0.x, uv1.y } };
            const float mode = s_BitmapFont ? 1.0f : -1.0f;
            const std::shared_ptr<Texture2D>& t = tex ? tex : Renderer2D::GetWhiteTexture();
            if (gradient)
            {
                glm::vec4 c[4];
                for (int i = 0; i < 4; ++i)
                    c[i] = glm::vec4(glm::vec3(gradient[i]), gradient[i].a * color.a * s_DrawAlpha);
                Renderer2D::DrawQuadCornersColors(p, uv, t, c, mode);
                return;
            }
            DrawCorners(p, uv, t, color, mode);
        }

        void Triangle(const glm::mat4& m, glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& color)
        {
            glm::vec3 p[4] = { glm::vec3(m * glm::vec4(a, 0, 1)), glm::vec3(m * glm::vec4(b, 0, 1)), glm::vec3(m * glm::vec4(c, 0, 1)), glm::vec3(m * glm::vec4(c, 0, 1)) };
            glm::vec2 uv[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 1, 1 } };
            DrawCorners(p, uv, Renderer2D::GetWhiteTexture(), color);
        }

        // 9-dilim (border: sol, alt, sağ, üst; piksel cinsinden hem hedef hem kaynak)
        void Sliced(const glm::mat4& m, glm::vec2 size, const std::shared_ptr<Texture2D>& tex, glm::vec2 uv0, glm::vec2 uv1, glm::vec4 srcBorderUV,
                    glm::vec4 border, const glm::vec4& color)
        {
            float sx = (border.x + border.z) > size.x ? size.x / (border.x + border.z) : 1.0f;
            float sy = (border.y + border.w) > size.y ? size.y / (border.y + border.w) : 1.0f;
            float s = std::min(sx, sy);
            border *= s;
            const float xs[4] = { 0.0f, border.x, size.x - border.z, size.x };
            const float ys[4] = { 0.0f, border.y, size.y - border.w, size.y };
            const float us[4] = { uv0.x, uv0.x + srcBorderUV.x, uv1.x - srcBorderUV.z, uv1.x };
            const float vs[4] = { uv0.y, uv0.y + srcBorderUV.y, uv1.y - srcBorderUV.w, uv1.y };
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                    if (xs[x + 1] > xs[x] && ys[y + 1] > ys[y])
                        Quad(m, { xs[x], ys[y] }, { xs[x + 1], ys[y + 1] }, tex, { us[x], vs[y] }, { us[x + 1], vs[y + 1] }, color);
        }

        void RoundRect(const glm::mat4& m, glm::vec2 mn, glm::vec2 mx, float radius, const glm::vec4& color)
        {
            EnsureSprites();
            if (radius <= 0.5f)
            {
                Quad(m, mn, mx, nullptr, { 0, 0 }, { 1, 1 }, color);
                return;
            }
            glm::mat4 mm = glm::translate(m, glm::vec3(mn, 0.0f));
            float uvb = kRoundRadius / 64.0f;
            Sliced(mm, mx - mn, s_RoundSprite, { 0, 0 }, { 1, 1 }, glm::vec4(uvb), glm::vec4(radius), color);
        }

        void Circle(const glm::mat4& m, glm::vec2 center, float r, const glm::vec4& color)
        {
            EnsureSprites();
            Quad(m, center - glm::vec2(r), center + glm::vec2(r), s_CircleSprite, { 0, 0 }, { 1, 1 }, color);
        }

        // ------------------------------------------------------------ Metin

        struct TextLine { size_t Begin, End; float Width; };

        void LayoutLines(FontAtlas& font, const std::vector<uint32_t>& cps, float fontSize, float maxWidth, bool wrap, std::vector<TextLine>& lines)
        {
            lines.clear();
            size_t lineStart = 0, lastSpace = (size_t)-1;
            float scale = fontSize / FontAtlas::BaseSize;
            float width = 0.0f;
            for (size_t i = 0; i < cps.size(); ++i)
            {
                uint32_t c = cps[i];
                if (c == '\n')
                {
                    lines.push_back({ lineStart, i, width });
                    lineStart = i + 1;
                    width = 0.0f;
                    lastSpace = (size_t)-1;
                    continue;
                }
                float adv = font.GetGlyph(c).Advance * scale;
                if (wrap && maxWidth > 0.0f && width + adv > maxWidth && i > lineStart)
                {
                    if (lastSpace != (size_t)-1 && lastSpace > lineStart)
                    {
                        lines.push_back({ lineStart, lastSpace, font.MeasureWidth(cps, lineStart, lastSpace, fontSize) });
                        lineStart = lastSpace + 1;
                    }
                    else
                    {
                        lines.push_back({ lineStart, i, width });
                        lineStart = i;
                    }
                    width = font.MeasureWidth(cps, lineStart, i + 1, fontSize);
                    lastSpace = (size_t)-1;
                    if (c == ' ') lastSpace = i;
                    continue;
                }
                if (c == ' ')
                    lastSpace = i;
                width += adv;
            }
            lines.push_back({ lineStart, cps.size(), width });
        }

        void DrawTextBlock(const glm::mat4& m, glm::vec2 size, const std::string& text, const std::string& fontPath, float fontSize, const glm::vec4& color,
                           UITextAlignH ah, UITextAlignV av, bool wrap, float lineSpacing, glm::vec2 offset = glm::vec2(0.0f))
        {
            auto font = FontAtlas::Get(fontPath);
            if (!font || !font->IsValid() || text.empty() || color.a <= 0.0f)
                return;
            static thread_local std::vector<uint32_t> cps;
            static thread_local std::vector<TextLine> lines;
            FontAtlas::DecodeUTF8(text, cps);
            s_BitmapFont = font->IsBitmap();
            LayoutLines(*font, cps, fontSize, size.x, wrap, lines);
            const float scale = fontSize / FontAtlas::BaseSize;
            const float lineH = font->GetLineHeight() * scale * lineSpacing;
            const float ascent = font->GetAscender() * scale;
            const float totalH = lineH * (float)lines.size();
            float top;
            switch (av)
            {
                case UITextAlignV::Top: top = size.y; break;
                case UITextAlignV::Bottom: top = totalH; break;
                default: top = size.y * 0.5f + totalH * 0.5f; break;
            }
            auto tex = font->GetTexture();
            for (size_t li = 0; li < lines.size(); ++li)
            {
                const auto& line = lines[li];
                float x = ah == UITextAlignH::Left ? 0.0f : ah == UITextAlignH::Center ? (size.x - line.Width) * 0.5f : size.x - line.Width;
                float baseline = top - ascent - lineH * (float)li - (lineH - font->GetLineHeight() * scale) * 0.5f;
                for (size_t i = line.Begin; i < line.End; ++i)
                {
                    const SDFGlyph& g = font->GetGlyph(cps[i]);
                    if (g.Size.x > 0.0f)
                    {
                        glm::vec2 mn(x + g.Bearing.x * scale, baseline + (g.Bearing.y - g.Size.y) * scale);
                        TextQuad(m, mn + offset, mn + g.Size * scale + offset, tex, g.UVMin, g.UVMax, color, 0.0f, 0.0f, s_TextGradient);
                    }
                    x += g.Advance * scale;
                    if (i + 1 < line.End)
                        x += font->GetKerning(cps[i], cps[i + 1]) * scale;
                }
            }
        }

        // ------------------------------------------------------------ Zengin metin (BBCode)

        // İtalik için eğik dörtgen: üst kenar taban çizgisine göre kaydırılır
        void GlyphQuad(const glm::mat4& m, glm::vec2 mn, glm::vec2 mx, const std::shared_ptr<Texture2D>& tex, glm::vec2 uv0, glm::vec2 uv1,
                       const glm::vec4& color, float skew, float baseline, const glm::vec4* gradient)
        {
            TextQuad(m, mn, mx, tex, uv0, uv1, color, skew, baseline, gradient);
        }

        glm::vec3 HueToRGB(float h)
        {
            const glm::vec3 k = glm::abs(glm::fract(glm::vec3(h) + glm::vec3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - glm::vec3(3.0f));
            return glm::mix(glm::vec3(1.0f), glm::clamp(k - glm::vec3(1.0f), 0.0f, 1.0f), 0.85f);
        }

        float RichTime()
        {
            static const auto s_Start = std::chrono::steady_clock::now();
            return std::chrono::duration<float>(std::chrono::steady_clock::now() - s_Start).count();
        }

        // fontSize: bu çizimdeki boyut (Best Fit sonrası); sizeRatio: fontSize / bileşen boyutu ([size=N] buna göre ölçeklenir).
        // overrideColor: gölge / dış çizgi geçişleri her harfi tek renkle çizer.
        void DrawRichTextBlock(const glm::mat4& m, glm::vec2 size, const std::string& text, const std::string& fontPath, float fontSize, float sizeRatio,
                               const glm::vec4& baseColor, bool overrideColor, UITextAlignH ah, UITextAlignV av, bool wrap, float lineSpacing,
                               glm::vec2 offset = glm::vec2(0.0f))
        {
            auto font = FontAtlas::Get(fontPath);
            if (!font || !font->IsValid() || text.empty() || baseColor.a <= 0.0f)
                return;
            static thread_local std::vector<uint32_t> cps;
            static thread_local std::vector<RichText::Style> styles;
            static thread_local std::vector<float> px;
            RichText::Parse(text, cps, styles);
            if (cps.empty())
                return;
            s_BitmapFont = font->IsBitmap();
            px.resize(cps.size());
            for (size_t i = 0; i < cps.size(); ++i)
            {
                const float v = styles[i].Size;
                px[i] = v > 0.0f ? v * sizeRatio : v < 0.0f ? fontSize * -v : fontSize;
            }
            // Tuş simgesi: satır yüksekliğinde resim (genişlik dokunun en-boy oranından)
            auto glyphTexture = [&](size_t i) -> std::shared_ptr<Texture2D> {
                if (!RichText::IsGlyph(cps[i]))
                    return nullptr;
                const std::string spec = RichText::GlyphSpec(cps[i]);
                if (spec.rfind("sprite:", 0) == 0)
                    return RichText::SpriteTexture(spec.substr(7));
                return InputGlyphs::Texture(spec);
            };
            auto glyphScale = [&](size_t i) {
                float scale = 1.0f;
                if (RichText::IsGlyph(cps[i]))
                {
                    const std::string spec = RichText::GlyphSpec(cps[i]);
                    if (spec.rfind("sprite:", 0) == 0)
                        RichText::SpriteTexture(spec.substr(7), &scale);
                }
                return scale;
            };
            auto glyphWidth = [&](size_t i, const std::shared_ptr<Texture2D>& t) {
                const float h = px[i] * 1.25f * glyphScale(i);
                return t && t->GetHeight() > 0 ? h * (float)t->GetWidth() / (float)t->GetHeight() : h;
            };
            auto advance = [&](size_t i) {
                if (RichText::IsGlyph(cps[i]))
                    return glyphWidth(i, glyphTexture(i)) + px[i] * 0.12f;
                return font->GetGlyph(cps[i]).Advance * px[i] / FontAtlas::BaseSize;
            };
            auto measure = [&](size_t b, size_t e) {
                float w = 0.0f;
                for (size_t k = b; k < e; ++k)
                {
                    w += advance(k);
                    if (k + 1 < e)
                        w += font->GetKerning(cps[k], cps[k + 1]) * px[k] / FontAtlas::BaseSize;
                }
                return w;
            };
            auto lineSize = [&](size_t b, size_t e) {
                float v = fontSize;
                for (size_t k = b; k < e; ++k)
                    v = std::max(v, px[k]);
                return v;
            };
            struct RichLine { size_t Begin, End; float Width, Size; };
            static thread_local std::vector<RichLine> lines;
            lines.clear();
            size_t lineStart = 0, lastSpace = (size_t)-1;
            float width = 0.0f;
            for (size_t i = 0; i < cps.size(); ++i)
            {
                const uint32_t c = cps[i];
                if (c == '\n')
                {
                    lines.push_back({ lineStart, i, width, lineSize(lineStart, i) });
                    lineStart = i + 1;
                    width = 0.0f;
                    lastSpace = (size_t)-1;
                    continue;
                }
                const float a = advance(i);
                if (wrap && size.x > 0.0f && width + a > size.x && i > lineStart)
                {
                    if (lastSpace != (size_t)-1 && lastSpace > lineStart)
                    {
                        lines.push_back({ lineStart, lastSpace, measure(lineStart, lastSpace), lineSize(lineStart, lastSpace) });
                        lineStart = lastSpace + 1;
                    }
                    else
                    {
                        lines.push_back({ lineStart, i, width, lineSize(lineStart, i) });
                        lineStart = i;
                    }
                    width = measure(lineStart, i + 1);
                    lastSpace = c == ' ' ? i : (size_t)-1;
                    continue;
                }
                if (c == ' ')
                    lastSpace = i;
                width += a;
            }
            lines.push_back({ lineStart, cps.size(), width, lineSize(lineStart, cps.size()) });

            const float lineFactor = font->GetLineHeight() / FontAtlas::BaseSize * lineSpacing;
            float totalH = 0.0f;
            for (const auto& l : lines)
                totalH += l.Size * lineFactor;
            float y = av == UITextAlignV::Top ? size.y : av == UITextAlignV::Bottom ? totalH : size.y * 0.5f + totalH * 0.5f;
            auto tex = font->GetTexture();
            const float time = RichTime();
            for (const auto& line : lines)
            {
                const float lineH = line.Size * lineFactor;
                const float ascent = font->GetAscender() * line.Size / FontAtlas::BaseSize;
                const float baseline = y - ascent - (lineH - font->GetLineHeight() * line.Size / FontAtlas::BaseSize) * 0.5f;
                y -= lineH;
                float x = ah == UITextAlignH::Left ? 0.0f : ah == UITextAlignH::Center ? (size.x - line.Width) * 0.5f : size.x - line.Width;
                for (size_t i = line.Begin; i < line.End; ++i)
                {
                    if (RichText::IsGlyph(cps[i]))
                    {
                        auto gt = glyphTexture(i);
                        const float h = px[i] * 1.25f * glyphScale(i), w = glyphWidth(i, gt);
                        // Yazının x yüksekliğine ortalanır
                        const float centerY = baseline + px[i] * 0.33f;
                        glm::vec4 tint(1.0f, 1.0f, 1.0f, baseColor.a);
                        if (overrideColor)
                            tint = baseColor;
                        if (gt)
                            Quad(m, glm::vec2(x + px[i] * 0.06f, centerY - h * 0.5f) + offset, glm::vec2(x + px[i] * 0.06f + w, centerY + h * 0.5f) + offset,
                                 gt, { 0, 0 }, { 1, 1 }, tint);
                        x += advance(i);
                        continue;
                    }
                    const RichText::Style& st = styles[i];
                    const float scale = px[i] / FontAtlas::BaseSize;
                    const SDFGlyph& g = font->GetGlyph(cps[i]);
                    glm::vec4 color = baseColor;
                    if (!overrideColor)
                    {
                        if (st.HasColor)
                            color = glm::vec4(glm::vec3(st.Color), st.Color.a * baseColor.a);
                        if (st.Flags & RichText::Rainbow)
                            color = glm::vec4(HueToRGB(time * st.RainbowFrequency + (float)i * 0.06f), color.a);
                    }
                    glm::vec2 jitter(0.0f);
                    if (st.Flags & RichText::Wave)
                        jitter.y += std::sin(time * st.WaveFrequency + (float)i * 0.55f) * st.WaveAmplitude * sizeRatio;
                    if (st.Flags & RichText::Shake)
                    {
                        const uint32_t tick = (uint32_t)(time * std::max(st.ShakeRate, 1.0f));
                        uint32_t h = (tick * 73856093u) ^ ((uint32_t)i * 19349663u);
                        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                        jitter += glm::vec2(((h & 1023u) / 511.5f - 1.0f), (((h >> 10) & 1023u) / 511.5f - 1.0f)) * st.ShakeLevel * sizeRatio;
                    }
                    const float adv = g.Advance * scale;
                    if (g.Size.x > 0.0f)
                    {
                        glm::vec2 mn(x + g.Bearing.x * scale, baseline + (g.Bearing.y - g.Size.y) * scale);
                        mn += offset + jitter;
                        const glm::vec2 mx = mn + g.Size * scale;
                        const float skew = (st.Flags & RichText::Italic) ? 0.2f : 0.0f;
                        const float base = baseline + offset.y + jitter.y;
                        const glm::vec4* gradient = overrideColor ? nullptr : (st.Gradient ? st.Gradient : s_TextGradient);
                        GlyphQuad(m, mn, mx, tex, g.UVMin, g.UVMax, color, skew, base, gradient);
                        // Kalın: yatayda hafif kaydırılmış ikinci çizim (SDF kenarı kalınlaşır)
                        if (st.Flags & RichText::Bold)
                        {
                            const glm::vec2 d(std::max(0.75f, px[i] * 0.035f), 0.0f);
                            GlyphQuad(m, mn + d, mx + d, tex, g.UVMin, g.UVMax, color, skew, base, gradient);
                        }
                    }
                    if (st.Flags & (RichText::Underline | RichText::Strike))
                    {
                        const float thick = std::max(1.0f, px[i] * 0.06f);
                        const glm::vec2 o = offset + glm::vec2(0.0f, jitter.y);
                        if (st.Flags & RichText::Underline)
                        {
                            const float uy = baseline - px[i] * 0.1f;
                            Quad(m, glm::vec2(x, uy - thick) + o, glm::vec2(x + adv, uy) + o, nullptr, { 0, 0 }, { 1, 1 }, color);
                        }
                        if (st.Flags & RichText::Strike)
                        {
                            const float sy = baseline + px[i] * 0.28f;
                            Quad(m, glm::vec2(x, sy - thick * 0.5f) + o, glm::vec2(x + adv, sy + thick * 0.5f) + o, nullptr, { 0, 0 }, { 1, 1 }, color);
                        }
                    }
                    x += adv;
                    if (i + 1 < line.End)
                        x += font->GetKerning(cps[i], cps[i + 1]) * scale;
                }
            }
        }

        glm::vec2 PreferredTextSize(const UITextComponent& t, float maxWidth)
        {
            auto font = FontAtlas::Get(t.FontPath);
            std::vector<uint32_t> cps;
            std::vector<TextLine> lines;
            FontAtlas::DecodeUTF8(t.RichText ? RichText::Strip(t.Text) : t.Text, cps);
            LayoutLines(*font, cps, t.FontSize, maxWidth, t.WordWrap && maxWidth < 1.0e5f, lines);
            float w = 0.0f;
            for (auto& l : lines) w = std::max(w, l.Width);
            float h = font->GetLineHeight() * (t.FontSize / FontAtlas::BaseSize) * t.LineSpacing * (float)std::max<size_t>(1, lines.size());
            return glm::vec2(w, h);
        }

        float BestFitSize(const UITextComponent& t, glm::vec2 size)
        {
            if (!t.BestFit)
                return t.FontSize;
            auto font = FontAtlas::Get(t.FontPath);
            std::vector<uint32_t> cps;
            std::vector<TextLine> lines;
            FontAtlas::DecodeUTF8(t.RichText ? RichText::Strip(t.Text) : t.Text, cps);
            float lo = t.MinSize, hi = t.MaxSize;
            for (int it = 0; it < 12; ++it)
            {
                float mid = (lo + hi) * 0.5f;
                LayoutLines(*font, cps, mid, size.x, t.WordWrap, lines);
                float h = font->GetLineHeight() * (mid / FontAtlas::BaseSize) * t.LineSpacing * (float)lines.size();
                float w = 0.0f;
                for (auto& l : lines) w = std::max(w, l.Width);
                if (h <= size.y && w <= size.x + 0.5f) lo = mid; else hi = mid;
            }
            return lo;
        }

        // ------------------------------------------------------------ Seçilebilir durum

        UISelectableState* GetSelectable(Entity e)
        {
            if (e.HasComponent<UIButtonComponent>()) return &e.GetComponent<UIButtonComponent>().Selectable;
            if (e.HasComponent<UIToggleComponent>()) return &e.GetComponent<UIToggleComponent>().Selectable;
            if (e.HasComponent<UISliderComponent>()) return &e.GetComponent<UISliderComponent>().Selectable;
            if (e.HasComponent<UIInputFieldComponent>()) return &e.GetComponent<UIInputFieldComponent>().Selectable;
            if (e.HasComponent<UIDropdownComponent>()) return &e.GetComponent<UIDropdownComponent>().Selectable;
            if (e.HasComponent<UIScrollbarComponent>()) return &e.GetComponent<UIScrollbarComponent>().Selectable;
            if (e.HasComponent<UISelectableComponent>()) return &e.GetComponent<UISelectableComponent>().Selectable;
            return nullptr;
        }

        // Etkileşim: kendi ayarı ve üstteki Canvas Group'ların ayarı
        bool CanInteract(const UISelectableState& s) { return s.Interactable && s.GroupInteractable; }

        // Scrollbar geometrisi: "değer 0" ucundan ölçülen konumlar
        struct ScrollbarGeom { bool Vertical = true; bool Reversed = false; float Track = 0.0f; float Handle = 0.0f; };
        ScrollbarGeom GetScrollbarGeom(const UIScrollbarComponent& sb, const glm::vec2& size)
        {
            ScrollbarGeom g;
            g.Vertical = sb.Direction == UIScrollbarDirection::BottomToTop || sb.Direction == UIScrollbarDirection::TopToBottom;
            g.Reversed = sb.Direction == UIScrollbarDirection::RightToLeft || sb.Direction == UIScrollbarDirection::TopToBottom;
            g.Track = g.Vertical ? size.y : size.x;
            g.Handle = std::min(g.Track, std::max(g.Track * std::clamp(sb.Size, 0.0f, 1.0f), std::min(size.x, size.y)));
            return g;
        }
        float ScrollbarPos(const ScrollbarGeom& g, const glm::vec2& local)
        {
            float p = g.Vertical ? local.y : local.x;
            return g.Reversed ? g.Track - p : p;
        }

        void PushEvent(Entity e, const std::string& configured, const char* type, float value = 0.0f);

        void SetScrollbarValue(Entity e, UIScrollbarComponent& sb, float v)
        {
            v = std::clamp(v, 0.0f, 1.0f);
            if (sb.NumberOfSteps > 1)
                v = std::round(v * (float)(sb.NumberOfSteps - 1)) / (float)(sb.NumberOfSteps - 1);
            if (std::fabs(v - sb.Value) > 1e-6f)
            {
                sb.Value = v;
                sb.ChangedThisFrame = true;
                PushEvent(e, sb.OnValueChanged, "ValueChanged", v);
            }
        }

        // Event Trigger: aynı varlıktaki betik yöntemini çağır (değer: kaydırma miktarı vb.)
        void Trigger(Scene& scene, Entity e, std::string UIEventTriggerComponent::* method, float value = 0.0f)
        {
            if (!e || !e.HasComponent<UIEventTriggerComponent>())
                return;
            const std::string name = e.GetComponent<UIEventTriggerComponent>().*method;
            if (name.empty())
                return;
            CQS::Value arg = CQS::Value::MakeFloat((double)value);
            scene.InvokeScriptMethod((uint32_t)(entt::entity)e, name.c_str(), &arg, 1);
        }

        bool IsRaycastTarget(Entity e)
        {
            if (GetSelectable(e)) return true;
            if (e.HasComponent<UIScrollRectComponent>()) return true;
            if (e.HasComponent<UIImageComponent>() && e.GetComponent<UIImageComponent>().RaycastTarget) return true;
            if (e.HasComponent<UITextComponent>() && e.GetComponent<UITextComponent>().RaycastTarget) return true;
            if (e.HasComponent<UIRawImageComponent>() && e.GetComponent<UIRawImageComponent>().RaycastTarget) return true;
            if (e.HasComponent<UIEventTriggerComponent>()) return true;
            return false;
        }

        std::string EventName(Entity e, const std::string& configured)
        {
            if (!configured.empty())
                return configured;
            return e.HasComponent<TagComponent>() ? e.GetComponent<TagComponent>().Tag : std::string("UI");
        }

        void PushEvent(Entity e, const std::string& configured, const char* type, float value)
        {
            s_PendingEvents.push_back({ EventName(e, configured), type, e, value });
        }

        // ------------------------------------------------------------ Öğe çizimi

        void DrawItem(const Item& it)
        {
            Entity e = it.E;
            const glm::mat4& m = it.M;
            const glm::vec2 size = it.Size;
            UISelectableState* sel = GetSelectable(e);
            glm::vec4 tint = sel ? sel->CurrentTint : glm::vec4(1.0f);
            glm::mat4 sm = m;
            if (sel && sel->Transition == UITransition::Scale && sel->CurrentScale != 1.0f)
                sm = m * glm::translate(glm::mat4(1.0f), glm::vec3(size * 0.5f, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(sel->CurrentScale, sel->CurrentScale, 1)) *
                     glm::translate(glm::mat4(1.0f), glm::vec3(-size * 0.5f, 0));

            // Mask: "Show Mask Graphic" kapalıysa maskenin kendi görseli çizilmez (çocuklar kırpılarak çizilir)
            const bool hideGraphic = e.HasComponent<UIMaskComponent>() && !e.GetComponent<UIMaskComponent>().ShowMaskGraphic;

            // Ham görsel (Raw Image)
            if (e.HasComponent<UIRawImageComponent>() && !hideGraphic)
            {
                auto& raw = e.GetComponent<UIRawImageComponent>();
                if (raw.LoadedPath != raw.TexturePath)
                {
                    raw.LoadedPath = raw.TexturePath;
                    raw.RuntimeTexture = raw.TexturePath.empty() ? nullptr : Texture2D::Create(raw.TexturePath);
                }
                auto tex = raw.ExternalTexture ? raw.ExternalTexture : raw.RuntimeTexture;
                glm::vec2 uv0(raw.UVRect.x, raw.UVRect.y), uv1(raw.UVRect.x + raw.UVRect.z, raw.UVRect.y + raw.UVRect.w);
                Quad(sm, { 0, 0 }, size, tex, uv0, uv1, raw.Color);
            }

            // Kaydırma çubuğu (Scrollbar): iz + tutamaç
            if (e.HasComponent<UIScrollbarComponent>())
            {
                auto& sb = e.GetComponent<UIScrollbarComponent>();
                glm::vec4 handleColor = sb.HandleColor * (sel && sel->Transition == UITransition::ColorTint ? tint : glm::vec4(1.0f));
                const bool vertical = sb.Direction == UIScrollbarDirection::BottomToTop || sb.Direction == UIScrollbarDirection::TopToBottom;
                const float radius = std::min(size.x, size.y) * 0.5f;
                RoundRect(sm, { 0, 0 }, size, radius, sb.BackgroundColor);
                const float track = vertical ? size.y : size.x;
                const float handle = std::max(track * std::clamp(sb.Size, 0.0f, 1.0f), std::min(size.x, size.y));
                float v = std::clamp(sb.Value, 0.0f, 1.0f);
                if (sb.Direction == UIScrollbarDirection::RightToLeft || sb.Direction == UIScrollbarDirection::TopToBottom)
                    v = 1.0f - v;
                const float start = (track - handle) * v;
                const float inset = 2.0f;
                if (vertical)
                    RoundRect(sm, { inset, start + inset }, { size.x - inset, start + handle - inset }, std::max(radius - inset, 0.0f), handleColor);
                else
                    RoundRect(sm, { start + inset, inset }, { start + handle - inset, size.y - inset }, std::max(radius - inset, 0.0f), handleColor);
            }

            // Görsel (Shadow / Outline efektleri aynı görseli efekt rengiyle kaydırarak önce çizer)
            struct GraphicEffect { glm::vec4 Color; bool UseGraphicAlpha; };
            auto drawImage = [&](const glm::mat4& gm, const GraphicEffect* effect) {
                auto& img = e.GetComponent<UIImageComponent>();
                if (img.LoadedPath != img.TexturePath)
                {
                    img.LoadedPath = img.TexturePath;
                    img.RuntimeTexture = img.TexturePath.empty() ? nullptr : Texture2D::Create(img.TexturePath);
                }
                glm::vec4 color = img.Color * (sel && sel->Transition == UITransition::ColorTint ? tint : glm::vec4(1.0f));
                if (effect)
                    color = glm::vec4(glm::vec3(effect->Color), effect->Color.a * (effect->UseGraphicAlpha ? color.a : 1.0f));
                auto tex = img.RuntimeTexture;
                const bool untextured = !tex || tex->GetWidth() == 0;
                if (untextured && img.Type == UIImageType::Filled && img.FillMethod != UIFillMethod::Radial360)
                {
                    // Düz renk dolum (can / ilerleme çubuğu): köşe yarıçapı korunur
                    const float fa = std::clamp(img.FillAmount, 0.0f, 1.0f);
                    const glm::vec2 fillMax = img.FillMethod == UIFillMethod::Horizontal ? glm::vec2(size.x * fa, size.y) : glm::vec2(size.x, size.y * fa);
                    if (fa > 0.0f)
                        RoundRect(gm, { 0, 0 }, fillMax, std::min(img.CornerRadius, std::min(fillMax.x, fillMax.y) * 0.5f), color);
                }
                else if (untextured && img.Type != UIImageType::Filled)
                {
                    RoundRect(gm, { 0, 0 }, size, std::min(img.CornerRadius, std::min(size.x, size.y) * 0.5f), color);
                }
                else
                {
                    if (untextured)
                        tex = Renderer2D::GetWhiteTexture();   // düz renk radyal dolum
                    glm::vec2 ts((float)tex->GetWidth(), (float)tex->GetHeight());
                    glm::vec4 rect(0, 0, ts.x, ts.y);
                    glm::vec4 border = img.Border;
                    if (!img.SpriteName.empty())
                        if (auto sheet = SpriteSheet::Get(img.TexturePath))
                            if (const SpriteDef* d = sheet->Find(img.SpriteName))
                            {
                                rect = d->Rect;
                                if (border == glm::vec4(0.0f)) border = d->Border;
                            }
                    glm::vec2 uv0(rect.x / ts.x, rect.y / ts.y), uv1((rect.x + rect.z) / ts.x, (rect.y + rect.w) / ts.y);
                    glm::vec2 mn(0.0f), mx = size;
                    if (img.PreserveAspect && img.Type == UIImageType::Simple)
                    {
                        float a = rect.z / std::max(rect.w, 1.0f);
                        glm::vec2 fit = size.x / size.y > a ? glm::vec2(size.y * a, size.y) : glm::vec2(size.x, size.x / a);
                        mn = (size - fit) * 0.5f;
                        mx = mn + fit;
                    }
                    switch (img.Type)
                    {
                        case UIImageType::Sliced:
                        {
                            glm::vec4 srcUV(border.x / ts.x, border.y / ts.y, border.z / ts.x, border.w / ts.y);
                            Sliced(gm, size, tex, uv0, uv1, srcUV, border * img.PixelsPerUnitMultiplier, color);
                            break;
                        }
                        case UIImageType::Tiled:
                        {
                            glm::vec2 tile(rect.z, rect.w);
                            int nx = std::min(128, (int)std::ceil(size.x / tile.x)), ny = std::min(128, (int)std::ceil(size.y / tile.y));
                            for (int y = 0; y < ny; ++y)
                                for (int x = 0; x < nx; ++x)
                                {
                                    glm::vec2 a(x * tile.x, y * tile.y), b = glm::min(a + tile, size);
                                    glm::vec2 f = (b - a) / tile;
                                    Quad(gm, a, b, tex, uv0, uv0 + (uv1 - uv0) * f, color);
                                }
                            break;
                        }
                        case UIImageType::Filled:
                        {
                            float fa = std::clamp(img.FillAmount, 0.0f, 1.0f);
                            if (img.FillMethod == UIFillMethod::Horizontal)
                                Quad(gm, mn, { mn.x + (mx.x - mn.x) * fa, mx.y }, tex, uv0, { uv0.x + (uv1.x - uv0.x) * fa, uv1.y }, color);
                            else if (img.FillMethod == UIFillMethod::Vertical)
                                Quad(gm, mn, { mx.x, mn.y + (mx.y - mn.y) * fa }, tex, uv0, { uv1.x, uv0.y + (uv1.y - uv0.y) * fa }, color);
                            else
                            {
                                // Radyal: merkezden üçgen yelpazesi (12 yönünden)
                                glm::vec2 c = (mn + mx) * 0.5f, half = (mx - mn) * 0.5f;
                                int seg = std::max(1, (int)std::ceil(64 * fa));
                                for (int i = 0; i < seg; ++i)
                                {
                                    float a0 = (float)i / seg * fa * 6.2831853f, a1 = (float)(i + 1) / seg * fa * 6.2831853f;
                                    float dir = img.FillClockwise ? -1.0f : 1.0f;
                                    auto pt = [&](float a) {
                                        glm::vec2 d(std::sin(a) * -dir, std::cos(a));
                                        float k = 1.0f / std::max(std::fabs(d.x), std::fabs(d.y)); // kareye yansıt
                                        return c + d * k * half;
                                    };
                                    glm::vec2 p0 = pt(a0), p1 = pt(a1);
                                    auto uvOf = [&](glm::vec2 p) { return uv0 + (uv1 - uv0) * ((p - mn) / (mx - mn)); };
                                    glm::vec3 P[4] = { glm::vec3(gm * glm::vec4(c, 0, 1)), glm::vec3(gm * glm::vec4(p0, 0, 1)), glm::vec3(gm * glm::vec4(p1, 0, 1)), glm::vec3(gm * glm::vec4(p1, 0, 1)) };
                                    glm::vec2 UV[4] = { uvOf(c), uvOf(p0), uvOf(p1), uvOf(p1) };
                                    DrawCorners(P, UV, tex, color);
                                }
                            }
                            break;
                        }
                        default:
                            Quad(gm, mn, mx, tex, uv0, uv1, color);
                            break;
                    }
                }
            };
            auto drawEffects = [&](const std::function<void(const glm::mat4&, const GraphicEffect*)>& draw) {
                if (e.HasComponent<UIShadowComponent>())
                {
                    const auto& fx = e.GetComponent<UIShadowComponent>();
                    GraphicEffect ge{ fx.EffectColor, fx.UseGraphicAlpha };
                    draw(glm::translate(sm, glm::vec3(fx.EffectDistance, 0.0f)), &ge);
                }
                if (e.HasComponent<UIOutlineComponent>())
                {
                    const auto& fx = e.GetComponent<UIOutlineComponent>();
                    GraphicEffect ge{ fx.EffectColor, fx.UseGraphicAlpha };
                    const glm::vec2 d = fx.EffectDistance;
                    for (glm::vec2 o : { glm::vec2(d.x, d.y), glm::vec2(-d.x, d.y), glm::vec2(d.x, -d.y), glm::vec2(-d.x, -d.y) })
                        draw(glm::translate(sm, glm::vec3(o, 0.0f)), &ge);
                }
            };
            if (e.HasComponent<UIImageComponent>() && !hideGraphic)
            {
                drawEffects(drawImage);
                drawImage(sm, nullptr);
            }

            // Toggle: sol kutu + onay işareti
            if (e.HasComponent<UIToggleComponent>())
            {
                auto& tg = e.GetComponent<UIToggleComponent>();
                float b = std::min(size.y, 40.0f);
                glm::vec2 mn(0.0f, (size.y - b) * 0.5f);
                RoundRect(sm, mn, mn + glm::vec2(b), 6.0f, glm::vec4(0.75f, 0.76f, 0.80f, 1.0f) * tint);
                RoundRect(sm, mn + glm::vec2(2.0f), mn + glm::vec2(b - 2.0f), 5.0f, glm::vec4(1.0f) * tint);
                if (tg.IsOn)
                {
                    glm::vec4 c(0.18f, 0.55f, 0.95f, 1.0f);
                    RoundRect(sm, mn + glm::vec2(4.0f), mn + glm::vec2(b - 4.0f), 4.0f, c);
                    // ✓ iki kalın çizgi
                    auto thick = [&](glm::vec2 a, glm::vec2 bb, float w) {
                        glm::vec2 d = glm::normalize(bb - a), n(-d.y, d.x);
                        glm::vec3 P[4] = { glm::vec3(sm * glm::vec4(a + n * w, 0, 1)), glm::vec3(sm * glm::vec4(bb + n * w, 0, 1)),
                                           glm::vec3(sm * glm::vec4(bb - n * w, 0, 1)), glm::vec3(sm * glm::vec4(a - n * w, 0, 1)) };
                        glm::vec2 UV[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
                        DrawCorners(P, UV, Renderer2D::GetWhiteTexture(), glm::vec4(1.0f));
                    };
                    float w = b * 0.07f;
                    thick(mn + glm::vec2(b * 0.24f, b * 0.52f), mn + glm::vec2(b * 0.43f, b * 0.30f), w);
                    thick(mn + glm::vec2(b * 0.43f, b * 0.30f), mn + glm::vec2(b * 0.78f, b * 0.72f), w);
                }
            }

            // Slider: arka plan + dolgu + tutamaç
            if (e.HasComponent<UISliderComponent>())
            {
                auto& sl = e.GetComponent<UISliderComponent>();
                float t = sl.MaxValue > sl.MinValue ? (sl.Value - sl.MinValue) / (sl.MaxValue - sl.MinValue) : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                bool horizontal = sl.Direction == UISliderDirection::LeftToRight || sl.Direction == UISliderDirection::RightToLeft;
                bool reversed = sl.Direction == UISliderDirection::RightToLeft || sl.Direction == UISliderDirection::TopToBottom;
                float hs = sl.HandleSize;
                if (horizontal)
                {
                    float barH = std::max(4.0f, size.y * 0.28f), y0 = (size.y - barH) * 0.5f;
                    RoundRect(sm, { hs * 0.5f, y0 }, { size.x - hs * 0.5f, y0 + barH }, barH * 0.5f, sl.BackgroundColor * tint);
                    float hx = hs * 0.5f + (size.x - hs) * (reversed ? 1.0f - t : t);
                    if (!reversed) RoundRect(sm, { hs * 0.5f, y0 }, { hx, y0 + barH }, barH * 0.5f, sl.FillColor * tint);
                    else RoundRect(sm, { hx, y0 }, { size.x - hs * 0.5f, y0 + barH }, barH * 0.5f, sl.FillColor * tint);
                    Circle(sm, { hx, size.y * 0.5f }, hs * 0.5f + 1.5f, glm::vec4(0, 0, 0, 0.25f));
                    Circle(sm, { hx, size.y * 0.5f }, hs * 0.5f, sl.HandleColor * tint);
                }
                else
                {
                    float barW = std::max(4.0f, size.x * 0.28f), x0 = (size.x - barW) * 0.5f;
                    RoundRect(sm, { x0, hs * 0.5f }, { x0 + barW, size.y - hs * 0.5f }, barW * 0.5f, sl.BackgroundColor * tint);
                    float hy = hs * 0.5f + (size.y - hs) * (reversed ? 1.0f - t : t);
                    RoundRect(sm, { x0, hs * 0.5f }, { x0 + barW, hy }, barW * 0.5f, sl.FillColor * tint);
                    Circle(sm, { size.x * 0.5f, hy }, hs * 0.5f, sl.HandleColor * tint);
                }
            }

            // Input field
            if (e.HasComponent<UIInputFieldComponent>())
            {
                auto& in = e.GetComponent<UIInputFieldComponent>();
                glm::vec4 border = in.Focused ? glm::vec4(0.26f, 0.59f, 0.98f, 1.0f) : glm::vec4(0.72f, 0.73f, 0.77f, 1.0f);
                RoundRect(sm, { 0, 0 }, size, 8.0f, border * tint);
                RoundRect(sm, { in.Focused ? 2.0f : 1.0f, in.Focused ? 2.0f : 1.0f }, size - glm::vec2(in.Focused ? 2.0f : 1.0f), 7.0f, in.BackgroundColor * tint);
                const float padX = 12.0f;
                glm::mat4 tm = glm::translate(sm, glm::vec3(padX, 0, 0));
                glm::vec2 inner(size.x - padX * 2.0f, size.y);
                std::string shown = in.Text;
                if (in.ContentType == UIContentType::Password)
                {
                    std::vector<uint32_t> cps;
                    FontAtlas::DecodeUTF8(in.Text, cps);
                    shown = std::string(cps.size(), '*');
                }
                if (shown.empty() && !in.Focused)
                    DrawTextBlock(tm, inner, in.Placeholder, "", in.FontSize, in.PlaceholderColor, UITextAlignH::Left, UITextAlignV::Middle, false, 1.0f);
                else
                    DrawTextBlock(tm, inner, shown, "", in.FontSize, in.TextColor, UITextAlignH::Left, UITextAlignV::Middle, false, 1.0f);
                if (in.Focused && std::fmod(in.CaretBlink, 1.0f) < 0.55f)
                {
                    auto font = FontAtlas::Get("");
                    std::vector<uint32_t> cps;
                    FontAtlas::DecodeUTF8(shown, cps);
                    float cx = font->MeasureWidth(cps, 0, (size_t)std::clamp(in.Caret, 0, (int)cps.size()), in.FontSize);
                    float h = in.FontSize * 1.1f;
                    Quad(tm, { cx, (size.y - h) * 0.5f }, { cx + 2.0f, (size.y + h) * 0.5f }, nullptr, { 0, 0 }, { 1, 1 }, in.TextColor);
                }
            }

            // Dropdown (kapalı hali)
            if (e.HasComponent<UIDropdownComponent>())
            {
                auto& dd = e.GetComponent<UIDropdownComponent>();
                RoundRect(sm, { 0, 0 }, size, 8.0f, glm::vec4(0.72f, 0.73f, 0.77f, 1.0f) * tint);
                RoundRect(sm, { 1, 1 }, size - glm::vec2(1.0f), 7.0f, glm::vec4(1.0f) * tint);
                std::string label = dd.Value >= 0 && dd.Value < (int)dd.Options.size() ? dd.Options[(size_t)dd.Value] : std::string();
                DrawTextBlock(glm::translate(sm, glm::vec3(12.0f, 0, 0)), { size.x - 44.0f, size.y }, label, "", dd.FontSize,
                              glm::vec4(0.12f, 0.12f, 0.14f, 1.0f), UITextAlignH::Left, UITextAlignV::Middle, false, 1.0f);
                float ax = size.x - 22.0f, ay = size.y * 0.5f, a = 6.0f;
                if (dd.Expanded) Triangle(sm, { ax - a, ay - a * 0.5f }, { ax + a, ay - a * 0.5f }, { ax, ay + a * 0.6f }, glm::vec4(0.3f, 0.3f, 0.35f, 1.0f));
                else Triangle(sm, { ax - a, ay + a * 0.5f }, { ax, ay - a * 0.6f }, { ax + a, ay + a * 0.5f }, glm::vec4(0.3f, 0.3f, 0.35f, 1.0f));
            }

            // Metin
            if (e.HasComponent<UITextComponent>())
            {
                auto& t = e.GetComponent<UITextComponent>();
                float fs = BestFitSize(t, size);
                // Zengin metin: aynı çağrı imzası, harf başına stil (gölge / dış çizgi tek renk)
                const float sizeRatio = t.FontSize > 0.0f ? fs / t.FontSize : 1.0f;
                auto drawText = [&](const glm::mat4& tm, const glm::vec4& col, bool overrideColor, glm::vec2 off) {
                    s_TextGradient = !overrideColor && !t.ColorGradient.empty() ? RichText::FindGradient(t.ColorGradient) : nullptr;
                    struct Reset { ~Reset() { s_TextGradient = nullptr; } } reset;
                    if (t.RichText)
                        DrawRichTextBlock(tm, size, t.Text, t.FontPath, fs, sizeRatio, col, overrideColor, t.AlignH, t.AlignV, t.WordWrap, t.LineSpacing, off);
                    else
                        DrawTextBlock(tm, size, t.Text, t.FontPath, fs, col, t.AlignH, t.AlignV, t.WordWrap, t.LineSpacing, off);
                };
                if (t.Shadow)
                    drawText(sm, t.ShadowColor, true, t.ShadowOffset);
                if (t.Outline)
                {
                    const float o = std::max(1.0f, fs * 0.05f);
                    static const glm::vec2 dirs[8] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 0.7f, 0.7f }, { -0.7f, 0.7f }, { 0.7f, -0.7f }, { -0.7f, -0.7f } };
                    for (auto d : dirs)
                        drawText(sm, t.OutlineColor, true, d * o);
                }
                glm::vec4 c = t.Color;
                Entity parent = e.GetParent();
                if (parent)
                    if (UISelectableState* ps = GetSelectable(parent); ps && !ps->Interactable)
                        c.a *= 0.5f;
                // Shadow / Outline bileşenleri metinde de çalışır (Unity BaseMeshEffect)
                drawEffects([&](const glm::mat4& gm, const GraphicEffect* fx) {
                    glm::vec4 ec(glm::vec3(fx->Color), fx->Color.a * (fx->UseGraphicAlpha ? c.a : 1.0f));
                    drawText(gm, ec, true, glm::vec2(0.0f));
                });
                drawText(sm, c, false, glm::vec2(0.0f));
            }

            // ScrollRect kaydırma çubuğu
            if (e.HasComponent<UIScrollRectComponent>())
            {
                auto& sr = e.GetComponent<UIScrollRectComponent>();
                if (sr.ShowScrollbar && sr.Vertical && sr.ContentSize.y > size.y + 0.5f)
                {
                    float frac = size.y / sr.ContentSize.y;
                    float barH = std::max(24.0f, size.y * frac);
                    float t = 1.0f - sr.NormalizedPosition.y;
                    float y1 = size.y - (size.y - barH) * t;
                    RoundRect(sm, { size.x - 8.0f, y1 - barH }, { size.x - 3.0f, y1 }, 2.5f, glm::vec4(0, 0, 0, 0.35f));
                }
            }
        }

        // Açık dropdown listesi (en üstte çizilir) ve satır dikdörtgenleri
        void DropdownListGeometry(const Item& it, const UIDropdownComponent& dd, float& rowH, glm::vec2& listMin, glm::vec2& listMax)
        {
            rowH = std::max(it.Size.y, dd.FontSize * 1.6f);
            float h = rowH * (float)dd.Options.size();
            listMin = glm::vec2(0.0f, -h - 4.0f);
            listMax = glm::vec2(it.Size.x, -4.0f);
        }

        void DrawDropdownList(const Item& it)
        {
            auto& dd = it.E.GetComponent<UIDropdownComponent>();
            float rowH;
            glm::vec2 mn, mx;
            DropdownListGeometry(it, dd, rowH, mn, mx);
            RoundRect(it.M, mn - glm::vec2(1.0f, 3.0f), mx + glm::vec2(1.0f, 1.0f), 8.0f, glm::vec4(0, 0, 0, 0.18f));
            RoundRect(it.M, mn, mx, 8.0f, glm::vec4(1.0f));
            for (int i = 0; i < (int)dd.Options.size(); ++i)
            {
                glm::vec2 a(mn.x + 3.0f, mx.y - rowH * (float)(i + 1)), b(mx.x - 3.0f, mx.y - rowH * (float)i);
                if (i == dd.HoveredOption) RoundRect(it.M, a, b, 6.0f, glm::vec4(0.88f, 0.92f, 1.0f, 1.0f));
                if (i == dd.Value) RoundRect(it.M, { a.x, a.y + 6.0f }, { a.x + 3.0f, b.y - 6.0f }, 1.5f, glm::vec4(0.26f, 0.59f, 0.98f, 1.0f));
                DrawTextBlock(glm::translate(it.M, glm::vec3(a.x + 10.0f, a.y, 0)), { b.x - a.x - 14.0f, rowH }, dd.Options[(size_t)i], "", dd.FontSize,
                              glm::vec4(0.12f, 0.12f, 0.14f, 1.0f), UITextAlignH::Left, UITextAlignV::Middle, false, 1.0f);
            }
        }

        // Dünya/editör uzayında maske: kırpma dikdörtgeni şablon (stencil) tamponuna yazılır,
        // içerik yalnızca o değere eşit piksellere çizilir (perspektifte de doğru).
        void ApplyStencilClip(const glm::vec4& clip, int& stencilRef)
        {
            Renderer2D::Flush();
            if (clip.z < 0.0f)
            {
                glDisable(GL_STENCIL_TEST);
                return;
            }
            stencilRef = stencilRef % 255 + 1;
            if (stencilRef == 1)
            {
                glStencilMask(0xFF);
                glClear(GL_STENCIL_BUFFER_BIT);
            }
            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xFF);
            glStencilFunc(GL_ALWAYS, stencilRef, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            GLboolean depthMask;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_FALSE);
            glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(clip.x + clip.z * 0.5f, clip.y + clip.w * 0.5f, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(clip.z, clip.w, 1.0f));
            Renderer2D::DrawQuad(m, glm::vec4(1.0f));
            Renderer2D::Flush();
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(depthMask);
            glStencilFunc(GL_EQUAL, stencilRef, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glStencilMask(0x00);
        }

        void RenderItems(const std::vector<Item>& items, const glm::mat4& canvasToScreenPx, bool useScissor, const glm::vec2& screenSize)
        {
            glm::vec4 currentClip(0, 0, -1, -1);
            const bool useStencil = !useScissor;
            int stencilRef = 0;
            for (const auto& it : items)
            {
                if (useStencil && it.Clip != currentClip)
                {
                    currentClip = it.Clip;
                    ApplyStencilClip(it.Clip, stencilRef);
                }
                if (useScissor && it.Clip != currentClip)
                {
                    Renderer2D::Flush();
                    currentClip = it.Clip;
                    if (it.Clip.z >= 0.0f)
                    {
                        glm::vec2 a = glm::vec2(canvasToScreenPx * glm::vec4(it.Clip.x, it.Clip.y, 0, 1));
                        glm::vec2 b = glm::vec2(canvasToScreenPx * glm::vec4(it.Clip.x + it.Clip.z, it.Clip.y + it.Clip.w, 0, 1));
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(s_ScreenOrigin.x + (GLint)std::floor(std::min(a.x, b.x)), s_ScreenOrigin.y + (GLint)std::floor(std::min(a.y, b.y)),
                                  (GLsizei)std::ceil(std::fabs(b.x - a.x)), (GLsizei)std::ceil(std::fabs(b.y - a.y)));
                    }
                    else
                        glDisable(GL_SCISSOR_TEST);
                }
                if (it.Alpha <= 0.001f)
                    continue;
                s_DrawAlpha = it.Alpha;
                DrawItem(it);
                s_DrawAlpha = 1.0f;
            }
            if (useScissor)
            {
                Renderer2D::Flush();
                glDisable(GL_SCISSOR_TEST);
            }
            if (useStencil && stencilRef > 0)
            {
                Renderer2D::Flush();
                glStencilMask(0xFF);
                glDisable(GL_STENCIL_TEST);
            }
            (void)screenSize;
            for (const auto& it : items)
                {
                Entity e = it.E;
                if (e.HasComponent<UIDropdownComponent>() && e.GetComponent<UIDropdownComponent>().Expanded)
                    DrawDropdownList(it);
            }
        }

        bool PointInItem(const Item& it, const glm::vec2& canvasPoint, glm::vec2& local)
        {
            if (it.Clip.z >= 0.0f && (canvasPoint.x < it.Clip.x || canvasPoint.y < it.Clip.y || canvasPoint.x > it.Clip.x + it.Clip.z || canvasPoint.y > it.Clip.y + it.Clip.w))
                return false;
            glm::vec4 l = glm::inverse(it.M) * glm::vec4(canvasPoint, 0, 1);
            local = glm::vec2(l);
            return local.x >= 0.0f && local.y >= 0.0f && local.x <= it.Size.x && local.y <= it.Size.y;
        }

        void ApplySliderValue(UISliderComponent& sl, const Item& it, glm::vec2 local)
        {
            bool horizontal = sl.Direction == UISliderDirection::LeftToRight || sl.Direction == UISliderDirection::RightToLeft;
            bool reversed = sl.Direction == UISliderDirection::RightToLeft || sl.Direction == UISliderDirection::TopToBottom;
            float len = (horizontal ? it.Size.x : it.Size.y) - sl.HandleSize;
            float p = ((horizontal ? local.x : local.y) - sl.HandleSize * 0.5f) / std::max(len, 1.0f);
            p = std::clamp(reversed ? 1.0f - p : p, 0.0f, 1.0f);
            float v = sl.MinValue + (sl.MaxValue - sl.MinValue) * p;
            if (sl.WholeNumbers) v = std::round(v);
            if (v != sl.Value)
            {
                sl.Value = v;
                sl.ChangedThisFrame = true;
                PushEvent(it.E, sl.OnValueChanged, "ValueChanged", v);
            }
        }

        bool AcceptsChar(const UIInputFieldComponent& in, uint32_t c, const std::string& current)
        {
            if (c < 32 || c == 127) return false;
            switch (in.ContentType)
            {
                case UIContentType::IntegerNumber: return (c >= '0' && c <= '9') || (c == '-' && current.empty());
                case UIContentType::DecimalNumber: return (c >= '0' && c <= '9') || (c == '-' && current.empty()) || (c == '.' && current.find('.') == std::string::npos);
                case UIContentType::Alphanumeric: return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                default: return true;
            }
        }

        std::string EncodeUTF8(const std::vector<uint32_t>& cps)
        {
            std::string s;
            for (uint32_t c : cps)
            {
                if (c < 0x80) s += (char)c;
                else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
                else if (c < 0x10000) { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
                else { s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 0x3F)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
            }
            return s;
        }
    }

    // ================================================================== Genel API

    float UISystem::GetCanvasScaleFactor(Entity canvas, const glm::vec2& screenSize)
    {
        return ComputeScaleFactor(canvas, screenSize);
    }

    glm::vec2 UISystem::GetCanvasSize(Entity canvas, const glm::vec2& screenSize)
    {
        return CanvasSize(canvas, screenSize);
    }

    Entity UISystem::CreateCanvas(Scene& scene, const std::string& name)
    {
        Entity c = scene.CreateEntity(name);
        c.AddComponent<CanvasComponent>();
        auto& sc = c.AddComponent<CanvasScalerComponent>();
        sc.ScaleMode = CanvasScalerComponent::UIScaleMode::ScaleWithScreenSize;
        c.AddComponent<GraphicRaycasterComponent>();
        if (c.HasComponent<LayerComponent>()) c.GetComponent<LayerComponent>().Layer = 5;
        return c;
    }

    Entity UISystem::CreateElement(Scene& scene, const std::string& type, Entity parent)
    {
        if (!parent)
        {
            auto view = scene.m_Registry.view<CanvasComponent>();
            parent = view.empty() ? CreateCanvas(scene) : Entity{ view.front(), &scene };
        }
        auto make = [&](const std::string& name, Entity p, glm::vec2 size) {
            Entity e = scene.CreateEntity(name);
            auto& rt = e.AddComponent<RectTransformComponent>();
            rt.SizeDelta = size;
            if (e.HasComponent<LayerComponent>()) e.GetComponent<LayerComponent>().Layer = 5;
            e.SetParent(p);
            return e;
        };
        auto stretch = [](Entity e, glm::vec4 inset = glm::vec4(0.0f)) {
            auto& rt = e.GetComponent<RectTransformComponent>();
            rt.AnchorMin = glm::vec2(0.0f);
            rt.AnchorMax = glm::vec2(1.0f);
            rt.SizeDelta = glm::vec2(-(inset.x + inset.y), -(inset.z + inset.w));
            rt.AnchoredPosition = glm::vec2((inset.x - inset.y) * 0.5f, (inset.w - inset.z) * 0.5f);
        };
        auto label = [&](Entity p, const std::string& text, UITextAlignH align, float size, glm::vec4 inset) {
            Entity t = make("Text", p, { 0, 0 });
            stretch(t, inset);
            auto& tc = t.AddComponent<UITextComponent>();
            tc.Text = text;
            tc.AlignH = align;
            tc.FontSize = size;
            return t;
        };

        Entity e;
        if (type == "Panel")
        {
            e = make("Panel", parent, { 0, 0 });
            stretch(e, glm::vec4(40.0f));
            auto& img = e.AddComponent<UIImageComponent>();
            img.Color = glm::vec4(1.0f, 1.0f, 1.0f, 0.55f);
            img.CornerRadius = 14.0f;
        }
        else if (type == "Image")
        {
            e = make("Image", parent, { 200, 200 });
            e.AddComponent<UIImageComponent>();
        }
        else if (type == "Text")
        {
            e = make("Text", parent, { 320, 60 });
            e.AddComponent<UITextComponent>();
        }
        else if (type == "Button")
        {
            e = make("Button", parent, { 240, 64 });
            auto& img = e.AddComponent<UIImageComponent>();
            img.CornerRadius = 10.0f;
            e.AddComponent<UIButtonComponent>();
            auto t = label(e, "Button", UITextAlignH::Center, 28.0f, glm::vec4(0.0f));
            (void)t;
        }
        else if (type == "Toggle")
        {
            e = make("Toggle", parent, { 260, 44 });
            e.AddComponent<UIToggleComponent>();
            label(e, "Toggle", UITextAlignH::Left, 26.0f, glm::vec4(54.0f, 0, 0, 0));
        }
        else if (type == "Slider")
        {
            e = make("Slider", parent, { 300, 40 });
            e.AddComponent<UISliderComponent>();
        }
        else if (type == "InputField")
        {
            e = make("Input Field", parent, { 340, 56 });
            e.AddComponent<UIInputFieldComponent>();
        }
        else if (type == "Dropdown")
        {
            e = make("Dropdown", parent, { 280, 52 });
            e.AddComponent<UIDropdownComponent>();
        }
        else if (type == "ScrollView")
        {
            e = make("Scroll View", parent, { 420, 320 });
            auto& img = e.AddComponent<UIImageComponent>();
            img.Color = glm::vec4(0.95f, 0.95f, 0.97f, 1.0f);
            img.CornerRadius = 10.0f;
            e.AddComponent<UIScrollRectComponent>();
            Entity content = make("Content", e, { 0, 600 });
            auto& rt = content.GetComponent<RectTransformComponent>();
            rt.AnchorMin = { 0.0f, 1.0f };
            rt.AnchorMax = { 1.0f, 1.0f };
            rt.Pivot = { 0.5f, 1.0f };
            auto& lg = content.AddComponent<UILayoutGroupComponent>();
            lg.Type = UILayoutType::Vertical;
            lg.FitContent = true;
            lg.ControlChildHeight = false;
            for (int i = 1; i <= 12; ++i)
            {
                Entity item = make("Item " + std::to_string(i), content, { 0, 48 });
                auto& tc = item.AddComponent<UITextComponent>();
                tc.Text = "List item " + std::to_string(i);
                tc.FontSize = 26.0f;
            }
        }
        else if (type == "RawImage")
        {
            e = make("Raw Image", parent, { 200, 200 });
            e.AddComponent<UIRawImageComponent>();
        }
        else if (type == "Scrollbar")
        {
            e = make("Scrollbar", parent, { 24, 300 });
            e.AddComponent<UIScrollbarComponent>();
        }
        else if (type == "LayoutGroup")
        {
            e = make("Layout Group", parent, { 320, 400 });
            auto& img = e.AddComponent<UIImageComponent>();
            img.Color = glm::vec4(1.0f, 1.0f, 1.0f, 0.3f);
            img.CornerRadius = 10.0f;
            e.AddComponent<UILayoutGroupComponent>();
        }
        return e;
    }

    glm::mat4 UISystem::GetCanvasWorldMatrix(Entity canvas, const glm::vec2& screenSize)
    {
        glm::vec2 size = CanvasSize(canvas, screenSize);
        const glm::mat4& w = canvas.GetComponent<TransformComponent>().WorldTransform;
        return w * glm::scale(glm::mat4(1.0f), glm::vec3(kWorldUnitsPerPixel)) * glm::translate(glm::mat4(1.0f), glm::vec3(-size * 0.5f, 0.0f));
    }

    const std::vector<UIEvent>& UISystem::GetEvents() { return s_Events; }

    Entity UISystem::GetSelected(Scene& scene) { return s_Selected.Get(scene); }

    void UISystem::SetSelected(Scene& scene, Entity element)
    {
        Entity old = s_Selected.Get(scene);
        if (old == element)
            return;
        Trigger(scene, old, &UIEventTriggerComponent::Deselect);
        s_Selected.Set(element);
        Trigger(scene, element, &UIEventTriggerComponent::Select);
        s_NavActive = (bool)element;
    }
    bool UISystem::IsPointerOverUI() { return s_PointerOverUI; }

    bool UISystem::WasClicked(const std::string& name)
    {
        for (const auto& e : s_Events)
            if (e.Type == "Click" && e.Name == name)
                return true;
        return false;
    }


    // ------------------------------------------------------------ Ekran klavyesi (Steam Deck / kumanda / dokunmatik)
    namespace
    {
        enum class OskAction { Char, Shift, Symbols, Space, Backspace, Done };
        struct OskKey
        {
            std::string Label;
            uint32_t Codepoint = 0;
            OskAction Action = OskAction::Char;
            float Width = 1.0f;
            glm::vec2 Min{ 0.0f }, Max{ 0.0f };   // ekran pikseli (sol-alt köken)
        };

        struct OnScreenKeyboard
        {
            int Mode = 0;                         // 0 otomatik, 1 hep, 2 hiç
            bool Open = false;
            bool Shift = false;
            bool Symbols = false;
            int Row = 1, Col = 0;
            glm::ivec2 Held{ 0 };
            float Repeat = 0.0f;
            uint32_t PrevButtons = 0;
            glm::vec2 PanelMin{ 0.0f }, PanelMax{ 0.0f };
            std::vector<std::vector<OskKey>> Rows;
            std::string Preview;                  // odaklı alanın metni (alan klavyenin altında kalabilir)
            int PreviewCaret = 0;
        };
        OnScreenKeyboard s_Osk;

        bool OskWanted(bool fromGamepad)
        {
            if (s_Osk.Mode == 2)
                return false;
            if (s_Osk.Mode == 1)
                return true;
#ifdef __ANDROID__
            return true;
#else
            return fromGamepad || Platform::IsSteamDeck() || InputGlyphs::UsingGamepad();
#endif
        }

        void OskBuildRows();

        // Açılış: harf düzeni, seçim ilk harf satırının başında
        void OskOpen()
        {
            if (s_Osk.Open)
                return;
            s_Osk.Open = true;
            s_Osk.Shift = false;
            s_Osk.Symbols = false;
            s_Osk.Row = 1;
            s_Osk.Col = 0;
            s_Osk.Held = glm::ivec2(0);
            OskBuildRows();
        }

        void OskBuildRows()
        {
            static const char* kLetters[] = { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.-" };
            static const char* kSymbols[] = { "1234567890", "!@#$%&*()_", "+=/\\:;\"?<>", "[]{}~^|`,." };
            s_Osk.Rows.clear();
            for (const char* row : s_Osk.Symbols ? kSymbols : kLetters)
            {
                std::vector<OskKey> keys;
                for (const char* c = row; *c; ++c)
                {
                    uint32_t cp = (uint32_t)(unsigned char)*c;
                    if (s_Osk.Shift && cp >= 'a' && cp <= 'z')
                        cp -= 32;
                    keys.push_back({ std::string(1, (char)cp), cp, OskAction::Char, 1.0f });
                }
                s_Osk.Rows.push_back(std::move(keys));
            }
            s_Osk.Rows.push_back({ { "Shift", 0, OskAction::Shift, 1.6f },
                                   { s_Osk.Symbols ? "ABC" : "?123", 0, OskAction::Symbols, 1.4f },
                                   { "Space", ' ', OskAction::Space, 3.6f },
                                   { "Del", 0, OskAction::Backspace, 1.6f },
                                   { "Done", 0, OskAction::Done, 1.8f } });
            s_Osk.Row = std::clamp(s_Osk.Row, 0, (int)s_Osk.Rows.size() - 1);
            s_Osk.Col = std::clamp(s_Osk.Col, 0, (int)s_Osk.Rows[(size_t)s_Osk.Row].size() - 1);
        }

        // Ekranın alt bölümü: 5 satır tuş + üstte önizleme şeridi
        void OskLayout(const glm::vec2& screen)
        {
            if (s_Osk.Rows.empty())
                OskBuildRows();
            const float width = std::min(screen.x, screen.y * 2.0f);
            const float x0 = (screen.x - width) * 0.5f;
            const float keyH = std::clamp(screen.y * 0.075f, 28.0f, 90.0f);
            const float gap = keyH * 0.12f;
            const float previewH = keyH * 0.9f;
            const float panelH = keyH * 5.0f + gap * 6.0f + previewH;
            s_Osk.PanelMin = { x0, 0.0f };
            s_Osk.PanelMax = { x0 + width, panelH };
            float y = panelH - previewH - gap - keyH;
            for (auto& row : s_Osk.Rows)
            {
                float units = 0.0f;
                for (auto& k : row)
                    units += k.Width;
                const float unitW = (width - gap * (float)(row.size() + 1)) / std::max(units, 1.0f);
                float x = x0 + gap;
                for (auto& k : row)
                {
                    k.Min = { x, y };
                    k.Max = { x + unitW * k.Width, y + keyH };
                    x += unitW * k.Width + gap;
                }
                y -= keyH + gap;
            }
        }

        // Tuşa basıldı: karakter / silme / bitti
        void OskPress(const OskKey& key, std::vector<uint32_t>& chars, bool& backspace, bool& done)
        {
            switch (key.Action)
            {
                case OskAction::Char:
                    chars.push_back(key.Codepoint);
                    if (s_Osk.Shift && !s_Osk.Symbols)
                    {
                        s_Osk.Shift = false;   // tek harf büyük (Caps Lock değil)
                        OskBuildRows();
                    }
                    break;
                case OskAction::Space: chars.push_back(' '); break;
                case OskAction::Backspace: backspace = true; break;
                case OskAction::Done: done = true; break;
                case OskAction::Shift: s_Osk.Shift = !s_Osk.Shift; OskBuildRows(); break;
                case OskAction::Symbols: s_Osk.Symbols = !s_Osk.Symbols; s_Osk.Shift = false; OskBuildRows(); break;
            }
        }

        // Kumanda / dokunma girdisi. Fare tıklaması klavyenin üstündeyse true (alttaki arayüz bu tıklamayı görmez).
        bool OskHandleInput(const UIInputState& input, float dt, std::vector<uint32_t>& chars, bool& backspace, bool& done, bool& cancel,
                            int& caretMove)
        {
            bool consumedPointer = false;
            if (input.Valid && input.MousePosition.x >= s_Osk.PanelMin.x && input.MousePosition.x <= s_Osk.PanelMax.x &&
                input.MousePosition.y >= s_Osk.PanelMin.y && input.MousePosition.y <= s_Osk.PanelMax.y)
            {
                consumedPointer = true;
                if (input.MousePressed)
                    for (size_t r = 0; r < s_Osk.Rows.size(); ++r)
                        for (size_t c = 0; c < s_Osk.Rows[r].size(); ++c)
                        {
                            const OskKey& k = s_Osk.Rows[r][c];
                            if (input.MousePosition.x >= k.Min.x && input.MousePosition.x <= k.Max.x && input.MousePosition.y >= k.Min.y &&
                                input.MousePosition.y <= k.Max.y)
                            {
                                s_Osk.Row = (int)r;
                                s_Osk.Col = (int)c;
                                const OskKey pressed = k;   // OskPress satırları yeniden kurabilir
                                OskPress(pressed, chars, backspace, done);
                                return true;
                            }
                        }
            }
            if (!Input::IsGamepadConnected(0))
                return consumedPointer;
            uint32_t buttons = 0;
            for (int b = 0; b <= 14; ++b)
                if (Input::IsGamepadButtonPressed(b, 0))
                    buttons |= 1u << b;
            const uint32_t pressedNow = buttons & ~s_Osk.PrevButtons;
            s_Osk.PrevButtons = buttons;
            auto edge = [&](int b) { return (pressedNow & (1u << b)) != 0; };

            glm::ivec2 pad(0);
            const glm::vec2 st = Input::GetGamepadLeftStick(0);
            if ((buttons & (1u << 11)) || st.y < -0.6f) pad.y = -1;
            else if ((buttons & (1u << 13)) || st.y > 0.6f) pad.y = 1;
            else if ((buttons & (1u << 14)) || st.x < -0.6f) pad.x = -1;
            else if ((buttons & (1u << 12)) || st.x > 0.6f) pad.x = 1;
            glm::ivec2 dir(0);
            if (pad != glm::ivec2(0))
            {
                if (pad != s_Osk.Held) { dir = pad; s_Osk.Repeat = 0.4f; }
                else if ((s_Osk.Repeat -= dt) <= 0.0f) { dir = pad; s_Osk.Repeat = 0.1f; }
            }
            s_Osk.Held = pad;
            if (dir.y != 0)
            {
                // Satır değişince en yakın yatay konumdaki tuş
                const auto& from = s_Osk.Rows[(size_t)s_Osk.Row][(size_t)s_Osk.Col];
                const float cx = (from.Min.x + from.Max.x) * 0.5f;
                s_Osk.Row = (s_Osk.Row + dir.y + (int)s_Osk.Rows.size()) % (int)s_Osk.Rows.size();
                const auto& row = s_Osk.Rows[(size_t)s_Osk.Row];
                int best = 0;
                float bestD = 1e30f;
                for (size_t c = 0; c < row.size(); ++c)
                {
                    const float d = std::fabs((row[c].Min.x + row[c].Max.x) * 0.5f - cx);
                    if (d < bestD) { bestD = d; best = (int)c; }
                }
                s_Osk.Col = best;
            }
            if (dir.x != 0)
            {
                const int n = (int)s_Osk.Rows[(size_t)s_Osk.Row].size();
                s_Osk.Col = (s_Osk.Col + dir.x + n) % n;
            }
            if (edge(0))   // A: tuşa bas
            {
                const OskKey pressed = s_Osk.Rows[(size_t)s_Osk.Row][(size_t)s_Osk.Col];
                OskPress(pressed, chars, backspace, done);
            }
            if (edge(1)) backspace = true;                                // B: sil
            if (edge(2)) { s_Osk.Shift = !s_Osk.Shift; OskBuildRows(); }  // X: büyük harf
            if (edge(3)) chars.push_back(' ');                            // Y: boşluk
            if (edge(7)) done = true;                                     // Menu / Start: bitti
            if (edge(6)) cancel = true;                                   // View / Back: kapat
            if (edge(4)) caretMove = -1;                                  // LB / RB: imleç
            if (edge(5)) caretMove = 1;
            return consumedPointer;
        }

        void OskRender()
        {
            if (!s_Osk.Open)
                return;
            RoundRect(glm::mat4(1.0f), s_Osk.PanelMin, s_Osk.PanelMax, 14.0f, { 0.07f, 0.08f, 0.1f, 0.95f });
            const float keyH = s_Osk.Rows.empty() || s_Osk.Rows[0].empty() ? 40.0f : s_Osk.Rows[0][0].Max.y - s_Osk.Rows[0][0].Min.y;
            // Önizleme: alanın metni ve imleç
            const float previewTop = s_Osk.PanelMax.y - keyH * 0.12f;
            const glm::vec2 pMin(s_Osk.PanelMin.x + keyH * 0.2f, previewTop - keyH * 0.8f), pMax(s_Osk.PanelMax.x - keyH * 0.2f, previewTop);
            RoundRect(glm::mat4(1.0f), pMin, pMax, 8.0f, { 0.16f, 0.17f, 0.21f, 1.0f });
            std::string preview = s_Osk.Preview;
            {
                std::vector<uint32_t> cps;
                FontAtlas::DecodeUTF8(preview, cps);
                const int caret = std::clamp(s_Osk.PreviewCaret, 0, (int)cps.size());
                cps.insert(cps.begin() + caret, (uint32_t)'|');
                preview = EncodeUTF8(cps);
            }
            glm::mat4 tm = glm::translate(glm::mat4(1.0f), glm::vec3(pMin.x + keyH * 0.25f, pMin.y, 0.0f));
            DrawTextBlock(tm, glm::vec2(pMax.x - pMin.x - keyH * 0.5f, pMax.y - pMin.y), preview, "", keyH * 0.42f,
                          { 0.95f, 0.96f, 0.98f, 1.0f }, UITextAlignH::Left, UITextAlignV::Middle, false, 1.0f);
            if (InputGlyphs::UsingGamepad())
            {
                // Kumanda kısayolları (Deck / Xbox / PlayStation simgeleriyle)
                const std::string hints = "<glyph=a> Type   <glyph=b> Delete   <glyph=x> Shift   <glyph=y> Space   <glyph=start> Done";
                glm::mat4 hm = glm::translate(glm::mat4(1.0f), glm::vec3(pMin.x, pMax.y + keyH * 0.08f, 0.0f));
                DrawRichTextBlock(hm, glm::vec2(pMax.x - pMin.x, keyH * 0.5f), hints, "", keyH * 0.3f, 1.0f, { 0.8f, 0.82f, 0.88f, 1.0f },
                                  false, UITextAlignH::Right, UITextAlignV::Middle, false, 1.0f);
            }
            for (size_t r = 0; r < s_Osk.Rows.size(); ++r)
                for (size_t c = 0; c < s_Osk.Rows[r].size(); ++c)
                {
                    const OskKey& k = s_Osk.Rows[r][c];
                    const bool selected = (int)r == s_Osk.Row && (int)c == s_Osk.Col && InputGlyphs::UsingGamepad();
                    const bool active = (k.Action == OskAction::Shift && s_Osk.Shift) || (k.Action == OskAction::Symbols && s_Osk.Symbols);
                    glm::vec4 col = k.Action == OskAction::Char ? glm::vec4(0.22f, 0.23f, 0.27f, 1.0f) : glm::vec4(0.16f, 0.17f, 0.2f, 1.0f);
                    if (k.Action == OskAction::Done)
                        col = { 0.2f, 0.42f, 0.86f, 1.0f };
                    if (active)
                        col = { 0.3f, 0.34f, 0.42f, 1.0f };
                    if (selected)
                        col = { 0.38f, 0.58f, 0.98f, 1.0f };
                    RoundRect(glm::mat4(1.0f), k.Min, k.Max, 8.0f, col);
                    glm::mat4 km = glm::translate(glm::mat4(1.0f), glm::vec3(k.Min, 0.0f));
                    const float fs = k.Action == OskAction::Char ? keyH * 0.48f : keyH * 0.34f;
                    DrawTextBlock(km, k.Max - k.Min, k.Label, "", fs, { 0.96f, 0.97f, 0.99f, 1.0f }, UITextAlignH::Center, UITextAlignV::Middle,
                                  false, 1.0f);
                }
        }
    }

    void UISystem::SetOnScreenKeyboardMode(int mode) { s_Osk.Mode = std::clamp(mode, 0, 2); }
    int UISystem::GetOnScreenKeyboardMode() { return s_Osk.Mode; }
    bool UISystem::IsOnScreenKeyboardOpen() { return s_Osk.Open; }

    bool UISystem::GetOnScreenKeyRect(const std::string& label, glm::vec4& rect)
    {
        if (!s_Osk.Open)
            return false;
        for (const auto& row : s_Osk.Rows)
            for (const auto& k : row)
                if (k.Label == label)
                {
                    rect = { k.Min.x, k.Min.y, k.Max.x - k.Min.x, k.Max.y - k.Min.y };
                    return true;
                }
        return false;
    }

    void UISystem::Update(Scene& scene, const glm::vec2& screenSize, const UIInputState& rawInput, float dt)
    {
        UIToolkit::Update(scene);
        // Touch Interface: ekran denetimleri fare / dokunuşla sanal kumandayı sürer
        if (auto& controls = AssetRuntime::ActiveTouchControls(); !controls.empty())
        {
            for (auto& c : controls)
            {
                const glm::vec2 center = c.Center * screenSize;
                const float radius = c.Radius * screenSize.y;
                const glm::vec2 offset = rawInput.MousePosition - center;
                if (rawInput.MousePressed && rawInput.Valid && glm::length(offset) <= radius)
                    c.Held = true;
                if (!rawInput.MouseDown)
                    c.Held = false;
                c.Knob = c.Held ? glm::clamp(offset / std::max(radius, 1.0f), glm::vec2(-1.0f), glm::vec2(1.0f)) : glm::vec2(0.0f);
                if (c.AxisX >= 0)
                {
                    Input::SetVirtualGamepadAxis(c.AxisX, c.Knob.x);
                    Input::SetVirtualGamepadAxis(c.AxisY, -c.Knob.y);
                }
                if (c.Button >= 0)
                    Input::SetVirtualGamepadButton(c.Button, c.Held);
            }
        }
        UIInputState input = rawInput;
        bool oskConsumed = false;
        // Ekran klavyesi açıksa: tuşları karakter / silme / bitti olarak girdiye ekler, üstündeki tıklamayı yutar
        if (s_Osk.Open)
        {
            OskLayout(screenSize);
            std::vector<uint32_t> chars;
            bool backspace = false, done = false, cancel = false;
            int caretMove = 0;
            if (OskHandleInput(input, dt, chars, backspace, done, cancel, caretMove))
            {
                input.MouseDown = input.MousePressed = input.MouseReleased = false;
                input.Valid = false;
                oskConsumed = true;
            }
            input.Characters.insert(input.Characters.end(), chars.begin(), chars.end());
            input.Backspace = input.Backspace || backspace;
            input.Enter = input.Enter || done;
            input.Escape = input.Escape || cancel;
            input.Left = input.Left || caretMove < 0;
            input.Right = input.Right || caretMove > 0;
        }
        s_PendingEvents.clear();
        s_PointerOverUI = oskConsumed;
        // Kare başı bayrakları sıfırla
        for (auto e : scene.m_Registry.view<UIButtonComponent>()) scene.m_Registry.get<UIButtonComponent>(e).ClickedThisFrame = false;
        for (auto e : scene.m_Registry.view<UIToggleComponent>()) scene.m_Registry.get<UIToggleComponent>(e).ChangedThisFrame = false;
        for (auto e : scene.m_Registry.view<UISliderComponent>()) scene.m_Registry.get<UISliderComponent>(e).ChangedThisFrame = false;
        for (auto e : scene.m_Registry.view<UIDropdownComponent>()) scene.m_Registry.get<UIDropdownComponent>(e).ChangedThisFrame = false;
        for (auto e : scene.m_Registry.view<UIInputFieldComponent>()) scene.m_Registry.get<UIInputFieldComponent>(e).SubmittedThisFrame = false;
        for (auto e : scene.m_Registry.view<UIScrollbarComponent>()) scene.m_Registry.get<UIScrollbarComponent>(e).ChangedThisFrame = false;

        // Seçilebilir durum sıfırlama (geçişler aşağıda)
        auto canvases = SortedCanvases(scene);
        std::vector<Item> items;

        // Tüm canvas'ların öğeleri (üstten alta sırayla hit-test için ters sıra)
        struct CanvasItems { Entity Canvas; float Scale; std::vector<Item> Items; };
        std::vector<CanvasItems> all;
        for (Entity canvas : canvases)
        {
            if (canvas.GetComponent<CanvasComponent>().Mode == CanvasComponent::RenderMode::WorldSpace)
                continue;
            CanvasItems ci;
            ci.Canvas = canvas;
            ci.Scale = ComputeScaleFactor(canvas, screenSize);
            BuildItems(scene, canvas, CanvasSize(canvas, screenSize), ci.Items);
            all.push_back(std::move(ci));
        }
        // Canvas Group etkileşimi seçilebilir öğelere yansır
        for (auto& ci : all)
            for (auto& it : ci.Items)
                if (UISelectableState* sel = GetSelectable(it.E))
                    sel->GroupInteractable = it.Interactable;

        Entity hovered;
        Item hoveredItem;
        glm::vec2 hoveredLocal(0.0f);
        int dropdownOption = -1;
        Entity dropdownHit;

        Entity openDropdown = s_OpenDropdown.Get(scene);
        if (input.Valid)
        {
            // Açık dropdown listesi her şeyin üstündedir
            if (openDropdown && openDropdown.HasComponent<UIDropdownComponent>())
            {
                for (auto& ci : all)
                    for (auto& it : ci.Items)
                        if (it.E == openDropdown)
                        {
                            auto& dd = it.E.GetComponent<UIDropdownComponent>();
                            float rowH;
                            glm::vec2 mn, mx;
                            DropdownListGeometry(it, dd, rowH, mn, mx);
                            glm::vec2 p = input.MousePosition / ci.Scale;
                            glm::vec2 local = glm::vec2(glm::inverse(it.M) * glm::vec4(p, 0, 1));
                            if (local.x >= mn.x && local.x <= mx.x && local.y >= mn.y && local.y <= mx.y)
                            {
                                dropdownOption = std::clamp((int)((mx.y - local.y) / rowH), 0, (int)dd.Options.size() - 1);
                                dropdownHit = it.E;
                                s_PointerOverUI = true;
                            }
                        }
            }

            if (!dropdownHit)
            {
                for (auto ci = all.rbegin(); ci != all.rend() && !hovered; ++ci)
                {
                    glm::vec2 p = input.MousePosition / ci->Scale;
                    for (auto it = ci->Items.rbegin(); it != ci->Items.rend(); ++it)
                    {
                        if (!it->Blocks || !IsRaycastTarget(it->E))
                            continue;
                        glm::vec2 local;
                        if (PointInItem(*it, p, local))
                        {
                            hovered = it->E;
                            hoveredItem = *it;
                            hoveredLocal = local;
                            s_PointerOverUI = true;
                            break;
                        }
                    }
                }
            }
        }

        // Seçilebilir öğeyi bul (kendisi ya da üstleri)
        Entity hoveredSelectable;
        for (Entity e = hovered; e; e = e.GetParent())
            if (GetSelectable(e)) { hoveredSelectable = e; break; }

        // Dropdown seçenek vurgusu
        if (openDropdown && openDropdown.HasComponent<UIDropdownComponent>())
            openDropdown.GetComponent<UIDropdownComponent>().HoveredOption = dropdownHit ? dropdownOption : -1;

        // ---- Basma
        if (input.MousePressed)
        {
            if (dropdownHit)
            {
                auto& dd = dropdownHit.GetComponent<UIDropdownComponent>();
                if (dropdownOption != dd.Value)
                {
                    dd.Value = dropdownOption;
                    dd.ChangedThisFrame = true;
                    PushEvent(dropdownHit, dd.OnValueChanged, "ValueChanged", (float)dd.Value);
                }
                dd.Expanded = false;
                s_OpenDropdown.Clear();
                s_Pressed.Clear();
            }
            else
            {
                // Dışarı tıklama: açık dropdown kapanır
                if (openDropdown && openDropdown != hoveredSelectable && openDropdown.HasComponent<UIDropdownComponent>())
                {
                    openDropdown.GetComponent<UIDropdownComponent>().Expanded = false;
                    s_OpenDropdown.Clear();
                }
                s_Pressed.Set(hoveredSelectable);
                // Odak (input field)
                Entity focused = s_Focused.Get(scene);
                if (focused && focused != hoveredSelectable && focused.HasComponent<UIInputFieldComponent>())
                    focused.GetComponent<UIInputFieldComponent>().Focused = false;
                if (hoveredSelectable && hoveredSelectable.HasComponent<UIInputFieldComponent>())
                {
                    auto& in = hoveredSelectable.GetComponent<UIInputFieldComponent>();
                    if (CanInteract(in.Selectable))
                    {
                        in.Focused = true;
                        in.CaretBlink = 0.0f;
                        std::vector<uint32_t> cps;
                        FontAtlas::DecodeUTF8(in.Text, cps);
                        in.Caret = (int)cps.size();
                        s_Focused.Set(hoveredSelectable);
                        if (OskWanted(false))
                            OskOpen();
                    }
                }
                else
                    s_Focused.Clear();
                // Scrollbar: tutamacı yakala ya da tıklanan yere atla
                if (hoveredSelectable && hoveredSelectable.HasComponent<UIScrollbarComponent>() && hoveredSelectable == hovered)
                {
                    auto& sb = hoveredSelectable.GetComponent<UIScrollbarComponent>();
                    if (CanInteract(sb.Selectable))
                    {
                        ScrollbarGeom g = GetScrollbarGeom(sb, hoveredItem.Size);
                        float p = ScrollbarPos(g, hoveredLocal);
                        float start = (g.Track - g.Handle) * sb.Value;
                        if (p >= start && p <= start + g.Handle)
                            sb.DragGrab = p - start;
                        else
                        {
                            sb.DragGrab = g.Handle * 0.5f;
                            SetScrollbarValue(hoveredSelectable, sb, g.Track > g.Handle ? (p - sb.DragGrab) / (g.Track - g.Handle) : 0.0f);
                        }
                        sb.Dragging = true;
                    }
                }
                // Slider: hemen değer al
                if (hoveredSelectable && hoveredSelectable.HasComponent<UISliderComponent>() && hoveredSelectable == hovered)
                {
                    auto& sl = hoveredSelectable.GetComponent<UISliderComponent>();
                    if (CanInteract(sl.Selectable))
                    {
                        sl.Dragging = true;
                        ApplySliderValue(sl, hoveredItem, hoveredLocal);
                    }
                }
            }
        }

        // ---- Sürükleme (slider)
        Entity pressed = s_Pressed.Get(scene);
        if (pressed && input.MouseDown && pressed.HasComponent<UISliderComponent>())
        {
            auto& sl = pressed.GetComponent<UISliderComponent>();
            if (sl.Dragging)
                for (auto& ci : all)
                    for (auto& it : ci.Items)
                        if (it.E == pressed)
                            ApplySliderValue(sl, it, glm::vec2(glm::inverse(it.M) * glm::vec4(input.MousePosition / ci.Scale, 0, 1)));
        }

        if (pressed && input.MouseDown && pressed.HasComponent<UIScrollbarComponent>())
        {
            auto& sb = pressed.GetComponent<UIScrollbarComponent>();
            if (sb.Dragging)
                for (auto& ci : all)
                    for (auto& it : ci.Items)
                        if (it.E == pressed)
                        {
                            ScrollbarGeom g = GetScrollbarGeom(sb, it.Size);
                            glm::vec2 local = glm::vec2(glm::inverse(it.M) * glm::vec4(input.MousePosition / ci.Scale, 0, 1));
                            float p = ScrollbarPos(g, local);
                            SetScrollbarValue(pressed, sb, g.Track > g.Handle ? (p - sb.DragGrab) / (g.Track - g.Handle) : 0.0f);
                        }
        }

        // ---- Bırakma = tıklama
        if (input.MouseReleased && pressed)
        {
            if (pressed == hoveredSelectable)
            {
                if (pressed.HasComponent<UIButtonComponent>())
                {
                    auto& b = pressed.GetComponent<UIButtonComponent>();
                    if (CanInteract(b.Selectable))
                    {
                        b.ClickCount++;
                        b.ClickedThisFrame = true;
                        PushEvent(pressed, b.OnClick, "Click");
                    }
                }
                if (pressed.HasComponent<UIToggleComponent>())
                {
                    // Grup: numaralı grup ya da en yakın üstteki Toggle Group bileşeni (UIExtras)
                    if (CanInteract(pressed.GetComponent<UIToggleComponent>().Selectable) && UIExtras::ClickToggle(scene, (entt::entity)pressed))
                    {
                        auto& tg = pressed.GetComponent<UIToggleComponent>();
                        PushEvent(pressed, tg.OnValueChanged, "ValueChanged", tg.IsOn ? 1.0f : 0.0f);
                    }
                }
                if (pressed.HasComponent<UIDropdownComponent>())
                {
                    auto& dd = pressed.GetComponent<UIDropdownComponent>();
                    if (CanInteract(dd.Selectable))
                    {
                        dd.Expanded = !dd.Expanded;
                        if (dd.Expanded) s_OpenDropdown.Set(pressed); else s_OpenDropdown.Clear();
                    }
                }
            }
            if (pressed.HasComponent<UISliderComponent>())
                pressed.GetComponent<UISliderComponent>().Dragging = false;
            if (pressed.HasComponent<UIScrollbarComponent>())
                pressed.GetComponent<UIScrollbarComponent>().Dragging = false;
            s_Pressed.Clear();
        }

        // ---- Kaydırma (fare tekerleği)
        if (input.Scroll != 0.0f && hovered)
        {
            for (Entity e = hovered; e; e = e.GetParent())
            {
                if (e.HasComponent<UIScrollRectComponent>())
                {
                    auto& sr = e.GetComponent<UIScrollRectComponent>();
                    if (sr.Vertical) sr.ContentOffset.y -= input.Scroll * sr.ScrollSensitivity;
                    else if (sr.Horizontal) sr.ContentOffset.x += input.Scroll * sr.ScrollSensitivity;
                    break;
                }
            }
        }

        // ---- Klavye (odaklı input field)
        Entity focused = s_Focused.Get(scene);
        const bool keyboardOwnedByField = focused && focused.HasComponent<UIInputFieldComponent>();
        if (focused && focused.HasComponent<UIInputFieldComponent>())
        {
            auto& in = focused.GetComponent<UIInputFieldComponent>();
            in.CaretBlink += dt;
            std::vector<uint32_t> cps;
            FontAtlas::DecodeUTF8(in.Text, cps);
            in.Caret = std::clamp(in.Caret, 0, (int)cps.size());
            bool changed = false;
            for (uint32_t c : input.Characters)
            {
                if (in.CharacterLimit > 0 && (int)cps.size() >= in.CharacterLimit)
                    break;
                if (!AcceptsChar(in, c, in.Text))
                    continue;
                cps.insert(cps.begin() + in.Caret, c);
                in.Caret++;
                changed = true;
            }
            if (input.Backspace && in.Caret > 0) { cps.erase(cps.begin() + in.Caret - 1); in.Caret--; changed = true; }
            if (input.Delete && in.Caret < (int)cps.size()) { cps.erase(cps.begin() + in.Caret); changed = true; }
            if (input.Left) in.Caret = std::max(0, in.Caret - 1);
            if (input.Right) in.Caret = std::min((int)cps.size(), in.Caret + 1);
            if (input.Home) in.Caret = 0;
            if (input.End) in.Caret = (int)cps.size();
            if (changed)
            {
                in.Text = EncodeUTF8(cps);
                in.CaretBlink = 0.0f;
                PushEvent(focused, "", "ValueChanged");
            }
            if (input.Enter || input.Escape)
            {
                if (input.Enter)
                {
                    in.SubmittedThisFrame = true;
                    PushEvent(focused, in.OnSubmit, "Submit");
                }
                in.Focused = false;
                s_Focused.Clear();
            }
        }

        // Ekran klavyesi: odak bitince kapanır; açıkken alanın metnini önizler
        {
            Entity f = s_Focused.Get(scene);
            if (!f || !f.HasComponent<UIInputFieldComponent>() || !f.GetComponent<UIInputFieldComponent>().Focused)
                s_Osk.Open = false;
            else if (s_Osk.Open)
            {
                const auto& in = f.GetComponent<UIInputFieldComponent>();
                s_Osk.Preview = in.ContentType == UIContentType::Password ? std::string(in.Text.size(), '*') : in.Text;
                s_Osk.PreviewCaret = in.Caret;
                OskLayout(screenSize);
            }
        }

        // ---- ScrollRect <-> Scrollbar eşitlemesi
        for (auto& ci : all)
            for (auto& it : ci.Items)
            {
                if (!it.E.HasComponent<UIScrollRectComponent>())
                    continue;
                auto& sr = it.E.GetComponent<UIScrollRectComponent>();
                glm::vec2 maxOffset = glm::max(sr.ContentSize - it.Size, glm::vec2(0.0f));
                auto sync = [&](uint64_t ref, bool vertical) {
                    Entity bar = ref ? scene.FindEntityByUUID(ref) : Entity{};
                    if (!bar || !bar.HasComponent<UIScrollbarComponent>())
                        return;
                    auto& sb = bar.GetComponent<UIScrollbarComponent>();
                    float viewport = vertical ? it.Size.y : it.Size.x, content = vertical ? sr.ContentSize.y : sr.ContentSize.x;
                    sb.Size = content > 1e-3f ? std::clamp(viewport / content, 0.05f, 1.0f) : 1.0f;
                    if (sb.Dragging || sb.ChangedThisFrame)
                    {
                        if (vertical) sr.ContentOffset.y = (1.0f - sb.Value) * maxOffset.y;
                        else sr.ContentOffset.x = -sb.Value * maxOffset.x;
                    }
                    else
                        sb.Value = vertical ? sr.NormalizedPosition.y : sr.NormalizedPosition.x;
                };
                sync(sr.VerticalScrollbar, true);
                sync(sr.HorizontalScrollbar, false);
            }

        // ---- Event Trigger (işaretçi olayları üstlere kabarcıklanır)
        {
            using T = UIEventTriggerComponent;
            Entity triggerHover;
            for (Entity e = hovered; e; e = e.GetParent())
                if (e.HasComponent<T>()) { triggerHover = e; break; }
            // UI öğesinin üstünde değilse dünya nesnesi (kamerada Physics / Physics 2D Raycaster): Event Trigger'ı olan en yakın üst nesne
            if (!hovered && input.Valid)
            {
                const entt::entity w = UIExtras::WorldPointerTarget(scene, screenSize, input.MousePosition);
                if (w != entt::null && scene.m_Registry.valid(w))
                    for (Entity e(w, &scene); e; e = e.GetParent())
                        if (e.HasComponent<T>()) { triggerHover = e; break; }
            }
            Entity prevHover = s_TriggerHover.Get(scene);
            if (triggerHover != prevHover)
            {
                Trigger(scene, prevHover, &T::PointerExit);
                Trigger(scene, triggerHover, &T::PointerEnter);
                s_TriggerHover.Set(triggerHover);
            }
            if (input.MousePressed && triggerHover)
            {
                s_TriggerPressed.Set(triggerHover);
                s_TriggerPressPos = input.MousePosition;
                Trigger(scene, triggerHover, &T::PointerDown);
            }
            Entity tp = s_TriggerPressed.Get(scene);
            if (tp && input.MouseDown && glm::distance(input.MousePosition, s_TriggerPressPos) > 2.0f)
                Trigger(scene, tp, &T::Drag);
            if (tp && input.MouseReleased)
            {
                Trigger(scene, tp, &T::PointerUp);
                if (tp == triggerHover)
                    Trigger(scene, tp, &T::PointerClick);
                s_TriggerPressed.Clear();
            }
            if (input.Scroll != 0.0f && triggerHover)
                Trigger(scene, triggerHover, &T::Scroll, input.Scroll);
        }
        // Ekran denetimleri (On-Screen Stick / Button): sanal gamepad
        const bool onScreenHeld = UIExtras::UpdateOnScreenControls(scene, input, hovered ? (entt::entity)hovered : entt::null);

        // ---- Klavye / gamepad gezinmesi (Unity EventSystem)
        {
            using T = UIEventTriggerComponent;
            if (input.Valid && s_LastMouse.x >= 0.0f && glm::distance(input.MousePosition, s_LastMouse) > 2.0f)
                s_NavActive = false;
            s_LastMouse = input.MousePosition;
            UIEventSystemComponent* es = nullptr;
            for (auto e : scene.m_Registry.view<UIEventSystemComponent>())
            {
                es = &scene.m_Registry.get<UIEventSystemComponent>(e);
                break;
            }
            const float repeatDelay = es ? es->RepeatDelay : 0.4f, repeatRate = es ? es->RepeatRate : 0.12f;
            glm::ivec2 dir(0);
            if (input.Up) dir.y = 1;
            else if (input.Down) dir.y = -1;
            else if (input.Left) dir.x = -1;
            else if (input.Right) dir.x = 1;
            // Gamepad: d-pad ya da sol çubuk; basılı tutunca tekrarlar (GLFW standart eşleme)
            glm::ivec2 pad(0);
            bool padSubmit = false, padCancel = false;
            if (Input::IsGamepadConnected(0) && !onScreenHeld)
            {
                glm::vec2 st = Input::GetGamepadLeftStick(0);
                if (Input::IsGamepadButtonPressed(11, 0) || st.y < -0.6f) pad.y = 1;
                else if (Input::IsGamepadButtonPressed(13, 0) || st.y > 0.6f) pad.y = -1;
                else if (Input::IsGamepadButtonPressed(14, 0) || st.x < -0.6f) pad.x = -1;
                else if (Input::IsGamepadButtonPressed(12, 0) || st.x > 0.6f) pad.x = 1;
                padSubmit = Input::IsGamepadButtonPressed(0, 0);
                padCancel = Input::IsGamepadButtonPressed(1, 0);
            }
            if (pad != glm::ivec2(0))
            {
                if (pad != s_NavHeld) { dir = pad; s_NavRepeat = repeatDelay; }
                else if ((s_NavRepeat -= dt) <= 0.0f) { dir = pad; s_NavRepeat = repeatRate; }
            }
            s_NavHeld = pad;
            bool submit = (input.Enter && !keyboardOwnedByField) || (input.Space && s_NavActive) || (padSubmit && !s_PadSubmitPrev);
            bool cancel = (input.Escape && !keyboardOwnedByField) || (padCancel && !s_PadCancelPrev);
            s_PadSubmitPrev = padSubmit;
            s_PadCancelPrev = padCancel;

            if ((!es || es->SendNavigationEvents) && !keyboardOwnedByField && !s_Focused.Get(scene) && (dir != glm::ivec2(0) || submit || cancel))
            {
                struct Candidate { Entity E; glm::vec2 Center; const Item* It; float Scale; };
                std::vector<Candidate> candidates;
                for (auto& ci : all)
                    for (auto& it : ci.Items)
                    {
                        UISelectableState* sel = GetSelectable(it.E);
                        if (!sel || !CanInteract(*sel) || !it.Blocks || it.Alpha <= 0.001f)
                            continue;
                        glm::vec2 c = glm::vec2(it.M * glm::vec4(it.Size * 0.5f, 0, 1)) * ci.Scale;
                        candidates.push_back({ it.E, c, &it, ci.Scale });
                    }
                auto select = [&](Entity e) {
                    Entity old = s_Selected.Get(scene);
                    if (old == e) return;
                    Trigger(scene, old, &T::Deselect);
                    s_Selected.Set(e);
                    Trigger(scene, e, &T::Select);
                };
                Entity cur = s_Selected.Get(scene);
                const Candidate* curC = nullptr;
                for (auto& c : candidates) if (c.E == cur) curC = &c;
                if (cur && !curC)
                    cur = {};   // silinmiş / etkileşimsiz olmuş
                s_NavActive = true;
                if (!cur && (dir != glm::ivec2(0) || submit))
                {
                    // İlk seçim: EventSystem.FirstSelected, yoksa en üstteki sol üst öğe
                    Entity first = es && es->FirstSelected ? scene.FindEntityByUUID(es->FirstSelected) : Entity{};
                    const Candidate* best = nullptr;
                    for (auto& c : candidates)
                    {
                        if (first && c.E == first) { best = &c; break; }
                        if (!best || c.Center.y > best->Center.y + 1.0f || (std::fabs(c.Center.y - best->Center.y) <= 1.0f && c.Center.x < best->Center.x))
                            best = &c;
                    }
                    if (best) select(best->E);
                }
                else if (cur && dir != glm::ivec2(0))
                {
                    bool consumed = false;
                    if (cur.HasComponent<UISliderComponent>())
                    {
                        auto& sl = cur.GetComponent<UISliderComponent>();
                        const bool horizontal = sl.Direction == UISliderDirection::LeftToRight || sl.Direction == UISliderDirection::RightToLeft;
                        int along = horizontal ? dir.x : dir.y;
                        if (sl.Direction == UISliderDirection::RightToLeft || sl.Direction == UISliderDirection::TopToBottom) along = -along;
                        if (along != 0)
                        {
                            float step = sl.WholeNumbers ? 1.0f : (sl.MaxValue - sl.MinValue) * 0.1f;
                            float v = std::clamp(sl.Value + step * (float)along, sl.MinValue, sl.MaxValue);
                            if (v != sl.Value) { sl.Value = v; sl.ChangedThisFrame = true; PushEvent(cur, sl.OnValueChanged, "ValueChanged", v); }
                            consumed = true;
                        }
                    }
                    if (cur.HasComponent<UIDropdownComponent>() && cur.GetComponent<UIDropdownComponent>().Expanded && dir.y != 0)
                    {
                        auto& dd = cur.GetComponent<UIDropdownComponent>();
                        int count = (int)dd.Options.size();
                        if (count > 0)
                            dd.HoveredOption = std::clamp((dd.HoveredOption < 0 ? dd.Value : dd.HoveredOption) - dir.y, 0, count - 1);
                        consumed = true;
                    }
                    if (!consumed)
                    {
                        // Uzamsal: yön boyunca en yakın, yana sapması az olan öğe
                        glm::vec2 d((float)dir.x, (float)dir.y);
                        const Candidate* best = nullptr;
                        float bestScore = 1e30f;
                        for (auto& c : candidates)
                        {
                            if (c.E == cur) continue;
                            glm::vec2 v = c.Center - curC->Center;
                            float along = glm::dot(v, d);
                            if (along <= 1.0f) continue;
                            float perp = std::fabs(v.x * d.y - v.y * d.x);
                            float score = along + perp * 2.0f;
                            if (score < bestScore) { bestScore = score; best = &c; }
                        }
                        if (best) select(best->E);
                    }
                }
                if (cur && submit)
                {
                    Trigger(scene, cur, &T::Submit);
                    if (cur.HasComponent<UIButtonComponent>())
                    {
                        auto& b = cur.GetComponent<UIButtonComponent>();
                        b.ClickCount++;
                        b.ClickedThisFrame = true;
                        PushEvent(cur, b.OnClick, "Click");
                    }
                    if (cur.HasComponent<UIToggleComponent>() && UIExtras::ClickToggle(scene, (entt::entity)cur))
                    {
                        auto& tg = cur.GetComponent<UIToggleComponent>();
                        PushEvent(cur, tg.OnValueChanged, "ValueChanged", tg.IsOn ? 1.0f : 0.0f);
                    }
                    if (cur.HasComponent<UIDropdownComponent>())
                    {
                        auto& dd = cur.GetComponent<UIDropdownComponent>();
                        if (dd.Expanded)
                        {
                            if (dd.HoveredOption >= 0 && dd.HoveredOption != dd.Value)
                            {
                                dd.Value = dd.HoveredOption;
                                dd.ChangedThisFrame = true;
                                PushEvent(cur, dd.OnValueChanged, "ValueChanged", (float)dd.Value);
                            }
                            dd.Expanded = false;
                            s_OpenDropdown.Clear();
                        }
                        else
                        {
                            dd.Expanded = true;
                            dd.HoveredOption = dd.Value;
                            s_OpenDropdown.Set(cur);
                        }
                    }
                    if (cur.HasComponent<UIInputFieldComponent>())
                    {
                        auto& in = cur.GetComponent<UIInputFieldComponent>();
                        in.Focused = true;
                        in.CaretBlink = 0.0f;
                        s_Focused.Set(cur);
                        if (OskWanted(padSubmit))
                        {
                            OskOpen();
                            // Bu karedeki A basışı klavyede tuşa basmış sayılmasın
                            s_Osk.PrevButtons = 0;
                            for (int b = 0; b <= 14; ++b)
                                if (Input::IsGamepadButtonPressed(b, 0))
                                    s_Osk.PrevButtons |= 1u << b;
                        }
                    }
                }
                if (cur && cancel)
                {
                    Trigger(scene, cur, &T::Cancel);
                    if (cur.HasComponent<UIDropdownComponent>() && cur.GetComponent<UIDropdownComponent>().Expanded)
                    {
                        cur.GetComponent<UIDropdownComponent>().Expanded = false;
                        s_OpenDropdown.Clear();
                    }
                }
            }
        }

        // ---- Seçilebilir görsel geçişleri
        for (auto& ci : all)
        {
            for (auto& it : ci.Items)
            {
                UISelectableState* s = GetSelectable(it.E);
                if (!s)
                    continue;
                s->Hovered = it.E == hoveredSelectable;
                s->Pressed = it.E == s_Pressed.Get(scene) && s->Hovered && input.MouseDown;
                // Gezinmeyle seçili öğe de vurgulanır (fare kullanılırken yalnızca üstünde olunan)
                s->Selected = it.E == s_Selected.Get(scene);
                const bool highlighted = s->Hovered || (s->Selected && s_NavActive);
                glm::vec4 target = !CanInteract(*s) ? s->DisabledColor : s->Pressed ? s->PressedColor : highlighted ? s->HighlightedColor : s->NormalColor;
                float targetScale = !CanInteract(*s) ? 1.0f : s->Pressed ? 0.95f : highlighted ? 1.04f : 1.0f;
                float k = s->FadeDuration > 0.0f ? std::min(1.0f, dt / s->FadeDuration) : 1.0f;
                s->CurrentTint += (target - s->CurrentTint) * k;
                s->CurrentScale += (targetScale - s->CurrentScale) * k;
            }
        }

        // Bu karenin olayları (scriptler aynı karede okur)
        s_Events = std::move(s_PendingEvents);
        s_PendingEvents.clear();
    }

    void UISystem::DrawCanvas(uint32_t framebuffer, uint32_t width, uint32_t height, const std::vector<CanvasCommand>& commands)
    {
        GLint prevFbo = 0;
        GLint prevViewport[4];
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, (GLsizei)width, (GLsizei)height);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        static auto& textures = *new std::unordered_map<std::string, std::shared_ptr<Texture2D>>();
        const glm::mat4 ortho = glm::ortho(0.0f, (float)width, 0.0f, (float)height, -1.0f, 1.0f);
        const glm::mat4 identity(1.0f);
        const float W = (float)width, H = (float)height;
        Renderer2D::BeginScene(ortho, false);
        Renderer2D::SetOutputMode(0);
        for (const auto& c : commands)
        {
            const float y = H - c.Box.y - c.Box.w;
            switch (c.Type)
            {
                case CanvasCommand::Kind::Clear:
                    Renderer2D::EndScene();
                    glClearColor(c.Color.r, c.Color.g, c.Color.b, c.Color.a);
                    glClear(GL_COLOR_BUFFER_BIT);
                    Renderer2D::BeginScene(ortho, false);
                    Renderer2D::SetOutputMode(0);
                    break;
                case CanvasCommand::Kind::Rect:
                    Quad(identity, { c.Box.x, y }, { c.Box.x + c.Box.z, y + c.Box.w }, nullptr, { 0, 0 }, { 1, 1 }, c.Color);
                    break;
                case CanvasCommand::Kind::Image:
                {
                    auto& tex = textures[c.Text];
                    if (!tex)
                        tex = Texture2D::Create(SceneSerializer::ResolveAssetPath(c.Text));
                    if (tex)
                        Quad(identity, { c.Box.x, y }, { c.Box.x + c.Box.z, y + c.Box.w }, tex, { 0, 0 }, { 1, 1 }, c.Color);
                    break;
                }
                case CanvasCommand::Kind::Text:
                {
                    const float lineH = c.Size * 1.3f;
                    DrawTextBlock(glm::translate(identity, glm::vec3(c.Box.x, H - c.Box.y - lineH, 0.0f)), { W - c.Box.x, lineH }, c.Text, "", c.Size, c.Color,
                                  UITextAlignH::Left, UITextAlignV::Middle, false, 1.0f);
                    break;
                }
            }
        }
        Renderer2D::EndScene();
        Renderer2D::SetOutputMode(-1);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    }

    void UISystem::RenderScreenSpace(Scene& scene, const glm::vec2& screenSize)
    {
        if (screenSize.x <= 0.0f || screenSize.y <= 0.0f)
            return;
        auto canvases = SortedCanvases(scene);
        bool any = false;
        for (Entity c : canvases)
            any |= c.GetComponent<CanvasComponent>().Mode != CanvasComponent::RenderMode::WorldSpace;
        std::string subtitleSpeaker, subtitleText;
        if (!any && !Dialogue::GetSubtitle(subtitleSpeaker, subtitleText) && AssetRuntime::ActiveTouchControls().empty())
            return;

        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        // Çağıranın görünüm kökü korunur (Android Automotive: oyun araç çubuklarının arasındaki alana çizilir)
        glViewport(vp[0], vp[1], (GLsizei)screenSize.x, (GLsizei)screenSize.y);
        s_ScreenOrigin = { vp[0], vp[1] };
        const glm::mat4 ortho = glm::ortho(0.0f, screenSize.x, 0.0f, screenSize.y, -1.0f, 1.0f);
        std::vector<Item> items;
        for (Entity canvas : canvases)
        {
            if (canvas.GetComponent<CanvasComponent>().Mode == CanvasComponent::RenderMode::WorldSpace)
                continue;
            float scale = ComputeScaleFactor(canvas, screenSize);
            BuildItems(scene, canvas, CanvasSize(canvas, screenSize), items);
            glm::mat4 toPx = glm::scale(glm::mat4(1.0f), glm::vec3(scale, scale, 1.0f));
            Renderer2D::BeginScene(ortho * toPx, false);
            Renderer2D::SetOutputMode(0);
            RenderItems(items, toPx, true, screenSize);
            Renderer2D::EndScene();
            Renderer2D::SetOutputMode(-1);
        }
        // Touch Interface denetimleri
        if (const auto& controls = AssetRuntime::ActiveTouchControls(); !controls.empty())
        {
            Renderer2D::BeginScene(ortho, false);
            Renderer2D::SetOutputMode(0);
            const glm::mat4 identity(1.0f);
            for (const auto& c : controls)
            {
                const glm::vec2 center = c.Center * screenSize;
                const float radius = c.Radius * screenSize.y;
                Circle(identity, center, radius, glm::vec4(1.0f, 1.0f, 1.0f, c.Held ? 0.45f : 0.25f));
                if (c.AxisX >= 0)
                    Circle(identity, center + c.Knob * radius * 0.6f, radius * 0.4f, glm::vec4(1.0f, 1.0f, 1.0f, 0.6f));
                else
                    DrawTextBlock(glm::translate(identity, glm::vec3(center.x - radius, center.y - radius, 0.0f)), glm::vec2(radius * 2.0f), c.Name, "", radius * 0.5f,
                                  glm::vec4(1.0f), UITextAlignH::Center, UITextAlignV::Middle, false, 1.0f);
            }
            Renderer2D::EndScene();
            Renderer2D::SetOutputMode(-1);
        }
        // Dialogue Wave altyazısı (ekranın altı)
        {
            std::string speaker, text;
            if (Dialogue::GetSubtitle(speaker, text))
            {
                const float size = std::max(18.0f, screenSize.y * 0.03f);
                const std::string line = speaker.empty() ? text : speaker + ": " + text;
                const glm::vec2 box(screenSize.x * 0.8f, size * 2.0f);
                const glm::mat4 at = glm::translate(glm::mat4(1.0f), glm::vec3(screenSize.x * 0.1f, size * 1.2f, 0.0f));
                Renderer2D::BeginScene(ortho, false);
                Renderer2D::SetOutputMode(0);
                Quad(at, glm::vec2(0.0f), box, nullptr, { 0, 0 }, { 1, 1 }, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
                DrawTextBlock(at, box, line, "", size, glm::vec4(1.0f), UITextAlignH::Center, UITextAlignV::Middle, true, 1.0f);
                Renderer2D::EndScene();
                Renderer2D::SetOutputMode(-1);
            }
        }
        if (s_Osk.Open)
        {
            Renderer2D::BeginScene(ortho, false);
            Renderer2D::SetOutputMode(0);
            OskRender();
            Renderer2D::EndScene();
            Renderer2D::SetOutputMode(-1);
        }
        s_ScreenOrigin = { 0, 0 };
        glViewport(vp[0], vp[1], vp[2], vp[3]);
    }

    void UISystem::RenderWorldSpace(Scene& scene, const glm::mat4& viewProjection, bool includeOverlay, const glm::vec2& editorScreenSize)
    {
        RenderCanvasesInWorld(scene, viewProjection, true, includeOverlay, editorScreenSize);
    }

    void UISystem::RenderEditorOverlayCanvases(Scene& scene, const glm::mat4& viewProjection, const glm::vec2& editorScreenSize, bool depthTest)
    {
        RenderCanvasesInWorld(scene, viewProjection, false, true, editorScreenSize, depthTest);
    }

    void UISystem::RenderCanvasesInWorld(Scene& scene, const glm::mat4& viewProjection, bool includeWorld, bool includeOverlay, const glm::vec2& editorScreenSize,
                                         bool depthTest)
    {
        std::vector<Item> items;
        for (Entity canvas : SortedCanvases(scene))
        {
            bool world = canvas.GetComponent<CanvasComponent>().Mode == CanvasComponent::RenderMode::WorldSpace;
            if ((world && !includeWorld) || (!world && !includeOverlay))
                continue;
            glm::vec2 screen = world ? glm::vec2(1920.0f, 1080.0f) : editorScreenSize;
            BuildItems(scene, canvas, CanvasSize(canvas, screen), items);
            if (items.empty())
                continue;
            Renderer2D::BeginScene(viewProjection * GetCanvasWorldMatrix(canvas, screen), depthTest);
            RenderItems(items, glm::mat4(1.0f), false, screen);
            Renderer2D::EndScene();
        }
    }

    void UISystem::UpdateEditorTransforms(Scene& scene, const glm::vec2& referenceScreenSize)
    {
        std::vector<Item> items;
        for (Entity canvas : SortedCanvases(scene))
        {
            bool world = canvas.GetComponent<CanvasComponent>().Mode == CanvasComponent::RenderMode::WorldSpace;
            glm::vec2 screen = world ? glm::vec2(1920.0f, 1080.0f) : referenceScreenSize;
            BuildItems(scene, canvas, CanvasSize(canvas, screen), items);
            glm::mat4 base = GetCanvasWorldMatrix(canvas, screen);
            for (auto& it : items)
            {
                auto& rt = it.E.GetComponent<RectTransformComponent>();
                it.E.GetComponent<TransformComponent>().WorldTransform = base * it.M * glm::translate(glm::mat4(1.0f), glm::vec3(it.Size * rt.Pivot, 0.0f));
            }
        }
    }

    void UISystem::MoveElementWorld(Entity element, const glm::vec3& worldDelta, const glm::vec2& screenSize)
    {
        if (!element || !element.HasComponent<RectTransformComponent>())
            return;
        // Canvas'ı bul
        Entity canvas = element.GetParent();
        while (canvas && !canvas.HasComponent<CanvasComponent>())
            canvas = canvas.GetParent();
        if (!canvas)
            return;
        glm::mat4 parentToWorld = GetCanvasWorldMatrix(canvas, screenSize);
        Entity parent = element.GetParent();
        if (parent && parent.HasComponent<RectTransformComponent>())
            parentToWorld = parentToWorld * parent.GetComponent<RectTransformComponent>().CanvasMatrix;
        glm::vec3 local = glm::vec3(glm::inverse(parentToWorld) * glm::vec4(worldDelta, 0.0f));
        element.GetComponent<RectTransformComponent>().AnchoredPosition += glm::vec2(local);
    }

    void UISystem::Shutdown()
    {
        s_RoundSprite.reset();
        s_CircleSprite.reset();
        s_Events.clear();
        s_PendingEvents.clear();
        FontAtlas::ShutdownAll();
    }
}
