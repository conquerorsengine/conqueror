#pragma once

#include "Scripting/Visual/VisualGraph.h"

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
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
        // Düzenlenen grafik (olay grafiği ya da açık makronun gövdesi) ve makro açma (boş ad: olay grafiğine dön)
        Visual::Graph& GetEditedGraph() { return G(); }
        void OpenMacro(const std::string& name) { EditMacro(name); }
        void MarkEdited();
        std::string GetGeneratedCode();
        int GetErrorCount() const;
        bool IsDirty() const { return m_Dirty; }

        // Blueprint hata ayıklayıcısı: grafik anahtarı (üretilen sınıf adı), kesme noktası, tetiklenen kesme noktasını göster
        std::string DebugKey() const;
        void ToggleBreakpoint(int node);
        void ShowBreakpointHit(int node);

        // Kaydedildiğinde çağrılır (oynatma sırasında sıcak yeniden yükleme)
        std::function<void(const std::filesystem::path&)> OnFileSaved;

        // ── Unity araç çubuğu / Unreal araçları (VisualScriptPanelTools.cpp) ──
        enum class AlignMode { Left, Center, Right, Top, Middle, Bottom };
        void AlignNodes(const std::vector<int>& ids, AlignMode mode);
        void DistributeNodes(const std::vector<int>& ids, bool horizontal);
        void StraightenNodes(const std::vector<int>& ids);
        // Düğümleri taşı; Carry açıksa bağlı alt düğümler de (Unity Carry)
        void MoveNodes(const std::map<int, ImVec2>& deltas);
        int CollapseToFunction(const std::vector<int>& ids, const std::string& name);
        int CollapseToMacro(const std::vector<int>& ids, const std::string& name);
        int PromoteToVariable(int node, const std::string& pin, bool output, const std::string& name);
        void ToggleWatch(int node, const std::string& pin);
        bool IsWatched(int node, const std::string& pin) const;
        void SetNodeDisabled(int node, bool disabled);
        int AddCommentAround(const std::vector<int>& ids, const std::string& text);
        // Bul (Unreal Find in Blueprints): allGraphs = projedeki tüm .cqgraph dosyaları
        struct FindResult { std::filesystem::path Path; std::string Macro; int Node = 0; std::string Text; };
        const std::vector<FindResult>& RunFind(const std::string& text, bool allGraphs);
        // Fark (Unreal Diff): "saved" (diskteki sürüm) ya da "head" (git HEAD)
        bool RunDiff(const std::string& against, std::string* error = nullptr);
        const std::vector<Visual::DiffEntry>& GetDiff() const { return m_Diff; }
        void AddBookmark(const std::string& name);
        bool JumpToBookmark(const std::string& name);
        // Çağrı yığını (kesme noktasında): üstten alta çerçeveler
        std::vector<std::string> BuildCallStack() const;
        bool IsDimmed(int node);
        int RelationCount(int node) const;
        bool& Toggle(const std::string& name);
        bool IsLocked() const { return m_Locked; }
        // Son karede imnodes'a verilmiş düğümün boyutu (ekran); verilmemişse (yeni / yorum kutusu) 0 — imnodes bilinmeyen kimlikte çöker
        ImVec2 NodeDimensions(int id) const;

    private:
        void RenderSecondToolbar();
        void RenderGraphInspector();
        void RenderBottomPanel();
        void RenderBoxes();
        void RenderPinContextMenu();
        void RenderNodeContextMenu();
        void RenderUserTypes();
        void RenderDispatchersAndInterfaces();
        void RegisterAutomation();
        void FollowSelection();
        std::vector<int> SelectedNodes() const;
        bool IsBoxNode(const Visual::Node& n) const;
        Visual::Graph* GraphForMacro(const std::string& macro);
        void RenderToolbar();
        void RenderSidebar();
        void RenderCanvas();
        void RenderNode(Visual::Node& node);
        void RenderSearchPopup();
        void RenderCodeView();
        bool DrawInputDefault(Visual::Node& node, const Visual::PinDef& pin);
        bool DrawNodeProperties(Visual::Node& node);

        void CreateNodeAt(const std::string& type, const ImVec2& gridPos, const std::vector<std::pair<std::string, std::string>>& props = {});
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
        // Düzenlenen grafik: boş = olay grafiği, değilse bu adlı makronun gövdesi (Blueprint Macro)
        std::string m_EditMacro;
        Visual::Graph& G();
        void EditMacro(const std::string& name);
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
        int m_BreakHitNode = 0;            // tetiklenen kesme noktası (oyun duraklatıldı)
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

        // Unity araç çubuğu anahtarları
        bool m_ShowRelations = false;
        bool m_ShowValues = true;
        bool m_Dim = true;
        bool m_Carry = false;
        bool m_Locked = false;
        bool m_ShowInspector = true;
        // Dim: ulaşılabilir düğümler (düzenlemeden sonra yeniden hesaplanır)
        std::set<int> m_Reachable;
        bool m_ReachableDirty = true;
        // Pin konumları (son çizim; ekran) ve düğüm başına pin satır ofsetleri (grafik birimi; Straighten)
        struct PinScreen { ImVec2 Pos; Visual::PinType Type; std::string Name; bool Output; };
        std::unordered_map<int, std::vector<PinScreen>> m_PinScreens;
        std::unordered_map<int, std::map<std::string, float>> m_PinOffsets[2];   // [0] giriş, [1] çıkış: pin -> düğüm üstünden y
        int m_LastHoveredNode = 0;
        std::set<int> m_RenderedNodes;      // son karede çizilen düğümler (imnodes havuzunda olanlar)
        // Bağlam menüleri
        int m_ContextNode = 0;
        int m_ContextPinAttr = -1;
        bool m_OpenNodeContext = false;
        bool m_OpenPinContext = false;
        // Yorum kutuları / yapışkan notlar
        int m_SelectedBox = 0;
        int m_EditBox = 0;
        int m_DragBox = 0;
        std::vector<int> m_BoxContents;
        std::string m_BoxSnapshot;
        bool m_BoxMoved = false;
        bool m_OpenBoxEdit = false;
        std::map<int, std::string> m_EntityGraphCache;
        // Alt panel: Find Results / Call Stack / Watches / Diff
        bool m_ShowBottom = false;
        int m_BottomTab = 0;
        char m_FindBuffer[128] = {};
        bool m_FindAll = true;
        std::vector<FindResult> m_FindResults;
        std::vector<Visual::DiffEntry> m_Diff;
        std::string m_DiffAgainst;
        std::string m_DiffError;
        std::set<int> m_DiffAdded, m_DiffChanged, m_DiffMoved;
        char m_BookmarkName[64] = {};
        // Seçimi izleme (kilit kapalıyken seçili varlığın görsel betiği açılır)
        uint64_t m_LastSelection = ~0ull;
        // Otomasyon takma adları (vsx komutu)
        std::map<std::string, int> m_AutoAlias;
        int m_Variablescope = 0;   // kenar çubuğu kapsam sekmesi
    };
}
