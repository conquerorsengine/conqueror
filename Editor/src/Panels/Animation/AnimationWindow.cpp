#include "AnimationWindow.h"

#include "EditorState.h"
#include "UI/EditorUI.h"
#include "UndoRedo/AssetFileCommand.h"
#include "UndoRedo/EntitySnapshotCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include "Core/Project/Project.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"

#include <IconsFontAwesome6.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>

namespace Conqueror::Editor
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr float kRowH = 22.0f;
        constexpr float kRulerH = 24.0f;
        constexpr float kEventsH = 20.0f;
        constexpr float kBottomH = 30.0f;

        struct RowInfo
        {
            int Curve = -1;   // -1: özet satırı (bütün anahtarlar)
            int Comp = -1;    // -1: ana satır, 0..3: bileşen alt satırı (x / y / z / w)
        };
        std::vector<RowInfo> s_Rows;

        bool AnimatableKind(FieldKind k)
        {
            switch (k)
            {
                case FieldKind::Float: case FieldKind::Angle: case FieldKind::Int: case FieldKind::Bool:
                case FieldKind::Vec2: case FieldKind::Vec3: case FieldKind::Vec4: case FieldKind::Color3: case FieldKind::Color4:
                    return true;
                default:
                    return false;
            }
        }

        int KindComponents(FieldKind k)
        {
            switch (k)
            {
                case FieldKind::Vec2: return 2;
                case FieldKind::Vec3: case FieldKind::Color3: return 3;
                case FieldKind::Vec4: case FieldKind::Color4: return 4;
                default: return 1;
            }
        }

        const ComponentField* FindRegistryField(const std::string& component, const std::string& field)
        {
            ComponentInfo* info = ComponentRegistry::Find(component);
            return info ? ComponentRegistry::FindField(*info, field) : nullptr;
        }

        ImU32 ComponentColor(int i, int count)
        {
            if (count == 1)
                return IM_COL32(236, 172, 76, 255);
            static const ImU32 c[] = { IM_COL32(232, 86, 86, 255), IM_COL32(112, 208, 92, 255), IM_COL32(92, 150, 240, 255), IM_COL32(205, 205, 205, 255) };
            return c[std::clamp(i, 0, 3)];
        }

        bool Differs(const glm::vec4& a, const glm::vec4& b)
        {
            for (int i = 0; i < 4; ++i)
                if (std::abs(a[i] - b[i]) > 1e-4f * std::max(1.0f, std::abs(b[i])))
                    return true;
            return false;
        }

        void Diamond(ImDrawList* dl, const ImVec2& c, float r, ImU32 fill, ImU32 border)
        {
            dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), fill);
            dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), border, 1.0f);
        }

        const char* ComponentSuffix(int i, bool color)
        {
            static const char* xyz[] = { "x", "y", "z", "w" };
            static const char* rgb[] = { "r", "g", "b", "a" };
            return color ? rgb[std::clamp(i, 0, 3)] : xyz[std::clamp(i, 0, 3)];
        }

        std::string ReadText(const std::string& path)
        {
            std::ifstream in(path, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        bool ToggleButton(const char* label, bool on, const ImVec4& onColor)
        {
            if (on)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, onColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(onColor.x * 1.15f, onColor.y * 1.15f, onColor.z * 1.15f, 1.0f));
            }
            const bool pressed = ImGui::Button(label);
            if (on)
                ImGui::PopStyleColor(2);
            return pressed;
        }
    }

    std::string AnimationWindow::Key(const std::string& path, const std::string& component, const std::string& field)
    {
        return path + '\x1f' + component + '\x1f' + field;
    }

    // ── Bağlam / hedef ──
    void AnimationWindow::SetContext(const std::shared_ptr<Scene>& scene)
    {
        if (scene == m_Scene)
            return;
        StopPreview();
        CommitEdit(true);
        m_Scene = scene;
        m_RootUUID = 0;
        m_LastSelection = 0;
        m_PendingRestoreUUID = 0;
        m_PendingRestore.clear();
    }

    Entity AnimationWindow::GetRoot() const
    {
        if (!m_Scene || !m_RootUUID)
            return {};
        return m_Scene->FindEntityByUUID(m_RootUUID);
    }

    void AnimationWindow::SyncTarget()
    {
        if (!m_Scene)
            return;
        if (m_PendingRestoreUUID)
            if (Entity back = m_Scene->FindEntityByUUID(m_PendingRestoreUUID))
            {
                // Silinen önizleme nesnesi geri alındı: silinirken canlandırılmış değerlerle saklanmıştı
                for (const auto& [key, o] : m_PendingRestore)
                    if (Entity t = PropertyClips::ResolvePath(back, o.Path))
                        PropertyClips::WriteValue(t, o.Component, o.Field, o.Value);
                m_PendingRestoreUUID = 0;
                m_PendingRestore.clear();
                m_Scene->UpdateWorldTransforms();
            }
        if (m_RootUUID && !GetRoot())
        {
            // Canlandırılan nesne silindi: değerleri geri alınırsa yazılmak üzere saklanır
            if (m_Previewing && !m_Originals.empty())
            {
                m_PendingRestoreUUID = m_RootUUID;
                m_PendingRestore = m_Originals;
            }
            m_Previewing = m_Recording = m_Playing = false;
            m_Originals.clear();
            m_Watch.clear();
            m_RootUUID = 0;
        }
        if (m_Lock)
            return;
        Entity sel = EditorState::Get().GetSelectedEntity();
        if (sel && sel.GetScene() != m_Scene.get())
            sel = {};
        const uint64_t selId = sel ? (uint64_t)sel.GetComponent<IDComponent>().ID : 0;
        if (selId == m_LastSelection)
            return;
        m_LastSelection = selId;
        if (!sel)
        {
            // Seçim kalktı (ya da bir varlık seçildi): önizleme biter, açık klip salt düzenleme için görünür kalır
            StopPreview();
            m_RootUUID = 0;
            return;
        }
        // Unity: canlandırılan kök Animation bileşenli en yakın üst nesne; alt nesne seçilince yolları kökten gösterilir
        Entity root = sel;
        for (Entity e = sel; e; e = e.GetParent())
            if (e.HasComponent<AnimationPlayerComponent>())
            {
                root = e;
                break;
            }
        const uint64_t rootId = (uint64_t)root.GetComponent<IDComponent>().ID;
        if (rootId == m_RootUUID)
            return;
        StopPreview();
        m_RootUUID = rootId;
        m_SelKeys.clear();
        m_SelEvent = -1;
        const auto clips = RootClips();
        if (clips.empty())
        {
            CommitEdit(true);
            m_ClipPath.clear();
            m_ClipFile.clear();
            m_ClipLoaded = false;
            m_Clip = PropertyClip{};
        }
        else if (std::find(clips.begin(), clips.end(), m_ClipPath) == clips.end())
            LoadClip(clips.front());
    }

    std::vector<std::string> AnimationWindow::RootClips() const
    {
        std::vector<std::string> out;
        Entity root = GetRoot();
        if (!root || !root.HasComponent<AnimationPlayerComponent>())
            return out;
        const auto& a = root.GetComponent<AnimationPlayerComponent>();
        auto add = [&](const std::string& p) {
            if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end())
                out.push_back(p);
        };
        add(a.Clip);
        for (const auto& c : a.Clips)
            add(c);
        return out;
    }

    void AnimationWindow::LoadClip(const std::string& assetPath)
    {
        CommitEdit(true);
        const bool wasPreviewing = m_Previewing;
        const bool wasRecording = m_Recording;
        StopPreview();
        m_ClipPath = assetPath;
        m_ClipFile = SceneSerializer::ResolveAssetPath(assetPath);
        m_Clip = PropertyClip{};
        m_LoadError.clear();
        m_ClipLoaded = PropertyClips::Load(m_ClipFile, m_Clip, &m_LoadError);
        std::error_code ec;
        m_ClipTime = fs::last_write_time(m_ClipFile, ec);
        m_SelKeys.clear();
        m_SelEvent = -1;
        m_SelRow = -1;
        m_FitPending = true;
        m_ApplyDirty = true;
        if (wasPreviewing)
        {
            StartPreview();
            if (wasRecording)
                SetRecording(true);
        }
    }

    void AnimationWindow::ReloadIfChanged()
    {
        if (m_ClipFile.empty() || m_EditOpen || m_Drag != Drag::None)
            return;
        std::error_code ec;
        const auto t = fs::last_write_time(m_ClipFile, ec);
        if (ec || t == m_ClipTime)
            return;
        m_ClipTime = t;
        // Geri al / yinele ya da Inspector dosyayı değiştirdi
        PropertyClip clip;
        if (PropertyClips::Load(m_ClipFile, clip, &m_LoadError))
        {
            m_Clip = std::move(clip);
            m_ClipLoaded = true;
            m_ApplyDirty = true;
        }
        m_SelKeys.erase(std::remove_if(m_SelKeys.begin(), m_SelKeys.end(), [&](const SelKey& s) { return s.Curve >= (int)m_Clip.Curves.size(); }),
                        m_SelKeys.end());
        if (m_SelEvent >= (int)m_Clip.Events.size())
            m_SelEvent = -1;
        if (m_SelRow >= (int)m_Clip.Curves.size())
            m_SelRow = -1;
    }

    void AnimationWindow::OpenClip(const std::string& assetPathIn)
    {
        std::string assetPath = assetPathIn;
        if (fs::path(assetPath).is_absolute())
            assetPath = SceneSerializer::ToAssetPath(assetPath);
        // Bu klibi kullanan seçili nesne varsa onunla; yoksa klip nesnesiz açılır (anahtarlar düzenlenir, önizleme olmaz)
        LoadClip(assetPath);
    }

    bool AnimationWindow::SelectClip(const std::string& assetPath, std::string* error)
    {
        std::string p = assetPath;
        if (fs::path(p).is_absolute())
            p = SceneSerializer::ToAssetPath(p);
        std::error_code ec;
        if (!fs::exists(SceneSerializer::ResolveAssetPath(p), ec))
        {
            if (error) *error = "no clip " + assetPath;
            return false;
        }
        LoadClip(p);
        return m_ClipLoaded;
    }

    void AnimationWindow::CollectProps(Entity root, std::vector<Prop>& out) const
    {
        out.clear();
        if (!root)
            return;
        std::vector<Entity> stack{ root };
        while (!stack.empty())
        {
            Entity e = stack.back();
            stack.pop_back();
            const std::string path = e == root ? std::string() : PropertyClips::PathOf(root, e);
            for (const char* f : { "Position", "Rotation", "Scale" })
                out.push_back({ e, path, "Transform", f });
            for (auto& info : ComponentRegistry::All())
            {
                if (!info.Has || !info.Has(e) || info.TypeName == "AnimationPlayerComponent" || info.TypeName == "TransformComponent")
                    continue;
                for (const auto& f : info.Fields)
                    if (AnimatableKind(f.Kind) && f.Ptr && !f.ReadOnly && f.Serialize)
                        out.push_back({ e, path, info.TypeName, f.Name });
            }
            for (Entity c : e.GetChildren())
                stack.push_back(c);
        }
    }

    int AnimationWindow::ComponentCount(const AnimCurveBinding& c) const
    {
        if (c.Component == "Transform")
            return 3;
        const ComponentField* f = FindRegistryField(c.Component, c.Field);
        return f ? KindComponents(f->Kind) : 1;
    }

    bool AnimationWindow::IsColorBinding(const AnimCurveBinding& c) const
    {
        const ComponentField* f = c.Component == "Transform" ? nullptr : FindRegistryField(c.Component, c.Field);
        return f && (f->Kind == FieldKind::Color3 || f->Kind == FieldKind::Color4);
    }

    std::string AnimationWindow::RowLabel(const AnimCurveBinding& c) const
    {
        std::string prop;
        if (c.Component == "Transform")
            prop = c.Field;
        else
        {
            ComponentInfo* info = ComponentRegistry::Find(c.Component);
            const ComponentField* f = info ? ComponentRegistry::FindField(*info, c.Field) : nullptr;
            prop = (info ? info->Name : c.Component) + "." + (f ? f->Label : c.Field);
        }
        if (c.Path.empty())
            return prop;
        const size_t slash = c.Path.rfind('/');
        return (slash == std::string::npos ? c.Path : c.Path.substr(slash + 1)) + " : " + prop;
    }

    Entity AnimationWindow::Resolve(const AnimCurveBinding& c) const
    {
        Entity root = GetRoot();
        return root ? PropertyClips::ResolvePath(root, c.Path) : Entity{};
    }

    glm::vec4 AnimationWindow::CurrentValue(int curve) const
    {
        const auto& c = m_Clip.Curves[(size_t)curve];
        glm::vec4 live(0.0f);
        Entity t = Resolve(c);
        const bool has = t && PropertyClips::ReadValue(t, c.Component, c.Field, live);
        if (m_Previewing && has)
            return live;
        return EvaluateChannel(c.Channel, m_Time, live);
    }

    // ── Düzenleme / geri alma ──
    void AnimationWindow::BeginEdit()
    {
        if (m_EditOpen || m_ClipFile.empty())
            return;
        m_EditBefore = AssetFileCommand::ReadFile(m_ClipFile);
        m_EditOpen = true;
    }

    void AnimationWindow::MarkChanged()
    {
        m_Dirty = true;
        m_ApplyDirty = true;
    }

    void AnimationWindow::SaveClip()
    {
        if (m_ClipFile.empty())
            return;
        PropertyClips::Save(m_ClipFile, m_Clip);
        std::error_code ec;
        m_ClipTime = fs::last_write_time(m_ClipFile, ec);
    }

    void AnimationWindow::CommitEdit(bool force)
    {
        if (!m_EditOpen)
            return;
        if (!force && (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_Drag != Drag::None))
            return;
        if (m_Dirty)
            SaveClip();
        m_EditOpen = false;
        m_Dirty = false;
        std::string after = AssetFileCommand::ReadFile(m_ClipFile);
        if (after != m_EditBefore)
            UndoRedoManager::Get().Execute(std::make_shared<AssetFileCommand>(m_ClipFile, m_EditBefore, std::move(after),
                                                                                "Edit " + fs::path(m_ClipFile).filename().string()));
        m_EditBefore.clear();
    }

    float AnimationWindow::FrameTime() const
    {
        return 1.0f / std::max(1.0f, m_Clip.FrameRate);
    }

    float AnimationWindow::Snap(float t) const
    {
        const float fr = std::max(1.0f, m_Clip.FrameRate);
        return std::round(t * fr) / fr;
    }

    float AnimationWindow::ViewEnd() const
    {
        return std::max(m_Clip.Length, m_Clip.ContentEnd());
    }

    std::string AnimationWindow::FormatTime(float t) const
    {
        // Unity zaman biçimi: saniye:kare
        const int fps = std::max(1, (int)std::lround(m_Clip.FrameRate));
        const long frames = std::lround(std::max(0.0f, t) * (float)fps);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%ld:%02ld", frames / fps, frames % fps);
        return buf;
    }

    void AnimationWindow::SetKeyAt(int curve, float time, const glm::vec4& value)
    {
        if (curve < 0 || curve >= (int)m_Clip.Curves.size())
            return;
        auto& ch = m_Clip.Curves[(size_t)curve].Channel;
        const float t = std::max(0.0f, Snap(time));
        KeyInterpolation interp = KeyInterpolation::Smooth;
        for (const auto& k : ch.Keys)
            if (std::abs(k.Time - t) <= 0.5f * FrameTime())
                interp = k.Interp;   // var olan anahtarın ara değerlemesi korunur
        SetKey(ch, t, value, m_Clip.FrameRate, interp);
        m_Clip.Length = std::max(m_Clip.Length, t);
    }

    bool AnimationWindow::IsSelected(int curve, float time) const
    {
        for (const auto& s : m_SelKeys)
            if (s.Curve == curve && std::abs(s.Time - time) < 1e-4f)
                return true;
        return false;
    }

    void AnimationWindow::SelectKey(int curve, float time, bool additive)
    {
        if (!additive)
            m_SelKeys.clear();
        m_SelEvent = -1;
        auto addOne = [&](int c, float t) {
            if (!IsSelected(c, t))
                m_SelKeys.push_back({ c, t });
        };
        if (curve >= 0)
        {
            addOne(curve, time);
            return;
        }
        // Özet satırı: o andaki bütün anahtarlar
        const float half = 0.5f * FrameTime();
        for (int c = 0; c < (int)m_Clip.Curves.size(); ++c)
            for (const auto& k : m_Clip.Curves[(size_t)c].Channel.Keys)
                if (std::abs(k.Time - time) <= half)
                    addOne(c, k.Time);
    }

    std::vector<float> AnimationWindow::AllKeyTimes() const
    {
        std::vector<float> times;
        const float half = 0.5f * FrameTime();
        for (const auto& c : m_Clip.Curves)
            for (const auto& k : c.Channel.Keys)
                if (std::none_of(times.begin(), times.end(), [&](float x) { return std::abs(x - k.Time) <= half; }))
                    times.push_back(k.Time);
        std::sort(times.begin(), times.end());
        return times;
    }

    void AnimationWindow::DeleteSelection()
    {
        if (m_SelKeys.empty() && m_SelEvent >= 0 && m_SelEvent < (int)m_Clip.Events.size())
        {
            BeginEdit();
            m_Clip.Events.erase(m_Clip.Events.begin() + m_SelEvent);
            m_SelEvent = -1;
            MarkChanged();
            return;
        }
        if (m_SelKeys.empty())
            return;
        BeginEdit();
        for (const auto& s : m_SelKeys)
        {
            if (s.Curve < 0 || s.Curve >= (int)m_Clip.Curves.size())
                continue;
            auto& keys = m_Clip.Curves[(size_t)s.Curve].Channel.Keys;
            keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const TimelineKey& k) { return std::abs(k.Time - s.Time) < 1e-4f; }), keys.end());
        }
        m_SelKeys.clear();
        MarkChanged();
    }

    void AnimationWindow::SetSelectedInterpolation(KeyInterpolation interp)
    {
        if (m_SelKeys.empty())
            return;
        BeginEdit();
        for (const auto& s : m_SelKeys)
            if (s.Curve >= 0 && s.Curve < (int)m_Clip.Curves.size())
                for (auto& k : m_Clip.Curves[(size_t)s.Curve].Channel.Keys)
                    if (std::abs(k.Time - s.Time) < 1e-4f)
                        k.Interp = interp;
        MarkChanged();
    }

    void AnimationWindow::RemoveCurve(int curve)
    {
        if (curve < 0 || curve >= (int)m_Clip.Curves.size())
            return;
        BeginEdit();
        m_Clip.Curves.erase(m_Clip.Curves.begin() + curve);
        std::vector<SelKey> keep;
        for (const auto& s : m_SelKeys)
            if (s.Curve != curve)
                keep.push_back({ s.Curve > curve ? s.Curve - 1 : s.Curve, s.Time });
        m_SelKeys = std::move(keep);
        if (m_SelRow == curve)
            m_SelRow = -1;
        else if (m_SelRow > curve)
            --m_SelRow;
        MarkChanged();
    }

    void AnimationWindow::CopySelection()
    {
        m_Clipboard.clear();
        if (m_SelKeys.empty())
            return;
        float first = 1e9f;
        for (const auto& s : m_SelKeys)
            first = std::min(first, s.Time);
        for (const auto& s : m_SelKeys)
            if (s.Curve >= 0 && s.Curve < (int)m_Clip.Curves.size())
                for (const auto& k : m_Clip.Curves[(size_t)s.Curve].Channel.Keys)
                    if (std::abs(k.Time - s.Time) < 1e-4f)
                    {
                        TimelineKey copy = k;
                        copy.Time -= first;
                        m_Clipboard.emplace_back(s.Curve, copy);
                    }
    }

    void AnimationWindow::PasteAtTime()
    {
        if (m_Clipboard.empty())
            return;
        BeginEdit();
        m_SelKeys.clear();
        for (const auto& [curve, key] : m_Clipboard)
        {
            if (curve < 0 || curve >= (int)m_Clip.Curves.size())
                continue;
            const float t = Snap(m_Time + key.Time);
            SetKey(m_Clip.Curves[(size_t)curve].Channel, t, key.Value, m_Clip.FrameRate, key.Interp);
            m_Clip.Length = std::max(m_Clip.Length, t);
            m_SelKeys.push_back({ curve, t });
        }
        MarkChanged();
    }

    // ── Dış işlemler (pencere düğmeleri ve otomasyon aynı yolu kullanır) ──
    bool AnimationWindow::CreateClip(Entity e, std::string* error)
    {
        if (!e || !m_Scene)
        {
            if (error) *error = "Select an object first";
            return false;
        }
        const fs::path projectDir = Project::GetActiveProjectDirectory();
        if (projectDir.empty())
        {
            if (error) *error = "No project";
            return false;
        }
        std::string name = e.GetComponent<TagComponent>().Tag;
        for (char& c : name)
            if (!std::isalnum((unsigned char)c) && c != '_' && c != '-' && c != ' ')
                c = '_';
        if (name.empty())
            name = "New Animation";
        std::error_code ec;
        fs::create_directories(projectDir / "Assets" / "Animations", ec);
        fs::path rel = fs::path("Assets") / "Animations" / (name + PropertyClips::Extension);
        for (int i = 1; fs::exists(projectDir / rel, ec); ++i)
            rel = fs::path("Assets") / "Animations" / (name + " " + std::to_string(i) + PropertyClips::Extension);
        PropertyClip clip;
        clip.Length = 1.0f;
        clip.FrameRate = 60.0f;
        clip.Loop = true;
        if (!PropertyClips::Save((projectDir / rel).string(), clip))
        {
            if (error) *error = "Could not write " + rel.generic_string();
            return false;
        }
        // Animation bileşeni (yoksa) + klip; nesne değişikliği Ctrl+Z ile geri alınır
        const uint64_t id = (uint64_t)e.GetComponent<IDComponent>().ID;
        auto before = EntitySnapshotCommand::Capture(e);
        if (!e.HasComponent<AnimationPlayerComponent>())
            e.AddComponent<AnimationPlayerComponent>();
        auto& a = e.GetComponent<AnimationPlayerComponent>();
        if (a.Clip.empty())
            a.Clip = rel.generic_string();
        else
            a.Clips.push_back(rel.generic_string());
        UndoRedoManager::Get().Execute(std::make_shared<EntitySnapshotCommand>(m_Scene, id, before, EntitySnapshotCommand::Capture(e),
                                                                               "Create Animation Clip"));
        // Önizleme açıksa yeni klipte de açık kalır (Unity)
        const bool wasPreviewing = m_Previewing && m_RootUUID == id;
        StopPreview();
        m_RootUUID = id;
        LoadClip(rel.generic_string());
        if (wasPreviewing)
            StartPreview();
        m_Status = "Created " + rel.generic_string();
        return true;
    }

    bool AnimationWindow::AddProperty(const std::string& path, const std::string& component, const std::string& field, std::string* error)
    {
        if (!m_ClipLoaded)
        {
            if (error) *error = "No clip";
            return false;
        }
        if (m_Clip.Find(path, component, field))
        {
            if (error) *error = "Already animated";
            return false;
        }
        glm::vec4 value(0.0f);
        Entity root = GetRoot();
        Entity target = root ? PropertyClips::ResolvePath(root, path) : Entity{};
        if (root && (!target || !PropertyClips::ReadValue(target, component, field, value)))
        {
            if (error) *error = "No property " + component + "." + field + " on '" + path + "'";
            return false;
        }
        BeginEdit();
        AnimCurveBinding b;
        b.Path = path;
        b.Component = component;
        b.Field = field;
        b.Channel.Name = field;
        // Unity: ilk ve son karede o anki değerle iki anahtar
        SetKey(b.Channel, 0.0f, value, m_Clip.FrameRate);
        if (m_Clip.Length > FrameTime() * 0.5f)
            SetKey(b.Channel, Snap(m_Clip.Length), value, m_Clip.FrameRate);
        m_Clip.Curves.push_back(std::move(b));
        m_SelRow = (int)m_Clip.Curves.size() - 1;
        MarkChanged();
        return true;
    }

    bool AnimationWindow::AddKeyAt(int curve, float time, std::string* error)
    {
        if (!m_ClipLoaded || m_Clip.Curves.empty())
        {
            if (error) *error = "No properties to key";
            return false;
        }
        if (curve >= (int)m_Clip.Curves.size())
        {
            if (error) *error = "No property row " + std::to_string(curve);
            return false;
        }
        BeginEdit();
        const float t = std::max(0.0f, Snap(time));
        auto valueAt = [&](int c) {
            const auto& b = m_Clip.Curves[(size_t)c];
            glm::vec4 live(0.0f);
            Entity target = Resolve(b);
            const bool has = target && PropertyClips::ReadValue(target, b.Component, b.Field, live);
            // Önizlemede o anki değer (kaydedilmemiş değişiklik de anahtar olur); değilse eğrinin o andaki değeri
            if (m_Previewing && has && std::abs(t - m_Time) < 1e-4f)
                return live;
            return EvaluateChannel(b.Channel, t, live);
        };
        m_SelKeys.clear();
        if (curve < 0)
            for (int c = 0; c < (int)m_Clip.Curves.size(); ++c)
            {
                SetKeyAt(c, t, valueAt(c));
                m_SelKeys.push_back({ c, t });
            }
        else
        {
            SetKeyAt(curve, t, valueAt(curve));
            m_SelKeys.push_back({ curve, t });
        }
        MarkChanged();
        return true;
    }

    bool AnimationWindow::AddEventAt(float time, const std::string& function, const std::string& argument, std::string* error)
    {
        if (!m_ClipLoaded)
        {
            if (error) *error = "No clip";
            return false;
        }
        BeginEdit();
        AnimEventDef e;
        e.Time = std::max(0.0f, Snap(time));
        e.Function = function;
        e.Argument = argument;
        m_Clip.Events.push_back(e);
        std::stable_sort(m_Clip.Events.begin(), m_Clip.Events.end(), [](const AnimEventDef& a, const AnimEventDef& b) { return a.Time < b.Time; });
        m_SelEvent = -1;
        for (int i = 0; i < (int)m_Clip.Events.size(); ++i)
            if (std::abs(m_Clip.Events[(size_t)i].Time - e.Time) < 1e-5f && m_Clip.Events[(size_t)i].Function == function)
                m_SelEvent = i;
        m_SelKeys.clear();
        m_Clip.Length = std::max(m_Clip.Length, e.Time);
        MarkChanged();
        return true;
    }

    void AnimationWindow::SetTime(float t)
    {
        m_Time = std::max(0.0f, Snap(t));
        if (EditorState::Get().IsEditing() && GetRoot() && m_ClipLoaded)
            StartPreview();   // Unity: oynatma kafasını sürüklemek önizlemeyi açar
    }

    void AnimationWindow::SetPreviewing(bool on)
    {
        if (on)
            StartPreview();
        else
            StopPreview();
    }

    void AnimationWindow::SetRecording(bool on)
    {
        if (on)
        {
            StartPreview();
            m_Recording = m_Previewing;
            if (m_Recording)
                RefreshWatch();
        }
        else
            m_Recording = false;
    }

    void AnimationWindow::SetPlaying(bool on)
    {
        if (on)
            StartPreview();
        m_Playing = on && m_Previewing;
    }

    // ── Önizleme / kayıt ──
    void AnimationWindow::StartPreview()
    {
        if (m_Previewing || !m_Scene || !m_ClipLoaded || !EditorState::Get().IsEditing())
            return;
        Entity root = GetRoot();
        if (!root)
            return;
        m_Originals.clear();
        m_Previewing = true;
        m_ApplyDirty = true;
        ApplyPreview(true);
    }

    void AnimationWindow::StopPreview()
    {
        if (!m_Previewing)
        {
            m_Recording = m_Playing = false;
            return;
        }
        m_Previewing = m_Recording = m_Playing = false;
        Entity root = GetRoot();
        if (root)
            for (const auto& [key, o] : m_Originals)
                if (Entity t = PropertyClips::ResolvePath(root, o.Path))
                    PropertyClips::WriteValue(t, o.Component, o.Field, o.Value);
        m_Originals.clear();
        m_Watch.clear();
        if (m_Scene)
            m_Scene->UpdateWorldTransforms();
    }

    void AnimationWindow::BeginSceneSave()
    {
        if (!m_Previewing)
            return;
        Entity root = GetRoot();
        if (root)
            for (const auto& [key, o] : m_Originals)
                if (Entity t = PropertyClips::ResolvePath(root, o.Path))
                    PropertyClips::WriteValue(t, o.Component, o.Field, o.Value);
        if (m_Scene)
            m_Scene->UpdateWorldTransforms();
    }

    void AnimationWindow::EndSceneSave()
    {
        if (!m_Previewing)
            return;
        m_ApplyDirty = true;
        ApplyPreview(true);
    }

    void AnimationWindow::ApplyPreview(bool force)
    {
        if (!m_Previewing)
            return;
        Entity root = GetRoot();
        if (!root || !m_ClipLoaded)
        {
            StopPreview();
            return;
        }
        // Artık canlandırılmayan özellik (silindi / geri alındı): eski değerine döner
        for (auto it = m_Originals.begin(); it != m_Originals.end();)
        {
            const Original& o = it->second;
            bool bound = false;
            for (const auto& c : m_Clip.Curves)
                if (c.Path == o.Path && c.Component == o.Component && c.Field == o.Field)
                    bound = true;
            if (!bound)
            {
                if (Entity t = PropertyClips::ResolvePath(root, o.Path))
                    PropertyClips::WriteValue(t, o.Component, o.Field, o.Value);
                it = m_Originals.erase(it);
                m_ApplyDirty = true;
            }
            else
                ++it;
        }
        // Yeni bağlanan özelliklerin önizleme öncesi değeri saklanır
        for (const auto& c : m_Clip.Curves)
        {
            const std::string k = Key(c.Path, c.Component, c.Field);
            if (m_Originals.count(k))
                continue;
            glm::vec4 v(0.0f);
            if (Entity t = PropertyClips::ResolvePath(root, c.Path); t && PropertyClips::ReadValue(t, c.Component, c.Field, v))
                m_Originals[k] = { c.Path, c.Component, c.Field, v };
        }
        if (!force && !m_ApplyDirty && std::abs(m_LastApplied - m_Time) < 1e-6f)
            return;
        PropertyClips::Apply(root, m_Clip, m_Time);
        m_Scene->UpdateWorldTransforms();
        m_LastApplied = m_Time;
        m_ApplyDirty = false;
        if (m_Recording)
            RefreshWatch();
    }

    void AnimationWindow::RefreshWatch()
    {
        m_Watch.clear();
        std::vector<Prop> props;
        CollectProps(GetRoot(), props);
        for (const auto& p : props)
        {
            glm::vec4 v(0.0f);
            if (PropertyClips::ReadValue(p.Target, p.Component, p.Field, v))
                m_Watch[Key(p.Path, p.Component, p.Field)] = v;
        }
    }

    void AnimationWindow::RecordChanges()
    {
        if (!m_Recording || !m_Previewing || m_Drag != Drag::None || !m_ClipLoaded)
            return;
        Entity root = GetRoot();
        if (!root)
            return;
        std::vector<Prop> props;
        CollectProps(root, props);
        bool any = false;
        const float t = std::max(0.0f, Snap(m_Time));
        for (const auto& p : props)
        {
            glm::vec4 v(0.0f);
            if (!PropertyClips::ReadValue(p.Target, p.Component, p.Field, v))
                continue;
            const std::string k = Key(p.Path, p.Component, p.Field);
            auto it = m_Watch.find(k);
            if (it == m_Watch.end())
            {
                m_Watch[k] = v;   // kayıt sırasında eklenen nesne / bileşen
                continue;
            }
            if (!Differs(v, it->second))
                continue;
            // Değişen özellik o karede anahtar olur (yeni eğriyse ilk karede eski değer de saklanır: Unity gibi)
            BeginEdit();
            int index = -1;
            for (int c = 0; c < (int)m_Clip.Curves.size(); ++c)
            {
                const auto& b = m_Clip.Curves[(size_t)c];
                if (b.Path == p.Path && b.Component == p.Component && b.Field == p.Field)
                    index = c;
            }
            if (index < 0)
            {
                if (!m_Originals.count(k))
                    m_Originals[k] = { p.Path, p.Component, p.Field, it->second };
                AnimCurveBinding b;
                b.Path = p.Path;
                b.Component = p.Component;
                b.Field = p.Field;
                b.Channel.Name = p.Field;
                if (t > 0.5f * FrameTime())
                    SetKey(b.Channel, 0.0f, it->second, m_Clip.FrameRate);
                m_Clip.Curves.push_back(std::move(b));
                index = (int)m_Clip.Curves.size() - 1;
            }
            SetKeyAt(index, t, v);
            it->second = v;
            any = true;
        }
        if (any)
        {
            m_Dirty = true;
            m_LastApplied = m_Time;   // canlı değerler zaten kaydedilenle aynı
        }
    }

    // ── Pencere ──
    void AnimationWindow::OnImGuiRender(bool* open, bool playing)
    {
        ImGui::SetNextWindowSize(ImVec2(980.0f, 420.0f), ImGuiCond_FirstUseEver);
        const bool visible = ImGui::Begin(ICON_FA_FILM " Animation###AnimationWindow", open);
        m_PlayMode = playing;
        if (playing && m_Previewing)
            StopPreview();
        SyncTarget();
        ReloadIfChanged();
        if (!visible)
        {
            CommitEdit(false);
            ImGui::End();
            return;
        }
        RecordChanges();
        if (m_Playing && m_Previewing)
        {
            const float end = std::max(ViewEnd(), FrameTime());
            m_Time += ImGui::GetIO().DeltaTime;
            if (m_Time > end)
                m_Time = m_Clip.Loop ? std::fmod(m_Time, end) : 0.0f;
        }

        DrawToolbar(playing);
        Entity root = GetRoot();
        if (m_ClipPath.empty())
            DrawPrompt(root);
        else if (!m_ClipLoaded)
            UI::InfoBox(("The clip could not be read: " + m_LoadError).c_str(), UI::InfoType::Error);
        else
        {
            // Satır listesi (özet + özellikler + açık vektörlerin bileşenleri): iki bölme aynı satırları çizer
            s_Rows.clear();
            s_Rows.push_back({ -1, -1 });
            for (int c = 0; c < (int)m_Clip.Curves.size(); ++c)
            {
                s_Rows.push_back({ c, -1 });
                const auto& b = m_Clip.Curves[(size_t)c];
                const int comps = ComponentCount(b);
                if (comps > 1 && m_Expanded[Key(b.Path, b.Component, b.Field)])
                    for (int i = 0; i < comps; ++i)
                        s_Rows.push_back({ c, i });
            }
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float minRows = kRulerH + kEventsH + kBottomH + kRowH * 2.0f;
            const ImVec2 size(avail.x, std::max(avail.y, minRows));
            m_LeftWidth = std::clamp(m_LeftWidth, 200.0f, std::max(200.0f, size.x - 200.0f));
            const float rowsH = size.y - kRulerH - kEventsH - kBottomH;
            const float maxScroll = std::max(0.0f, (float)s_Rows.size() * kRowH - rowsH);
            m_RowsScroll = std::clamp(m_RowsScroll, 0.0f, maxScroll);

            DrawPropertyPane(pos, ImVec2(m_LeftWidth, size.y));
            // Bölme ayırıcı
            ImGui::SetCursorScreenPos(ImVec2(pos.x + m_LeftWidth, pos.y));
            ImGui::InvisibleButton("##AnimSplitter", ImVec2(4.0f, size.y));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive())
                m_LeftWidth += ImGui::GetIO().MouseDelta.x;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x + m_LeftWidth + 1.0f, pos.y), ImVec2(pos.x + m_LeftWidth + 3.0f, pos.y + size.y),
                                                      IM_COL32(24, 25, 28, 255));

            const ImVec2 rightPos(pos.x + m_LeftWidth + 4.0f, pos.y);
            const ImVec2 rightSize(size.x - m_LeftWidth - 4.0f, size.y);
            if (m_CurvesView)
                DrawCurves(rightPos, rightSize);
            else
                DrawDopesheet(rightPos, rightSize);
            HandleDrags();
            ImGui::SetCursorScreenPos(pos);
            ImGui::Dummy(size);
        }
        DrawAddPropertyPopup();
        DrawEventPopup();
        DrawKeyContextMenu();
        HandleShortcuts();
        // Satır listesini değiştiren işlemler (çizim bitti)
        if (m_DeferredRemove >= 0)
        {
            RemoveCurve(m_DeferredRemove);
            m_DeferredRemove = -1;
        }
        if (!m_DeferredClip.empty())
        {
            const std::string clip = std::move(m_DeferredClip);
            m_DeferredClip.clear();
            LoadClip(clip);
        }
        if (m_DeferredCreate)
        {
            m_DeferredCreate = false;
            if (Entity r = GetRoot())
                CreateClip(r, &m_Status);
        }
        ApplyPreview(false);
        CommitEdit(false);
        ImGui::End();
    }

    void AnimationWindow::DrawToolbar(bool playing)
    {
        Entity root = GetRoot();
        const ImVec4 accent = UI::Colors::Accent;
        const ImVec4 red(0.78f, 0.22f, 0.22f, 1.0f);
        const bool canPreview = !playing && root && m_ClipLoaded;
        ImGui::BeginDisabled(!canPreview);
        if (ToggleButton("Preview##AnimPreview", m_Previewing, ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, 1.0f)))
            SetPreviewing(!m_Previewing);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Preview the clip on the object in the scene (values return when preview ends)");
        ImGui::SameLine();
        if (ToggleButton(ICON_FA_CIRCLE "##AnimRecord", m_Recording, red))
            SetRecording(!m_Recording);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Record mode: changing the object (Inspector or Scene view) adds keys at the current frame");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_ClipLoaded);
        const auto times = AllKeyTimes();
        if (ImGui::Button(ICON_FA_BACKWARD_FAST "##AnimFirst"))
            SetTime(times.empty() ? 0.0f : times.front());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("First keyframe");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_BACKWARD_STEP "##AnimPrevKey"))
        {
            float t = 0.0f;
            for (float k : times)
                if (k < m_Time - 1e-4f)
                    t = k;
            SetTime(t);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Previous keyframe (Alt+,)");
        ImGui::SameLine();
        ImGui::BeginDisabled(!canPreview);
        if (ImGui::Button(m_Playing ? ICON_FA_PAUSE "##AnimPlay" : ICON_FA_PLAY "##AnimPlay"))
            SetPlaying(!m_Playing);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Play the clip in the scene (Space)");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FORWARD_STEP "##AnimNextKey"))
        {
            for (float k : times)
                if (k > m_Time + 1e-4f)
                {
                    SetTime(k);
                    break;
                }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Next keyframe (Alt+.)");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FORWARD_FAST "##AnimLast"))
            SetTime(times.empty() ? ViewEnd() : times.back());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Last keyframe");
        ImGui::SameLine();
        const float fr = std::max(1.0f, m_Clip.FrameRate);
        int frame = (int)std::lround(m_Time * fr);
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::InputInt("##AnimFrame", &frame, 0, 0))
            SetTime((float)std::max(frame, 0) / fr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Current frame");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", FormatTime(m_Time).c_str());
        ImGui::SameLine(0.0f, 18.0f);
        ImGui::TextUnformatted("Samples");
        ImGui::SameLine();
        int samples = (int)std::lround(m_Clip.FrameRate);
        ImGui::SetNextItemWidth(60.0f);
        if (ImGui::InputInt("##AnimSamples", &samples, 0, 0))
        {
            BeginEdit();
            m_Clip.FrameRate = (float)std::clamp(samples, 1, 240);
            MarkChanged();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Frames per second of the clip (key snapping and the ruler)");
        ImGui::EndDisabled();
        if (!m_Status.empty())
        {
            ImGui::SameLine(0.0f, 18.0f);
            ImGui::TextDisabled("%s", m_Status.c_str());
        }
        // Sağda: kilit (seçim değişse de bu nesne kalır)
        const float lockW = ImGui::GetFrameHeight() + 4.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - lockW));
        if (ToggleButton(m_Lock ? ICON_FA_LOCK "##AnimLock" : ICON_FA_LOCK_OPEN "##AnimLock", m_Lock, ImVec4(accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, 1.0f)))
            m_Lock = !m_Lock;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(m_Lock ? "Locked to this object" : "Lock the window to the current object");
    }

    void AnimationWindow::DrawPrompt(Entity root)
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 start = ImGui::GetCursorPos();
        std::string text;
        if (!root)
            text = "No animatable object selected.";
        else
            text = "To begin animating " + root.GetComponent<TagComponent>().Tag + ", create an Animation Clip.";
        const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        ImGui::SetCursorPos(ImVec2(start.x + std::max(0.0f, (avail.x - ts.x) * 0.5f), start.y + std::max(0.0f, avail.y * 0.4f - ts.y)));
        ImGui::TextUnformatted(text.c_str());
        if (!root)
            return;
        const float w = 120.0f;
        ImGui::SetCursorPosX(start.x + std::max(0.0f, (avail.x - w) * 0.5f));
        ImGui::BeginDisabled(!EditorState::Get().IsEditing());
        if (ImGui::Button("Create##AnimCreateClip", ImVec2(w, 0.0f)))
            CreateClip(root, &m_Status);
        ImGui::EndDisabled();
    }

    void AnimationWindow::DrawPropertyPane(const ImVec2& pos, const ImVec2& size)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        Entity root = GetRoot();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(32, 34, 38, 255));

        // Üst: klip seçimi, anahtar / olay ekle (sağdaki cetvel + olay satırıyla aynı yükseklik)
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 2.0f, pos.y + 1.0f));
        const float buttons = 2.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x) + 4.0f;
        // Klip seçici (Unity'deki açılır liste): bu nesnenin klipleri + Create New Clip
        const std::string current = fs::path(m_ClipPath).stem().string();
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        const bool openClips = ImGui::Button((current + "  " ICON_FA_CARET_DOWN "##AnimClip").c_str(), ImVec2(std::max(60.0f, size.x - buttons - 4.0f), 0.0f));
        ImGui::PopStyleVar();
        const ImVec2 clipButtonMin = ImGui::GetItemRectMin(), clipButtonMax = ImGui::GetItemRectMax();
        if (openClips)
            ImGui::OpenPopup("##AnimClipMenu");
        const bool clipHovered = ImGui::IsItemHovered();
        ImGui::SetNextWindowPos(ImVec2(clipButtonMin.x, clipButtonMax.y), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(clipButtonMax.x - clipButtonMin.x, 0.0f), ImVec2(600.0f, 400.0f));
        if (ImGui::BeginPopup("##AnimClipMenu"))
        {
            const auto clips = RootClips();
            for (const auto& c : clips)
                if (ImGui::Selectable((fs::path(c).stem().string() + "##clip_" + c).c_str(), c == m_ClipPath))
                    m_DeferredClip = c;
            if (clips.empty())
                ImGui::TextDisabled("%s", fs::path(m_ClipPath).filename().string().c_str());
            if (root)
            {
                ImGui::Separator();
                ImGui::BeginDisabled(!EditorState::Get().IsEditing());
                if (ImGui::Selectable("Create New Clip...##AnimNewClip"))
                    m_DeferredCreate = true;
                ImGui::EndDisabled();
            }
            ImGui::EndPopup();
        }
        if (clipHovered)
            ImGui::SetTooltip("%s", m_ClipPath.c_str());
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_DIAMOND "##AnimAddKey"))
            AddKeyAt(-1, m_Time, &m_Status);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add Keyframe: keys every property at the current frame (K)");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_BOOKMARK "##AnimAddEvent"))
        {
            if (AddEventAt(m_Time, "", "", &m_Status))
                m_OpenEventPopup = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add Event: calls a script method of the object at this frame");
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 6.0f, pos.y + kRulerH + 2.0f));
        if (root)
            ImGui::TextDisabled(ICON_FA_CUBE " %s", root.GetComponent<TagComponent>().Tag.c_str());
        else
            ImGui::TextDisabled("No object: keys can be edited, preview needs an object using this clip");

        // Satırlar
        const float rowsTop = pos.y + kRulerH + kEventsH;
        const float rowsH = size.y - kRulerH - kEventsH - kBottomH;
        const ImVec2 rowsMin(pos.x, rowsTop), rowsMax(pos.x + size.x, rowsTop + rowsH);
        ImGui::PushClipRect(rowsMin, rowsMax, true);
        const float valueW = std::max(80.0f, size.x * 0.42f);
        for (size_t r = 0; r < s_Rows.size(); ++r)
        {
            const float y = rowsTop + (float)r * kRowH - m_RowsScroll;
            if (y + kRowH < rowsTop || y > rowsTop + rowsH)
                continue;
            const RowInfo row = s_Rows[r];
            if (row.Curve >= (int)m_Clip.Curves.size())
                continue;
            const bool selected = row.Curve >= 0 && row.Curve == m_SelRow;
            dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y + kRowH),
                              selected ? IM_COL32(52, 74, 106, 255) : (r % 2 ? IM_COL32(38, 40, 44, 255) : IM_COL32(34, 36, 40, 255)));
            ImGui::PushID((int)r);
            if (row.Curve < 0)
            {
                ImGui::SetCursorScreenPos(ImVec2(pos.x + 6.0f, y + 3.0f));
                ImGui::TextDisabled("%s", m_Clip.Curves.empty() ? "No properties: use Add Property below" : "All properties");
                ImGui::PopID();
                continue;
            }
            auto& b = m_Clip.Curves[(size_t)row.Curve];
            const int comps = ComponentCount(b);
            const bool color = IsColorBinding(b);
            Entity target = Resolve(b);
            glm::vec4 probe(0.0f);
            const bool missing = root && (!target || !PropertyClips::ReadValue(target, b.Component, b.Field, probe));
            glm::vec4 value = CurrentValue(row.Curve);
            const std::string key = Key(b.Path, b.Component, b.Field);
            const int depth = b.Path.empty() ? 0 : 1 + (int)std::count(b.Path.begin(), b.Path.end(), '/');
            float x = pos.x + 4.0f + depth * 10.0f;
            if (row.Comp < 0)
            {
                if (comps > 1)
                {
                    ImGui::SetCursorScreenPos(ImVec2(x, y + 2.0f));
                    bool& open = m_Expanded[key];
                    if (ImGui::ArrowButton("##expand", open ? ImGuiDir_Down : ImGuiDir_Right))
                        open = !open;
                }
                x += ImGui::GetFrameHeight() + 2.0f;
                ImGui::SetCursorScreenPos(ImVec2(x, y + 1.0f));
                const std::string label = RowLabel(b) + (missing ? " (Missing!)" : "");
                if (missing)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.8f, 0.3f, 1.0f));
                const float labelW = std::max(30.0f, pos.x + size.x - valueW - x - 8.0f);
                if (ImGui::Selectable((label + "##row").c_str(), selected, ImGuiSelectableFlags_AllowOverlap, ImVec2(labelW, kRowH - 4.0f)))
                    m_SelRow = selected ? -1 : row.Curve;
                if (missing)
                    ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s%s%s.%s", b.Path.empty() ? "" : b.Path.c_str(), b.Path.empty() ? "" : " : ", b.Component.c_str(), b.Field.c_str());
                if (ImGui::BeginPopupContextItem("##rowmenu"))
                {
                    if (ImGui::MenuItem("Add Key"))
                        AddKeyAt(row.Curve, m_Time, &m_Status);
                    if (ImGui::MenuItem(ICON_FA_TRASH " Remove Properties"))
                        m_DeferredRemove = row.Curve;
                    ImGui::EndPopup();
                }
                // Değer: o karedeki değer; değiştirmek o karede anahtar olur (Unity)
                ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - valueW - 4.0f, y + 1.0f));
                ImGui::SetNextItemWidth(valueW);
                bool changed = false;
                const ComponentField* f = b.Component == "Transform" ? nullptr : FindRegistryField(b.Component, b.Field);
                if (f && f->Kind == FieldKind::Bool)
                {
                    bool v = value.x >= 0.5f;
                    if (ImGui::Checkbox("##value", &v))
                    {
                        value.x = v ? 1.0f : 0.0f;
                        changed = true;
                    }
                }
                else if (color)
                    changed = comps == 4 ? ImGui::ColorEdit4("##value", &value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)
                                         : ImGui::ColorEdit3("##value", &value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                else if (comps == 1)
                    changed = ImGui::DragFloat("##value", &value.x, 0.01f);
                else if (comps == 2)
                    changed = ImGui::DragFloat2("##value", &value.x, 0.01f);
                else if (comps == 3)
                    changed = ImGui::DragFloat3("##value", &value.x, 0.01f);
                else
                    changed = ImGui::DragFloat4("##value", &value.x, 0.01f);
                if (changed)
                {
                    BeginEdit();
                    SetKeyAt(row.Curve, m_Time, value);
                    MarkChanged();
                }
            }
            else
            {
                x += ImGui::GetFrameHeight() + 14.0f;
                ImGui::SetCursorScreenPos(ImVec2(x, y + 3.0f));
                const std::string label = (b.Component == "Transform" ? b.Field : b.Field) + "." + ComponentSuffix(row.Comp, color);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ComponentColor(row.Comp, comps)), "%s", label.c_str());
                ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - valueW - 4.0f, y + 1.0f));
                ImGui::SetNextItemWidth(valueW);
                float v = value[row.Comp];
                if (ImGui::DragFloat("##compvalue", &v, 0.01f))
                {
                    value[row.Comp] = v;
                    BeginEdit();
                    SetKeyAt(row.Curve, m_Time, value);
                    MarkChanged();
                }
            }
            ImGui::PopID();
        }
        ImGui::PopClipRect();
        if (ImGui::IsMouseHoveringRect(rowsMin, rowsMax) && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            m_RowsScroll -= ImGui::GetIO().MouseWheel * kRowH * 2.0f;

        // Alt: Add Property + Dopesheet / Curves
        const float by = pos.y + size.y - kBottomH + 4.0f;
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 4.0f, by));
        if (ImGui::Button(ICON_FA_PLUS " Add Property##AnimAddProperty", ImVec2(size.x * 0.5f - 6.0f, 0.0f)))
        {
            m_AddPropertyAnchor = ImGui::GetItemRectMin();
            ImGui::OpenPopup("##AnimAddPropertyPopup");
        }
        ImGui::SameLine();
        const ImVec4 on(0.30f, 0.42f, 0.62f, 1.0f);
        if (ToggleButton("Dopesheet##AnimDopesheet", !m_CurvesView, on))
            m_CurvesView = false;
        ImGui::SameLine();
        if (ToggleButton("Curves##AnimCurves", m_CurvesView, on))
        {
            m_CurvesView = true;
            m_FitPending = true;
        }
    }

    void AnimationWindow::DrawRuler(const ImVec2& pos, float width, float height)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), m_Recording ? IM_COL32(96, 38, 38, 255) : IM_COL32(34, 36, 40, 255));
        const float fr = std::max(1.0f, std::round(m_Clip.FrameRate));
        const float pxPerFrame = m_Zoom / fr;
        static const int steps[] = { 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 3000, 6000, 12000 };
        int stepFrames = steps[0];
        for (int s : steps)
        {
            stepFrames = s;
            if ((float)s * pxPerFrame >= 70.0f)
                break;
        }
        ImGui::PushClipRect(pos, ImVec2(pos.x + width, pos.y + height), true);
        const long f0 = std::max(0L, (long)std::floor(XToTime(pos.x) * fr / (float)stepFrames) * (long)stepFrames);
        for (long f = f0;; f += stepFrames)
        {
            const float x = TimeToX((float)f / fr);
            if (x > pos.x + width)
                break;
            dl->AddLine(ImVec2(x, pos.y + height - 9.0f), ImVec2(x, pos.y + height), IM_COL32(170, 172, 178, 255));
            dl->AddText(ImVec2(x + 3.0f, pos.y + 2.0f), IM_COL32(170, 172, 178, 255), FormatTime((float)f / fr).c_str());
            const int minor = stepFrames >= 5 ? 5 : stepFrames;
            for (int k = 1; k < minor; ++k)
            {
                const float xs = TimeToX(((float)f + (float)stepFrames * (float)k / (float)minor) / fr);
                dl->AddLine(ImVec2(xs, pos.y + height - 4.0f), ImVec2(xs, pos.y + height), IM_COL32(110, 112, 118, 255));
            }
        }
        // Klip sonu
        const float endX = TimeToX(m_Clip.Length);
        dl->AddLine(ImVec2(endX, pos.y), ImVec2(endX, pos.y + height), IM_COL32(150, 150, 160, 160), 1.0f);
        ImGui::PopClipRect();
        // Oynatma kafası: tıkla / sürükle
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##AnimRuler", ImVec2(width, height));
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_Playing = false;
            SetTime(XToTime(ImGui::GetIO().MousePos.x));
        }
        const float px = TimeToX(m_Time);
        if (px >= pos.x - 1.0f && px <= pos.x + width + 1.0f)
        {
            const std::string label = std::to_string((long)std::lround(m_Time * fr));
            const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            const ImU32 col = m_Recording ? IM_COL32(230, 70, 70, 255) : IM_COL32(222, 222, 228, 255);
            dl->AddRectFilled(ImVec2(px - ts.x * 0.5f - 4.0f, pos.y + height - ts.y - 4.0f), ImVec2(px + ts.x * 0.5f + 4.0f, pos.y + height), col, 3.0f);
            dl->AddText(ImVec2(px - ts.x * 0.5f, pos.y + height - ts.y - 2.0f), IM_COL32(20, 20, 24, 255), label.c_str());
        }
    }

    void AnimationWindow::DrawEventsRow(const ImVec2& pos, float width, float height)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(28, 30, 33, 255));
        dl->AddLine(ImVec2(pos.x, pos.y + height - 1.0f), ImVec2(pos.x + width, pos.y + height - 1.0f), IM_COL32(20, 20, 22, 255));
        ImGui::SetCursorScreenPos(pos);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##AnimEventsRow", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool rowHovered = ImGui::IsItemHovered();
        if (rowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (AddEventAt(XToTime(io.MousePos.x), "", "", &m_Status))
                m_OpenEventPopup = true;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            m_MenuTime = Snap(XToTime(io.MousePos.x));
            ImGui::OpenPopup("##AnimEventsMenu");
        }
        if (ImGui::BeginPopup("##AnimEventsMenu"))
        {
            if (ImGui::MenuItem("Add Animation Event"))
                if (AddEventAt(m_MenuTime, "", "", &m_Status))
                    m_OpenEventPopup = true;
            ImGui::EndPopup();
        }
        ImGui::PushClipRect(pos, ImVec2(pos.x + width, pos.y + height), true);
        for (int i = 0; i < (int)m_Clip.Events.size(); ++i)
        {
            const auto& e = m_Clip.Events[(size_t)i];
            const float x = TimeToX(e.Time);
            if (x < pos.x - 8.0f || x > pos.x + width + 8.0f)
                continue;
            const bool sel = i == m_SelEvent;
            const ImU32 fill = sel ? IM_COL32(90, 170, 255, 255) : (e.Function.empty() ? IM_COL32(150, 150, 150, 255) : IM_COL32(235, 235, 240, 255));
            dl->AddRectFilled(ImVec2(x - 3.0f, pos.y + 3.0f), ImVec2(x + 3.0f, pos.y + height - 6.0f), fill, 1.5f);
            dl->AddTriangleFilled(ImVec2(x - 3.0f, pos.y + height - 6.0f), ImVec2(x + 3.0f, pos.y + height - 6.0f), ImVec2(x, pos.y + height - 2.0f), fill);
            ImGui::SetCursorScreenPos(ImVec2(x - 5.0f, pos.y));
            ImGui::InvisibleButton(("##event_" + std::to_string(i)).c_str(), ImVec2(10.0f, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s(%s) at %s", e.Function.empty() ? "(no function)" : e.Function.c_str(), e.Argument.c_str(), FormatTime(e.Time).c_str());
            if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                m_SelEvent = i;
                m_SelKeys.clear();
                m_Drag = Drag::Event;
                m_DragStart = io.MousePos;
                m_DragTime0 = XToTime(io.MousePos.x);
                m_DragEventsBackup = m_Clip.Events;
                m_DragMoved = false;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_SelEvent = i;
                m_OpenEventPopup = true;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                m_SelEvent = i;
                ImGui::OpenPopup("##AnimEventMenu");
            }
        }
        ImGui::PopClipRect();
        if (ImGui::BeginPopup("##AnimEventMenu"))
        {
            if (ImGui::MenuItem("Edit Animation Event"))
                m_OpenEventPopup = true;
            if (ImGui::MenuItem(ICON_FA_TRASH " Delete Animation Event"))
            {
                m_SelKeys.clear();
                DeleteSelection();
            }
            ImGui::EndPopup();
        }
    }

    bool AnimationWindow::LanePoint(int curve, float time, ImVec2& out) const
    {
        auto it = m_LaneCenterY.find(curve);
        if (it == m_LaneCenterY.end() || m_CurvesView)
            return false;
        out = ImVec2(TimeToX(time), it->second);
        return true;
    }

    void AnimationWindow::DrawDopesheet(const ImVec2& pos, const ImVec2& size)
    {
        m_LaneCenterY.clear();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        m_LanesX0 = pos.x + 8.0f;
        const float w = size.x;
        DrawRuler(pos, w, kRulerH);
        DrawEventsRow(ImVec2(pos.x, pos.y + kRulerH), w, kEventsH);
        const float rowsTop = pos.y + kRulerH + kEventsH;
        const float rowsH = size.y - kRulerH - kEventsH - kBottomH;
        m_BoxRowsTop = rowsTop;
        const ImVec2 rowsMin(pos.x, rowsTop), rowsMax(pos.x + w, rowsTop + rowsH);
        dl->AddRectFilled(rowsMin, ImVec2(rowsMax.x, pos.y + size.y), IM_COL32(30, 32, 36, 255));
        ImGui::PushClipRect(rowsMin, rowsMax, true);
        // Klip sonundan sonrası koyu
        const float endX = TimeToX(m_Clip.Length);
        if (endX < rowsMax.x)
            dl->AddRectFilled(ImVec2(std::max(endX, rowsMin.x), rowsMin.y), rowsMax, IM_COL32(0, 0, 0, 60));
        const float half = 0.5f * FrameTime();
        for (size_t r = 0; r < s_Rows.size(); ++r)
        {
            const float y = rowsTop + (float)r * kRowH - m_RowsScroll;
            if (y + kRowH < rowsTop || y > rowsTop + rowsH)
                continue;
            const RowInfo row = s_Rows[r];
            if (row.Curve >= (int)m_Clip.Curves.size())
                continue;
            if (row.Comp < 0)
                m_LaneCenterY[row.Curve] = y + kRowH * 0.5f;
            dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + w, y + kRowH), r % 2 ? IM_COL32(38, 40, 44, 255) : IM_COL32(34, 36, 40, 255));
            const std::string rowId = row.Curve < 0 ? std::string("sum") : std::to_string(row.Curve) + (row.Comp >= 0 ? "_" + std::to_string(row.Comp) : "");
            // Şerit: boş yerde sürükle = kutu seçimi, çift tıkla = anahtar ekle, sağ tık = menü
            ImGui::SetCursorScreenPos(ImVec2(pos.x, y));
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton(("##lane_" + rowId).c_str(), ImVec2(w, kRowH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                AddKeyAt(row.Curve, XToTime(io.MousePos.x), &m_Status);
            else if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_Drag == Drag::None)
            {
                if (!io.KeyCtrl && !io.KeyShift)
                {
                    m_SelKeys.clear();
                    m_SelEvent = -1;
                }
                m_Drag = Drag::Box;
                m_DragStart = io.MousePos;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                m_MenuCurve = row.Curve;
                m_MenuTime = Snap(XToTime(io.MousePos.x));
                m_OpenKeyMenu = true;
            }
            // Anahtarlar
            std::vector<float> times;
            if (row.Curve < 0)
                times = AllKeyTimes();
            else
                for (const auto& k : m_Clip.Curves[(size_t)row.Curve].Channel.Keys)
                    times.push_back(k.Time);
            for (size_t k = 0; k < times.size(); ++k)
            {
                const float t = times[k];
                const float x = TimeToX(t);
                if (x < pos.x - 8.0f || x > pos.x + w + 8.0f)
                    continue;
                bool sel = false;
                if (row.Curve >= 0)
                    sel = IsSelected(row.Curve, t);
                else
                    for (const auto& s : m_SelKeys)
                        if (std::abs(s.Time - t) <= half)
                            sel = true;
                const ImU32 fill = sel ? IM_COL32(86, 168, 255, 255) : (row.Curve < 0 ? IM_COL32(170, 170, 176, 255) : IM_COL32(214, 214, 220, 255));
                Diamond(dl, ImVec2(x, y + kRowH * 0.5f), row.Comp >= 0 ? 4.0f : 5.5f, fill, IM_COL32(16, 16, 18, 255));
                ImGui::SetCursorScreenPos(ImVec2(x - 6.0f, y + kRowH * 0.5f - 7.0f));
                ImGui::InvisibleButton(("##key_" + rowId + "_" + std::to_string(k)).c_str(), ImVec2(12.0f, 14.0f),
                                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                if (ImGui::IsItemHovered() && m_Drag == Drag::None)
                {
                    if (row.Curve >= 0)
                    {
                        const glm::vec4 v = EvaluateChannel(m_Clip.Curves[(size_t)row.Curve].Channel, t, glm::vec4(0.0f));
                        const int comps = ComponentCount(m_Clip.Curves[(size_t)row.Curve]);
                        std::string vs;
                        for (int i = 0; i < comps; ++i)
                        {
                            char b[32];
                            std::snprintf(b, sizeof(b), i ? ", %.3g" : "%.3g", v[i]);
                            vs += b;
                        }
                        ImGui::SetTooltip("%s  (%s)", FormatTime(t).c_str(), vs.c_str());
                    }
                    else
                        ImGui::SetTooltip("%s", FormatTime(t).c_str());
                }
                if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    const bool additive = io.KeyCtrl || io.KeyShift;
                    if (!sel)
                        SelectKey(row.Curve, t, additive);
                    else if (io.KeyCtrl)
                    {
                        // Ctrl: seçimden çıkar
                        m_SelKeys.erase(std::remove_if(m_SelKeys.begin(), m_SelKeys.end(),
                                                       [&](const SelKey& s) { return (row.Curve < 0 || s.Curve == row.Curve) && std::abs(s.Time - t) <= half; }),
                                        m_SelKeys.end());
                    }
                    m_Drag = Drag::Keys;
                    m_DragStart = io.MousePos;
                    m_DragTime0 = XToTime(io.MousePos.x);
                    m_DragSel = m_SelKeys;
                    m_DragBackup = m_Clip.Curves;
                    m_DragMoved = false;
                    if (row.Curve >= 0)
                        m_SelRow = row.Curve;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                {
                    if (!sel)
                        SelectKey(row.Curve, t, false);
                    m_MenuCurve = row.Curve;
                    m_MenuTime = t;
                    m_OpenKeyMenu = true;
                }
            }
        }
        if (m_Drag == Drag::Box)
        {
            const ImVec2 a(std::min(m_DragStart.x, io.MousePos.x), std::min(m_DragStart.y, io.MousePos.y));
            const ImVec2 b(std::max(m_DragStart.x, io.MousePos.x), std::max(m_DragStart.y, io.MousePos.y));
            dl->AddRectFilled(a, b, IM_COL32(90, 150, 240, 40));
            dl->AddRect(a, b, IM_COL32(90, 150, 240, 200));
        }
        ImGui::PopClipRect();
        // Oynatma kafası çizgisi
        const float px = TimeToX(m_Time);
        if (px >= pos.x && px <= pos.x + w)
            dl->AddLine(ImVec2(px, pos.y + kRulerH), ImVec2(px, rowsTop + rowsH), m_Recording ? IM_COL32(230, 70, 70, 255) : IM_COL32(222, 222, 228, 200), 1.5f);
        // Alt bilgi
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 6.0f, pos.y + size.y - kBottomH + 7.0f));
        ImGui::TextDisabled("Length %s (%.2f s)  |  %d curves, %d events  |  wheel: zoom, Shift+wheel / middle drag: pan, F: frame all",
                            FormatTime(m_Clip.Length).c_str(), m_Clip.Length, (int)m_Clip.Curves.size(), (int)m_Clip.Events.size());
        // Tekerlek: yakınlaştır (fare altındaki zaman sabit) / Shift: kaydır; orta tuş: kaydır
        if (ImGui::IsMouseHoveringRect(ImVec2(pos.x, pos.y), ImVec2(pos.x + w, rowsTop + rowsH)) && ImGui::IsWindowHovered())
        {
            if (io.MouseWheel != 0.0f)
            {
                if (io.KeyShift)
                    m_ScrollTime -= io.MouseWheel * 60.0f / m_Zoom;
                else
                {
                    const float t = XToTime(io.MousePos.x);
                    m_Zoom = std::clamp(m_Zoom * std::pow(1.15f, io.MouseWheel), 10.0f, 6000.0f);
                    m_ScrollTime = t - (io.MousePos.x - m_LanesX0) / m_Zoom;
                }
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                m_Drag = Drag::Pan;
        }
    }

    float AnimationWindow::ValueToY(float v) const
    {
        const float top = m_CurveAreaMin.y + 10.0f, bottom = m_CurveAreaMax.y - 10.0f;
        const float range = std::max(m_ValueMax - m_ValueMin, 1e-6f);
        return bottom - (v - m_ValueMin) / range * (bottom - top);
    }

    float AnimationWindow::YToValue(float y) const
    {
        const float top = m_CurveAreaMin.y + 10.0f, bottom = m_CurveAreaMax.y - 10.0f;
        const float range = std::max(m_ValueMax - m_ValueMin, 1e-6f);
        return m_ValueMin + (bottom - y) / std::max(bottom - top, 1.0f) * range;
    }

    void AnimationWindow::FitCurves()
    {
        float lo = 1e30f, hi = -1e30f;
        const float end = std::max(ViewEnd(), FrameTime());
        for (int c = 0; c < (int)m_Clip.Curves.size(); ++c)
        {
            if (m_SelRow >= 0 && c != m_SelRow)
                continue;
            const auto& b = m_Clip.Curves[(size_t)c];
            const int comps = ComponentCount(b);
            for (int s = 0; s <= 64; ++s)
            {
                const glm::vec4 v = EvaluateChannel(b.Channel, end * (float)s / 64.0f, glm::vec4(0.0f));
                for (int i = 0; i < comps; ++i)
                {
                    lo = std::min(lo, v[i]);
                    hi = std::max(hi, v[i]);
                }
            }
            for (const auto& k : b.Channel.Keys)
                for (int i = 0; i < comps; ++i)
                {
                    lo = std::min(lo, k.Value[i]);
                    hi = std::max(hi, k.Value[i]);
                }
        }
        if (lo > hi)
        {
            lo = -1.0f;
            hi = 1.0f;
        }
        if (hi - lo < 1e-3f)
        {
            lo -= 1.0f;
            hi += 1.0f;
        }
        const float pad = (hi - lo) * 0.1f;
        m_ValueMin = lo - pad;
        m_ValueMax = hi + pad;
        m_FitPending = false;
    }

    void AnimationWindow::DrawCurves(const ImVec2& pos, const ImVec2& size)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        m_LanesX0 = pos.x + 8.0f;
        const float w = size.x;
        DrawRuler(pos, w, kRulerH);
        DrawEventsRow(ImVec2(pos.x, pos.y + kRulerH), w, kEventsH);
        m_CurveAreaMin = ImVec2(pos.x, pos.y + kRulerH + kEventsH);
        m_CurveAreaMax = ImVec2(pos.x + w, pos.y + size.y - kBottomH);
        if (m_FitPending)
            FitCurves();
        dl->AddRectFilled(m_CurveAreaMin, ImVec2(m_CurveAreaMax.x, pos.y + size.y), IM_COL32(30, 32, 36, 255));
        ImGui::PushClipRect(m_CurveAreaMin, m_CurveAreaMax, true);
        // Değer ızgarası
        {
            const float range = std::max(m_ValueMax - m_ValueMin, 1e-6f);
            const float pxPerUnit = (m_CurveAreaMax.y - m_CurveAreaMin.y - 20.0f) / range;
            float step = std::pow(10.0f, std::floor(std::log10(std::max(40.0f / std::max(pxPerUnit, 1e-6f), 1e-6f))));
            for (float m : { 1.0f, 2.0f, 5.0f, 10.0f })
                if (m * step * pxPerUnit >= 40.0f)
                {
                    step *= m;
                    break;
                }
            for (float v = std::floor(m_ValueMin / step) * step; v <= m_ValueMax; v += step)
            {
                const float y = ValueToY(v);
                dl->AddLine(ImVec2(m_CurveAreaMin.x, y), ImVec2(m_CurveAreaMax.x, y), std::abs(v) < step * 0.01f ? IM_COL32(90, 92, 98, 255) : IM_COL32(52, 54, 60, 255));
                char label[32];
                std::snprintf(label, sizeof(label), "%g", std::abs(v) < step * 0.01f ? 0.0f : v);
                dl->AddText(ImVec2(m_CurveAreaMin.x + 3.0f, y - 14.0f), IM_COL32(140, 142, 150, 255), label);
            }
        }
        const float endX = TimeToX(m_Clip.Length);
        if (endX < m_CurveAreaMax.x)
            dl->AddRectFilled(ImVec2(std::max(endX, m_CurveAreaMin.x), m_CurveAreaMin.y), m_CurveAreaMax, IM_COL32(0, 0, 0, 50));
        // Alan: çift tıkla = seçili özelliğe anahtar, sağ tık = menü, orta tuş = kaydır
        ImGui::SetCursorScreenPos(m_CurveAreaMin);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##AnimCurveArea", ImVec2(m_CurveAreaMax.x - m_CurveAreaMin.x, m_CurveAreaMax.y - m_CurveAreaMin.y),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const bool areaHovered = ImGui::IsItemHovered();
        if (areaHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !m_Clip.Curves.empty())
            AddKeyAt(m_SelRow, XToTime(io.MousePos.x), &m_Status);
        else if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_SelKeys.clear();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            m_MenuCurve = m_SelRow;
            m_MenuTime = Snap(XToTime(io.MousePos.x));
            m_OpenKeyMenu = true;
        }
        if (areaHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            m_Drag = Drag::Pan;
        // Eğriler (seçili özellik; seçim yoksa hepsi)
        for (int c = 0; c < (int)m_Clip.Curves.size(); ++c)
        {
            if (m_SelRow >= 0 && c != m_SelRow)
                continue;
            const auto& b = m_Clip.Curves[(size_t)c];
            const int comps = ComponentCount(b);
            for (int i = 0; i < comps; ++i)
            {
                const ImU32 col = ComponentColor(i, comps);
                std::vector<ImVec2> pts;
                for (float x = m_CurveAreaMin.x; x <= m_CurveAreaMax.x + 3.0f; x += 3.0f)
                {
                    const float t = std::max(0.0f, XToTime(x));
                    pts.push_back(ImVec2(x, ValueToY(EvaluateChannel(b.Channel, t, glm::vec4(0.0f))[i])));
                }
                dl->AddPolyline(pts.data(), (int)pts.size(), col, ImDrawFlags_None, 1.6f);
                for (size_t k = 0; k < b.Channel.Keys.size(); ++k)
                {
                    const auto& key = b.Channel.Keys[k];
                    const ImVec2 p(TimeToX(key.Time), ValueToY(key.Value[i]));
                    const bool sel = IsSelected(c, key.Time);
                    dl->AddCircleFilled(p, sel ? 5.0f : 4.0f, sel ? IM_COL32(255, 255, 255, 255) : col);
                    dl->AddCircle(p, sel ? 5.0f : 4.0f, IM_COL32(16, 16, 18, 255));
                    ImGui::SetCursorScreenPos(ImVec2(p.x - 5.0f, p.y - 5.0f));
                    ImGui::InvisibleButton(("##ckey_" + std::to_string(c) + "_" + std::to_string(k) + "_" + std::to_string(i)).c_str(), ImVec2(10.0f, 10.0f),
                                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                    if (ImGui::IsItemHovered() && m_Drag == Drag::None)
                        ImGui::SetTooltip("%s  %s.%s = %.4g", FormatTime(key.Time).c_str(), b.Field.c_str(), ComponentSuffix(i, IsColorBinding(b)), key.Value[i]);
                    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        SelectKey(c, key.Time, io.KeyCtrl || io.KeyShift);
                        m_Drag = Drag::CurveKey;
                        m_DragCurve = c;
                        m_DragComp = i;
                        m_DragKeyTime = key.Time;
                        m_DragValue0 = key.Value[i];
                        m_DragStart = io.MousePos;
                        m_DragBackup = m_Clip.Curves;
                        m_DragMoved = false;
                    }
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    {
                        if (!sel)
                            SelectKey(c, key.Time, false);
                        m_MenuCurve = c;
                        m_MenuTime = key.Time;
                        m_OpenKeyMenu = true;
                    }
                }
            }
        }
        ImGui::PopClipRect();
        const float px = TimeToX(m_Time);
        if (px >= pos.x && px <= pos.x + w)
            dl->AddLine(ImVec2(px, pos.y + kRulerH), ImVec2(px, m_CurveAreaMax.y), m_Recording ? IM_COL32(230, 70, 70, 255) : IM_COL32(222, 222, 228, 200), 1.5f);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 6.0f, pos.y + size.y - kBottomH + 7.0f));
        ImGui::TextDisabled("%s  |  wheel: zoom time, Ctrl+wheel: zoom values, middle drag: pan, F: frame all",
                            m_SelRow >= 0 ? RowLabel(m_Clip.Curves[(size_t)m_SelRow]).c_str() : "All properties (select a row to show one)");
        if (ImGui::IsMouseHoveringRect(m_CurveAreaMin, m_CurveAreaMax) && ImGui::IsWindowHovered() && io.MouseWheel != 0.0f)
        {
            if (io.KeyCtrl)
            {
                const float v = YToValue(io.MousePos.y);
                const float k = std::pow(1.15f, -io.MouseWheel);
                m_ValueMin = v + (m_ValueMin - v) * k;
                m_ValueMax = v + (m_ValueMax - v) * k;
            }
            else
            {
                const float t = XToTime(io.MousePos.x);
                m_Zoom = std::clamp(m_Zoom * std::pow(1.15f, io.MouseWheel), 10.0f, 6000.0f);
                m_ScrollTime = t - (io.MousePos.x - m_LanesX0) / m_Zoom;
            }
        }
    }

    void AnimationWindow::HandleDrags()
    {
        ImGuiIO& io = ImGui::GetIO();
        if (m_Drag == Drag::None)
            return;
        const bool released = m_Drag == Drag::Pan ? !ImGui::IsMouseDown(ImGuiMouseButton_Middle) : !ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (released)
        {
            if (m_Drag == Drag::Box)
            {
                // Kutudaki anahtarlar seçilir
                const ImVec2 a(std::min(m_DragStart.x, io.MousePos.x), std::min(m_DragStart.y, io.MousePos.y));
                const ImVec2 b(std::max(m_DragStart.x, io.MousePos.x), std::max(m_DragStart.y, io.MousePos.y));
                if (b.x - a.x > 2.0f || b.y - a.y > 2.0f)
                {
                    // Kutuya değen anahtar işaretleri (yarıçap ~6 px) seçilir
                    constexpr float r6 = 6.0f;
                    const float t0 = XToTime(a.x - r6), t1 = XToTime(b.x + r6);
                    for (size_t r = 0; r < s_Rows.size(); ++r)
                    {
                        const float y = m_BoxRowsTop + (float)r * kRowH - m_RowsScroll + kRowH * 0.5f;
                        if (y + r6 < a.y || y - r6 > b.y)
                            continue;
                        const RowInfo row = s_Rows[r];
                        for (int c = 0; c < (int)m_Clip.Curves.size(); ++c)
                        {
                            if (row.Curve >= 0 && c != row.Curve)
                                continue;
                            for (const auto& k : m_Clip.Curves[(size_t)c].Channel.Keys)
                                if (k.Time >= t0 && k.Time <= t1 && !IsSelected(c, k.Time))
                                    m_SelKeys.push_back({ c, k.Time });
                        }
                    }
                }
            }
            if (m_Drag == Drag::Event && m_DragMoved)
            {
                // Olaylar zamana göre sıralı kalır
                const AnimEventDef moved = m_SelEvent >= 0 && m_SelEvent < (int)m_Clip.Events.size() ? m_Clip.Events[(size_t)m_SelEvent] : AnimEventDef{};
                std::stable_sort(m_Clip.Events.begin(), m_Clip.Events.end(), [](const AnimEventDef& x, const AnimEventDef& y) { return x.Time < y.Time; });
                for (int i = 0; i < (int)m_Clip.Events.size(); ++i)
                    if (m_Clip.Events[(size_t)i].Time == moved.Time && m_Clip.Events[(size_t)i].Function == moved.Function)
                        m_SelEvent = i;
                MarkChanged();
            }
            m_Drag = Drag::None;
            m_DragBackup.clear();
            m_DragEventsBackup.clear();
            m_DragSel.clear();
            return;
        }
        const float moveDist = std::abs(io.MousePos.x - m_DragStart.x) + std::abs(io.MousePos.y - m_DragStart.y);
        switch (m_Drag)
        {
            case Drag::Keys:
            {
                if (!m_DragMoved && moveDist < 3.0f)
                    break;
                if (!m_DragMoved)
                {
                    BeginEdit();
                    m_DragMoved = true;
                }
                float minT = 1e9f;
                for (const auto& s : m_DragSel)
                    minT = std::min(minT, s.Time);
                const float delta = std::max(Snap(XToTime(io.MousePos.x)) - Snap(m_DragTime0), -minT);
                m_Clip.Curves = m_DragBackup;
                const float half = 0.5f * FrameTime();
                float maxT = 0.0f;
                for (int c = 0; c < (int)m_Clip.Curves.size(); ++c)
                {
                    auto& keys = m_Clip.Curves[(size_t)c].Channel.Keys;
                    std::vector<TimelineKey> moved;
                    for (auto it = keys.begin(); it != keys.end();)
                    {
                        bool selected = false;
                        for (const auto& s : m_DragSel)
                            if (s.Curve == c && std::abs(s.Time - it->Time) < 1e-4f)
                                selected = true;
                        if (selected)
                        {
                            moved.push_back(*it);
                            it = keys.erase(it);
                        }
                        else
                            ++it;
                    }
                    for (auto& k : moved)
                    {
                        k.Time = std::max(0.0f, Snap(k.Time + delta));
                        maxT = std::max(maxT, k.Time);
                        // Üzerine bırakılan seçili olmayan anahtar silinir (Unity)
                        keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const TimelineKey& o) { return std::abs(o.Time - k.Time) <= half; }), keys.end());
                        keys.push_back(k);
                    }
                    std::sort(keys.begin(), keys.end(), [](const TimelineKey& x, const TimelineKey& y) { return x.Time < y.Time; });
                }
                m_SelKeys = m_DragSel;
                for (auto& s : m_SelKeys)
                    s.Time = std::max(0.0f, Snap(s.Time + delta));
                m_Clip.Length = std::max(m_Clip.Length, maxT);
                MarkChanged();
                break;
            }
            case Drag::Event:
            {
                if (!m_DragMoved && moveDist < 3.0f)
                    break;
                if (!m_DragMoved)
                {
                    BeginEdit();
                    m_DragMoved = true;
                }
                if (m_SelEvent < 0 || m_SelEvent >= (int)m_DragEventsBackup.size())
                    break;
                m_Clip.Events = m_DragEventsBackup;
                const float delta = Snap(XToTime(io.MousePos.x)) - Snap(m_DragTime0);
                auto& e = m_Clip.Events[(size_t)m_SelEvent];
                e.Time = std::max(0.0f, Snap(m_DragEventsBackup[(size_t)m_SelEvent].Time + delta));
                m_Clip.Length = std::max(m_Clip.Length, e.Time);
                MarkChanged();
                break;
            }
            case Drag::CurveKey:
            {
                if (!m_DragMoved && moveDist < 3.0f)
                    break;
                if (!m_DragMoved)
                {
                    BeginEdit();
                    m_DragMoved = true;
                }
                if (m_DragCurve < 0 || m_DragCurve >= (int)m_DragBackup.size())
                    break;
                m_Clip.Curves = m_DragBackup;
                auto& keys = m_Clip.Curves[(size_t)m_DragCurve].Channel.Keys;
                auto it = std::find_if(keys.begin(), keys.end(), [&](const TimelineKey& k) { return std::abs(k.Time - m_DragKeyTime) < 1e-4f; });
                if (it == keys.end())
                    break;
                TimelineKey k = *it;
                keys.erase(it);
                const float newT = std::max(0.0f, Snap(m_DragKeyTime + XToTime(io.MousePos.x) - XToTime(m_DragStart.x)));
                k.Time = newT;
                k.Value[m_DragComp] = m_DragValue0 + (YToValue(io.MousePos.y) - YToValue(m_DragStart.y));
                const float half = 0.5f * FrameTime();
                keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const TimelineKey& o) { return std::abs(o.Time - newT) <= half; }), keys.end());
                keys.push_back(k);
                std::sort(keys.begin(), keys.end(), [](const TimelineKey& x, const TimelineKey& y) { return x.Time < y.Time; });
                m_SelKeys = { { m_DragCurve, newT } };
                m_Clip.Length = std::max(m_Clip.Length, newT);
                MarkChanged();
                break;
            }
            case Drag::Pan:
            {
                m_ScrollTime -= io.MouseDelta.x / m_Zoom;
                if (m_CurvesView && io.MouseDelta.y != 0.0f)
                {
                    const float dv = YToValue(io.MousePos.y) - YToValue(io.MousePos.y - io.MouseDelta.y);
                    m_ValueMin -= dv;
                    m_ValueMax -= dv;
                }
                break;
            }
            default:
                break;
        }
    }

    void AnimationWindow::DrawKeyContextMenu()
    {
        if (m_OpenKeyMenu)
        {
            ImGui::OpenPopup("##AnimKeyMenu");
            m_OpenKeyMenu = false;
        }
        if (!ImGui::BeginPopup("##AnimKeyMenu"))
            return;
        const bool hasSel = !m_SelKeys.empty();
        if (ImGui::MenuItem("Add Key", "K", false, !m_Clip.Curves.empty()))
            AddKeyAt(m_MenuCurve, m_MenuTime, &m_Status);
        if (ImGui::MenuItem(ICON_FA_TRASH " Delete Keys", "Del", false, hasSel))
            DeleteSelection();
        ImGui::Separator();
        if (ImGui::BeginMenu("Both Tangents", hasSel))
        {
            if (ImGui::MenuItem("Auto (Smooth)"))
                SetSelectedInterpolation(KeyInterpolation::Smooth);
            if (ImGui::MenuItem("Linear"))
                SetSelectedInterpolation(KeyInterpolation::Linear);
            if (ImGui::MenuItem("Constant"))
                SetSelectedInterpolation(KeyInterpolation::Constant);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSel))
            CopySelection();
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_Clipboard.empty()))
            PasteAtTime();
        ImGui::EndPopup();
    }

    void AnimationWindow::DrawAddPropertyPopup()
    {
        // Sabit boyut, düğmenin üstünde: düğüm açıldıkça liste kaymaz (içerik kendi içinde kayar)
        ImGui::SetNextWindowPos(m_AddPropertyAnchor, ImGuiCond_Appearing, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(380.0f, 420.0f), ImGuiCond_Always);
        if (!ImGui::BeginPopup("##AnimAddPropertyPopup"))
            return;
        Entity root = GetRoot();
        if (!root)
        {
            ImGui::TextDisabled("Select an object that uses this clip to add its properties.");
            ImGui::EndPopup();
            return;
        }
        auto drawField = [&](const std::string& path, const std::string& component, const std::string& field, const std::string& label) {
            const bool exists = m_Clip.Find(path, component, field) != nullptr;
            ImGui::BeginDisabled(exists);
            ImGui::TextUnformatted(label.c_str());
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - 28.0f));
            const std::string id = std::string(ICON_FA_PLUS) + "##addprop_" + (path.empty() ? std::string(".") : path) + "_" + component + "_" + field;
            if (ImGui::SmallButton(id.c_str()))
            {
                AddProperty(path, component, field, &m_Status);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
        };
        std::function<void(Entity, int)> drawEntity = [&](Entity e, int depth) {
            const std::string path = e == root ? std::string() : PropertyClips::PathOf(root, e);
            const std::string tag = path.empty() ? std::string(".") : path;
            bool open = true;
            if (depth > 0)
                open = ImGui::TreeNodeEx((std::string(ICON_FA_CUBE " ") + e.GetComponent<TagComponent>().Tag + "##ent_" + tag).c_str(),
                                         ImGuiTreeNodeFlags_SpanAvailWidth);
            if (!open)
                return;
            if (ImGui::TreeNodeEx(("Transform##tr_" + tag).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                for (const char* f : { "Position", "Rotation", "Scale" })
                    drawField(path, "Transform", f, f);
                ImGui::TreePop();
            }
            for (auto& info : ComponentRegistry::All())
            {
                if (!info.Has || !info.Has(e) || info.TypeName == "AnimationPlayerComponent" || info.TypeName == "TransformComponent")
                    continue;
                bool any = false;
                for (const auto& f : info.Fields)
                    if (AnimatableKind(f.Kind) && f.Ptr && !f.ReadOnly && f.Serialize)
                        any = true;
                if (!any)
                    continue;
                if (ImGui::TreeNodeEx((info.Name + "##comp_" + tag + "_" + info.TypeName).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    for (const auto& f : info.Fields)
                        if (AnimatableKind(f.Kind) && f.Ptr && !f.ReadOnly && f.Serialize)
                            drawField(path, info.TypeName, f.Name, f.Label.empty() ? f.Name : f.Label);
                    ImGui::TreePop();
                }
            }
            for (Entity c : e.GetChildren())
                drawEntity(c, depth + 1);
            if (depth > 0)
                ImGui::TreePop();
        };
        ImGui::TextDisabled(ICON_FA_CUBE " %s", root.GetComponent<TagComponent>().Tag.c_str());
        ImGui::Separator();
        drawEntity(root, 0);
        ImGui::EndPopup();
    }

    std::vector<std::string> AnimationWindow::ScriptMethods() const
    {
        std::set<std::string> names;
        Entity root = GetRoot();
        if (!root || !root.HasComponent<ConquerorScriptComponent>())
            return {};
        static const std::set<std::string> lifecycle = { "Start", "Update", "FixedUpdate", "LateUpdate", "Awake", "OnCreate", "OnUpdate",
                                                         "OnDestroy", "OnEnable", "OnDisable", "__init__" };
        for (const auto& s : root.GetComponent<ConquerorScriptComponent>().Scripts)
        {
            const std::string file = SceneSerializer::ResolveAssetPath(s.ScriptPath);
            const std::string text = ReadText(file);
            std::string ext = fs::path(file).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            std::regex re;
            if (ext == ".cqs")
                re = std::regex(R"(\bfunc\s+([A-Za-z_]\w*)\s*\()");
            else if (ext == ".py")
                re = std::regex(R"(\bdef\s+([A-Za-z_]\w*)\s*\()");
            else if (ext == ".luaq")
                re = std::regex(R"(\bfunction\s+(?:[A-Za-z_]\w*[.:])?([A-Za-z_]\w*)\s*\()");
            else
                re = std::regex(R"(\bvoid\s+([A-Za-z_]\w*)\s*\()");
            for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
            {
                const std::string n = (*it)[1].str();
                if (!lifecycle.count(n))
                    names.insert(n);
            }
        }
        return std::vector<std::string>(names.begin(), names.end());
    }

    void AnimationWindow::DrawEventPopup()
    {
        if (m_OpenEventPopup)
        {
            ImGui::OpenPopup("##AnimEventPopup");
            m_OpenEventPopup = false;
        }
        if (!ImGui::BeginPopup("##AnimEventPopup"))
            return;
        if (m_SelEvent < 0 || m_SelEvent >= (int)m_Clip.Events.size())
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        auto& e = m_Clip.Events[(size_t)m_SelEvent];
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_BOOKMARK);
        ImGui::SameLine();
        ImGui::TextUnformatted("Animation Event");
        ImGui::Separator();
        char fn[128];
        std::snprintf(fn, sizeof(fn), "%s", e.Function.c_str());
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputText("Function##AnimEventFunction", fn, sizeof(fn)))
        {
            BeginEdit();
            e.Function = fn;
            MarkChanged();
        }
        const auto methods = ScriptMethods();
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("##AnimEventMethods", methods.empty() ? "(no script methods found)" : "Pick a script method"))
        {
            for (const auto& m : methods)
                if (ImGui::Selectable((m + "##m").c_str(), m == e.Function))
                {
                    BeginEdit();
                    e.Function = m;
                    MarkChanged();
                }
            ImGui::EndCombo();
        }
        char arg[256];
        std::snprintf(arg, sizeof(arg), "%s", e.Argument.c_str());
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputText("String##AnimEventArg", arg, sizeof(arg)))
        {
            BeginEdit();
            e.Argument = arg;
            MarkChanged();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Optional text passed to the method");
        float t = e.Time;
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::DragFloat("Time (s)##AnimEventTime", &t, 0.01f, 0.0f, 3600.0f, "%.3f"))
        {
            BeginEdit();
            e.Time = std::max(0.0f, Snap(t));
            m_Clip.Length = std::max(m_Clip.Length, e.Time);
            MarkChanged();
        }
        ImGui::Spacing();
        if (ImGui::Button(ICON_FA_TRASH " Delete##AnimEventDelete"))
        {
            m_SelKeys.clear();
            DeleteSelection();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close##AnimEventClose"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void AnimationWindow::HandleShortcuts()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput || !m_ClipLoaded)
            return;
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))
            DeleteSelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
            CopySelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
            PasteAtTime();
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K))
            AddKeyAt(-1, m_Time, &m_Status);
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space) && EditorState::Get().IsEditing() && GetRoot())
            SetPlaying(!m_Playing);
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F))
        {
            const float w = (m_CurveAreaMax.x > m_CurveAreaMin.x ? m_CurveAreaMax.x - m_CurveAreaMin.x : 600.0f) - 40.0f;
            m_Zoom = std::clamp(w / std::max(ViewEnd(), FrameTime()), 10.0f, 6000.0f);
            m_ScrollTime = -10.0f / m_Zoom;
            m_FitPending = true;
        }
        // , . : önceki / sonraki kare; Alt: önceki / sonraki anahtar; Shift: ilk / son anahtar (Unity)
        const bool prev = ImGui::IsKeyPressed(ImGuiKey_Comma), next = ImGui::IsKeyPressed(ImGuiKey_Period);
        if (prev || next)
        {
            const auto times = AllKeyTimes();
            if (io.KeyShift)
                SetTime(times.empty() ? 0.0f : (prev ? times.front() : times.back()));
            else if (io.KeyAlt)
            {
                float t = m_Time;
                if (prev)
                {
                    for (float k : times)
                        if (k < m_Time - 1e-4f)
                            t = k;
                }
                else
                    for (float k : times)
                        if (k > m_Time + 1e-4f)
                        {
                            t = k;
                            break;
                        }
                SetTime(t);
            }
            else
                SetTime(m_Time + (prev ? -FrameTime() : FrameTime()));
        }
    }
}
