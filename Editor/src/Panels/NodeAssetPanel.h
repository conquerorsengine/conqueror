#pragma once

#include "Core/Assets/Graph/NodeAsset.h"

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

struct ImNodesContext;

namespace Conqueror::Editor
{
    struct NodeAssetProp
    {
        enum class Kind { Text, Number, Bool, Choice, Asset };
        std::string Name;
        Kind Type = Kind::Text;
        std::string Default;
        std::string Tooltip;
        std::vector<std::string> Options;   // Choice: seçenekler; Asset: uzantılar ("@scripts" = tüm betik uzantıları)
    };

    struct NodeAssetType
    {
        std::string Id, Title, Category, Description;
        ImU32 Color = IM_COL32(70, 90, 130, 255);
        std::vector<std::string> Inputs, Outputs;
        std::vector<NodeAssetProp> Props;
        std::string TitleProp;              // doluysa başlıkta bu özellik gösterilir (durum adı gibi)
        bool Single = false;                // grafikte yalnızca bir tane olabilir (Root / Output)
        bool SingleInput = false;           // giriş pini tek bağlantı alır
    };

    struct NodeAssetSchema
    {
        std::string Kind;                   // dosyadaki NodeAsset değeri
        std::string Extension;
        std::string Title;
        std::string Icon;
        std::string Hint;                   // araç çubuğundaki kısa kullanım notu
        std::string MenuCategory;           // Create menüsü kategorisi
        std::string MenuSubmenu;
        std::string MenuTooltip;
        ImU32 Color = IM_COL32(120, 220, 200, 255);   // Content Browser simgesi
        std::vector<NodeAssetType> Types;
        std::function<void(NodeAssets::File&)> Populate;                      // yeni dosyanın başlangıç içeriği
        std::function<std::string(const NodeAssets::File&)> Validate;         // boş değilse uyarı olarak gösterilir
        std::function<std::vector<int>(const std::string&)> ActiveNodes;      // oyun sırasında çalışan düğümler
    };

    // Düğüm grafiği varlıklarının ortak düzenleyicisi: tuval, düğüm arama, bağlantılar, özellikler, geri al, otomatik kayıt.
    class NodeAssetPanel
    {
    public:
        NodeAssetPanel();
        ~NodeAssetPanel();

        void OnImGuiRender();
        void Open(const std::filesystem::path& path);
        bool Save();
        bool& GetIsOpen() { return m_IsOpen; }
        const std::filesystem::path& GetPath() const { return m_Path; }

        // Varlığı panelde aç (Inspector düğmesi, çift tıklama)
        static inline std::function<void(const std::string&)> s_Open;

        static void Register(NodeAssetSchema schema);
        static const NodeAssetSchema* SchemaForExtension(const std::string& extension);
        static const NodeAssetSchema* SchemaForKind(const std::string& kind);
        static bool IsNodeAsset(const std::filesystem::path& path);
        static const std::vector<NodeAssetSchema>& Schemas();
        // Yeni dosya: türün başlangıç içeriğiyle yazar
        static bool CreateFile(const NodeAssetSchema& schema, const std::filesystem::path& path);
        // Düğüm özelliklerini türün varsayılanlarıyla doldurur
        static void FillDefaults(const NodeAssetSchema& schema, NodeAssets::Node& node);

        // Otomasyon
        NodeAssets::File& GetFile() { return m_File; }
        void MarkEdited();

    private:
        void RenderToolbar();
        void RenderCanvas();
        void RenderNode(NodeAssets::Node& node, const NodeAssetType& type, bool active);
        bool DrawProp(NodeAssets::Node& node, const NodeAssetProp& prop);
        void RenderAddPopup();
        void AddNodeAt(const std::string& type, const ImVec2& grid);
        void DeleteSelection();
        void Duplicate();
        void CopySelection();
        void Paste(const ImVec2& grid);
        void PushUndo();
        void Undo();
        void Redo();
        void FrameAll();
        void ApplyZoomStyle();
        void SetZoom(float zoom, const ImVec2& anchor);
        float Z(float v) const { return v * m_Zoom; }
        const NodeAssetType* TypeOf(const NodeAssets::Node& node) const;

        static int Attr(int node, int pin, bool output) { return node * 1024 + (output ? 512 : 0) + pin; }
        static void Decode(int id, int& node, int& pin, bool& output)
        {
            node = id / 1024;
            int rest = id % 1024;
            output = rest >= 512;
            pin = output ? rest - 512 : rest;
        }

        NodeAssets::File m_File;
        const NodeAssetSchema* m_Schema = nullptr;
        std::filesystem::path m_Path;
        bool m_IsOpen = false;
        bool m_Dirty = false;
        bool m_PositionsDirty = true;
        bool m_RequestFocus = false;
        bool m_Dragging = false;
        bool m_EditingValue = false;
        double m_LastEdit = 0.0;
        float m_Zoom = 1.0f;
        std::string m_Warning;
        std::string m_PreFrame, m_DragSnapshot;
        std::vector<std::string> m_Undo, m_Redo;
        ImNodesContext* m_Context = nullptr;

        bool m_OpenAdd = false, m_FocusSearch = false;
        ImVec2 m_AddScreen{ 0, 0 }, m_AddGrid{ 0, 0 };
        ImVec2 m_CanvasOrigin{ 0, 0 }, m_CanvasSize{ 0, 0 };
        char m_Search[96] = {};
        int m_PendingLink = -1;

        struct ClipNode { NodeAssets::Node Node; };
        std::vector<NodeAssets::Node> m_ClipNodes;
        std::vector<NodeAssets::Link> m_ClipLinks;
        std::vector<int> m_PendingSelect;
    };

    // Yerleşik grafik türlerini kaydeder (bir kez)
    void RegisterBuiltinNodeAssetSchemas();
}
