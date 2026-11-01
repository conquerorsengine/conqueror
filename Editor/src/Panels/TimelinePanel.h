#pragma once

#include "Timeline/Timeline.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"

#include <imgui.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Conqueror::Editor
{
    // Window > Timeline (Unity Timeline benzeri): seçili Playable Director'ın parçaları, klipleri, anahtarları
    // ve sinyalleri; çizelge üzerinde sürükleme, oynatma kafası ile önizleme (sahne durumu sonra geri yüklenir),
    // anahtar kaydı ve geri alma.
    class TimelinePanel
    {
    public:
        void SetContext(const std::shared_ptr<Scene>& scene);
        void OnImGuiRender(bool* open, bool playing);
        // Önizlemeyi kapat (Play'den önce, sahne değişiminde): bağlı varlıklar geri yüklenir
        void StopPreview();

        // Otomasyon
        bool SelectDirector(Entity e);
        bool CreateTimelineFor(Entity e, std::string* error = nullptr);
        bool AddTrack(TimelineTrackType type, const std::string& bindingName, std::string* error = nullptr);
        bool AddKeyAt(int track, float time, std::string* error = nullptr);
        bool AddClipAt(int track, float start, float duration, const std::string& asset, std::string* error = nullptr);
        bool AddMarkerAt(int track, float time, const std::string& method, std::string* error = nullptr);
        bool SetPropertyTarget(int track, const std::string& component, const std::string& field, std::string* error = nullptr);
        void SetPreviewTime(double t);
        double GetPreviewTime() const { return m_PreviewTime; }
        std::shared_ptr<TimelineAsset> GetAsset() const { return m_Asset; }
        void Undo();
        void Redo();

    private:
        enum class SelKind { None, Track, Clip, Key, Marker };
        struct Selection
        {
            SelKind Kind = SelKind::None;
            int Track = -1;
            int Index = -1;          // klip / işaretçi
            float KeyTime = 0.0f;    // anahtar (tüm kanallarda bu zamandaki anahtarlar)
        };
        enum class DragKind { None, Playhead, ClipMove, ClipStart, ClipEnd, Key, Marker, Pan };

        Entity DirectorEntity() const;
        bool LoadAsset();
        void Save();
        void PushUndo();
        void MarkChanged();
        void StartPreview();
        void ApplyPreview();
        float Snap(float t) const;
        Entity BoundEntity(int track) const;
        void EnsureBindings();

        void DrawToolbar(bool playing);
        void DrawTracks(bool playing);
        void DrawTrackHeader(int index, const ImVec2& pos, float width, float height);
        void DrawLane(int index, const ImVec2& pos, float width, float height, bool playing);
        void DrawRuler(const ImVec2& pos, float width, float height, bool playing);
        void DrawSelectionInspector();
        void DrawAddTrackMenu();
        void DrawNoDirector();

        float TimeToX(float t) const { return m_LanesX0 + (t - m_ScrollTime) * m_Zoom; }
        float XToTime(float x) const { return m_ScrollTime + (x - m_LanesX0) / m_Zoom; }

        std::shared_ptr<Scene> m_Scene;
        uint64_t m_DirectorUUID = 0;
        bool m_Lock = false;
        std::string m_AssetPath;
        std::shared_ptr<TimelineAsset> m_Asset;

        // Önizleme
        bool m_Previewing = false;
        bool m_PreviewPlaying = false;
        double m_PreviewTime = 0.0;
        double m_LastEvaluated = -1.0;
        std::vector<std::pair<uint64_t, std::shared_ptr<Scene>>> m_Snapshots;

        // Görünüm
        float m_Zoom = 120.0f;            // piksel / saniye
        float m_ScrollTime = -0.2f;
        float m_LanesX0 = 0.0f;
        float m_HeaderWidth = 230.0f;
        Selection m_Sel;
        DragKind m_Drag = DragKind::None;
        float m_DragOffset = 0.0f;
        float m_DragOrigStart = 0.0f;
        float m_DragOrigDuration = 0.0f;
        bool m_DragUndoPushed = false;

        std::vector<TimelineAsset> m_UndoStack;
        std::vector<TimelineAsset> m_RedoStack;
        std::string m_Status;
    };
}
