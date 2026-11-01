#pragma once

#include <imgui.h>
#include <string>

namespace Conqueror::Editor
{
    // Unity Audio Mixer karşılığı: kanal şeritleri (ölçer, fader, pan, M/S/B),
    // efekt zincirleri, grup ağacı, snapshot'lar, ducking ve test tonu.
    class AudioMixerPanel
    {
    public:
        AudioMixerPanel() = default;

        void OnImGuiRender(bool* open);

        const std::string& GetSelectedGroup() const { return m_SelectedGroup; }
        void SelectGroup(const std::string& name) { m_SelectedGroup = name; m_SelectedEffect = -1; }

    private:
        void DrawToolbar();
        void DrawGroupTree();
        void DrawGroupTreeNode(const std::string& name, int depth);
        void DrawSnapshots();
        void DrawStrips();
        void DrawStrip(int index, float height);
        void DrawInspector();

        bool Fader(const char* id, float& db, const ImVec2& size);
        bool PanKnob(const char* id, float& pan, float radius);
        void Meter(const ImVec2& pos, const ImVec2& size, float peak, float rms, float hold);

        void MarkDirty() { m_Dirty = true; }

        std::string m_SelectedGroup = "Master";
        int m_SelectedEffect = -1;
        std::string m_RenamingGroup;
        char m_RenameBuffer[64] = {};
        char m_NewSnapshot[64] = "Snapshot";
        int m_ActiveSnapshot = 0;
        float m_SnapshotTransition = 0.5f;
        float m_TestFrequency = 440.0f;
        bool m_Dirty = false;
        bool m_AutoSave = true;
        float m_AutoSaveTimer = 0.0f;
    };
}
