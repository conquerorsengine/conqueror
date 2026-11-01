#pragma once

#include "Scripting/Visual/VisualGraph.h"

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

struct ImNodesContext;

namespace Conqueror::Editor
{
    // Görsel betik düzenleyicisi (Blueprint benzeri): düğüm tuvali, değişkenler, düğüm paleti,
    // arama menüsü, kopyala/yapıştır, geri al, canlı derleme ve hata vurgulama.
    class VisualScriptPanel
    {
    public:
        VisualScriptPanel();
        ~VisualScriptPanel();

        void OnImGuiRender();
        void Open(const std::filesystem::path& path);
        bool Save();
        bool& GetIsOpen() { return m_IsOpen; }
        const std::filesystem::path& GetPath() const { return m_Path; }

        // Otomasyon / testler
        Visual::Graph& GetGraph() { return m_Graph; }
        void MarkEdited();
        std::string GetGeneratedCode();
        int GetErrorCount() const;
        bool IsDirty() const { return m_Dirty; }

        // Kaydedildiğinde çağrılır (oynatma sırasında sıcak yeniden yükleme)
        std::function<void(const std::filesystem::path&)> OnFileSaved;

    private:
        void RenderToolbar();
        void RenderSidebar();
        void RenderCanvas();
        void RenderNode(Visual::Node& node);
        void RenderSearchPopup();
        void RenderCodeView();
        bool DrawInputDefault(Visual::Node& node, const Visual::PinDef& pin);
        bool DrawNodeProperties(Visual::Node& node);

        void CreateNodeAt(const std::string& type, const ImVec2& gridPos);
        void DeleteSelection();
        void CopySelection();
        void Paste(const ImVec2& gridPos);
        void Duplicate();
        void PushUndo();
        void Undo();
        void Redo();
        void Recompile();
        void FrameAll();
        void FocusNode(int id);
        // Yakınlaştırma: düğümler mantıksal (grafik) koordinatlarda saklanır, imnodes'a zoom ile ölçeklenmiş verilir
        void ApplyZoomStyle();
        void SetZoom(float zoom, const ImVec2& anchorLocal);
        float Z(float v) const { return v * m_Zoom; }
        ImVec2 ScreenToGraph(const ImVec2& screen) const;

        static int AttributeId(int node, int pinIndex, bool output) { return node * 1024 + (output ? 512 : 0) + pinIndex; }
        static void DecodeAttribute(int id, int& node, int& pinIndex, bool& output)
        {
            node = id / 1024;
            int rest = id % 1024;
            output = rest >= 512;
            pinIndex = output ? rest - 512 : rest;
        }

        Visual::Graph m_Graph;
        std::filesystem::path m_Path;
        bool m_IsOpen = false;
        bool m_Dirty = false;
        bool m_PositionsDirty = true;      // grafik yüklendi: düğüm konumlarını imnodes'a uygula
        ImNodesContext* m_Context = nullptr;

        // Arama menüsü
        bool m_OpenSearch = false;
        ImVec2 m_SearchScreenPos{ 0, 0 };
        ImVec2 m_SearchGridPos{ 0, 0 };
        char m_SearchBuffer[128] = {};
        int m_SearchSelected = 0;
        int m_PendingLinkAttribute = -1;  // bırakılan bağlantının kaynağı (otomatik bağlama)
        bool m_SearchScrollToSelected = false;
        std::string m_LastSearchFilter;

        // Derleme
        std::vector<Visual::CompileMessage> m_Messages;
        std::set<int> m_ErrorNodes;
        std::string m_GeneratedCode;
        double m_LastEdit = 0.0;
        bool m_NeedsCompile = true;
        bool m_ShowCode = false;
        bool m_ShowMinimap = true;

        // Geri al / yinele (JSON anlık görüntüleri)
        std::vector<std::string> m_UndoStack;
        std::vector<std::string> m_RedoStack;
        std::string m_Clipboard;

        std::string m_Toast;
        double m_ToastTime = 0.0;
        int m_FocusNode = 0;
        char m_NewVariableName[64] = {};
        bool m_FocusSearch = false;
        bool m_RequestFocus = false;      // açıldığında sekmeyi öne getir

        // Seçim / kadraj istekleri (bir sonraki karede uygulanır)
        std::vector<int> m_PendingSelect;
        ImVec2 m_FrameRequest{ 0, 0 };
        bool m_HasFrameRequest = false;
        float m_FrameZoom = 1.0f;

        float m_Zoom = 1.0f;
        ImVec2 m_CanvasOrigin{ 0, 0 };     // imnodes tuvalinin ekran konumu
        ImVec2 m_CanvasSize{ 0, 0 };

        // Sürükleme ve satır içi düzenleme için geri al anlık görüntüleri
        bool m_Dragging = false;
        bool m_EditingNodeValue = false;
        std::string m_DragSnapshot;
        std::string m_PreFrameSnapshot;
    };
}
