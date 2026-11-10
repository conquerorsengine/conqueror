// Sprite Editor modları: Custom Outline, Custom Physics Shape, Secondary Textures, Skinning Editor (Unity Sprite Editor menüsü).
#include "SpriteEditorPanel.h"
#include "ObjectPicker.h"
#include "Core/Math/Triangulate2D.h"
#include "Core/Project/Project.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Utils/FileSystem.h"
#include "Renderer/RHI/Texture.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <unordered_map>

namespace Conqueror::Editor
{
    namespace
    {
        glm::mat3 Mat3Translate(const glm::vec2& t)
        {
            glm::mat3 m(1.0f);
            m[2] = glm::vec3(t, 1.0f);
            return m;
        }

        glm::mat3 Mat3Rotate(float angle)
        {
            const float c = std::cos(angle), s = std::sin(angle);
            glm::mat3 m(1.0f);
            m[0] = glm::vec3(c, s, 0.0f);
            m[1] = glm::vec3(-s, c, 0.0f);
            return m;
        }

        glm::vec2 TransformPoint(const glm::mat3& m, const glm::vec2& p) { return glm::vec2(m * glm::vec3(p, 1.0f)); }

        ImU32 BoneColor(int index, float alpha = 1.0f)
        {
            float r, g, b;
            ImGui::ColorConvertHSVtoRGB(std::fmod(0.07f + (float)index * 0.618034f, 1.0f), 0.75f, 1.0f, r, g, b);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, alpha));
        }

        float SegmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, glm::vec2* closest = nullptr)
        {
            const glm::vec2 ab = b - a;
            const float len2 = glm::dot(ab, ab);
            const float t = len2 > 1e-8f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
            const glm::vec2 q = a + ab * t;
            if (closest)
                *closest = q;
            return glm::length(p - q);
        }

        bool PointInTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
        {
            auto cross = [](const glm::vec2& u, const glm::vec2& v, const glm::vec2& w) { return (v.x - u.x) * (w.y - u.y) - (v.y - u.y) * (w.x - u.x); };
            const float d0 = cross(a, b, p), d1 = cross(b, c, p), d2 = cross(c, a, p);
            return (d0 >= 0.0f && d1 >= 0.0f && d2 >= 0.0f) || (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f);
        }

        // Proje içindeyse proje göreli, motor Resources içindeyse CQ: önekli yol (sahneler ve .cqsprite taşınabilir kalsın)
        std::string PortablePath(const std::string& path)
        {
            if (path.empty() || !std::filesystem::path(path).is_absolute())
                return path;
            std::error_code ec;
            const auto projectDir = Project::GetActiveProjectDirectory();
            if (!projectDir.empty())
            {
                const auto rel = std::filesystem::relative(path, projectDir, ec);
                if (!ec && !rel.empty() && rel.native()[0] != '.')
                    return rel.generic_string();
            }
            const std::filesystem::path resources = FileSystem::ResolveResourcePath("Resources");
            if (!resources.empty())
            {
                const auto rel = std::filesystem::relative(path, resources.parent_path(), ec);
                if (!ec && !rel.empty() && rel.native()[0] != '.')
                    return "CQ:" + rel.generic_string();
            }
            return path;
        }

        // Köşe başına en çok 4 kemik: uzaklığın 4. kuvvetinin tersi (kemik doğru parçasına), toplam 1
        void AutoWeightVertex(SpriteSkinData& skin, size_t v)
        {
            glm::ivec4 index(-1);
            glm::vec4 weight(0.0f);
            if (!skin.Bones.empty())
            {
                std::vector<std::pair<float, int>> ranked;
                ranked.reserve(skin.Bones.size());
                for (int b = 0; b < (int)skin.Bones.size(); ++b)
                {
                    const float d = SegmentDistance(skin.Vertices[v], skin.Bones[(size_t)b].Position, skin.Bones[(size_t)b].Tip);
                    ranked.emplace_back(1.0f / (d * d * d * d + 0.06f), b);
                }
                std::sort(ranked.begin(), ranked.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
                float total = 0.0f;
                for (int k = 0; k < 4 && k < (int)ranked.size(); ++k)
                    total += ranked[(size_t)k].first;
                float kept = 0.0f;
                for (int k = 0; k < 4 && k < (int)ranked.size(); ++k)
                {
                    const float w = ranked[(size_t)k].first / total;
                    if (w < 0.01f)
                        continue;   // etkisi yok denecek kadar az
                    index[k] = ranked[(size_t)k].second;
                    weight[k] = w;
                    kept += w;
                }
                if (kept > 0.0f)
                    weight /= kept;
            }
            skin.BoneIndex[v] = index;
            skin.BoneWeight[v] = weight;
        }

        // Bir kemiğin ağırlığını value yapar; kalan pay diğer kemiklere orantılı dağılır (toplam 1)
        void SetVertexWeight(glm::ivec4& index, glm::vec4& weight, int bone, float value)
        {
            int slot = -1;
            for (int k = 0; k < 4; ++k)
                if (index[k] == bone)
                    slot = k;
            if (slot < 0)
            {
                // Boş yuva, yoksa en zayıf kemiğin yuvası
                slot = 0;
                for (int k = 0; k < 4; ++k)
                {
                    if (index[k] < 0) { slot = k; break; }
                    if (weight[k] < weight[slot]) slot = k;
                }
                index[slot] = bone;
                weight[slot] = 0.0f;
            }
            value = std::clamp(value, 0.0f, 1.0f);
            float others = 0.0f;
            for (int k = 0; k < 4; ++k)
                if (k != slot && index[k] >= 0)
                    others += weight[k];
            if (others <= 1e-5f)
            {
                // Paylaşacak başka kemik yok: köşe bu kemiğe tam bağlı kalır
                weight[slot] = 1.0f;
                return;
            }
            const float scale = (1.0f - value) / others;
            for (int k = 0; k < 4; ++k)
                if (k != slot && index[k] >= 0)
                    weight[k] *= scale;
            weight[slot] = value;
            for (int k = 0; k < 4; ++k)
                if (index[k] >= 0 && weight[k] < 1e-4f)
                {
                    index[k] = -1;
                    weight[k] = 0.0f;
                }
        }

        float VertexWeight(const glm::ivec4& index, const glm::vec4& weight, int bone)
        {
            for (int k = 0; k < 4; ++k)
                if (index[k] == bone)
                    return weight[k];
            return 0.0f;
        }

        std::vector<std::vector<int>> VertexNeighbours(const SpriteSkinData& skin)
        {
            std::vector<std::vector<int>> adj(skin.Vertices.size());
            auto link = [&](uint32_t a, uint32_t b) {
                if (a >= adj.size() || b >= adj.size())
                    return;
                if (std::find(adj[a].begin(), adj[a].end(), (int)b) == adj[a].end())
                    adj[a].push_back((int)b);
                if (std::find(adj[b].begin(), adj[b].end(), (int)a) == adj[b].end())
                    adj[b].push_back((int)a);
            };
            for (size_t t = 0; t + 2 < skin.Indices.size(); t += 3)
            {
                link(skin.Indices[t], skin.Indices[t + 1]);
                link(skin.Indices[t + 1], skin.Indices[t + 2]);
                link(skin.Indices[t + 2], skin.Indices[t]);
            }
            return adj;
        }

        // Komşuların ağırlık ortalamasına doğru yumuşatır (amount 0..1)
        void SmoothVertex(SpriteSkinData& skin, size_t v, const std::vector<std::vector<int>>& adj, float amount)
        {
            if (adj[v].empty())
                return;
            std::map<int, float> sum;
            for (int k = 0; k < 4; ++k)
                if (skin.BoneIndex[v][k] >= 0)
                    sum[skin.BoneIndex[v][k]] += skin.BoneWeight[v][k] * (1.0f - amount);
            const float share = amount / (float)adj[v].size();
            for (int n : adj[v])
                for (int k = 0; k < 4; ++k)
                    if (skin.BoneIndex[(size_t)n][k] >= 0)
                        sum[skin.BoneIndex[(size_t)n][k]] += skin.BoneWeight[(size_t)n][k] * share;
            std::vector<std::pair<float, int>> ranked;
            for (const auto& [bone, w] : sum)
                ranked.emplace_back(w, bone);
            std::sort(ranked.begin(), ranked.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
            glm::ivec4 index(-1);
            glm::vec4 weight(0.0f);
            float total = 0.0f;
            for (int k = 0; k < 4 && k < (int)ranked.size(); ++k)
            {
                index[k] = ranked[(size_t)k].second;
                weight[k] = ranked[(size_t)k].first;
                total += weight[k];
            }
            if (total <= 0.0f)
                return;
            skin.BoneIndex[v] = index;
            skin.BoneWeight[v] = weight / total;
        }

        // Köşeyi siler; çevresindeki boşluk yeniden üçgenlenir
        void RemoveVertex(SpriteSkinData& skin, uint32_t v)
        {
            std::vector<uint32_t> kept;
            std::unordered_map<uint32_t, uint32_t> next;
            std::unordered_map<uint32_t, int> incoming;
            for (size_t t = 0; t + 2 < skin.Indices.size(); t += 3)
            {
                const uint32_t tri[3] = { skin.Indices[t], skin.Indices[t + 1], skin.Indices[t + 2] };
                const int at = tri[0] == v ? 0 : tri[1] == v ? 1 : tri[2] == v ? 2 : -1;
                if (at < 0)
                {
                    kept.insert(kept.end(), tri, tri + 3);
                    continue;
                }
                const uint32_t p = tri[(at + 1) % 3], q = tri[(at + 2) % 3];
                next[p] = q;
                incoming[q]++;
            }
            if (!next.empty())
            {
                // Açık halkada (kenardaki köşe) başlangıç: gelen kenarı olmayan köşe
                uint32_t start = next.begin()->first;
                for (const auto& [p, q] : next)
                    if (!incoming.count(p))
                    {
                        start = p;
                        break;
                    }
                std::vector<uint32_t> ring;
                uint32_t cur = start;
                for (size_t guard = 0; guard <= next.size(); ++guard)
                {
                    ring.push_back(cur);
                    const auto it = next.find(cur);
                    if (it == next.end() || it->second == start)
                        break;
                    cur = it->second;
                }
                std::vector<glm::vec2> pts;
                for (uint32_t i : ring)
                    pts.push_back(skin.Vertices[i]);
                std::vector<uint32_t> tris;
                if (ring.size() >= 3 && Triangulate2D::Triangulate(pts, tris))
                    for (uint32_t i : tris)
                        kept.push_back(ring[i]);
            }
            for (uint32_t& i : kept)
                if (i > v)
                    --i;
            skin.Indices = std::move(kept);
            skin.Vertices.erase(skin.Vertices.begin() + v);
            skin.BoneIndex.erase(skin.BoneIndex.begin() + v);
            skin.BoneWeight.erase(skin.BoneWeight.begin() + v);
        }

        int AddMeshVertex(SpriteSkinData& skin, const glm::vec2& p)
        {
            skin.Vertices.push_back(p);
            skin.BoneIndex.emplace_back(-1);
            skin.BoneWeight.emplace_back(0.0f);
            AutoWeightVertex(skin, skin.Vertices.size() - 1);
            return (int)skin.Vertices.size() - 1;
        }

        void RemoveBone(SpriteSkinData& skin, int bone)
        {
            const int parent = skin.Bones[(size_t)bone].Parent;
            for (auto& b : skin.Bones)
                if (b.Parent == bone)
                    b.Parent = parent;
            for (auto& b : skin.Bones)
                if (b.Parent > bone)
                    --b.Parent;
            skin.Bones.erase(skin.Bones.begin() + bone);
            for (size_t v = 0; v < skin.Vertices.size(); ++v)
            {
                float total = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    int& index = skin.BoneIndex[v][k];
                    if (index == bone)
                    {
                        index = -1;
                        skin.BoneWeight[v][k] = 0.0f;
                    }
                    else if (index > bone)
                        --index;
                    if (index >= 0)
                        total += skin.BoneWeight[v][k];
                }
                if (total > 0.0f)
                    skin.BoneWeight[v] /= total;
                else
                    AutoWeightVertex(skin, v);   // tek kemiği silinen köşe kalan kemiklere bağlanır
            }
        }

        void DrawBoneShape(ImDrawList* dl, const glm::vec2& a, const glm::vec2& b, ImU32 fill, bool selected)
        {
            const glm::vec2 dir = b - a;
            const float len = glm::length(dir);
            if (len < 1.0f)
                return;
            const glm::vec2 n = glm::vec2(-dir.y, dir.x) / len;
            const float w = std::min(len * 0.14f, 8.0f);
            const glm::vec2 q = a + dir * 0.18f;
            const glm::vec2 l = q + n * w, r = q - n * w;
            const ImVec2 A(a.x, a.y), B(b.x, b.y), L(l.x, l.y), R(r.x, r.y);
            const ImU32 edge = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(10, 10, 12, 220);
            const float thickness = selected ? 2.0f : 1.0f;
            dl->AddTriangleFilled(A, L, B, fill);
            dl->AddTriangleFilled(A, B, R, fill);
            dl->AddLine(A, L, edge, thickness);
            dl->AddLine(L, B, edge, thickness);
            dl->AddLine(B, R, edge, thickness);
            dl->AddLine(R, A, edge, thickness);
            dl->AddCircleFilled(A, 4.5f, fill, 14);
            dl->AddCircle(A, 4.5f, edge, 14, thickness);
            dl->AddCircleFilled(B, 3.0f, edge, 10);
        }
    }

    // ------------------------------------------------------------------ Ortak

    void SpriteEditorPanel::SetMode(EditorMode mode)
    {
        if (mode == m_Mode)
            return;
        m_Mode = mode;
        m_Drag = DragMode::None;
        m_SkinDrag = SkinDrag::None;
        m_PolyDragging = m_PolyCreating = m_BonePlacing = false;
        m_PolyPath = m_PolyVertex = -1;
        if (mode != EditorMode::Sprite && mode != EditorMode::Secondary)
        {
            EnsureSingleSprite();
            if (m_Selected < 0 && !m_Sheet.Sprites.empty())
                m_Selected = 0;
        }
    }

    void SpriteEditorPanel::SelectSprite(int index)
    {
        if (index == m_Selected || index >= (int)m_Sheet.Sprites.size())
            return;
        m_Selected = index;
        m_PolyPath = m_PolyVertex = -1;
        m_SelectedBone = -1;
        m_SelectedVerts.clear();
        m_BonePlacing = false;
        m_PoseRotation.clear();
        m_PoseOffset.clear();
    }

    void SpriteEditorPanel::ValidateModeSelection()
    {
        m_PolyPath = m_PolyVertex = -1;
        m_SelectedVerts.clear();
        m_SkinDrag = SkinDrag::None;
        if (m_Selected < 0 || m_Selected >= (int)m_Sheet.Sprites.size())
        {
            m_SelectedBone = -1;
            return;
        }
        m_SelectedBone = std::min(m_SelectedBone, (int)m_Sheet.Sprites[(size_t)m_Selected].Skin.Bones.size() - 1);
    }

    // Unity: dilimlenmemiş doku tek sprite'tır; değişiklik yapılmadıkça dosyaya yazılmaz
    void SpriteEditorPanel::EnsureSingleSprite()
    {
        if (m_ReadOnly || !m_Sheet.Sprites.empty() || m_Width <= 0 || m_Height <= 0)
            return;
        SpriteDef d;
        d.Rect = glm::vec4(0.0f, 0.0f, (float)m_Width, (float)m_Height);
        d.Name = m_TexturePath.stem().string();
        m_Sheet.Sprites.push_back(d);
        m_Selected = 0;
    }

    int SpriteEditorPanel::SpriteAt(const glm::vec2& t) const
    {
        int hit = -1;
        float best = 1e30f;
        for (int i = 0; i < (int)m_Sheet.Sprites.size(); ++i)
        {
            const glm::vec4& r = m_Sheet.Sprites[(size_t)i].Rect;
            if (t.x >= r.x && t.x <= r.x + r.z && t.y >= r.y && t.y <= r.y + r.w && r.z * r.w < best)
            {
                best = r.z * r.w;
                hit = i;
            }
        }
        return hit;
    }

    void SpriteEditorPanel::DrawModeCanvas(bool hovered)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyAlt)))
            m_Drag = DragMode::Pan;
        if (m_Drag == DragMode::Pan)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_Pan += glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
            else
                m_Drag = DragMode::None;
        }
        const bool canEdit = hovered && m_Drag != DragMode::Pan;

        for (int i = 0; i < (int)m_Sheet.Sprites.size(); ++i)
        {
            const glm::vec4& r = m_Sheet.Sprites[(size_t)i].Rect;
            const glm::vec2 a = TexToScreen({ r.x, r.y + r.w }), b = TexToScreen({ r.x + r.z, r.y });
            const bool sel = i == m_Selected;
            dl->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), sel ? IM_COL32(80, 170, 255, 255) : IM_COL32(255, 255, 255, 90), 0.0f, 0, sel ? 1.5f : 1.0f);
        }

        if (m_Mode == EditorMode::Outline || m_Mode == EditorMode::Physics)
            DrawPolygonMode(dl, canEdit);
        else if (m_Mode == EditorMode::Skin)
            DrawSkinMode(dl, canEdit);
    }

    void SpriteEditorPanel::HandleModeShortcuts()
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_BonePlacing = false;
        if (m_ReadOnly || !ImGui::IsKeyPressed(ImGuiKey_Delete, false) || m_Selected < 0 || m_Selected >= (int)m_Sheet.Sprites.size())
            return;
        SpriteDef& s = m_Sheet.Sprites[(size_t)m_Selected];
        if (m_Mode == EditorMode::Outline || m_Mode == EditorMode::Physics)
        {
            auto& polys = m_Mode == EditorMode::Outline ? s.Outlines : s.PhysicsShapes;
            if (m_PolyPath < 0 || m_PolyPath >= (int)polys.size() || m_PolyVertex < 0 || m_PolyVertex >= (int)polys[(size_t)m_PolyPath].size())
                return;
            PushUndo();
            auto& poly = polys[(size_t)m_PolyPath];
            poly.erase(poly.begin() + m_PolyVertex);
            if (poly.size() < 3)
                polys.erase(polys.begin() + m_PolyPath);
            m_PolyPath = m_PolyVertex = -1;
            MarkDirty();
        }
        else if (m_Mode == EditorMode::Skin)
        {
            const bool geometryTool = m_SkinTool == SkinTool::EditGeometry || m_SkinTool == SkinTool::CreateVertex;
            if (geometryTool && !m_SelectedVerts.empty())
            {
                PushUndo();
                std::vector<int> order = m_SelectedVerts;
                std::sort(order.begin(), order.end(), std::greater<int>());
                for (int v : order)
                    if (v < (int)s.Skin.Vertices.size())
                        RemoveVertex(s.Skin, (uint32_t)v);
                m_SelectedVerts.clear();
                MarkDirty();
            }
            else if (!geometryTool && m_SelectedBone >= 0 && m_SelectedBone < (int)s.Skin.Bones.size())
            {
                PushUndo();
                RemoveBone(s.Skin, m_SelectedBone);
                m_SelectedBone = -1;
                EnsurePoseSize(s.Skin);
                MarkDirty();
            }
        }
    }

    // ------------------------------------------------------------------ Custom Outline / Custom Physics Shape

    void SpriteEditorPanel::GenerateShapes(float tolerance)
    {
        if (m_ReadOnly || m_Pixels.empty() || (m_Mode != EditorMode::Outline && m_Mode != EditorMode::Physics))
            return;
        EnsureSingleSprite();
        PushUndo();
        auto generate = [&](SpriteDef& s) {
            auto shapes = SpriteSheet::GenerateOutline(m_Pixels.data(), m_Width, m_Height, s.Rect, tolerance);
            (m_Mode == EditorMode::Outline ? s.Outlines : s.PhysicsShapes) = std::move(shapes);
        };
        if (m_Selected >= 0 && m_Selected < (int)m_Sheet.Sprites.size())
            generate(m_Sheet.Sprites[(size_t)m_Selected]);
        else
            for (auto& s : m_Sheet.Sprites)
                generate(s);
        m_PolyPath = m_PolyVertex = -1;
        MarkDirty();
    }

    void SpriteEditorPanel::DrawPolygonToolbar()
    {
        const bool hasSelection = m_Selected >= 0 && m_Selected < (int)m_Sheet.Sprites.size();
        auto current = [&]() -> std::vector<std::vector<glm::vec2>>& {
            SpriteDef& s = m_Sheet.Sprites[(size_t)m_Selected];
            return m_Mode == EditorMode::Outline ? s.Outlines : s.PhysicsShapes;
        };
        ImGui::Checkbox("Snap", &m_Snap);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Snap points to whole pixels");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_WAND_MAGIC_SPARKLES " Generate " ICON_FA_CARET_DOWN))
            ImGui::OpenPopup("##GenerateShapes");
        if (ImGui::BeginPopup("##GenerateShapes"))
        {
            ImGui::TextDisabled("Outline Tolerance");
            ImGui::SetNextItemWidth(220.0f);
            ImGui::SliderFloat("##tolerance", &m_OutlineTolerance, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("0: few points, 1: follows the pixels closely");
            if (UI::AccentButton("Generate", ImVec2(-1, 0)))
            {
                GenerateShapes(m_OutlineTolerance);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasSelection);
        if (ImGui::Button(ICON_FA_COPY " Copy") && hasSelection)
        {
            m_PolyClipboard = current();
            m_HasPolyClipboard = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasSelection || !m_HasPolyClipboard);
        if (ImGui::Button(ICON_FA_PASTE " Paste") && hasSelection && m_HasPolyClipboard)
        {
            PushUndo();
            current() = m_PolyClipboard;
            m_PolyPath = m_PolyVertex = -1;
            MarkDirty();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_HasPolyClipboard || m_Sheet.Sprites.empty());
        if (ImGui::Button("Paste All") && m_HasPolyClipboard)
        {
            PushUndo();
            for (auto& s : m_Sheet.Sprites)
                (m_Mode == EditorMode::Outline ? s.Outlines : s.PhysicsShapes) = m_PolyClipboard;
            m_PolyPath = m_PolyVertex = -1;
            MarkDirty();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasSelection || (hasSelection && current().empty()));
        if (ImGui::Button(ICON_FA_TRASH " Clear") && hasSelection)
        {
            PushUndo();
            current().clear();
            m_PolyPath = m_PolyVertex = -1;
            MarkDirty();
        }
        ImGui::EndDisabled();
    }

    void SpriteEditorPanel::DrawPolygonMode(ImDrawList* dl, bool hovered)
    {
        ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 mouseTex = ScreenToTex({ io.MousePos.x, io.MousePos.y });
        const glm::vec2 mouseScreen(io.MousePos.x, io.MousePos.y);
        const bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt;
        if (m_Selected < 0 || m_Selected >= (int)m_Sheet.Sprites.size())
        {
            if (pressed)
                SelectSprite(SpriteAt(mouseTex));
            return;
        }
        SpriteDef& s = m_Sheet.Sprites[(size_t)m_Selected];
        auto& polys = m_Mode == EditorMode::Outline ? s.Outlines : s.PhysicsShapes;
        const glm::vec2 origin(s.Rect.x, s.Rect.y), size(s.Rect.z, s.Rect.w);
        const glm::vec2 mouseLocal = mouseTex - origin;
        const bool inside = mouseLocal.x >= 0.0f && mouseLocal.y >= 0.0f && mouseLocal.x <= size.x && mouseLocal.y <= size.y;
        auto snap = [&](glm::vec2 p) {
            if (m_Snap)
                p = glm::round(p);
            return glm::clamp(p, glm::vec2(0.0f), size);
        };
        auto toScreen = [&](const glm::vec2& p) { return TexToScreen(origin + p); };
        const ImU32 lineColor = m_Mode == EditorMode::Outline ? IM_COL32(255, 205, 60, 255) : IM_COL32(90, 235, 120, 255);

        // Fare altındaki köşe / kenar
        int hoverPath = -1, hoverVertex = -1, edgePath = -1, edgeIndex = -1;
        glm::vec2 edgeLocal(0.0f);
        if (hovered && !m_ReadOnly && !m_PolyDragging && !m_PolyCreating)
        {
            float best = 8.0f;
            for (int p = 0; p < (int)polys.size(); ++p)
                for (int v = 0; v < (int)polys[(size_t)p].size(); ++v)
                {
                    const float d = glm::length(mouseScreen - toScreen(polys[(size_t)p][(size_t)v]));
                    if (d < best)
                    {
                        best = d;
                        hoverPath = p;
                        hoverVertex = v;
                    }
                }
            if (hoverVertex < 0)
            {
                float bestEdge = 5.0f;
                for (int p = 0; p < (int)polys.size(); ++p)
                {
                    const auto& poly = polys[(size_t)p];
                    for (int i = 0; i < (int)poly.size(); ++i)
                    {
                        glm::vec2 q;
                        const float d = SegmentDistance(mouseScreen, toScreen(poly[(size_t)i]), toScreen(poly[(size_t)((i + 1) % (int)poly.size())]), &q);
                        if (d < bestEdge)
                        {
                            bestEdge = d;
                            edgePath = p;
                            edgeIndex = i;
                            edgeLocal = ScreenToTex(q) - origin;
                        }
                    }
                }
            }
        }

        // Çizim
        for (const auto& poly : polys)
            for (size_t i = 0; i < poly.size(); ++i)
            {
                const glm::vec2 a = toScreen(poly[i]), b = toScreen(poly[(i + 1) % poly.size()]);
                dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), IM_COL32(10, 10, 12, 200), 3.5f);
                dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), lineColor, 1.6f);
            }
        for (int p = 0; p < (int)polys.size(); ++p)
            for (int v = 0; v < (int)polys[(size_t)p].size(); ++v)
            {
                const glm::vec2 q = toScreen(polys[(size_t)p][(size_t)v]);
                const bool hot = p == hoverPath && v == hoverVertex, sel = p == m_PolyPath && v == m_PolyVertex;
                const ImU32 col = sel ? IM_COL32(80, 170, 255, 255) : hot ? IM_COL32(255, 255, 255, 255) : lineColor;
                const float r = sel || hot ? 5.0f : 4.0f;
                dl->AddRectFilled(ImVec2(q.x - r, q.y - r), ImVec2(q.x + r, q.y + r), col);
                dl->AddRect(ImVec2(q.x - r, q.y - r), ImVec2(q.x + r, q.y + r), IM_COL32(10, 10, 12, 255));
            }
        if (edgeIndex >= 0)
        {
            const glm::vec2 q = toScreen(edgeLocal);
            dl->AddRectFilled(ImVec2(q.x - 4.0f, q.y - 4.0f), ImVec2(q.x + 4.0f, q.y + 4.0f), IM_COL32(255, 255, 255, 130));
            dl->AddRect(ImVec2(q.x - 4.0f, q.y - 4.0f), ImVec2(q.x + 4.0f, q.y + 4.0f), IM_COL32(10, 10, 12, 255));
        }
        if (hoverVertex >= 0 || m_PolyDragging)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        else if (edgeIndex >= 0)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // Basış
        if (pressed)
        {
            m_DragPushedUndo = false;
            if (hoverVertex >= 0)
            {
                m_PolyPath = hoverPath;
                m_PolyVertex = hoverVertex;
                m_PolyDragging = true;
            }
            else if (edgeIndex >= 0)
            {
                PushUndo();
                m_DragPushedUndo = true;
                auto& poly = polys[(size_t)edgePath];
                poly.insert(poly.begin() + edgeIndex + 1, snap(edgeLocal));
                m_PolyPath = edgePath;
                m_PolyVertex = edgeIndex + 1;
                m_PolyDragging = true;
                MarkDirty();
            }
            else if (inside && !m_ReadOnly)
            {
                m_PolyCreating = true;
                m_PolyCreateStart = snap(mouseLocal);
                m_PolyPath = m_PolyVertex = -1;
            }
            else if (!inside)
            {
                const int hit = SpriteAt(mouseTex);
                if (hit >= 0)
                    SelectSprite(hit);
                else
                    m_PolyPath = m_PolyVertex = -1;
            }
        }

        // Köşe sürükleme
        if (m_PolyDragging)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_PolyPath < 0 || m_PolyPath >= (int)polys.size() ||
                m_PolyVertex < 0 || m_PolyVertex >= (int)polys[(size_t)m_PolyPath].size())
                m_PolyDragging = false;
            else
            {
                const glm::vec2 p = snap(mouseLocal);
                if (p != polys[(size_t)m_PolyPath][(size_t)m_PolyVertex])
                {
                    if (!m_DragPushedUndo)
                    {
                        PushUndo();
                        m_DragPushedUndo = true;
                    }
                    polys[(size_t)m_PolyPath][(size_t)m_PolyVertex] = p;
                    MarkDirty();
                }
            }
        }

        // Boş alanda sürükle: yeni dörtgen şekil
        if (m_PolyCreating)
        {
            const glm::vec2 a = m_PolyCreateStart, b = snap(mouseLocal);
            const glm::vec2 sa = toScreen(a), sb = toScreen(b);
            dl->AddRect(ImVec2(std::min(sa.x, sb.x), std::min(sa.y, sb.y)), ImVec2(std::max(sa.x, sb.x), std::max(sa.y, sb.y)), lineColor, 0.0f, 0, 1.5f);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                m_PolyCreating = false;
                if (std::abs(b.x - a.x) >= 2.0f && std::abs(b.y - a.y) >= 2.0f)
                {
                    const glm::vec2 mn = glm::min(a, b), mx = glm::max(a, b);
                    PushUndo();
                    polys.push_back({ { mn.x, mn.y }, { mx.x, mn.y }, { mx.x, mx.y }, { mn.x, mx.y } });
                    MarkDirty();
                }
            }
        }
    }

    void SpriteEditorPanel::DrawPolygonSidebar()
    {
        const bool outline = m_Mode == EditorMode::Outline;
        if (!UI::BeginSection(outline ? "Custom Outline" : "Custom Physics Shape", true, ICON_FA_DRAW_POLYGON))
            return;
        if (m_Selected < 0 || m_Selected >= (int)m_Sheet.Sprites.size())
        {
            ImGui::TextDisabled("Select a sprite.");
            UI::EndSection();
            return;
        }
        SpriteDef& s = m_Sheet.Sprites[(size_t)m_Selected];
        auto& polys = outline ? s.Outlines : s.PhysicsShapes;
        size_t points = 0;
        for (const auto& poly : polys)
            points += poly.size();
        UI::Text("Sprite", "%s", s.Name.c_str());
        UI::Text("Shapes", "%zu (%zu points)", polys.size(), points);
        if (m_PolyPath >= 0 && m_PolyPath < (int)polys.size() && m_PolyVertex >= 0 && m_PolyVertex < (int)polys[(size_t)m_PolyPath].size())
        {
            ImGui::BeginDisabled(m_ReadOnly);
            glm::vec2 p = polys[(size_t)m_PolyPath][(size_t)m_PolyVertex];
            if (UI::Vec2("Point", p, 1.0f))
            {
                if (m_Snap)
                    p = glm::round(p);
                polys[(size_t)m_PolyPath][(size_t)m_PolyVertex] = glm::clamp(p, glm::vec2(0.0f), glm::vec2(s.Rect.z, s.Rect.w));
                MarkDirty();
            }
            ImGui::EndDisabled();
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled(outline ? "The sprite is drawn only inside these shapes (less overdraw). Without shapes the full rectangle is used."
                                    : "Polygon Collider 2D uses these shapes when it is added to the sprite (or with Reset From Sprite).");
        ImGui::TextDisabled("Drag points to move them, click an edge to add a point, Delete removes the selected point, drag inside the sprite to add a new shape.");
        ImGui::PopTextWrapPos();
        UI::EndSection();
    }

    // ------------------------------------------------------------------ Secondary Textures

    void SpriteEditorPanel::AddSecondaryTexture(const std::string& name, const std::string& path)
    {
        if (m_ReadOnly)
            return;
        m_Sheet.SecondaryTextures.push_back({ name, PortablePath(path) });
        MarkDirty();
    }

    void SpriteEditorPanel::DrawSecondarySidebar()
    {
        if (!UI::BeginSection("Secondary Textures", true, ICON_FA_LAYER_GROUP))
            return;
        static const char* const kSuggested[] = { "_NormalMap", "_MaskTex" };
        auto& list = m_Sheet.SecondaryTextures;
        ImGui::BeginDisabled(m_ReadOnly);
        int remove = -1;
        for (int i = 0; i < (int)list.size(); ++i)
        {
            ImGui::PushID(i);
            auto& t = list[(size_t)i];
            const bool selected = i == m_SecondarySelected;
            if (ImGui::Selectable(selected ? ICON_FA_EYE " Shown on the canvas" : ICON_FA_EYE_SLASH " Show on the canvas", selected))
                m_SecondarySelected = selected ? -1 : i;
            if (UI::InputText("Name", t.Name, "Shader property: _NormalMap (2D lights) or _MaskTex"))
                MarkDirty();
            UI::BeginProperty("##suggest", nullptr);
            if (ImGui::BeginCombo("##names", "Suggested names"))
            {
                for (const char* n : kSuggested)
                    if (ImGui::Selectable(n, t.Name == n))
                    {
                        t.Name = n;
                        MarkDirty();
                    }
                ImGui::EndCombo();
            }
            UI::EndProperty();
            UI::BeginProperty("Texture", "Drag an image from the Content Browser");
            const std::string label = t.Path.empty() ? std::string("None") : std::filesystem::path(t.Path).filename().string();
            ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4.0f, 0));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    t.Path = PortablePath((const char*)p->Data);
                    MarkDirty();
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine(0, 4);
            if (std::string picked; ObjectPicker::Button("secondarypick", "Texture", ObjectPicker::ImageExtensions(), t.Path, picked))
            {
                t.Path = PortablePath(picked);
                MarkDirty();
            }
            UI::EndProperty();
            if (ImGui::SmallButton(ICON_FA_MINUS " Remove"))
                remove = i;
            ImGui::Separator();
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            list.erase(list.begin() + remove);
            m_SecondarySelected = -1;
            MarkDirty();
        }
        if (ImGui::Button(ICON_FA_PLUS " Add Secondary Texture", ImVec2(-1, 0)))
        {
            list.push_back({ "", "" });
            MarkDirty();
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("_NormalMap: Point 2D lights with Use Normal Map shade this sprite by the light direction.");
        ImGui::PopTextWrapPos();
        UI::EndSection();

        // Kanvasta gösterilecek doku
        const std::string want = m_SecondarySelected >= 0 && m_SecondarySelected < (int)list.size() ? list[(size_t)m_SecondarySelected].Path : std::string();
        if (want != m_SecondaryPreviewPath)
        {
            m_SecondaryPreviewPath = want;
            m_SecondaryPreview = want.empty() ? nullptr : Texture2D::Create(SceneSerializer::ResolveAssetPath(want));
        }
    }

    // ------------------------------------------------------------------ Skinning Editor

    void SpriteEditorPanel::SetSkinTool(SkinTool tool)
    {
        m_SkinTool = tool;
        m_SkinDrag = SkinDrag::None;
        m_BonePlacing = false;
    }

    std::string SpriteEditorPanel::UniqueBoneName(const SpriteSkinData& skin) const
    {
        for (int i = 1;; ++i)
        {
            const std::string name = "bone_" + std::to_string(i);
            bool used = false;
            for (const auto& b : skin.Bones)
                used = used || b.Name == name;
            if (!used)
                return name;
        }
    }

    void SpriteEditorPanel::EnsurePoseSize(const SpriteSkinData& skin)
    {
        m_PoseRotation.resize(skin.Bones.size(), 0.0f);
        m_PoseOffset.resize(skin.Bones.size(), glm::vec2(0.0f));
    }

    std::vector<glm::mat3> SpriteEditorPanel::SkinPoseMatrices(const SpriteSkinData& skin) const
    {
        const int n = (int)skin.Bones.size();
        std::vector<glm::mat3> m((size_t)n, glm::mat3(1.0f));
        std::vector<char> done((size_t)n, 0);
        std::function<void(int)> solve = [&](int i) {
            if (done[(size_t)i])
                return;
            done[(size_t)i] = 1;
            const SpriteBone& b = skin.Bones[(size_t)i];
            glm::mat3 parent(1.0f);
            if (b.Parent >= 0 && b.Parent < n)
            {
                solve(b.Parent);
                parent = m[(size_t)b.Parent];
            }
            const float angle = (size_t)i < m_PoseRotation.size() ? m_PoseRotation[(size_t)i] : 0.0f;
            const glm::vec2 offset = (size_t)i < m_PoseOffset.size() ? m_PoseOffset[(size_t)i] : glm::vec2(0.0f);
            m[(size_t)i] = parent * Mat3Translate(b.Position + offset) * Mat3Rotate(angle) * Mat3Translate(-b.Position);
        };
        for (int i = 0; i < n; ++i)
            solve(i);
        return m;
    }

    int SpriteEditorPanel::SkinAddBone(const glm::vec2& start, const glm::vec2& tip, int parent)
    {
        if (m_ReadOnly)
            return -1;
        EnsureSingleSprite();
        if (m_Selected < 0 || m_Selected >= (int)m_Sheet.Sprites.size())
            return -1;
        PushUndo();
        SpriteSkinData& skin = m_Sheet.Sprites[(size_t)m_Selected].Skin;
        SpriteBone b;
        b.Name = UniqueBoneName(skin);
        b.Parent = parent >= 0 && parent < (int)skin.Bones.size() ? parent : -1;
        b.Position = start;
        b.Tip = tip;
        skin.Bones.push_back(b);
        EnsurePoseSize(skin);
        MarkDirty();
        return (int)skin.Bones.size() - 1;
    }

    void SpriteEditorPanel::SkinAutoGeometry(int density, int alphaTolerance)
    {
        if (m_ReadOnly || m_Pixels.empty())
            return;
        EnsureSingleSprite();
        if (m_Selected < 0 || m_Selected >= (int)m_Sheet.Sprites.size())
            return;
        PushUndo();
        SpriteDef& s = m_Sheet.Sprites[(size_t)m_Selected];
        SpriteSkinData& skin = s.Skin;
        const int x0 = std::clamp((int)s.Rect.x, 0, m_Width), y0 = std::clamp((int)s.Rect.y, 0, m_Height);
        const int w = std::min((int)s.Rect.z, m_Width - x0), h = std::min((int)s.Rect.w, m_Height - y0);
        skin.Vertices.clear();
        skin.Indices.clear();
        skin.BoneIndex.clear();
        skin.BoneWeight.clear();
        m_SelectedVerts.clear();
        if (w > 0 && h > 0)
        {
            density = std::clamp(density, 1, 128);
            const float aspect = (float)w / (float)h;
            const int cols = w >= h ? density : std::max(1, (int)std::round((float)density * aspect));
            const int rows = h >= w ? density : std::max(1, (int)std::round((float)density / aspect));
            // Saydam olmayan piksel içeren hücreler ağa girer
            auto solid = [&](int cx, int cy) {
                const int px0 = x0 + cx * w / cols, px1 = x0 + (cx + 1) * w / cols;
                const int py0 = y0 + cy * h / rows, py1 = y0 + (cy + 1) * h / rows;
                for (int y = py0; y < py1; ++y)
                {
                    const int row = m_Height - 1 - y;
                    for (int x = px0; x < px1; ++x)
                        if (m_Pixels[((size_t)row * m_Width + x) * 4 + 3] > alphaTolerance)
                            return true;
                }
                return false;
            };
            std::vector<int> map((size_t)(cols + 1) * (rows + 1), -1);
            auto vertex = [&](int cx, int cy) -> uint32_t {
                int& id = map[(size_t)cy * (cols + 1) + cx];
                if (id < 0)
                {
                    id = (int)skin.Vertices.size();
                    skin.Vertices.emplace_back((float)(cx * w / cols), (float)(cy * h / rows));
                }
                return (uint32_t)id;
            };
            for (int cy = 0; cy < rows; ++cy)
                for (int cx = 0; cx < cols; ++cx)
                {
                    if (!solid(cx, cy))
                        continue;
                    const uint32_t a = vertex(cx, cy), b = vertex(cx + 1, cy), c = vertex(cx + 1, cy + 1), d = vertex(cx, cy + 1);
                    for (uint32_t i : { a, b, c, a, c, d })
                        skin.Indices.push_back(i);
                }
        }
        skin.BoneIndex.assign(skin.Vertices.size(), glm::ivec4(-1));
        skin.BoneWeight.assign(skin.Vertices.size(), glm::vec4(0.0f));
        if (m_GeometryWeights)
            for (size_t v = 0; v < skin.Vertices.size(); ++v)
                AutoWeightVertex(skin, v);
        MarkDirty();
    }

    void SpriteEditorPanel::SkinAutoWeights()
    {
        if (m_ReadOnly || m_Selected < 0 || m_Selected >= (int)m_Sheet.Sprites.size())
            return;
        PushUndo();
        SpriteSkinData& skin = m_Sheet.Sprites[(size_t)m_Selected].Skin;
        for (size_t v = 0; v < skin.Vertices.size(); ++v)
            AutoWeightVertex(skin, v);
        MarkDirty();
    }

    void SpriteEditorPanel::SkinSetPose(int bone, float degrees)
    {
        if (m_Selected < 0 || m_Selected >= (int)m_Sheet.Sprites.size())
            return;
        EnsurePoseSize(m_Sheet.Sprites[(size_t)m_Selected].Skin);
        if (bone >= 0 && bone < (int)m_PoseRotation.size())
            m_PoseRotation[(size_t)bone] = glm::radians(degrees);
    }

    void SpriteEditorPanel::SkinResetPose()
    {
        std::fill(m_PoseRotation.begin(), m_PoseRotation.end(), 0.0f);
        std::fill(m_PoseOffset.begin(), m_PoseOffset.end(), glm::vec2(0.0f));
    }

    void SpriteEditorPanel::DrawSkinMode(ImDrawList* dl, bool hovered)
    {
        ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 mouseTex = ScreenToTex({ io.MousePos.x, io.MousePos.y });
        const glm::vec2 mouseScreen(io.MousePos.x, io.MousePos.y);
        const bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt;
        const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (m_Selected < 0 || m_Selected >= (int)m_Sheet.Sprites.size())
        {
            if (pressed)
                SelectSprite(SpriteAt(mouseTex));
            return;
        }
        SpriteDef& s = m_Sheet.Sprites[(size_t)m_Selected];
        SpriteSkinData& skin = s.Skin;
        EnsurePoseSize(skin);
        const glm::vec2 origin(s.Rect.x, s.Rect.y), size(std::max(s.Rect.z, 1.0f), std::max(s.Rect.w, 1.0f));
        const glm::vec2 mouseLocal = mouseTex - origin;
        auto toScreen = [&](const glm::vec2& p) { return TexToScreen(origin + p); };
        auto im = [](const glm::vec2& p) { return ImVec2(p.x, p.y); };

        const bool posing = m_SkinTool == SkinTool::PreviewPose;
        const bool showWeights = m_SkinTool == SkinTool::AutoWeights || m_SkinTool == SkinTool::WeightSlider || m_SkinTool == SkinTool::WeightBrush;
        const bool showMesh = m_SkinTool == SkinTool::AutoGeometry || m_SkinTool == SkinTool::EditGeometry || m_SkinTool == SkinTool::CreateVertex || showWeights;

        const std::vector<glm::mat3> pose = SkinPoseMatrices(skin);
        const int boneCount = (int)skin.Bones.size();
        const size_t vertexCount = skin.Vertices.size();

        // Pozlu köşeler ve kemikler (yalnız Preview Pose'ta; diğer araçlarda bağlama pozu)
        std::vector<glm::vec2> verts = skin.Vertices;
        if (posing)
            for (size_t v = 0; v < vertexCount; ++v)
            {
                glm::vec2 acc(0.0f);
                float total = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    const int b = skin.BoneIndex[v][k];
                    const float w = skin.BoneWeight[v][k];
                    if (b < 0 || b >= boneCount || w <= 0.0f)
                        continue;
                    acc += TransformPoint(pose[(size_t)b], skin.Vertices[v]) * w;
                    total += w;
                }
                if (total > 0.0f)
                    verts[v] = acc / total;
            }
        std::vector<glm::vec2> boneA((size_t)boneCount), boneB((size_t)boneCount);
        for (int i = 0; i < boneCount; ++i)
        {
            const SpriteBone& b = skin.Bones[(size_t)i];
            boneA[(size_t)i] = posing ? TransformPoint(pose[(size_t)i], b.Position) : b.Position;
            boneB[(size_t)i] = posing ? TransformPoint(pose[(size_t)i], b.Tip) : b.Tip;
        }

        // Dokulu ağ
        const ImTextureID textureId = (ImTextureID)(uint64_t)m_Texture->GetRendererID();
        auto uvOf = [&](uint32_t v) {
            const glm::vec2 t = origin + skin.Vertices[v];
            return ImVec2(t.x / (float)m_Width, t.y / (float)m_Height);
        };
        for (size_t t = 0; t + 2 < skin.Indices.size(); t += 3)
        {
            const uint32_t a = skin.Indices[t], b = skin.Indices[t + 1], c = skin.Indices[t + 2];
            if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
                continue;
            const ImVec2 pc = im(toScreen(verts[c])), uc = uvOf(c);
            dl->AddImageQuad(textureId, im(toScreen(verts[a])), im(toScreen(verts[b])), pc, pc, uvOf(a), uvOf(b), uc, uc);
        }

        // Ağırlıklar: köşe rengi, kemik renklerinin ağırlıklı karışımı
        if (showWeights)
        {
            auto vertexColor = [&](uint32_t v) -> ImU32 {
                glm::vec3 c(0.0f);
                float total = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    const int b = skin.BoneIndex[v][k];
                    const float w = skin.BoneWeight[v][k];
                    if (b < 0 || w <= 0.0f)
                        continue;
                    const ImVec4 col = ImGui::ColorConvertU32ToFloat4(BoneColor(b));
                    c += glm::vec3(col.x, col.y, col.z) * w;
                    total += w;
                }
                if (total <= 0.0f)
                    return IM_COL32(120, 120, 120, 90);
                c /= total;
                return IM_COL32((int)(c.r * 255.0f), (int)(c.g * 255.0f), (int)(c.b * 255.0f), 150);
            };
            const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
            for (size_t t = 0; t + 2 < skin.Indices.size(); t += 3)
            {
                const uint32_t a = skin.Indices[t], b = skin.Indices[t + 1], c = skin.Indices[t + 2];
                if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
                    continue;
                dl->PrimReserve(3, 3);
                dl->PrimVtx(im(toScreen(verts[a])), white, vertexColor(a));
                dl->PrimVtx(im(toScreen(verts[b])), white, vertexColor(b));
                dl->PrimVtx(im(toScreen(verts[c])), white, vertexColor(c));
            }
        }

        // Tel kafes ve köşeler
        if (showMesh)
        {
            for (size_t t = 0; t + 2 < skin.Indices.size(); t += 3)
            {
                const uint32_t a = skin.Indices[t], b = skin.Indices[t + 1], c = skin.Indices[t + 2];
                if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
                    continue;
                dl->AddTriangle(im(toScreen(verts[a])), im(toScreen(verts[b])), im(toScreen(verts[c])), IM_COL32(255, 255, 255, 70));
            }
            for (int v = 0; v < (int)vertexCount; ++v)
            {
                const bool sel = std::find(m_SelectedVerts.begin(), m_SelectedVerts.end(), v) != m_SelectedVerts.end();
                dl->AddCircleFilled(im(toScreen(verts[(size_t)v])), sel ? 4.5f : 3.0f, sel ? IM_COL32(80, 170, 255, 255) : IM_COL32(255, 255, 255, 220), 8);
            }
        }

        // Fare altındaki kemik: 1 eklem, 2 uç, 3 gövde
        int hoverBone = -1, hoverPart = 0;
        if (hovered && m_SkinDrag == SkinDrag::None)
        {
            float best = 9.0f;
            for (int i = 0; i < boneCount; ++i)
            {
                const float tip = glm::length(mouseScreen - toScreen(boneB[(size_t)i]));
                if (tip < best)
                {
                    best = tip;
                    hoverBone = i;
                    hoverPart = 2;
                }
                const float joint = glm::length(mouseScreen - toScreen(boneA[(size_t)i]));
                if (joint < best)
                {
                    best = joint;
                    hoverBone = i;
                    hoverPart = 1;
                }
            }
            if (hoverBone < 0)
            {
                float bestBody = 6.0f;
                for (int i = 0; i < boneCount; ++i)
                {
                    const float d = SegmentDistance(mouseScreen, toScreen(boneA[(size_t)i]), toScreen(boneB[(size_t)i]));
                    if (d < bestBody)
                    {
                        bestBody = d;
                        hoverBone = i;
                        hoverPart = 3;
                    }
                }
            }
        }

        // Kemikler
        const bool boneTool = posing || m_SkinTool == SkinTool::EditBone || m_SkinTool == SkinTool::CreateBone || m_SkinTool == SkinTool::SplitBone;
        for (int i = 0; i < boneCount; ++i)
        {
            const float alpha = i == hoverBone && boneTool ? 1.0f : 0.72f;
            DrawBoneShape(dl, toScreen(boneA[(size_t)i]), toScreen(boneB[(size_t)i]), BoneColor(i, alpha), i == m_SelectedBone);
        }

        const bool editable = !m_ReadOnly;
        const bool validBone = m_SelectedBone >= 0 && m_SelectedBone < boneCount;

        // Sürükleme bitti: kutu seçimi uygula
        if (m_SkinDrag != SkinDrag::None && !down)
        {
            if (m_SkinDrag == SkinDrag::BoxSelect)
            {
                const glm::vec2 mn = glm::min(m_BoxStart, mouseLocal), mx = glm::max(m_BoxStart, mouseLocal);
                if (!io.KeyCtrl)
                    m_SelectedVerts.clear();
                for (int v = 0; v < (int)vertexCount; ++v)
                {
                    const glm::vec2& p = skin.Vertices[(size_t)v];
                    if (p.x >= mn.x && p.y >= mn.y && p.x <= mx.x && p.y <= mx.y &&
                        std::find(m_SelectedVerts.begin(), m_SelectedVerts.end(), v) == m_SelectedVerts.end())
                        m_SelectedVerts.push_back(v);
                }
            }
            m_SkinDrag = SkinDrag::None;
        }

        switch (m_SkinTool)
        {
            case SkinTool::PreviewPose:
            {
                if (pressed && hoverBone >= 0)
                {
                    m_SelectedBone = hoverBone;
                    m_SkinDrag = hoverPart == 1 ? SkinDrag::PoseMove : SkinDrag::PoseRotate;
                    m_SkinDragLast = mouseLocal;
                }
                if (validBone && m_SkinDrag == SkinDrag::PoseRotate)
                {
                    const glm::vec2 pivot = boneA[(size_t)m_SelectedBone];
                    const glm::vec2 a = m_SkinDragLast - pivot, b = mouseLocal - pivot;
                    if (glm::length(a) > 1e-3f && glm::length(b) > 1e-3f)
                        m_PoseRotation[(size_t)m_SelectedBone] += std::atan2(a.x * b.y - a.y * b.x, glm::dot(a, b));
                    m_SkinDragLast = mouseLocal;
                }
                else if (validBone && m_SkinDrag == SkinDrag::PoseMove)
                {
                    const glm::vec2 d = mouseLocal - m_SkinDragLast;
                    const int parent = skin.Bones[(size_t)m_SelectedBone].Parent;
                    glm::vec2 local = d;
                    if (parent >= 0 && parent < boneCount)
                    {
                        const glm::mat3& pm = pose[(size_t)parent];
                        local = glm::vec2(glm::dot(d, glm::vec2(pm[0])), glm::dot(d, glm::vec2(pm[1])));
                    }
                    m_PoseOffset[(size_t)m_SelectedBone] += local;
                    m_SkinDragLast = mouseLocal;
                }
                break;
            }
            case SkinTool::EditBone:
            {
                if (pressed)
                {
                    m_SelectedBone = hoverBone;
                    if (hoverBone >= 0 && editable)
                    {
                        m_SkinDrag = hoverPart == 1 ? SkinDrag::Joint : hoverPart == 2 ? SkinDrag::Tip : SkinDrag::Body;
                        m_SkinDragLast = mouseLocal;
                        m_DragPushedUndo = false;
                    }
                }
                if (m_SelectedBone >= 0 && m_SelectedBone < boneCount && (m_SkinDrag == SkinDrag::Joint || m_SkinDrag == SkinDrag::Tip || m_SkinDrag == SkinDrag::Body))
                {
                    const glm::vec2 d = mouseLocal - m_SkinDragLast;
                    if (d.x != 0.0f || d.y != 0.0f)
                    {
                        if (!m_DragPushedUndo)
                        {
                            PushUndo();
                            m_DragPushedUndo = true;
                        }
                        SpriteBone& b = skin.Bones[(size_t)m_SelectedBone];
                        auto connected = [](const glm::vec2& p, const glm::vec2& q) { return glm::length(p - q) < 0.5f; };
                        if (m_SkinDrag == SkinDrag::Joint)
                        {
                            if (b.Parent >= 0 && b.Parent < boneCount && connected(skin.Bones[(size_t)b.Parent].Tip, b.Position))
                                skin.Bones[(size_t)b.Parent].Tip += d;
                            b.Position += d;
                        }
                        else if (m_SkinDrag == SkinDrag::Tip)
                        {
                            for (auto& c : skin.Bones)
                                if (c.Parent == m_SelectedBone && connected(c.Position, b.Tip))
                                    c.Position += d;
                            b.Tip += d;
                        }
                        else
                        {
                            // Gövde: kemik ve altındaki zincir birlikte taşınır
                            std::vector<int> group{ m_SelectedBone };
                            for (size_t g = 0; g < group.size(); ++g)
                                for (int c = 0; c < boneCount; ++c)
                                    if (skin.Bones[(size_t)c].Parent == group[g] && std::find(group.begin(), group.end(), c) == group.end())
                                        group.push_back(c);
                            for (int g : group)
                            {
                                skin.Bones[(size_t)g].Position += d;
                                skin.Bones[(size_t)g].Tip += d;
                            }
                        }
                        m_SkinDragLast = mouseLocal;
                        MarkDirty();
                    }
                }
                break;
            }
            case SkinTool::CreateBone:
            {
                if (!editable)
                    break;
                if (m_BonePlacing)
                {
                    const glm::vec2 a = toScreen(m_BoneStart);
                    dl->AddLine(im(a), io.MousePos, IM_COL32(80, 170, 255, 255), 2.0f);
                    dl->AddCircleFilled(im(a), 4.5f, IM_COL32(80, 170, 255, 255), 14);
                }
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    m_BonePlacing = false;
                auto finish = [&]() {
                    if (glm::length(mouseLocal - m_BoneStart) * m_Zoom < 4.0f)
                        return;
                    PushUndo();
                    SpriteBone b;
                    b.Name = UniqueBoneName(skin);
                    b.Parent = m_BoneParent;
                    b.Position = m_BoneStart;
                    b.Tip = mouseLocal;
                    skin.Bones.push_back(b);
                    EnsurePoseSize(skin);
                    m_SelectedBone = (int)skin.Bones.size() - 1;
                    m_BoneParent = m_SelectedBone;      // zincir: sonraki kemik bunun ucundan başlar
                    m_BoneStart = mouseLocal;
                    MarkDirty();
                };
                if (pressed)
                {
                    m_BonePressScreen = io.MousePos;
                    if (m_BonePlacing)
                        finish();
                    else
                    {
                        // Bir kemiğin ucuna yakınsa ona eklenir, değilse seçili kemiğin çocuğu (seçim yoksa kök)
                        m_BoneStart = mouseLocal;
                        m_BoneParent = m_SelectedBone >= 0 && m_SelectedBone < boneCount ? m_SelectedBone : -1;
                        if (hoverBone >= 0 && hoverPart == 2)
                        {
                            m_BoneStart = skin.Bones[(size_t)hoverBone].Tip;
                            m_BoneParent = hoverBone;
                        }
                        m_BonePlacing = true;
                    }
                }
                else if (m_BonePlacing && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                         glm::length(glm::vec2(io.MousePos.x - m_BonePressScreen.x, io.MousePos.y - m_BonePressScreen.y)) > 6.0f)
                    finish();
                break;
            }
            case SkinTool::SplitBone:
            {
                if (pressed && editable && hoverBone >= 0)
                {
                    glm::vec2 q;
                    SegmentDistance(mouseLocal, skin.Bones[(size_t)hoverBone].Position, skin.Bones[(size_t)hoverBone].Tip, &q);
                    PushUndo();
                    const int id = (int)skin.Bones.size();
                    SpriteBone nb;
                    nb.Name = UniqueBoneName(skin);
                    nb.Parent = hoverBone;
                    nb.Position = q;
                    nb.Tip = skin.Bones[(size_t)hoverBone].Tip;
                    for (auto& c : skin.Bones)
                        if (c.Parent == hoverBone && glm::length(c.Position - nb.Tip) < 0.5f)
                            c.Parent = id;
                    skin.Bones[(size_t)hoverBone].Tip = q;
                    skin.Bones.push_back(nb);
                    EnsurePoseSize(skin);
                    m_SelectedBone = id;
                    MarkDirty();
                }
                break;
            }
            case SkinTool::EditGeometry:
            case SkinTool::WeightSlider:
            {
                const bool canMove = m_SkinTool == SkinTool::EditGeometry && editable;
                int hoverVertex = -1;
                if (hovered && m_SkinDrag == SkinDrag::None)
                {
                    float best = 8.0f;
                    for (int v = 0; v < (int)vertexCount; ++v)
                    {
                        const float d = glm::length(mouseScreen - toScreen(skin.Vertices[(size_t)v]));
                        if (d < best)
                        {
                            best = d;
                            hoverVertex = v;
                        }
                    }
                }
                if (pressed)
                {
                    m_SkinDragLast = mouseLocal;
                    m_DragPushedUndo = false;
                    if (hoverVertex >= 0)
                    {
                        const auto it = std::find(m_SelectedVerts.begin(), m_SelectedVerts.end(), hoverVertex);
                        if (io.KeyCtrl)
                        {
                            if (it == m_SelectedVerts.end()) m_SelectedVerts.push_back(hoverVertex);
                            else m_SelectedVerts.erase(it);
                        }
                        else if (it == m_SelectedVerts.end())
                            m_SelectedVerts.assign(1, hoverVertex);
                        if (canMove)
                            m_SkinDrag = SkinDrag::Vertices;
                    }
                    else
                    {
                        m_BoxStart = mouseLocal;
                        m_SkinDrag = SkinDrag::BoxSelect;
                    }
                }
                if (m_SkinDrag == SkinDrag::Vertices)
                {
                    const glm::vec2 d = mouseLocal - m_SkinDragLast;
                    if ((d.x != 0.0f || d.y != 0.0f) && !m_SelectedVerts.empty())
                    {
                        if (!m_DragPushedUndo)
                        {
                            PushUndo();
                            m_DragPushedUndo = true;
                        }
                        for (int v : m_SelectedVerts)
                            if (v < (int)vertexCount)
                                skin.Vertices[(size_t)v] = glm::clamp(skin.Vertices[(size_t)v] + d, glm::vec2(0.0f), size);
                        m_SkinDragLast = mouseLocal;
                        MarkDirty();
                    }
                }
                if (m_SkinDrag == SkinDrag::BoxSelect)
                {
                    const glm::vec2 a = toScreen(m_BoxStart);
                    dl->AddRectFilled(ImVec2(std::min(a.x, mouseScreen.x), std::min(a.y, mouseScreen.y)), ImVec2(std::max(a.x, mouseScreen.x), std::max(a.y, mouseScreen.y)), IM_COL32(80, 170, 255, 40));
                    dl->AddRect(ImVec2(std::min(a.x, mouseScreen.x), std::min(a.y, mouseScreen.y)), ImVec2(std::max(a.x, mouseScreen.x), std::max(a.y, mouseScreen.y)), IM_COL32(80, 170, 255, 255));
                }
                break;
            }
            case SkinTool::CreateVertex:
            {
                if (!pressed || !editable || vertexCount == 0)
                    break;
                // Kenara yakınsa kenarı böler, üçgenin içindeyse üçgeni üçe böler
                uint32_t edgeA = 0, edgeB = 0;
                bool onEdge = false;
                glm::vec2 edgePoint(0.0f);
                float best = 6.0f;
                for (size_t t = 0; t + 2 < skin.Indices.size(); t += 3)
                    for (int e = 0; e < 3; ++e)
                    {
                        const uint32_t a = skin.Indices[t + (size_t)e], b = skin.Indices[t + (size_t)((e + 1) % 3)];
                        glm::vec2 q;
                        const float d = SegmentDistance(mouseScreen, toScreen(skin.Vertices[a]), toScreen(skin.Vertices[b]), &q);
                        if (d < best)
                        {
                            best = d;
                            edgeA = a;
                            edgeB = b;
                            onEdge = true;
                            edgePoint = ScreenToTex(q) - origin;
                        }
                    }
                if (onEdge)
                {
                    PushUndo();
                    const uint32_t p = (uint32_t)AddMeshVertex(skin, glm::clamp(edgePoint, glm::vec2(0.0f), size));
                    std::vector<uint32_t> next;
                    next.reserve(skin.Indices.size() + 6);
                    for (size_t t = 0; t + 2 < skin.Indices.size(); t += 3)
                    {
                        const uint32_t tri[3] = { skin.Indices[t], skin.Indices[t + 1], skin.Indices[t + 2] };
                        int e = -1;
                        for (int k = 0; k < 3; ++k)
                            if ((tri[k] == edgeA && tri[(k + 1) % 3] == edgeB) || (tri[k] == edgeB && tri[(k + 1) % 3] == edgeA))
                                e = k;
                        if (e < 0)
                        {
                            next.insert(next.end(), tri, tri + 3);
                            continue;
                        }
                        const uint32_t x = tri[e], y = tri[(e + 1) % 3], c = tri[(e + 2) % 3];
                        for (uint32_t i : { x, p, c, p, y, c })
                            next.push_back(i);
                    }
                    skin.Indices = std::move(next);
                    m_SelectedVerts.assign(1, (int)p);
                    MarkDirty();
                }
                else
                    for (size_t t = 0; t + 2 < skin.Indices.size(); t += 3)
                    {
                        const uint32_t a = skin.Indices[t], b = skin.Indices[t + 1], c = skin.Indices[t + 2];
                        if (!PointInTriangle(mouseLocal, skin.Vertices[a], skin.Vertices[b], skin.Vertices[c]))
                            continue;
                        PushUndo();
                        const uint32_t p = (uint32_t)AddMeshVertex(skin, mouseLocal);
                        skin.Indices.erase(skin.Indices.begin() + (std::ptrdiff_t)t, skin.Indices.begin() + (std::ptrdiff_t)t + 3);
                        for (uint32_t i : { a, b, p, b, c, p, c, a, p })
                            skin.Indices.push_back(i);
                        m_SelectedVerts.assign(1, (int)p);
                        MarkDirty();
                        break;
                    }
                break;
            }
            case SkinTool::WeightBrush:
            {
                if (!hovered && m_SkinDrag != SkinDrag::Brush)
                    break;
                dl->AddCircle(io.MousePos, m_BrushSize * m_Zoom, IM_COL32(255, 255, 255, 220), 40, 1.5f);
                if (pressed && editable && validBone)
                {
                    PushUndo();
                    m_SkinDrag = SkinDrag::Brush;
                }
                if (m_SkinDrag == SkinDrag::Brush && validBone)
                {
                    const float radius = std::max(m_BrushSize, 0.01f);
                    const float inner = radius * m_BrushHardness;
                    const auto adj = m_BrushMode == 2 ? VertexNeighbours(skin) : std::vector<std::vector<int>>{};
                    for (size_t v = 0; v < vertexCount; ++v)
                    {
                        const float d = glm::length(skin.Vertices[v] - mouseLocal);
                        if (d > radius)
                            continue;
                        const float falloff = d <= inner || radius <= inner ? 1.0f : 1.0f - (d - inner) / (radius - inner);
                        const float amount = m_BrushStep * falloff * std::min(io.DeltaTime * 8.0f, 1.0f);
                        if (m_BrushMode == 2)
                            SmoothVertex(skin, v, adj, std::clamp(amount, 0.0f, 1.0f));
                        else
                        {
                            const float current = VertexWeight(skin.BoneIndex[v], skin.BoneWeight[v], m_SelectedBone);
                            SetVertexWeight(skin.BoneIndex[v], skin.BoneWeight[v], m_SelectedBone, current + (m_BrushMode == 0 ? amount : -amount));
                        }
                    }
                    MarkDirty();
                }
                break;
            }
            default:
                break;
        }
    }

    void SpriteEditorPanel::DrawSkinSidebar()
    {
        if (m_Selected < 0 || m_Selected >= (int)m_Sheet.Sprites.size())
        {
            ImGui::TextDisabled("Select a sprite.");
            return;
        }
        SpriteDef& s = m_Sheet.Sprites[(size_t)m_Selected];
        SpriteSkinData& skin = s.Skin;
        EnsurePoseSize(skin);

        if (UI::BeginSection("Tools", true, ICON_FA_BONE))
        {
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float w = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
            auto tool = [&](const char* label, SkinTool t) {
                if (m_SkinTool == t)
                    UI::AccentButton(label, ImVec2(w, 0));
                else if (ImGui::Button(label, ImVec2(w, 0)))
                    SetSkinTool(t);
            };
            ImGui::TextDisabled("Pose");
            tool("Preview", SkinTool::PreviewPose);
            ImGui::SameLine();
            if (ImGui::Button("Reset Pose", ImVec2(w, 0)))
                SkinResetPose();
            ImGui::TextDisabled("Bones");
            tool("Edit Bone", SkinTool::EditBone);
            ImGui::SameLine();
            tool("Create Bone", SkinTool::CreateBone);
            ImGui::SameLine();
            tool("Split Bone", SkinTool::SplitBone);
            ImGui::TextDisabled("Geometry");
            tool("Auto Geom.", SkinTool::AutoGeometry);
            ImGui::SameLine();
            tool("Edit Geom.", SkinTool::EditGeometry);
            ImGui::SameLine();
            tool("Add Vertex", SkinTool::CreateVertex);
            ImGui::TextDisabled("Weights");
            tool("Auto", SkinTool::AutoWeights);
            ImGui::SameLine();
            tool("Slider", SkinTool::WeightSlider);
            ImGui::SameLine();
            tool("Brush", SkinTool::WeightBrush);
            ImGui::Spacing();
            UI::EndSection();
        }

        const bool validBone = m_SelectedBone >= 0 && m_SelectedBone < (int)skin.Bones.size();
        const char* hint = nullptr;
        ImGui::BeginDisabled(m_ReadOnly);
        switch (m_SkinTool)
        {
            case SkinTool::PreviewPose: hint = "Drag a bone to rotate it, drag its joint to move it. The pose is only a preview and is not saved."; break;
            case SkinTool::EditBone: hint = "Drag the joint, the tip or the body of a bone. Delete removes the selected bone."; break;
            case SkinTool::CreateBone: hint = "Click to start a bone and click again to end it; the chain continues from its tip. Right click or Esc stops."; break;
            case SkinTool::SplitBone: hint = "Click a bone to split it in two."; break;
            case SkinTool::AutoGeometry:
                if (UI::BeginSection("Auto Geometry", true))
                {
                    UI::SliderInt("Density", m_Density, 1, 64, "Cells along the longest side of the sprite");
                    UI::SliderInt("Alpha Tolerance", m_AlphaTolerance, 0, 254, "Pixels above this alpha count as part of the sprite");
                    UI::Checkbox("Weights", m_GeometryWeights, "Also calculate the weights");
                    if (UI::AccentButton("Generate", ImVec2(-1, 0)))
                        SkinAutoGeometry(m_Density, m_AlphaTolerance);
                    UI::EndSection();
                }
                break;
            case SkinTool::EditGeometry: hint = "Drag vertices to move them, drag on empty space for a box selection, Ctrl adds to the selection, Delete removes vertices."; break;
            case SkinTool::CreateVertex: hint = "Click a triangle or an edge to add a vertex there. Use Auto Geometry first."; break;
            case SkinTool::AutoWeights:
                if (UI::BeginSection("Auto Weights", true))
                {
                    ImGui::BeginDisabled(skin.Vertices.empty() || skin.Bones.empty());
                    if (UI::AccentButton("Generate", ImVec2(-1, 0)))
                        SkinAutoWeights();
                    ImGui::EndDisabled();
                    if (skin.Vertices.empty() || skin.Bones.empty())
                        ImGui::TextDisabled("Needs bones and geometry.");
                    UI::EndSection();
                }
                break;
            case SkinTool::WeightSlider:
                if (UI::BeginSection("Weight Slider", true))
                {
                    static const char* const kModes[] = { "Add & Subtract", "Smooth" };
                    UI::Combo("Mode", m_SliderMode, kModes, 2);
                    UI::BeginProperty("Amount", "Applies to the selected bone on the selected vertices (all vertices when none is selected)");
                    const float before = m_SliderValue;
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::BeginDisabled(m_SliderMode == 0 && !validBone);
                    const bool changed = ImGui::SliderFloat("##amount", &m_SliderValue, m_SliderMode == 0 ? -1.0f : 0.0f, 1.0f, "%.2f");
                    const bool active = ImGui::IsItemActive();
                    ImGui::EndDisabled();
                    UI::EndProperty();
                    if (changed)
                    {
                        if (!m_SliderPushedUndo)
                        {
                            PushUndo();
                            m_SliderPushedUndo = true;
                        }
                        const float delta = m_SliderValue - before;
                        const auto adj = m_SliderMode == 1 ? VertexNeighbours(skin) : std::vector<std::vector<int>>{};
                        auto apply = [&](size_t v) {
                            if (m_SliderMode == 1)
                                SmoothVertex(skin, v, adj, std::clamp(delta, 0.0f, 1.0f));
                            else if (validBone)
                                SetVertexWeight(skin.BoneIndex[v], skin.BoneWeight[v], m_SelectedBone,
                                                VertexWeight(skin.BoneIndex[v], skin.BoneWeight[v], m_SelectedBone) + delta);
                        };
                        if (m_SelectedVerts.empty())
                            for (size_t v = 0; v < skin.Vertices.size(); ++v)
                                apply(v);
                        else
                            for (int v : m_SelectedVerts)
                                if (v < (int)skin.Vertices.size())
                                    apply((size_t)v);
                        MarkDirty();
                    }
                    if (!active)
                    {
                        m_SliderValue = 0.0f;
                        m_SliderPushedUndo = false;
                    }
                    UI::EndSection();
                }
                hint = "Click vertices or drag a box to choose where the slider applies.";
                break;
            case SkinTool::WeightBrush:
                if (UI::BeginSection("Weight Brush", true))
                {
                    static const char* const kModes[] = { "Add", "Subtract", "Smooth" };
                    UI::Combo("Mode", m_BrushMode, kModes, 3);
                    UI::DragFloat("Size", m_BrushSize, 0.5f, 1.0f, 2048.0f, "%.0f px");
                    UI::SliderFloat("Hardness", m_BrushHardness, 0.0f, 1.0f);
                    UI::SliderFloat("Step", m_BrushStep, 0.01f, 1.0f);
                    if (!validBone && m_BrushMode != 2)
                        ImGui::TextDisabled("Select a bone in the list below.");
                    UI::EndSection();
                }
                break;
            default:
                break;
        }
        ImGui::EndDisabled();
        if (hint)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", hint);
            ImGui::PopTextWrapPos();
        }

        if (UI::BeginSection("Bones", true, ICON_FA_BONE))
        {
            ImGui::BeginChild("##bones", ImVec2(0, 112), ImGuiChildFlags_Borders);
            for (int i = 0; i < (int)skin.Bones.size(); ++i)
            {
                ImGui::PushID(i);
                const SpriteBone& b = skin.Bones[(size_t)i];
                int depth = 0;
                for (int p = b.Parent; p >= 0 && p < (int)skin.Bones.size() && depth < 16; p = skin.Bones[(size_t)p].Parent)
                    ++depth;
                const float h = ImGui::GetTextLineHeight() + 4.0f;
                const ImVec2 cur = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##bone", i == m_SelectedBone, 0, ImVec2(0, h)))
                    m_SelectedBone = i;
                const float x = cur.x + 8.0f + (float)depth * 12.0f;
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(x, cur.y + h * 0.5f), 4.5f, BoneColor(i), 12);
                ImGui::GetWindowDrawList()->AddText(ImVec2(x + 9.0f, cur.y + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), b.Name.c_str());
                ImGui::PopID();
            }
            if (skin.Bones.empty())
                ImGui::TextDisabled("No bones. Use Create Bone.");
            ImGui::EndChild();
            if (validBone)
            {
                ImGui::BeginDisabled(m_ReadOnly);
                SpriteBone& b = skin.Bones[(size_t)m_SelectedBone];
                std::string name = b.Name;
                if (UI::InputText("Name", name, "Bone 2D object name; must be unique within the sprite") && !name.empty())
                {
                    bool used = false;
                    for (int i = 0; i < (int)skin.Bones.size(); ++i)
                        used = used || (i != m_SelectedBone && skin.Bones[(size_t)i].Name == name);
                    if (!used)
                    {
                        b.Name = name;
                        MarkDirty();
                    }
                }
                UI::Text("Parent", "%s", b.Parent >= 0 && b.Parent < (int)skin.Bones.size() ? skin.Bones[(size_t)b.Parent].Name.c_str() : "None");
                if (UI::Vec2("Position", b.Position, 1.0f))
                    MarkDirty();
                if (UI::Vec2("Tip", b.Tip, 1.0f))
                    MarkDirty();
                if (UI::DangerButton(ICON_FA_TRASH " Delete Bone", ImVec2(-1, 0)))
                {
                    PushUndo();
                    RemoveBone(skin, m_SelectedBone);
                    m_SelectedBone = -1;
                    EnsurePoseSize(skin);
                    MarkDirty();
                }
                ImGui::EndDisabled();
            }
            UI::Text("Mesh", "%zu vertices, %zu triangles", skin.Vertices.size(), skin.Indices.size() / 3);
            UI::EndSection();
        }
    }
}
