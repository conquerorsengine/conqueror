#include "Scene/Signals.h"
#include "Assets/AssetUI.h"
#include "Scripting/ScriptRuntime.h"
#include <filesystem>
#include "TimelinePanel.h"
#include "EditorState.h"
#include "UI/EditorUI.h"
#include "UndoRedo/EntitySnapshotCommand.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Timeline/TimelineSystem.h"
#include "Core/Project/Project.h"
#include "Core/Animation/AnimationClip.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace fs = std::filesystem;

namespace Conqueror::Editor
{
    namespace
    {
        constexpr float kTrackHeight = 30.0f;
        constexpr float kRulerHeight = 24.0f;

        ImU32 TrackColor(TimelineTrackType t, float alpha = 1.0f)
        {
            ImVec4 c;
            switch (t)
            {
                case TimelineTrackType::Activation: c = ImVec4(0.36f, 0.75f, 0.42f, 1.0f); break;
                case TimelineTrackType::Transform: c = ImVec4(0.32f, 0.58f, 0.95f, 1.0f); break;
                case TimelineTrackType::Property: c = ImVec4(0.75f, 0.52f, 0.95f, 1.0f); break;
                case TimelineTrackType::Audio: c = ImVec4(0.95f, 0.66f, 0.25f, 1.0f); break;
                case TimelineTrackType::Signal: c = ImVec4(0.95f, 0.38f, 0.45f, 1.0f); break;
                case TimelineTrackType::Animation: c = ImVec4(0.30f, 0.82f, 0.85f, 1.0f); break;
                case TimelineTrackType::Playable: c = ImVec4(0.55f, 0.72f, 0.40f, 1.0f); break;
                default: c = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); break;
            }
            c.w = alpha;
            return ImGui::ColorConvertFloat4ToU32(c);
        }

        const char* TrackIcon(TimelineTrackType t)
        {
            switch (t)
            {
                case TimelineTrackType::Activation: return ICON_FA_TOGGLE_ON;
                case TimelineTrackType::Transform: return ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT;
                case TimelineTrackType::Property: return ICON_FA_SLIDERS;
                case TimelineTrackType::Audio: return ICON_FA_VOLUME_HIGH;
                case TimelineTrackType::Signal: return ICON_FA_BOLT;
                case TimelineTrackType::Animation: return ICON_FA_PERSON_RUNNING;
                case TimelineTrackType::Playable: return ICON_FA_FILE_CODE;
                default: return ICON_FA_CIRCLE;
            }
        }

        bool IsKeyTrack(const TimelineTrack& t)
        {
            return t.Type == TimelineTrackType::Transform || t.Type == TimelineTrackType::Property;
        }

        bool IsClipTrack(const TimelineTrack& t)
        {
            return t.Type == TimelineTrackType::Activation || t.Type == TimelineTrackType::Audio || t.Type == TimelineTrackType::Animation ||
                   t.Type == TimelineTrackType::Playable;
        }

        // Parçadaki tüm kanalların anahtar zamanları (birleşik)
        std::vector<float> KeyTimes(const TimelineTrack& t, float frameRate)
        {
            std::vector<float> times;
            const float half = 0.5f / std::max(1.0f, frameRate);
            for (const auto& ch : t.Channels)
                for (const auto& k : ch.Keys)
                    if (std::none_of(times.begin(), times.end(), [&](float x) { return std::abs(x - k.Time) <= half; }))
                        times.push_back(k.Time);
            std::sort(times.begin(), times.end());
            return times;
        }

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

        glm::vec4 ReadFieldValue(const ComponentField& field, void* comp)
        {
            void* p = field.Ptr ? field.Ptr(comp) : nullptr;
            if (!p) return glm::vec4(0.0f);
            switch (field.Kind)
            {
                case FieldKind::Float: case FieldKind::Angle: return glm::vec4(*static_cast<float*>(p), 0, 0, 0);
                case FieldKind::Int: return glm::vec4((float)*static_cast<int*>(p), 0, 0, 0);
                case FieldKind::Bool: return glm::vec4(*static_cast<bool*>(p) ? 1.0f : 0.0f, 0, 0, 0);
                case FieldKind::Vec2: return glm::vec4(*static_cast<glm::vec2*>(p), 0, 0);
                case FieldKind::Vec3: case FieldKind::Color3: return glm::vec4(*static_cast<glm::vec3*>(p), 0);
                case FieldKind::Vec4: case FieldKind::Color4: return *static_cast<glm::vec4*>(p);
                default: return glm::vec4(0.0f);
            }
        }

        int FieldComponents(FieldKind k)
        {
            switch (k)
            {
                case FieldKind::Vec2: return 2;
                case FieldKind::Vec3: case FieldKind::Color3: return 3;
                case FieldKind::Vec4: case FieldKind::Color4: return 4;
                default: return 1;
            }
        }
    }

    // ── Bağlam / varlık ──
    void TimelinePanel::SetContext(const std::shared_ptr<Scene>& scene)
    {
        if (scene == m_Scene)
            return;
        StopPreview();
        m_Scene = scene;
        m_LastEvaluated = -1.0;
    }

    Entity TimelinePanel::DirectorEntity() const
    {
        if (!m_Scene || !m_DirectorUUID)
            return {};
        Entity e = m_Scene->FindEntityByUUID(m_DirectorUUID);
        return e && e.HasComponent<PlayableDirectorComponent>() ? e : Entity{};
    }

    bool TimelinePanel::SelectDirector(Entity e)
    {
        if (!e || !e.HasComponent<PlayableDirectorComponent>())
            return false;
        const uint64_t uuid = (uint64_t)e.GetComponent<IDComponent>().ID;
        if (uuid != m_DirectorUUID)
        {
            StopPreview();
            m_DirectorUUID = uuid;
            m_Sel = {};
            m_UndoStack.clear();
            m_RedoStack.clear();
        }
        LoadAsset();
        return (bool)m_Asset;
    }

    bool TimelinePanel::LoadAsset()
    {
        Entity d = DirectorEntity();
        if (!d)
        {
            m_Asset.reset();
            m_AssetPath.clear();
            return false;
        }
        const std::string path = d.GetComponent<PlayableDirectorComponent>().Timeline;
        if (path == m_AssetPath && m_Asset)
            return true;
        m_AssetPath = path;
        TimelineSystem::InvalidateAsset(path);
        auto loaded = TimelineSystem::GetAsset(path);
        // Düzenleme kopyası (önbellekteki çalışma zamanı kopyasından bağımsız)
        m_Asset = loaded ? std::make_shared<TimelineAsset>(*loaded) : nullptr;
        m_LastEvaluated = -1.0;
        return (bool)m_Asset;
    }

    void TimelinePanel::Save()
    {
        if (!m_Asset || m_AssetPath.empty())
            return;
        std::string error;
        if (!m_Asset->Save(TimelineSystem::ResolvePath(m_AssetPath), &error))
            m_Status = error;
        TimelineSystem::InvalidateAsset(m_AssetPath);
    }

    void TimelinePanel::PushUndo()
    {
        if (!m_Asset)
            return;
        m_UndoStack.push_back(*m_Asset);
        if (m_UndoStack.size() > 100)
            m_UndoStack.erase(m_UndoStack.begin());
        m_RedoStack.clear();
    }

    void TimelinePanel::MarkChanged()
    {
        Save();
        m_LastEvaluated = -1.0;   // önizleme yeniden değerlendirilsin
    }

    void TimelinePanel::Undo()
    {
        if (m_UndoStack.empty() || !m_Asset)
            return;
        m_RedoStack.push_back(*m_Asset);
        *m_Asset = m_UndoStack.back();
        m_UndoStack.pop_back();
        m_Sel = {};
        MarkChanged();
    }

    void TimelinePanel::Redo()
    {
        if (m_RedoStack.empty() || !m_Asset)
            return;
        m_UndoStack.push_back(*m_Asset);
        *m_Asset = m_RedoStack.back();
        m_RedoStack.pop_back();
        m_Sel = {};
        MarkChanged();
    }

    float TimelinePanel::Snap(float t) const
    {
        const float fps = m_Asset ? std::max(1.0f, m_Asset->FrameRate) : 60.0f;
        if (ImGui::GetIO().KeyShift)
            return std::max(0.0f, t);   // Shift: kareye yapışmadan
        return std::max(0.0f, std::round(t * fps) / fps);
    }

    Entity TimelinePanel::BoundEntity(int track) const
    {
        Entity d = DirectorEntity();
        if (!d || !m_Scene)
            return {};
        return TimelineSystem::ResolveBinding(*m_Scene, d, (size_t)track);
    }

    void TimelinePanel::EnsureBindings()
    {
        Entity d = DirectorEntity();
        if (!d || !m_Asset)
            return;
        auto& b = d.GetComponent<PlayableDirectorComponent>().Bindings;
        if (b.size() < m_Asset->Tracks.size())
            b.resize(m_Asset->Tracks.size(), 0);
    }

    // ── Önizleme ──
    void TimelinePanel::StartPreview()
    {
        if (m_Previewing || !m_Asset || !m_Scene)
            return;
        Entity d = DirectorEntity();
        if (!d)
            return;
        // Bağlı tüm varlıkların durumu saklanır; önizleme bitince geri yüklenir (Unity önizleme modu)
        m_Snapshots.clear();
        std::set<uint64_t> seen;
        auto capture = [&](Entity e) {
            if (!e) return;
            const uint64_t id = (uint64_t)e.GetComponent<IDComponent>().ID;
            if (seen.insert(id).second)
                m_Snapshots.emplace_back(id, EntitySnapshotCommand::Capture(e));
        };
        capture(d);
        for (int i = 0; i < (int)m_Asset->Tracks.size(); ++i)
            capture(BoundEntity(i));
        m_Previewing = true;
        m_LastEvaluated = -1.0;
    }

    void TimelinePanel::StopPreview()
    {
        if (!m_Previewing)
            return;
        m_Previewing = false;
        m_PreviewPlaying = false;
        if (m_Scene)
            for (auto& [id, snap] : m_Snapshots)
                if (Entity e = m_Scene->FindEntityByUUID(id))
                    EntitySnapshotCommand::Restore(snap, e);
        m_Snapshots.clear();
    }

    void TimelinePanel::SetPreviewTime(double t)
    {
        m_PreviewTime = std::clamp(t, 0.0, m_Asset ? (double)m_Asset->Duration : 0.0);
        if (EditorState::Get().IsEditing())
            StartPreview();
    }

    void TimelinePanel::ApplyPreview()
    {
        if (!m_Previewing || !m_Scene || std::abs(m_LastEvaluated - m_PreviewTime) < 1e-9)
            return;
        Entity d = DirectorEntity();
        if (!d)
            return;
        // Düzenlenen kopya (sürükleme sırasında henüz kaydedilmemiş olabilir)
        TimelineSystem::Evaluate(*m_Scene, d, *m_Asset, m_PreviewTime);
        m_Scene->UpdateWorldTransforms();
        m_LastEvaluated = m_PreviewTime;
    }

    // ── Oluşturma / düzenleme işlemleri ──
    bool TimelinePanel::CreateTimelineFor(Entity e, std::string* error)
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
            if (!std::isalnum((unsigned char)c) && c != '_' && c != '-')
                c = '_';
        if (name.empty()) name = "Timeline";
        fs::path rel = fs::path("Assets") / "Timelines" / (name + ".cqtimeline");
        std::error_code ec;
        for (int i = 2; fs::exists(projectDir / rel, ec); ++i)
            rel = fs::path("Assets") / "Timelines" / (name + "_" + std::to_string(i) + ".cqtimeline");
        TimelineAsset asset;
        TimelineTrack t;
        t.Name = name;
        t.Type = TimelineTrackType::Transform;
        asset.Tracks.push_back(t);
        if (!asset.Save(projectDir / rel, error))
            return false;
        if (!e.HasComponent<PlayableDirectorComponent>())
            e.AddComponent<PlayableDirectorComponent>();
        auto& d = e.GetComponent<PlayableDirectorComponent>();
        d.Timeline = rel.generic_string();
        d.Bindings = { 0 };
        m_AssetPath.clear();
        return SelectDirector(e);
    }

    bool TimelinePanel::AddTrack(TimelineTrackType type, const std::string& bindingName, std::string* error)
    {
        if (!m_Asset)
        {
            if (error) *error = "No timeline";
            return false;
        }
        PushUndo();
        EnsureBindings();
        TimelineTrack t;
        t.Type = type;
        t.Name = std::string(TimelineAsset::TrackTypeName(type)) + " Track";
        uint64_t binding = 0;
        if (!bindingName.empty() && m_Scene)
        {
            m_Scene->m_Registry.view<TagComponent, IDComponent>().each([&](auto, auto& tag, auto& id) {
                if (!binding && tag.Tag == bindingName)
                    binding = (uint64_t)id.ID;
            });
            if (!binding)
            {
                if (error) *error = "No object named " + bindingName;
                m_UndoStack.pop_back();
                return false;
            }
            t.Name = bindingName;
        }
        m_Asset->Tracks.push_back(t);
        if (Entity d = DirectorEntity())
            d.GetComponent<PlayableDirectorComponent>().Bindings.push_back(binding);
        EnsureBindings();
        m_Sel = { SelKind::Track, (int)m_Asset->Tracks.size() - 1, -1, 0.0f };
        MarkChanged();
        return true;
    }

    bool TimelinePanel::AddKeyAt(int track, float time, std::string* error)
    {
        if (!m_Asset || track < 0 || track >= (int)m_Asset->Tracks.size())
        {
            if (error) *error = "No such track";
            return false;
        }
        auto& t = m_Asset->Tracks[(size_t)track];
        Entity target = BoundEntity(track);
        if (!target)
        {
            if (error) *error = "Track is not bound to an object";
            return false;
        }
        if (t.Type == TimelineTrackType::Transform)
        {
            PushUndo();
            const auto& tc = target.GetComponent<TransformComponent>();
            SetKey(t.GetOrAddChannel("Position"), time, glm::vec4(tc.Position, 0.0f), m_Asset->FrameRate);
            SetKey(t.GetOrAddChannel("Rotation"), time, glm::vec4(glm::degrees(tc.Rotation), 0.0f), m_Asset->FrameRate);
            SetKey(t.GetOrAddChannel("Scale"), time, glm::vec4(tc.Scale, 0.0f), m_Asset->FrameRate);
        }
        else if (t.Type == TimelineTrackType::Property)
        {
            ComponentInfo* info = ComponentRegistry::Find(t.Component);
            const ComponentField* field = info ? ComponentRegistry::FindField(*info, t.Field) : nullptr;
            if (!info || !field || !info->Has(target))
            {
                if (error) *error = "Choose the component and field of this property track first";
                return false;
            }
            PushUndo();
            SetKey(t.GetOrAddChannel("Value"), time, ReadFieldValue(*field, info->Get(target)), m_Asset->FrameRate);
        }
        else
        {
            if (error) *error = "Keys belong to Transform and Property tracks";
            return false;
        }
        m_Sel = { SelKind::Key, track, -1, time };
        MarkChanged();
        return true;
    }

    bool TimelinePanel::AddClipAt(int track, float start, float duration, const std::string& asset, std::string* error)
    {
        if (!m_Asset || track < 0 || track >= (int)m_Asset->Tracks.size() || !IsClipTrack(m_Asset->Tracks[(size_t)track]))
        {
            if (error) *error = "Clips belong to Activation, Audio and Animation tracks";
            return false;
        }
        PushUndo();
        auto& t = m_Asset->Tracks[(size_t)track];
        TimelineClip c;
        c.Start = std::max(0.0f, start);
        c.Duration = std::max(1.0f / m_Asset->FrameRate, duration);
        c.Asset = asset;
        c.Name = asset.empty() ? (t.Type == TimelineTrackType::Activation ? "Active" : "Clip") : fs::path(asset).stem().string();
        t.Clips.push_back(c);
        std::sort(t.Clips.begin(), t.Clips.end(), [](const TimelineClip& a, const TimelineClip& b) { return a.Start < b.Start; });
        for (int i = 0; i < (int)t.Clips.size(); ++i)
            if (t.Clips[(size_t)i].Start == c.Start)
                m_Sel = { SelKind::Clip, track, i, 0.0f };
        m_Asset->Duration = std::max(m_Asset->Duration, c.End());
        MarkChanged();
        return true;
    }

    bool TimelinePanel::AddMarkerAt(int track, float time, const std::string& method, std::string* error)
    {
        if (!m_Asset || track < 0 || track >= (int)m_Asset->Tracks.size() || m_Asset->Tracks[(size_t)track].Type != TimelineTrackType::Signal)
        {
            if (error) *error = "Signals belong to Signal tracks";
            return false;
        }
        PushUndo();
        auto& t = m_Asset->Tracks[(size_t)track];
        // ".cqsignal" yolu: Signal Emitter (bağlı nesnenin Signal Receiver'ı tepki verir); değilse betik metodu
        TimelineMarker marker{ std::max(0.0f, time), method, "", "" };
        const std::string ext = SignalAssets::Extension;
        if (method.size() > ext.size() && method.compare(method.size() - ext.size(), ext.size(), ext) == 0)
        {
            marker.Method.clear();
            marker.Signal = method;
        }
        t.Markers.push_back(marker);
        std::sort(t.Markers.begin(), t.Markers.end(), [](const TimelineMarker& a, const TimelineMarker& b) { return a.Time < b.Time; });
        MarkChanged();
        return true;
    }

    bool TimelinePanel::SetPropertyTarget(int track, const std::string& component, const std::string& field, std::string* error)
    {
        if (!m_Asset || track < 0 || track >= (int)m_Asset->Tracks.size() || m_Asset->Tracks[(size_t)track].Type != TimelineTrackType::Property)
        {
            if (error) *error = "Not a property track";
            return false;
        }
        ComponentInfo* info = ComponentRegistry::Find(component);
        const ComponentField* f = info ? ComponentRegistry::FindField(*info, field) : nullptr;
        if (!info || !f || !AnimatableKind(f->Kind) || !f->Ptr)
        {
            if (error) *error = "Field " + component + "." + field + " cannot be animated";
            return false;
        }
        PushUndo();
        auto& t = m_Asset->Tracks[(size_t)track];
        t.Component = info->TypeName;
        t.Field = f->Name;
        t.Name = info->Name + " / " + f->Label;
        MarkChanged();
        return true;
    }

    // ── Çizim ──
    void TimelinePanel::OnImGuiRender(bool* open, bool playing)
    {
        if (!ImGui::Begin(ICON_FA_FILM "  Timeline###Timeline", open))
        {
            ImGui::End();
            return;
        }

        // Seçimi izle (kilitli değilse): Playable Director'ı olan varlık seçilince ona geçilir
        Entity selected = EditorState::Get().GetSelectedEntity();
        if (!m_Lock && selected && selected.GetScene() == m_Scene.get() && selected.HasComponent<PlayableDirectorComponent>())
            SelectDirector(selected);
        else if (m_DirectorUUID && !DirectorEntity())
        {
            m_Asset.reset();
            m_AssetPath.clear();
        }
        else
            LoadAsset();

        if (playing && m_Previewing)
            StopPreview();

        if (!m_Asset)
        {
            DrawNoDirector();
            ImGui::End();
            return;
        }

        // Önizleme oynatma
        if (m_Previewing && m_PreviewPlaying)
        {
            m_PreviewTime += ImGui::GetIO().DeltaTime;
            if (m_PreviewTime >= m_Asset->Duration)
                m_PreviewTime = std::fmod(m_PreviewTime, std::max(0.001, (double)m_Asset->Duration));
        }
        ApplyPreview();

        // Kısayollar (pencere odaktayken)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
        {
            const bool ctrl = ImGui::GetIO().KeyCtrl;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo();
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
            else if (!playing && ImGui::IsKeyPressed(ImGuiKey_Space, false))
            {
                StartPreview();
                m_PreviewPlaying = !m_PreviewPlaying;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_Sel.Kind != SelKind::None && m_Sel.Track >= 0 &&
                     m_Sel.Track < (int)m_Asset->Tracks.size())
            {
                PushUndo();
                auto& t = m_Asset->Tracks[(size_t)m_Sel.Track];
                switch (m_Sel.Kind)
                {
                    case SelKind::Clip: if (m_Sel.Index < (int)t.Clips.size()) t.Clips.erase(t.Clips.begin() + m_Sel.Index); break;
                    case SelKind::Marker: if (m_Sel.Index < (int)t.Markers.size()) t.Markers.erase(t.Markers.begin() + m_Sel.Index); break;
                    case SelKind::Key:
                    {
                        const float half = 0.5f / m_Asset->FrameRate;
                        for (auto& ch : t.Channels)
                            ch.Keys.erase(std::remove_if(ch.Keys.begin(), ch.Keys.end(), [&](const TimelineKey& k) { return std::abs(k.Time - m_Sel.KeyTime) <= half; }), ch.Keys.end());
                        break;
                    }
                    case SelKind::Track:
                    {
                        m_Asset->Tracks.erase(m_Asset->Tracks.begin() + m_Sel.Track);
                        if (Entity d = DirectorEntity())
                        {
                            auto& b = d.GetComponent<PlayableDirectorComponent>().Bindings;
                            if (m_Sel.Track < (int)b.size())
                                b.erase(b.begin() + m_Sel.Track);
                        }
                        break;
                    }
                    default: break;
                }
                m_Sel = {};
                MarkChanged();
            }
        }

        DrawToolbar(playing);
        ImGui::Separator();
        // Seçim ayrıntıları sağ sütunda (alt yuva kısa olduğundan parça listesi yüksekliği korunur)
        const float inspectorW = m_Sel.Kind != SelKind::None ? std::min(330.0f, ImGui::GetContentRegionAvail().x * 0.4f) : 0.0f;
        ImGui::BeginChild("##tracks", ImVec2(inspectorW > 0.0f ? -inspectorW - ImGui::GetStyle().ItemSpacing.x : 0.0f, 0.0f),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse);
        DrawTracks(playing);
        ImGui::EndChild();
        if (inspectorW > 0.0f)
        {
            ImGui::SameLine();
            ImGui::BeginChild("##selinspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
            DrawSelectionInspector();
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void TimelinePanel::DrawNoDirector()
    {
        Entity selected = EditorState::Get().GetSelectedEntity();
        ImGui::Spacing();
        if (selected && selected.GetScene() == m_Scene.get())
        {
            const std::string text = "To start animating " + selected.GetComponent<TagComponent>().Tag +
                                     ", create a Timeline asset and a Playable Director component.";
            ImGui::TextWrapped("%s", text.c_str());
            ImGui::Spacing();
            if (UI::AccentButton(ICON_FA_PLUS "  Create Timeline", ImVec2(220, 32)))
            {
                std::string err;
                if (!CreateTimelineFor(selected, &err))
                    m_Status = err;
            }
        }
        else
        {
            ImGui::TextDisabled("Select an object with a Playable Director, or select any object to create a Timeline.");
        }
        if (!m_Status.empty())
            UI::InfoBox(m_Status.c_str(), UI::InfoType::Warning);
    }

    void TimelinePanel::DrawToolbar(bool playing)
    {
        Entity d = DirectorEntity();
        auto* dc = d ? &d.GetComponent<PlayableDirectorComponent>() : nullptr;
        const double time = playing && dc ? dc->Time : m_PreviewTime;

        // Önizleme anahtarı
        if (!playing)
        {
            const bool prev = m_Previewing;
            if (prev) ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
            if (ImGui::Button(ICON_FA_EYE " Preview"))
            {
                if (m_Previewing) StopPreview();
                else StartPreview();
            }
            if (prev) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Preview the timeline in the Scene view. Objects return to their saved state when preview ends.");
            ImGui::SameLine();
        }
        if (ImGui::Button(ICON_FA_BACKWARD_STEP))
        {
            if (playing && d) TimelineSystem::SetTime(*m_Scene, d, 0.0);
            else SetPreviewTime(0.0);
        }
        ImGui::SameLine();
        const bool isPlaying = playing ? (dc && dc->Playing) : m_PreviewPlaying;
        if (ImGui::Button(isPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY))
        {
            if (playing && d)
            {
                if (dc->Playing) TimelineSystem::Pause(*m_Scene, d);
                else TimelineSystem::Play(*m_Scene, d);
            }
            else
            {
                StartPreview();
                m_PreviewPlaying = !m_PreviewPlaying;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FORWARD_STEP))
        {
            if (playing && d) TimelineSystem::SetTime(*m_Scene, d, m_Asset->Duration);
            else SetPreviewTime(m_Asset->Duration);
        }
        ImGui::SameLine();
        const int frame = (int)std::round(time * m_Asset->FrameRate);
        ImGui::Text("%6.2f s  (frame %d)", time, frame);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        float duration = m_Asset->Duration;
        if (ImGui::DragFloat("##dur", &duration, 0.05f, 0.1f, 3600.0f, "%.2f s"))
        {
            if (!m_DragUndoPushed) { PushUndo(); m_DragUndoPushed = true; }
            m_Asset->Duration = std::max(0.1f, duration);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { m_DragUndoPushed = false; MarkChanged(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duration");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        static const float rates[] = { 24.0f, 25.0f, 30.0f, 50.0f, 60.0f, 120.0f };
        char rateLabel[16];
        std::snprintf(rateLabel, sizeof(rateLabel), "%.0f fps", m_Asset->FrameRate);
        if (ImGui::BeginCombo("##fps", rateLabel))
        {
            for (float r : rates)
            {
                char l[16];
                std::snprintf(l, sizeof(l), "%.0f fps", r);
                if (ImGui::Selectable(l, r == m_Asset->FrameRate))
                {
                    PushUndo();
                    m_Asset->FrameRate = r;
                    MarkChanged();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLUS " Track"))
            ImGui::OpenPopup("##addtrack");
        if (ImGui::BeginPopup("##addtrack"))
        {
            DrawAddTrackMenu();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        const bool canKey = m_Sel.Track >= 0 && m_Sel.Track < (int)m_Asset->Tracks.size() && IsKeyTrack(m_Asset->Tracks[(size_t)m_Sel.Track]);
        ImGui::BeginDisabled(!canKey);
        if (ImGui::Button(ICON_FA_DIAMOND " Key"))
        {
            std::string err;
            if (!AddKeyAt(m_Sel.Track, Snap((float)time), &err))
                m_Status = err;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Record the bound object's current values as a key at the playhead (select a Transform / Property track)");

        // Sağ: kilit + yakınlaştırma + dosya
        const float rightW = 260.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowContentRegionMax().x - rightW));
        if (ImGui::Button(m_Lock ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN))
            m_Lock = !m_Lock;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(m_Lock ? "Locked to this director" : "Follows the selected object");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("##zoom", &m_Zoom, 20.0f, 800.0f, ICON_FA_MAGNIFYING_GLASS " %.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", fs::path(m_AssetPath).filename().string().c_str());
        if (!m_Status.empty())
        {
            ImGui::TextColored(UI::Colors::Warning, "%s", m_Status.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK)) m_Status.clear();
        }
    }

    void TimelinePanel::DrawAddTrackMenu()
    {
        Entity selected = EditorState::Get().GetSelectedEntity();
        const std::string bindName = selected && selected != DirectorEntity() && selected.GetScene() == m_Scene.get()
                                         ? selected.GetComponent<TagComponent>().Tag : std::string();
        if (!bindName.empty())
            ImGui::TextDisabled("Bound to: %s", bindName.c_str());
        else
            ImGui::TextDisabled("Bound to the director object (drag another object onto the track later)");
        for (int i = 0; i < (int)TimelineTrackType::Count; ++i)
        {
            const auto type = (TimelineTrackType)i;
            std::string label = std::string(TrackIcon(type)) + "  " + TimelineAsset::TrackTypeName(type) + " Track";
            if (ImGui::MenuItem(label.c_str()))
            {
                std::string err;
                if (!AddTrack(type, bindName, &err))
                    m_Status = err;
            }
        }
    }

    void TimelinePanel::DrawTracks(bool playing)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        m_LanesX0 = origin.x + m_HeaderWidth;
        const float lanesW = std::max(10.0f, avail.x - m_HeaderWidth);
        const int trackCount = (int)m_Asset->Tracks.size();
        const float contentH = kRulerHeight + std::max(1, trackCount) * kTrackHeight + 40.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Tekerlek: parça başlıkları üzerinde dikey kaydırma; şeritlerde yakınlaştırma, Shift ile yatay kaydırma
        if (ImGui::IsWindowHovered())
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                if (ImGui::GetIO().MousePos.x <= m_LanesX0)
                    ImGui::SetScrollY(ImGui::GetScrollY() - wheel * kTrackHeight * 2.0f);
                else if (ImGui::GetIO().KeyShift)
                    m_ScrollTime -= wheel * 60.0f / m_Zoom;
                else
                {
                    // Fare altındaki zaman sabit kalacak şekilde yakınlaştır
                    const float mouseT = XToTime(ImGui::GetIO().MousePos.x);
                    m_Zoom = std::clamp(m_Zoom * std::pow(1.15f, wheel), 20.0f, 800.0f);
                    m_ScrollTime = mouseT - (ImGui::GetIO().MousePos.x - m_LanesX0) / m_Zoom;
                }
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                m_ScrollTime -= ImGui::GetIO().MouseDelta.x / m_Zoom;
        }
        m_ScrollTime = std::max(-0.5f, m_ScrollTime);

        // Arka plan: başlık sütunu ve şeritler
        dl->AddRectFilled(origin, ImVec2(origin.x + m_HeaderWidth, origin.y + std::max(contentH, avail.y)), IM_COL32(28, 29, 33, 255));
        dl->AddRectFilled(ImVec2(m_LanesX0, origin.y), ImVec2(m_LanesX0 + lanesW, origin.y + std::max(contentH, avail.y)), IM_COL32(22, 23, 26, 255));
        // Çizelge sonu (Duration) gölgesi
        const float endX = TimeToX(m_Asset->Duration);
        if (endX < m_LanesX0 + lanesW)
            dl->AddRectFilled(ImVec2(std::max(endX, m_LanesX0), origin.y + kRulerHeight), ImVec2(m_LanesX0 + lanesW, origin.y + std::max(contentH, avail.y)),
                              IM_COL32(0, 0, 0, 70));

        DrawRuler(ImVec2(m_LanesX0, origin.y), lanesW, kRulerHeight, playing);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y));
        ImGui::Dummy(ImVec2(m_HeaderWidth, kRulerHeight));

        for (int i = 0; i < trackCount; ++i)
        {
            const float y = origin.y + kRulerHeight + i * kTrackHeight;
            DrawTrackHeader(i, ImVec2(origin.x, y), m_HeaderWidth, kTrackHeight);
            DrawLane(i, ImVec2(m_LanesX0, y), lanesW, kTrackHeight, playing);
            dl->AddLine(ImVec2(origin.x, y + kTrackHeight - 1.0f), ImVec2(m_LanesX0 + lanesW, y + kTrackHeight - 1.0f), IM_COL32(45, 47, 52, 255));
        }
        if (trackCount == 0)
            dl->AddText(ImVec2(m_LanesX0 + 12.0f, origin.y + kRulerHeight + 8.0f), IM_COL32(150, 152, 158, 255),
                        "No tracks. Use \"+ Track\" (a selected object is bound automatically).");

        // Oynatma kafası
        Entity d = DirectorEntity();
        const double time = playing && d ? d.GetComponent<PlayableDirectorComponent>().Time : m_PreviewTime;
        const float px = TimeToX((float)time);
        if (px >= m_LanesX0 && px <= m_LanesX0 + lanesW)
        {
            dl->AddLine(ImVec2(px, origin.y), ImVec2(px, origin.y + std::max(contentH, avail.y)), IM_COL32(255, 80, 80, 255), 1.5f);
            dl->AddTriangleFilled(ImVec2(px - 6.0f, origin.y), ImVec2(px + 6.0f, origin.y), ImVec2(px, origin.y + 8.0f), IM_COL32(255, 80, 80, 255));
        }

        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + contentH));
        ImGui::Dummy(ImVec2(1, 1));

        // Sürükleme bitti: kaydet
        if (m_Drag != DragKind::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (m_Drag != DragKind::Playhead && m_Drag != DragKind::Pan)
            {
                // Sıralamayı koru
                for (auto& t : m_Asset->Tracks)
                {
                    std::sort(t.Clips.begin(), t.Clips.end(), [](const TimelineClip& a, const TimelineClip& b) { return a.Start < b.Start; });
                    std::sort(t.Markers.begin(), t.Markers.end(), [](const TimelineMarker& a, const TimelineMarker& b) { return a.Time < b.Time; });
                    for (auto& ch : t.Channels)
                        std::sort(ch.Keys.begin(), ch.Keys.end(), [](const TimelineKey& a, const TimelineKey& b) { return a.Time < b.Time; });
                }
                MarkChanged();
            }
            m_Drag = DragKind::None;
            m_DragUndoPushed = false;
        }
    }

    void TimelinePanel::DrawRuler(const ImVec2& pos, float width, float height, bool playing)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(34, 36, 40, 255));
        // Etiket aralığı: ~80 px
        double step = 80.0 / m_Zoom;
        const double mag = std::pow(10.0, std::floor(std::log10(step)));
        for (double m : { 1.0, 2.0, 5.0, 10.0 })
            if (m * mag >= step) { step = m * mag; break; }
        const double t0 = std::floor(std::max(0.0, (double)m_ScrollTime) / step) * step;
        for (double t = t0; TimeToX((float)t) < pos.x + width; t += step)
        {
            const float x = TimeToX((float)t);
            if (x < pos.x) continue;
            dl->AddLine(ImVec2(x, pos.y + height - 8.0f), ImVec2(x, pos.y + height), IM_COL32(170, 172, 178, 255));
            char label[32];
            std::snprintf(label, sizeof(label), step < 1.0 ? "%.2f" : "%.0f", t);
            dl->AddText(ImVec2(x + 3.0f, pos.y + 2.0f), IM_COL32(170, 172, 178, 255), label);
            // Ara çizgiler
            for (int k = 1; k < 5; ++k)
            {
                const float xs = TimeToX((float)(t + step * k / 5.0));
                if (xs < pos.x + width)
                    dl->AddLine(ImVec2(xs, pos.y + height - 4.0f), ImVec2(xs, pos.y + height), IM_COL32(110, 112, 118, 255));
            }
        }
        // Cetvelde tıkla/sürükle: oynatma kafası
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##ruler", ImVec2(width, height));
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_Drag = DragKind::Playhead;
            const double t = std::clamp((double)Snap(XToTime(ImGui::GetIO().MousePos.x)), 0.0, (double)m_Asset->Duration);
            Entity d = DirectorEntity();
            if (playing && d)
                TimelineSystem::SetTime(*m_Scene, d, t);
            else
                SetPreviewTime(t);
        }
    }

    void TimelinePanel::DrawTrackHeader(int index, const ImVec2& pos, float width, float height)
    {
        auto& t = m_Asset->Tracks[(size_t)index];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool selected = m_Sel.Track == index;
        dl->AddRectFilled(pos, ImVec2(pos.x + width - 1.0f, pos.y + height - 1.0f), selected ? IM_COL32(48, 58, 78, 255) : IM_COL32(32, 33, 38, 255));
        dl->AddRectFilled(pos, ImVec2(pos.x + 4.0f, pos.y + height - 1.0f), TrackColor(t.Type));
        ImGui::PushID(index);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 8.0f, pos.y + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(TrackColor(t.Type)), "%s", TrackIcon(t.Type));
        ImGui::SameLine();
        Entity bound = BoundEntity(index);
        const std::string boundName = bound ? bound.GetComponent<TagComponent>().Tag : "(missing)";
        std::string label = t.Name;
        if (label.size() > 20) label = label.substr(0, 19) + "...";
        ImGui::TextUnformatted(label.c_str());
        // Başlık tamamı: seç + varlık bırakma hedefi
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##hdr", ImVec2(width - 52.0f, height));
        if (ImGui::IsItemClicked())
            m_Sel = { SelKind::Track, index, -1, 0.0f };
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s track bound to %s\nDrag an object from the Hierarchy here to rebind.", TimelineAsset::TrackTypeName(t.Type), boundName.c_str());
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
            {
                Entity dropped = *(const Entity*)p->Data;
                if (dropped && dropped.GetScene() == m_Scene.get())
                {
                    EnsureBindings();
                    if (Entity d = DirectorEntity())
                    {
                        d.GetComponent<PlayableDirectorComponent>().Bindings[(size_t)index] = (uint64_t)dropped.GetComponent<IDComponent>().ID;
                        PushUndo();
                        t.Name = dropped.GetComponent<TagComponent>().Tag;
                        MarkChanged();
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        // Sessiz / kilit
        ImGui::SetCursorScreenPos(ImVec2(pos.x + width - 50.0f, pos.y + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::SmallButton(t.Muted ? ICON_FA_VOLUME_XMARK : ICON_FA_CIRCLE_CHECK))
        {
            PushUndo();
            t.Muted = !t.Muted;
            MarkChanged();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(t.Muted ? "Muted (click to enable)" : "Enabled (click to mute)");
        ImGui::SameLine(0, 2.0f);
        if (ImGui::SmallButton(t.Locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN))
        {
            PushUndo();
            t.Locked = !t.Locked;
            MarkChanged();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(t.Locked ? "Locked" : "Unlocked");
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    void TimelinePanel::DrawLane(int index, const ImVec2& pos, float width, float height, bool playing)
    {
        auto& t = m_Asset->Tracks[(size_t)index];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float alpha = t.Muted ? 0.35f : 1.0f;
        const float fps = std::max(1.0f, m_Asset->FrameRate);
        ImGui::PushID(1000 + index);
        dl->PushClipRect(pos, ImVec2(pos.x + width, pos.y + height), true);

        // Şerit alanı: boş tıklama seçimi temizler, sağ tık menüsü
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##lane", ImVec2(width, height - 1.0f));
        const bool laneHovered = ImGui::IsItemHovered();
        const float hoverTime = Snap(XToTime(mouse.x));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            m_Sel = { SelKind::Track, index, -1, 0.0f };
        if (ImGui::BeginPopupContextItem("##lanectx"))
        {
            const float at = Snap(XToTime(ImGui::GetMousePosOnOpeningCurrentPopup().x));
            std::string err;
            if (IsKeyTrack(t) && ImGui::MenuItem(ICON_FA_DIAMOND "  Add Key Here")) AddKeyAt(index, at, &err);
            if (IsClipTrack(t) && ImGui::MenuItem(ICON_FA_PLUS "  Add Clip Here")) AddClipAt(index, at, 1.0f, "", &err);
            if (t.Type == TimelineTrackType::Signal && ImGui::MenuItem(ICON_FA_BOLT "  Add Signal Here")) AddMarkerAt(index, at, "OnSignal", &err);
            if (!err.empty()) m_Status = err;
            ImGui::EndPopup();
        }
        // Playable Asset / davranış betiği bırakma
        if (t.Type == TimelineTrackType::Playable && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                fs::path path(std::string((const char*)p->Data, (size_t)p->DataSize - 1));
                if (path.extension() == ".cqplayable" || Scripting::ScriptRuntimes::IsScriptFile(path.string()))
                {
                    std::error_code ec;
                    fs::path rel = fs::relative(path, Project::GetActiveProjectDirectory(), ec);
                    std::string err;
                    AddClipAt(index, hoverTime, 2.0f, ec ? path.generic_string() : rel.generic_string(), &err);
                }
            }
            ImGui::EndDragDropTarget();
        }
        // Ses dosyası bırakma: imleç konumunda ses klibi
        if (t.Type == TimelineTrackType::Audio && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                fs::path path(std::string((const char*)p->Data, (size_t)p->DataSize - 1));
                const std::string ext = path.extension().string();
                if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
                {
                    std::error_code ec;
                    fs::path rel = fs::relative(path, Project::GetActiveProjectDirectory(), ec);
                    std::string err;
                    AddClipAt(index, hoverTime, 2.0f, ec ? path.generic_string() : rel.generic_string(), &err);
                }
            }
            ImGui::EndDragDropTarget();
        }

        const bool editable = !t.Locked;

        // Klipler
        for (int c = 0; c < (int)t.Clips.size(); ++c)
        {
            auto& clip = t.Clips[(size_t)c];
            const float x0 = TimeToX(clip.Start), x1 = TimeToX(clip.End());
            if (x1 < pos.x || x0 > pos.x + width)
                continue;
            const bool sel = m_Sel.Kind == SelKind::Clip && m_Sel.Track == index && m_Sel.Index == c;
            const ImVec2 a(x0, pos.y + 3.0f), b(std::max(x0 + 4.0f, x1), pos.y + height - 4.0f);
            dl->AddRectFilled(a, b, TrackColor(t.Type, 0.75f * alpha), 4.0f);
            dl->AddRect(a, b, sel ? IM_COL32(255, 255, 255, 255) : TrackColor(t.Type, alpha), 4.0f, 0, sel ? 2.0f : 1.0f);
            const std::string name = clip.Name.empty() ? std::string("Clip") : clip.Name;
            dl->PushClipRect(a, b, true);
            dl->AddText(ImVec2(x0 + 6.0f, pos.y + 7.0f), IM_COL32(15, 15, 18, 255), name.c_str());
            dl->PopClipRect();

            ImGui::PushID(c);
            const float edge = std::min(6.0f, (b.x - a.x) * 0.3f);
            // Gövde
            ImGui::SetCursorScreenPos(ImVec2(a.x + edge, a.y));
            ImGui::InvisibleButton("##clip", ImVec2(std::max(1.0f, b.x - a.x - edge * 2.0f), b.y - a.y));
            if (ImGui::IsItemClicked())
            {
                m_Sel = { SelKind::Clip, index, c, 0.0f };
                if (editable)
                {
                    m_Drag = DragKind::ClipMove;
                    m_DragOffset = XToTime(mouse.x) - clip.Start;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%.2f s - %.2f s", name.c_str(), clip.Start, clip.End());
            // Kenarlar (boyutlandırma)
            ImGui::SetCursorScreenPos(a);
            ImGui::InvisibleButton("##l", ImVec2(std::max(1.0f, edge), b.y - a.y));
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemClicked() && editable)
            {
                m_Sel = { SelKind::Clip, index, c, 0.0f };
                m_Drag = DragKind::ClipStart;
                m_DragOrigStart = clip.Start;
                m_DragOrigDuration = clip.Duration;
            }
            ImGui::SetCursorScreenPos(ImVec2(b.x - edge, a.y));
            ImGui::InvisibleButton("##r", ImVec2(std::max(1.0f, edge), b.y - a.y));
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemClicked() && editable)
            {
                m_Sel = { SelKind::Clip, index, c, 0.0f };
                m_Drag = DragKind::ClipEnd;
            }
            ImGui::PopID();
        }

        // Anahtarlar (birleşik zamanlar)
        if (IsKeyTrack(t))
        {
            const auto times = KeyTimes(t, fps);
            for (size_t k = 0; k < times.size(); ++k)
            {
                const float x = TimeToX(times[k]);
                if (x < pos.x - 8.0f || x > pos.x + width + 8.0f)
                    continue;
                const bool sel = m_Sel.Kind == SelKind::Key && m_Sel.Track == index && std::abs(m_Sel.KeyTime - times[k]) <= 0.5f / fps;
                const float cy = pos.y + height * 0.5f, r = 6.0f;
                dl->AddQuadFilled(ImVec2(x, cy - r), ImVec2(x + r, cy), ImVec2(x, cy + r), ImVec2(x - r, cy),
                                  sel ? IM_COL32(255, 255, 255, 255) : TrackColor(t.Type, alpha));
                dl->AddQuad(ImVec2(x, cy - r), ImVec2(x + r, cy), ImVec2(x, cy + r), ImVec2(x - r, cy), IM_COL32(10, 10, 12, 255));
                ImGui::PushID((int)k + 5000);
                ImGui::SetCursorScreenPos(ImVec2(x - r, cy - r));
                ImGui::InvisibleButton("##key", ImVec2(r * 2.0f, r * 2.0f));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Key at %.2f s (frame %d)", times[k], (int)std::round(times[k] * fps));
                if (ImGui::IsItemClicked())
                {
                    m_Sel = { SelKind::Key, index, -1, times[k] };
                    if (editable)
                    {
                        m_Drag = DragKind::Key;
                        m_DragOrigStart = times[k];
                    }
                    if (!playing)
                        SetPreviewTime(times[k]);
                }
                ImGui::PopID();
            }
            // Eğri izi: anahtarlar arası ince çizgi
            if (times.size() >= 2)
                dl->AddLine(ImVec2(TimeToX(times.front()), pos.y + height * 0.5f), ImVec2(TimeToX(times.back()), pos.y + height * 0.5f),
                            TrackColor(t.Type, 0.35f * alpha), 2.0f);
        }

        // Sinyaller
        if (t.Type == TimelineTrackType::Signal)
        {
            for (int m = 0; m < (int)t.Markers.size(); ++m)
            {
                const auto& marker = t.Markers[(size_t)m];
                const float x = TimeToX(marker.Time);
                const bool sel = m_Sel.Kind == SelKind::Marker && m_Sel.Track == index && m_Sel.Index == m;
                dl->AddTriangleFilled(ImVec2(x - 6.0f, pos.y + 4.0f), ImVec2(x + 6.0f, pos.y + 4.0f), ImVec2(x, pos.y + 14.0f),
                                      sel ? IM_COL32(255, 255, 255, 255) : TrackColor(t.Type, alpha));
                dl->AddLine(ImVec2(x, pos.y + 14.0f), ImVec2(x, pos.y + height - 3.0f), TrackColor(t.Type, alpha), 1.5f);
                dl->AddText(ImVec2(x + 5.0f, pos.y + 13.0f), IM_COL32(220, 220, 225, 200), !marker.Method.empty() ? marker.Method.c_str() : std::filesystem::path(marker.Signal).stem().string().c_str());
                ImGui::PushID(m + 9000);
                ImGui::SetCursorScreenPos(ImVec2(x - 6.0f, pos.y + 2.0f));
                ImGui::InvisibleButton("##marker", ImVec2(12.0f, height - 4.0f));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s(%s) at %.2f s", marker.Method.c_str(), marker.Argument.c_str(), marker.Time);
                if (ImGui::IsItemClicked())
                {
                    m_Sel = { SelKind::Marker, index, m, 0.0f };
                    if (editable)
                    {
                        m_Drag = DragKind::Marker;
                        m_DragOffset = XToTime(mouse.x) - marker.Time;
                    }
                }
                ImGui::PopID();
            }
        }
        dl->PopClipRect();

        // Sürükleme uygulaması (bu parçaya ait seçim)
        if (m_Drag != DragKind::None && m_Sel.Track == index && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
        {
            if (!m_DragUndoPushed && m_Drag != DragKind::Playhead)
            {
                // Sürükleme başlamadan önceki durum (değişiklikten önce kaydedilmeli: değişiklik henüz uygulanmadı)
                PushUndo();
                m_DragUndoPushed = true;
            }
            const float mt = XToTime(mouse.x);
            switch (m_Drag)
            {
                case DragKind::ClipMove:
                    if (m_Sel.Index >= 0 && m_Sel.Index < (int)t.Clips.size())
                        t.Clips[(size_t)m_Sel.Index].Start = Snap(mt - m_DragOffset);
                    break;
                case DragKind::ClipStart:
                    if (m_Sel.Index >= 0 && m_Sel.Index < (int)t.Clips.size())
                    {
                        auto& c = t.Clips[(size_t)m_Sel.Index];
                        const float end = m_DragOrigStart + m_DragOrigDuration;
                        c.Start = std::min(Snap(mt), end - 1.0f / fps);
                        c.Duration = end - c.Start;
                    }
                    break;
                case DragKind::ClipEnd:
                    if (m_Sel.Index >= 0 && m_Sel.Index < (int)t.Clips.size())
                    {
                        auto& c = t.Clips[(size_t)m_Sel.Index];
                        c.Duration = std::max(1.0f / fps, Snap(mt) - c.Start);
                    }
                    break;
                case DragKind::Marker:
                    if (m_Sel.Index >= 0 && m_Sel.Index < (int)t.Markers.size())
                        t.Markers[(size_t)m_Sel.Index].Time = Snap(mt - m_DragOffset);
                    break;
                case DragKind::Key:
                {
                    const float newTime = Snap(mt);
                    const float half = 0.5f / fps;
                    if (std::abs(newTime - m_Sel.KeyTime) > half)
                    {
                        for (auto& ch : t.Channels)
                            for (auto& k : ch.Keys)
                                if (std::abs(k.Time - m_Sel.KeyTime) <= half)
                                    k.Time = newTime;
                        m_Sel.KeyTime = newTime;
                    }
                    break;
                }
                default:
                    break;
            }
            m_LastEvaluated = -1.0;
        }
        (void)laneHovered;
        ImGui::PopID();
    }

    void TimelinePanel::DrawSelectionInspector()
    {
        if (m_Sel.Track < 0 || m_Sel.Track >= (int)m_Asset->Tracks.size())
        {
            m_Sel = {};
            return;
        }
        auto& t = m_Asset->Tracks[(size_t)m_Sel.Track];
        bool changed = false;
        bool commit = false;
        auto track = [&](bool edit) {
            if (edit && !m_DragUndoPushed) { PushUndo(); m_DragUndoPushed = true; }
            if (edit) changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) { commit = true; m_DragUndoPushed = false; }
        };

        switch (m_Sel.Kind)
        {
            case SelKind::Track:
            {
                UI::SubHeader((std::string(TrackIcon(t.Type)) + "  " + TimelineAsset::TrackTypeName(t.Type) + " Track").c_str());
                char name[128];
                std::snprintf(name, sizeof(name), "%s", t.Name.c_str());
                UI::BeginProperty("Name");
                ImGui::SetNextItemWidth(-1);
                track(ImGui::InputText("##tname", name, sizeof(name)));
                if (changed) t.Name = name;
                UI::EndProperty();
                Entity bound = BoundEntity(m_Sel.Track);
                UI::Text("Bound To", "%s", bound ? bound.GetComponent<TagComponent>().Tag.c_str() : "(missing - drag an object onto the track)");
                if (t.Type == TimelineTrackType::Activation)
                {
                    static const char* post[] = { "Leave As Is", "Active", "Inactive" };
                    int v = t.PostPlayback;
                    if (UI::Combo("Post-Playback", v, post, 3)) { PushUndo(); t.PostPlayback = v; commit = true; }
                }
                if (t.Type == TimelineTrackType::Property && bound)
                {
                    // Bağlı varlıktaki bileşen ve canlandırılabilir alan seçimi
                    std::string current = t.Component.empty() ? std::string("(choose)") : t.Component + "." + t.Field;
                    UI::BeginProperty("Property");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::BeginCombo("##prop", current.c_str()))
                    {
                        for (auto& info : ComponentRegistry::All())
                        {
                            if (!info.Has || !info.Has(bound))
                                continue;
                            for (const auto& f : info.Fields)
                            {
                                if (!AnimatableKind(f.Kind) || !f.Ptr || f.ReadOnly)
                                    continue;
                                const std::string label = info.Name + " / " + f.Label;
                                if (ImGui::Selectable(label.c_str(), t.Component == info.TypeName && t.Field == f.Name))
                                {
                                    std::string err;
                                    if (!SetPropertyTarget(m_Sel.Track, info.TypeName, f.Name, &err))
                                        m_Status = err;
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                    UI::EndProperty();
                }
                ImGui::TextDisabled("Delete removes the track. Right-click the lane to add keys, clips or signals.");
                break;
            }
            case SelKind::Clip:
            {
                if (m_Sel.Index < 0 || m_Sel.Index >= (int)t.Clips.size()) { m_Sel = {}; return; }
                auto& c = t.Clips[(size_t)m_Sel.Index];
                UI::SubHeader("Clip");
                char name[128];
                std::snprintf(name, sizeof(name), "%s", c.Name.c_str());
                UI::BeginProperty("Name");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##cname", name, sizeof(name))) { track(true); c.Name = name; }
                else track(false);
                UI::EndProperty();
                float start = c.Start, dur = c.Duration;
                if (UI::DragFloat("Start", start, 0.01f, 0.0f, 3600.0f, "%.3f s")) { track(true); c.Start = std::max(0.0f, start); } else track(false);
                if (UI::DragFloat("Duration", dur, 0.01f, 0.01f, 3600.0f, "%.3f s")) { track(true); c.Duration = std::max(0.01f, dur); } else track(false);
                if (t.Type == TimelineTrackType::Audio)
                {
                    char asset[260];
                    std::snprintf(asset, sizeof(asset), "%s", c.Asset.c_str());
                    UI::BeginProperty("Audio");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputText("##casset", asset, sizeof(asset))) { track(true); c.Asset = asset; } else track(false);
                    UI::EndProperty();
                    float vol = c.Volume;
                    if (UI::SliderFloat("Volume", vol, 0.0f, 2.0f)) { track(true); c.Volume = vol; } else track(false);
                }
                if (t.Type == TimelineTrackType::Playable)
                {
                    std::vector<std::string> exts = { ".cqplayable" };
                    for (auto* runtime : Scripting::ScriptRuntimes::All())
                        for (const auto& e : runtime->GetExtensions())
                            exts.push_back(e);
                    UI::BeginProperty("Playable", "A Playable Asset, or a script with OnClipStart / OnClipUpdate(time, progress) / OnClipStop.");
                    std::string asset = c.Asset;
                    if (AssetUI::AssetSlot("##cplayable", "Playable Asset", exts, asset))
                    {
                        track(true);
                        c.Asset = asset;
                        if (!asset.empty())
                            c.Name = fs::path(asset).stem().string();
                    }
                    else
                        track(false);
                    UI::EndProperty();
                }
                if (t.Type == TimelineTrackType::Animation)
                {
                    // Bağlı modelin klipleri adlarıyla (model yoksa sıra numarası)
                    Entity bound = BoundEntity(m_Sel.Track);
                    const MeshRendererComponent* mc = bound && bound.HasComponent<MeshRendererComponent>() ? &bound.GetComponent<MeshRendererComponent>() : nullptr;
                    if (mc && mc->ModelData && !mc->ModelData->Animations.empty())
                    {
                        const auto& anims = mc->ModelData->Animations;
                        const int count = (int)anims.size();
                        const int cur = std::clamp(c.AnimationIndex, 0, count - 1);
                        auto label = [&](int i) { return anims[(size_t)i] ? std::to_string(i) + "  " + anims[(size_t)i]->Name : std::to_string(i); };
                        UI::BeginProperty("Animation Clip");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##animclip", label(cur).c_str()))
                        {
                            for (int i = 0; i < count; ++i)
                                if (ImGui::Selectable(label(i).c_str(), i == cur))
                                {
                                    PushUndo();
                                    c.AnimationIndex = i;
                                    MarkChanged();
                                }
                            ImGui::EndCombo();
                        }
                        UI::EndProperty();
                    }
                    else
                    {
                        int idx = c.AnimationIndex;
                        if (UI::DragInt("Animation Clip", idx, 0.1f, 0, 256)) { track(true); c.AnimationIndex = std::max(0, idx); } else track(false);
                    }
                    bool loop = c.Loop;
                    if (UI::Checkbox("Loop", loop)) { PushUndo(); c.Loop = loop; MarkChanged(); }
                    ImGui::TextDisabled("Overlap two clips on the track to blend between them.");
                }
                if (t.Type != TimelineTrackType::Activation)
                {
                    float speed = c.Speed, in = c.ClipIn;
                    if (UI::DragFloat("Speed", speed, 0.01f, 0.05f, 10.0f)) { track(true); c.Speed = speed; } else track(false);
                    if (UI::DragFloat("Clip In", in, 0.01f, 0.0f, 3600.0f, "%.3f s")) { track(true); c.ClipIn = std::max(0.0f, in); } else track(false);
                }
                break;
            }
            case SelKind::Key:
            {
                UI::SubHeader("Key");
                const float half = 0.5f / std::max(1.0f, m_Asset->FrameRate);
                UI::Text("Time", "%.3f s (frame %d)", m_Sel.KeyTime, (int)std::round(m_Sel.KeyTime * m_Asset->FrameRate));
                for (auto& ch : t.Channels)
                {
                    for (auto& k : ch.Keys)
                    {
                        if (std::abs(k.Time - m_Sel.KeyTime) > half)
                            continue;
                        ImGui::PushID(&k);
                        int comps = 3;
                        if (t.Type == TimelineTrackType::Property)
                        {
                            ComponentInfo* info = ComponentRegistry::Find(t.Component);
                            const ComponentField* f = info ? ComponentRegistry::FindField(*info, t.Field) : nullptr;
                            comps = f ? FieldComponents(f->Kind) : 1;
                        }
                        UI::BeginProperty(ch.Name.c_str());
                        ImGui::SetNextItemWidth(-1);
                        bool edited = false;
                        if (comps == 1) edited = ImGui::DragFloat("##v", &k.Value.x, 0.01f);
                        else if (comps == 2) edited = ImGui::DragFloat2("##v", &k.Value.x, 0.01f);
                        else if (comps == 3) edited = ImGui::DragFloat3("##v", &k.Value.x, 0.01f);
                        else edited = ImGui::DragFloat4("##v", &k.Value.x, 0.01f);
                        track(edited);
                        UI::EndProperty();
                        static const char* interp[] = { "Smooth", "Linear", "Constant" };
                        int iv = (int)k.Interp;
                        if (UI::Combo((ch.Name + " Interp").c_str(), iv, interp, 3)) { PushUndo(); k.Interp = (KeyInterpolation)iv; commit = true; }
                        ImGui::PopID();
                    }
                }
                break;
            }
            case SelKind::Marker:
            {
                if (m_Sel.Index < 0 || m_Sel.Index >= (int)t.Markers.size()) { m_Sel = {}; return; }
                auto& m = t.Markers[(size_t)m_Sel.Index];
                UI::SubHeader("Signal");
                float time = m.Time;
                if (UI::DragFloat("Time", time, 0.01f, 0.0f, 3600.0f, "%.3f s")) { track(true); m.Time = std::max(0.0f, time); } else track(false);
                char method[128], arg[256];
                std::snprintf(method, sizeof(method), "%s", m.Method.c_str());
                std::snprintf(arg, sizeof(arg), "%s", m.Argument.c_str());
                UI::BeginProperty("Method", "Script method called on the bound object when the playhead passes this signal.");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##method", method, sizeof(method))) { track(true); m.Method = method; } else track(false);
                UI::EndProperty();
                UI::BeginProperty("Argument");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##arg", arg, sizeof(arg))) { track(true); m.Argument = arg; } else track(false);
                UI::EndProperty();
                // Signal Emitter (Unity): Signal varlığı bağlı nesnenin Signal Receiver'ına gider
                UI::BeginProperty("Emit Signal", "A Signal asset (.cqsignal); the bound object's Signal Receiver reacts to it.");
                {
                    std::string signal = m.Signal;
                    if (AssetUI::AssetSlot("emitsignal", "Signal", { SignalAssets::Extension }, signal)) { track(true); m.Signal = signal; } else track(false);
                }
                UI::EndProperty();
                break;
            }
            default:
                break;
        }
        if (commit)
            MarkChanged();
        else if (changed)
            m_LastEvaluated = -1.0;
    }
}
