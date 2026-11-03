#pragma once

#include "Core/Animation/PropertyAnimation.h"
#include "Scene/Entity.h"

#include <imgui.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor
{
    // Window > Animation (Unity Animation penceresi): seçili nesnenin (ya da Animation bileşenli üst nesnesinin) Animation
    // Clip'leri (.cqanim). Solda özellik listesi (Add Property, o andaki değerler), sağda dopesheet ya da eğri görünümü;
    // cetvelde oynatma kafası ile önizleme (çıkınca nesneler eski değerlerine döner), kayıt modu (Inspector'da ya da
    // sahnede yapılan değişiklik o karede anahtar olur), Animation Event'ler. Her düzenleme Ctrl+Z ile geri alınır.
    class AnimationWindow
    {
    public:
        void SetContext(const std::shared_ptr<Scene>& scene);
        void OnImGuiRender(bool* open, bool playing);
        // Önizlemeyi kapat (Play'den önce, sahne kaydedilmeden / değişmeden önce): canlandırılan değerler geri yazılır
        void StopPreview();
        // Content Browser çift tıkı / Inspector "Open Animation Window"
        void OpenClip(const std::string& assetPath);

        // Otomasyon ve dış çağrılar
        bool CreateClip(Entity e, std::string* error = nullptr);
        bool SelectClip(const std::string& assetPath, std::string* error = nullptr);
        bool AddProperty(const std::string& path, const std::string& component, const std::string& field, std::string* error = nullptr);
        bool AddKeyAt(int curve, float time, std::string* error = nullptr);   // curve < 0: bütün özellikler (Add Keyframe)
        bool AddEventAt(float time, const std::string& function, const std::string& argument, std::string* error = nullptr);
        void SetTime(float t);
        float GetTime() const { return m_Time; }
        void SetPreviewing(bool on);
        void SetRecording(bool on);
        void SetCurvesView(bool curves) { m_CurvesView = curves; }
        void SetPlaying(bool on);
        bool IsPreviewing() const { return m_Previewing; }
        bool IsRecording() const { return m_Recording; }
        const PropertyClip& GetClip() const { return m_Clip; }
        const std::string& GetClipPath() const { return m_ClipPath; }
        Entity GetRoot() const;
        // Otomasyon: dopesheet satırının (eğri; -1 özet) verilen zamandaki ekran noktası (son çizime göre)
        bool LanePoint(int curve, float time, ImVec2& out) const;

        // Inspector'dan klip dosyası değişti (Length / Samples): pencere yeniden okur
        void InvalidateClip() { m_ClipTime = {}; }
        // Sahne kaydedilirken canlandırılan değerler yerine özgün değerler yazılır; önizleme sonra sürer
        void BeginSceneSave();
        void EndSceneSave();

    private:
        struct SelKey
        {
            int Curve = 0;
            float Time = 0.0f;   // anahtar zamanıyla tanınır (sürükleyince sıra değişse de seçim korunur)
        };
        struct Prop
        {
            Entity Target;
            std::string Path, Component, Field;
        };
        struct Original
        {
            std::string Path, Component, Field;
            glm::vec4 Value{ 0.0f };
        };
        enum class Drag { None, Playhead, Keys, Box, Event, CurveKey, Pan, Splitter };

        // Hedef / klip
        void SyncTarget();
        void LoadClip(const std::string& assetPath);
        void ReloadIfChanged();
        std::vector<std::string> RootClips() const;
        void CollectProps(Entity root, std::vector<Prop>& out) const;
        int ComponentCount(const AnimCurveBinding& c) const;
        bool IsColorBinding(const AnimCurveBinding& c) const;
        std::string RowLabel(const AnimCurveBinding& c) const;
        Entity Resolve(const AnimCurveBinding& c) const;
        glm::vec4 CurrentValue(int curve) const;

        // Düzenleme (geri alma: dosyanın önceki / sonraki metni)
        void BeginEdit();
        void MarkChanged();
        void CommitEdit(bool force);
        void SaveClip();
        float Snap(float t) const;
        float FrameTime() const;
        float ViewEnd() const;
        void SetKeyAt(int curve, float time, const glm::vec4& value);
        void DeleteSelection();
        void SetSelectedInterpolation(KeyInterpolation interp);
        void RemoveCurve(int curve);
        bool IsSelected(int curve, float time) const;
        void SelectKey(int curve, float time, bool additive);
        std::vector<float> AllKeyTimes() const;
        void CopySelection();
        void PasteAtTime();

        // Önizleme / kayıt
        void StartPreview();
        void ApplyPreview(bool force);
        void RefreshWatch();
        void RecordChanges();
        static std::string Key(const std::string& path, const std::string& component, const std::string& field);

        // Çizim
        void DrawToolbar(bool playing);
        void DrawPrompt(Entity root);
        void DrawRuler(const ImVec2& pos, float width, float height);
        void DrawEventsRow(const ImVec2& pos, float width, float height);
        void DrawPropertyPane(const ImVec2& pos, const ImVec2& size);
        void DrawDopesheet(const ImVec2& pos, const ImVec2& size);
        void DrawCurves(const ImVec2& pos, const ImVec2& size);
        void DrawAddPropertyPopup();
        void DrawEventPopup();
        void DrawKeyContextMenu();
        void HandleDrags();
        void HandleShortcuts();
        std::string FormatTime(float t) const;
        std::vector<std::string> ScriptMethods() const;
        void FitCurves();

        float TimeToX(float t) const { return m_LanesX0 + (t - m_ScrollTime) * m_Zoom; }
        float XToTime(float x) const { return m_ScrollTime + (x - m_LanesX0) / m_Zoom; }
        float ValueToY(float v) const;
        float YToValue(float y) const;

        std::shared_ptr<Scene> m_Scene;
        uint64_t m_RootUUID = 0;
        uint64_t m_LastSelection = 0;
        bool m_Lock = false;
        bool m_PlayMode = false;

        std::string m_ClipPath;   // bileşende saklanan varlık yolu
        std::string m_ClipFile;   // mutlak dosya
        PropertyClip m_Clip;
        bool m_ClipLoaded = false;
        std::filesystem::file_time_type m_ClipTime{};
        std::string m_LoadError;

        bool m_Previewing = false, m_Recording = false, m_Playing = false;
        float m_Time = 0.0f;
        float m_LastApplied = -1.0f;
        bool m_ApplyDirty = false;
        std::unordered_map<std::string, glm::vec4> m_Watch;       // kayıt: son bilinen değerler
        std::unordered_map<std::string, Original> m_Originals;    // önizlemeden önceki değerler
        // Önizlenen nesne silindi: geri alınıp geri gelirse canlandırılmış değerler yerine özgün değerleri yazılır
        uint64_t m_PendingRestoreUUID = 0;
        std::unordered_map<std::string, Original> m_PendingRestore;

        bool m_EditOpen = false, m_Dirty = false;
        std::string m_EditBefore;

        // Görünüm
        bool m_CurvesView = false;
        float m_Zoom = 240.0f;          // piksel / saniye
        float m_ScrollTime = -0.05f;
        float m_LanesX0 = 0.0f;
        float m_LeftWidth = 300.0f;
        float m_RowsScroll = 0.0f;
        float m_BoxRowsTop = 0.0f;      // dopesheet satırlarının üstü (kutu seçimi)
        std::unordered_map<int, float> m_LaneCenterY;   // son çizimde eğri satırlarının orta y'si (otomasyon)
        ImVec2 m_AddPropertyAnchor{};                   // Add Property düğmesinin sol üstü (açılır liste yukarı açılır)
        // Çizim sırasında satır listesini değiştiren işlemler kare sonunda yapılır (aynı karede eski satırlarla çizim sürer)
        std::string m_DeferredClip;
        bool m_DeferredCreate = false;
        int m_DeferredRemove = -1;
        float m_ValueMin = -1.0f, m_ValueMax = 1.0f;
        bool m_FitPending = true;
        ImVec2 m_CurveAreaMin{}, m_CurveAreaMax{};
        std::unordered_map<std::string, bool> m_Expanded;
        std::vector<SelKey> m_SelKeys;
        int m_SelRow = -1;
        int m_SelEvent = -1;
        bool m_OpenEventPopup = false;
        bool m_OpenKeyMenu = false;
        int m_MenuCurve = -1;           // bağlam menüsünün satırı (-1: özet satırı)
        float m_MenuTime = 0.0f;
        std::vector<std::pair<int, TimelineKey>> m_Clipboard;   // kopyalanan anahtarlar (eğri, anahtar; zaman ilk anahtara göreli)

        Drag m_Drag = Drag::None;
        ImVec2 m_DragStart{};
        float m_DragTime0 = 0.0f;
        std::vector<SelKey> m_DragSel;
        std::vector<AnimCurveBinding> m_DragBackup;
        std::vector<AnimEventDef> m_DragEventsBackup;
        int m_DragCurve = -1, m_DragComp = 0;
        float m_DragKeyTime = 0.0f, m_DragValue0 = 0.0f;
        bool m_DragMoved = false;
        std::string m_Status;
    };
}
