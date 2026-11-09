#include "NodeAssetPanel.h"

#include "Assets/AssetUI.h"
#include "Scripting/ScriptRuntime.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <imnodes.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace Conqueror::Editor
{
    namespace
    {
        double Now()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        ImU32 Brighten(ImU32 c, float f)
        {
            ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
            v.x = std::min(1.0f, v.x * f);
            v.y = std::min(1.0f, v.y * f);
            v.z = std::min(1.0f, v.z * f);
            return ImGui::ColorConvertFloat4ToU32(v);
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        std::vector<NodeAssetSchema>& Storage()
        {
            static std::vector<NodeAssetSchema> s;
            return s;
        }

        std::vector<std::string> ExpandExtensions(const std::vector<std::string>& in)
        {
            std::vector<std::string> out;
            for (const auto& e : in)
            {
                if (e != "@scripts")
                {
                    out.push_back(e);
                    continue;
                }
                for (auto* runtime : Scripting::ScriptRuntimes::All())
                    if (std::string(runtime->GetLanguageId()) != "stategraph")
                        for (const auto& x : runtime->GetExtensions())
                            out.push_back(x);
            }
            return out;
        }

        constexpr float kMinZoom = 0.25f, kMaxZoom = 2.0f;
    }

    void NodeAssetPanel::Register(NodeAssetSchema schema)
    {
        Storage().push_back(std::move(schema));
    }

    const std::vector<NodeAssetSchema>& NodeAssetPanel::Schemas()
    {
        return Storage();
    }

    const NodeAssetSchema* NodeAssetPanel::SchemaForExtension(const std::string& extension)
    {
        const std::string e = Lower(extension);
        for (const auto& s : Storage())
            if (s.Extension == e)
                return &s;
        return nullptr;
    }

    const NodeAssetSchema* NodeAssetPanel::SchemaForKind(const std::string& kind)
    {
        for (const auto& s : Storage())
            if (s.Kind == kind)
                return &s;
        return nullptr;
    }

    bool NodeAssetPanel::IsNodeAsset(const std::filesystem::path& path)
    {
        return SchemaForExtension(path.extension().string()) != nullptr;
    }

    void NodeAssetPanel::FillDefaults(const NodeAssetSchema& schema, NodeAssets::Node& node)
    {
        for (const auto& t : schema.Types)
            if (t.Id == node.Type)
                for (const auto& p : t.Props)
                    node.Props.try_emplace(p.Name, p.Default);
    }

    bool NodeAssetPanel::CreateFile(const NodeAssetSchema& schema, const std::filesystem::path& path)
    {
        NodeAssets::File file;
        file.Kind = schema.Kind;
        if (schema.Populate)
            schema.Populate(file);
        for (auto& n : file.Nodes)
            FillDefaults(schema, n);
        return NodeAssets::Save(path.string(), file);
    }

    NodeAssetPanel::NodeAssetPanel()
    {
        RegisterBuiltinNodeAssetSchemas();
        ImNodesContext* previous = ImNodes::GetCurrentContext();
        m_Context = ImNodes::CreateContext();
        ImNodes::SetCurrentContext(m_Context);
        ImNodesStyle& style = ImNodes::GetStyle();
        style.Colors[ImNodesCol_GridBackground] = IM_COL32(22, 23, 27, 255);
        style.Colors[ImNodesCol_GridLine] = IM_COL32(40, 42, 48, 255);
        style.Colors[ImNodesCol_GridLinePrimary] = IM_COL32(52, 55, 62, 255);
        style.Colors[ImNodesCol_NodeBackground] = IM_COL32(38, 40, 46, 245);
        style.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(44, 46, 53, 245);
        style.Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(48, 51, 60, 245);
        style.Colors[ImNodesCol_NodeOutline] = IM_COL32(20, 20, 24, 255);
        style.Colors[ImNodesCol_Link] = IM_COL32(205, 205, 215, 255);
        style.Colors[ImNodesCol_LinkHovered] = IM_COL32(255, 255, 255, 255);
        style.Colors[ImNodesCol_LinkSelected] = IM_COL32(255, 200, 80, 255);
        style.Colors[ImNodesCol_Pin] = IM_COL32(205, 205, 215, 255);
        style.Colors[ImNodesCol_PinHovered] = IM_COL32(255, 255, 255, 255);
        style.Flags |= ImNodesStyleFlags_GridLinesPrimary | ImNodesStyleFlags_GridSnapping;
        ApplyZoomStyle();
        ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;
        ImNodes::GetIO().MultipleSelectModifier.Modifier = &ImGui::GetIO().KeyShift;
        if (previous)
            ImNodes::SetCurrentContext(previous);
    }

    NodeAssetPanel::~NodeAssetPanel()
    {
        if (m_Context)
            ImNodes::DestroyContext(m_Context);
    }

    void NodeAssetPanel::ApplyZoomStyle()
    {
        ImNodesStyle& style = ImNodes::GetStyle();
        const float z = m_Zoom;
        style.GridSpacing = 16.0f * z;
        style.NodeCornerRounding = 6.0f * z;
        style.NodePadding = ImVec2(8.0f * z, 6.0f * z);
        style.LinkThickness = std::max(1.0f, 2.6f * z);
        style.LinkHoverDistance = 10.0f * z;
        style.PinCircleRadius = 4.5f * z;
        style.PinTriangleSideLength = 10.0f * z;
        style.PinQuadSideLength = 7.0f * z;
        style.PinHoverRadius = 10.0f * z;
    }

    void NodeAssetPanel::SetZoom(float zoom, const ImVec2& anchor)
    {
        zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        if (std::fabs(zoom - m_Zoom) < 1e-4f)
            return;
        ImVec2 pan = ImNodes::EditorContextGetPanning();
        ImVec2 logical((anchor.x - pan.x) / m_Zoom, (anchor.y - pan.y) / m_Zoom);
        m_Zoom = zoom;
        ImNodes::EditorContextResetPanning(ImVec2(anchor.x - logical.x * m_Zoom, anchor.y - logical.y * m_Zoom));
        ApplyZoomStyle();
        m_PositionsDirty = true;
    }

    void NodeAssetPanel::Open(const std::filesystem::path& path)
    {
        m_RequestFocus = true;
        if (m_Dirty)
            Save();
        m_Schema = SchemaForExtension(path.extension().string());
        NodeAssets::File file;
        if (!m_Schema || !NodeAssets::Load(path.string(), file))
        {
            m_Path.clear();
            return;
        }
        m_Path = path;
        m_File = std::move(file);
        m_File.Kind = m_Schema->Kind;
        for (auto& n : m_File.Nodes)
            FillDefaults(*m_Schema, n);
        m_Undo.clear();
        m_Redo.clear();
        m_Dirty = false;
        m_PositionsDirty = true;
        m_PreFrame.clear();
        m_Warning = m_Schema->Validate ? m_Schema->Validate(m_File) : std::string();
        FrameAll();
    }

    bool NodeAssetPanel::Save()
    {
        if (m_Path.empty() || !m_Schema)
            return false;
        m_Dirty = false;
        return NodeAssets::Save(m_Path.string(), m_File);
    }

    void NodeAssetPanel::MarkEdited()
    {
        m_Dirty = true;
        m_LastEdit = Now();
        m_Warning = m_Schema && m_Schema->Validate ? m_Schema->Validate(m_File) : std::string();
    }

    void NodeAssetPanel::PushUndo()
    {
        m_Undo.push_back(m_File.ToText());
        if (m_Undo.size() > 200)
            m_Undo.erase(m_Undo.begin());
        m_Redo.clear();
    }

    void NodeAssetPanel::Undo()
    {
        if (m_Undo.empty())
            return;
        m_Redo.push_back(m_File.ToText());
        m_File.FromText(m_Undo.back());
        m_Undo.pop_back();
        m_PositionsDirty = true;
        MarkEdited();
    }

    void NodeAssetPanel::Redo()
    {
        if (m_Redo.empty())
            return;
        m_Undo.push_back(m_File.ToText());
        m_File.FromText(m_Redo.back());
        m_Redo.pop_back();
        m_PositionsDirty = true;
        MarkEdited();
    }

    const NodeAssetType* NodeAssetPanel::TypeOf(const NodeAssets::Node& node) const
    {
        if (!m_Schema)
            return nullptr;
        for (const auto& t : m_Schema->Types)
            if (t.Id == node.Type)
                return &t;
        return nullptr;
    }

    void NodeAssetPanel::FrameAll()
    {
        if (m_File.Nodes.empty() || !m_Context)
            return;
        ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
        for (const auto& n : m_File.Nodes)
        {
            lo = ImVec2(std::min(lo.x, n.X), std::min(lo.y, n.Y));
            hi = ImVec2(std::max(hi.x, n.X + 240.0f), std::max(hi.y, n.Y + 120.0f));
        }
        ImNodesContext* previous = ImNodes::GetCurrentContext();
        ImNodes::SetCurrentContext(m_Context);
        const ImVec2 canvas = m_CanvasSize.x > 10.0f ? m_CanvasSize : ImVec2(900.0f, 560.0f);
        m_Zoom = std::clamp(std::min(canvas.x / std::max(1.0f, hi.x - lo.x), canvas.y / std::max(1.0f, hi.y - lo.y)) * 0.9f, kMinZoom, 1.0f);
        ApplyZoomStyle();
        ImNodes::EditorContextResetPanning(ImVec2(canvas.x * 0.5f - (lo.x + hi.x) * 0.5f * m_Zoom, canvas.y * 0.5f - (lo.y + hi.y) * 0.5f * m_Zoom));
        m_PositionsDirty = true;
        if (previous)
            ImNodes::SetCurrentContext(previous);
    }

    void NodeAssetPanel::AddNodeAt(const std::string& typeId, const ImVec2& grid)
    {
        if (!m_Schema)
            return;
        for (const auto& t : m_Schema->Types)
        {
            if (t.Id != typeId)
                continue;
            if (t.Single)
                for (const auto& n : m_File.Nodes)
                    if (n.Type == typeId)
                        return;
            PushUndo();
            int id = m_File.Add(typeId, grid.x, grid.y);
            FillDefaults(*m_Schema, *m_File.Find(id));
            ImNodesContext* previous = ImNodes::GetCurrentContext();
            ImNodes::SetCurrentContext(m_Context);
            ImNodes::SetNodeGridSpacePos(id, ImVec2(grid.x * m_Zoom, grid.y * m_Zoom));
            if (previous)
                ImNodes::SetCurrentContext(previous);
            // Bırakılan bağlantıyı yeni düğümün ilk uygun pinine bağla
            if (m_PendingLink >= 0)
            {
                int n, p;
                bool output;
                Decode(m_PendingLink, n, p, output);
                if (output && !t.Inputs.empty())
                    m_File.Connect(n, p, id, 0);
                else if (!output && !t.Outputs.empty())
                    m_File.Connect(id, 0, n, p);
                m_PendingLink = -1;
            }
            MarkEdited();
            return;
        }
    }

    void NodeAssetPanel::DeleteSelection()
    {
        const int nodes = ImNodes::NumSelectedNodes(), links = ImNodes::NumSelectedLinks();
        if (nodes <= 0 && links <= 0)
            return;
        PushUndo();
        if (links > 0)
        {
            std::vector<int> ids((size_t)links);
            ImNodes::GetSelectedLinks(ids.data());
            for (int id : ids)
                m_File.Disconnect(id);
        }
        if (nodes > 0)
        {
            std::vector<int> ids((size_t)nodes);
            ImNodes::GetSelectedNodes(ids.data());
            for (int id : ids)
                m_File.Remove(id);
        }
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
        MarkEdited();
    }

    void NodeAssetPanel::CopySelection()
    {
        const int count = ImNodes::NumSelectedNodes();
        if (count <= 0)
            return;
        std::vector<int> ids((size_t)count);
        ImNodes::GetSelectedNodes(ids.data());
        m_ClipNodes.clear();
        m_ClipLinks.clear();
        for (int id : ids)
            if (const auto* n = m_File.Find(id))
                m_ClipNodes.push_back(*n);
        for (const auto& l : m_File.Links)
            if (std::find(ids.begin(), ids.end(), l.From) != ids.end() && std::find(ids.begin(), ids.end(), l.To) != ids.end())
                m_ClipLinks.push_back(l);
    }

    void NodeAssetPanel::Paste(const ImVec2& grid)
    {
        if (m_ClipNodes.empty())
            return;
        PushUndo();
        ImVec2 lo(1e9f, 1e9f);
        for (const auto& n : m_ClipNodes)
            lo = ImVec2(std::min(lo.x, n.X), std::min(lo.y, n.Y));
        std::vector<std::pair<int, int>> remap;
        m_PendingSelect.clear();
        for (const auto& n : m_ClipNodes)
        {
            int id = m_File.Add(n.Type, n.X - lo.x + grid.x, n.Y - lo.y + grid.y);
            m_File.Find(id)->Props = n.Props;
            remap.emplace_back(n.Id, id);
            m_PendingSelect.push_back(id);
        }
        auto mapped = [&](int old) { for (auto& [a, b] : remap) if (a == old) return b; return 0; };
        for (const auto& l : m_ClipLinks)
            m_File.Connect(mapped(l.From), l.FromPin, mapped(l.To), l.ToPin);
        m_PositionsDirty = true;
        MarkEdited();
    }

    void NodeAssetPanel::Duplicate()
    {
        CopySelection();
        if (m_ClipNodes.empty())
            return;
        ImVec2 lo(1e9f, 1e9f);
        for (const auto& n : m_ClipNodes)
            lo = ImVec2(std::min(lo.x, n.X), std::min(lo.y, n.Y));
        Paste(ImVec2(lo.x + 32.0f, lo.y + 32.0f));
    }

    void NodeAssetPanel::OnImGuiRender()
    {
        if (!m_IsOpen)
            return;
        ImGui::SetNextWindowSize(ImVec2(1000, 640), ImGuiCond_FirstUseEver);
        std::string title = (m_Schema ? m_Schema->Icon + "  " + m_Schema->Title : std::string(ICON_FA_DIAGRAM_PROJECT "  Graph")) +
                            (m_Path.empty() ? "" : " - " + m_Path.filename().string() + (m_Dirty ? " *" : "")) + "###NodeAsset";
        if (m_RequestFocus)
        {
            ImGui::SetNextWindowFocus();
            m_RequestFocus = false;
        }
        if (!ImGui::Begin(title.c_str(), &m_IsOpen))
        {
            ImGui::End();
            return;
        }
        if (m_Path.empty() || !m_Schema)
        {
            ImGui::TextDisabled("No graph open. Double-click a State Graph, Behavior Tree, Sound Cue or PCG Graph in the Content Browser.");
            ImGui::End();
            return;
        }
        if (m_Dirty && Now() - m_LastEdit > 0.4 && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            Save();
        RenderToolbar();
        ImGui::BeginChild("##na_canvas", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderCanvas();
        ImGui::EndChild();
        ImGui::End();
    }

    void NodeAssetPanel::RenderToolbar()
    {
        if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save"))
            Save();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_Undo.empty());
        if (ImGui::Button(ICON_FA_ROTATE_LEFT))
            Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_Redo.empty());
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT))
            Redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_EXPAND "  Frame All"))
            FrameAll();
        ImGui::SameLine();
        if (!m_Warning.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", m_Warning.c_str());
        else if (!m_Schema->Hint.empty())
            ImGui::TextDisabled("%s", m_Schema->Hint.c_str());
    }

    bool NodeAssetPanel::DrawProp(NodeAssets::Node& node, const NodeAssetProp& prop)
    {
        using K = NodeAssetProp::Kind;
        ImGui::PushID(prop.Name.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(prop.Name.c_str());
        if (!prop.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", prop.Tooltip.c_str());
        ImGui::SameLine(Z(88.0f));
        std::string& value = node.Props[prop.Name];
        bool changed = false;
        ImGui::SetNextItemWidth(Z(150.0f));
        switch (prop.Type)
        {
            case K::Text:
                changed = AssetUI::InputString("##v", value);
                break;
            case K::Number:
            {
                float v = (float)std::atof(value.c_str());
                if (ImGui::DragFloat("##v", &v, 0.05f, 0.0f, 0.0f, "%.3g"))
                {
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "%g", v);
                    value = buf;
                    changed = true;
                }
                break;
            }
            case K::Bool:
            {
                bool v = value == "true" || value == "1";
                if (ImGui::Checkbox("##v", &v))
                {
                    value = v ? "true" : "false";
                    changed = true;
                }
                break;
            }
            case K::Choice:
                if (ImGui::BeginCombo("##v", value.c_str()))
                {
                    for (const auto& option : prop.Options)
                        if (ImGui::Selectable(option.c_str(), option == value))
                        {
                            value = option;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
                break;
            case K::Asset:
            {
                ImGui::BeginGroup();
                ImGui::PushItemWidth(Z(150.0f));
                ImVec2 size(Z(150.0f), 0.0f);
                ImGui::BeginChild("##slot", ImVec2(size.x, ImGui::GetFrameHeight()), false, ImGuiWindowFlags_NoScrollbar);
                changed = AssetUI::AssetSlot("##a", prop.Name, ExpandExtensions(prop.Options), value);
                ImGui::EndChild();
                ImGui::PopItemWidth();
                ImGui::EndGroup();
                break;
            }
        }
        ImGui::PopID();
        return changed;
    }

    void NodeAssetPanel::RenderNode(NodeAssets::Node& node, const NodeAssetType& type, bool active)
    {
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, type.Color);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, Brighten(type.Color, 1.15f));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, Brighten(type.Color, 1.3f));
        ImNodes::PushColorStyle(ImNodesCol_NodeOutline, active ? IM_COL32(90, 235, 130, 255) : IM_COL32(18, 18, 22, 255));
        if (active)
            ImNodes::PushStyleVar(ImNodesStyleVar_NodeBorderThickness, 3.0f);
        ImNodes::BeginNode(node.Id);
        ImNodes::BeginNodeTitleBar();
        std::string shown = type.Title;
        if (!type.TitleProp.empty())
        {
            std::string named = node.Str(type.TitleProp);
            if (named.find('/') != std::string::npos)
                named = std::filesystem::path(named).stem().string();
            if (!named.empty())
                shown = named + "  (" + type.Title + ")";
        }
        ImGui::TextUnformatted(shown.c_str());
        if (!type.Description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", type.Description.c_str());
        ImNodes::EndNodeTitleBar();

        bool changed = false;
        if (!type.Props.empty())
        {
            ImNodes::BeginStaticAttribute(node.Id * 1024 + 1023);
            ImGui::PushID(node.Id);
            for (const auto& prop : type.Props)
            {
                std::string before = node.Props[prop.Name];
                std::string snapshot;
                if (!m_EditingValue)
                    snapshot = m_File.ToText();
                if (DrawProp(node, prop))
                {
                    if (!m_EditingValue)
                    {
                        m_Undo.push_back(snapshot);
                        m_Redo.clear();
                        m_EditingValue = true;
                    }
                    changed = true;
                }
            }
            ImGui::PopID();
            ImNodes::EndStaticAttribute();
        }
        const size_t rows = std::max(type.Inputs.size(), type.Outputs.size());
        float outWidth = 0.0f;
        for (const auto& o : type.Outputs)
            outWidth = std::max(outWidth, ImGui::CalcTextSize(o.c_str()).x);
        for (size_t r = 0; r < rows; ++r)
        {
            const float startX = ImGui::GetCursorPosX();
            if (r < type.Inputs.size())
            {
                const bool connected = !m_File.In(node.Id, (int)r).empty();
                ImNodes::BeginInputAttribute(Attr(node.Id, (int)r, false), connected ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle);
                ImGui::TextUnformatted(type.Inputs[r].c_str());
                ImNodes::EndInputAttribute();
            }
            if (r < type.Outputs.size())
            {
                if (r < type.Inputs.size())
                    ImGui::SameLine();
                ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), startX + Z(150.0f)));
                const bool connected = !m_File.Out(node.Id, (int)r).empty();
                ImNodes::BeginOutputAttribute(Attr(node.Id, (int)r, true), connected ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle);
                const float w = ImGui::CalcTextSize(type.Outputs[r].c_str()).x;
                ImGui::Indent(outWidth - w);
                ImGui::TextUnformatted(type.Outputs[r].c_str());
                ImGui::Unindent(outWidth - w);
                ImNodes::EndOutputAttribute();
            }
        }
        ImNodes::EndNode();
        if (active)
            ImNodes::PopStyleVar();
        for (int i = 0; i < 4; ++i)
            ImNodes::PopColorStyle();
        if (changed)
            MarkEdited();
    }

    void NodeAssetPanel::RenderCanvas()
    {
        ImNodesContext* previous = ImNodes::GetCurrentContext();
        ImNodes::SetCurrentContext(m_Context);
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;

        ApplyZoomStyle();
        ImNodes::BeginNodeEditor();
        m_CanvasOrigin = ImGui::GetCursorScreenPos();
        m_CanvasSize = ImGui::GetContentRegionAvail();
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f && ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive())
            SetZoom(m_Zoom * std::pow(1.12f, io.MouseWheel), ImVec2(io.MousePos.x - m_CanvasOrigin.x, io.MousePos.y - m_CanvasOrigin.y));
        if (m_PositionsDirty)
        {
            for (auto& n : m_File.Nodes)
                ImNodes::SetNodeGridSpacePos(n.Id, ImVec2(n.X * m_Zoom, n.Y * m_Zoom));
            m_PositionsDirty = false;
        }

        std::vector<int> activeNodes;
        if (m_Schema->ActiveNodes)
            activeNodes = m_Schema->ActiveNodes(m_Path.string());

        ImGui::PushFont(nullptr, GImGui->FontSizeBase * m_Zoom);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x * m_Zoom, ImGui::GetStyle().ItemSpacing.y * m_Zoom));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(ImGui::GetStyle().ItemInnerSpacing.x * m_Zoom, ImGui::GetStyle().ItemInnerSpacing.y * m_Zoom));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x * m_Zoom, ImGui::GetStyle().FramePadding.y * m_Zoom));
        for (auto& n : m_File.Nodes)
            if (const NodeAssetType* type = TypeOf(n))
                RenderNode(n, *type, std::find(activeNodes.begin(), activeNodes.end(), n.Id) != activeNodes.end());
        ImGui::PopStyleVar(3);
        ImGui::PopFont();
        for (const auto& l : m_File.Links)
            ImNodes::Link(l.Id, Attr(l.From, l.FromPin, true), Attr(l.To, l.ToPin, false));
        ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        if (!m_PendingSelect.empty())
        {
            ImNodes::ClearNodeSelection();
            for (int id : m_PendingSelect)
                ImNodes::SelectNode(id);
            m_PendingSelect.clear();
        }

        bool moved = false;
        for (auto& n : m_File.Nodes)
        {
            ImVec2 p = ImNodes::GetNodeGridSpacePos(n.Id);
            p = ImVec2(p.x / m_Zoom, p.y / m_Zoom);
            if (std::fabs(p.x - n.X) > 0.5f || std::fabs(p.y - n.Y) > 0.5f)
            {
                n.X = std::round(p.x);
                n.Y = std::round(p.y);
                moved = true;
            }
        }
        if (moved)
        {
            if (!m_Dragging)
            {
                m_DragSnapshot = m_PreFrame;
                m_Dragging = true;
            }
            m_Dirty = true;
            m_LastEdit = Now();
        }
        if (m_Dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_Dragging = false;
            if (!m_DragSnapshot.empty())
            {
                m_Undo.push_back(m_DragSnapshot);
                m_Redo.clear();
            }
            m_DragSnapshot.clear();
        }
        if (!ImGui::IsAnyItemActive())
            m_EditingValue = false;
        m_PreFrame = m_File.ToText();

        int start = 0, end = 0;
        if (ImNodes::IsLinkCreated(&start, &end))
        {
            int sn, sp, en, ep;
            bool so, eo;
            Decode(start, sn, sp, so);
            Decode(end, en, ep, eo);
            if (!so)
            {
                std::swap(sn, en);
                std::swap(sp, ep);
            }
            PushUndo();
            const auto* target = m_File.Find(en);
            const NodeAssetType* targetType = target ? TypeOf(*target) : nullptr;
            if (targetType && targetType->SingleInput)
                for (const auto* l : m_File.In(en, ep))
                {
                    m_File.Disconnect(l->Id);
                    break;
                }
            if (m_File.Connect(sn, sp, en, ep))
                MarkEdited();
            else
                m_Undo.pop_back();
        }
        int destroyed = 0;
        if (ImNodes::IsLinkDestroyed(&destroyed))
        {
            PushUndo();
            m_File.Disconnect(destroyed);
            MarkEdited();
        }
        int dropped = -1;
        if (ImNodes::IsLinkDropped(&dropped, false))
        {
            m_PendingLink = dropped;
            m_OpenAdd = true;
            m_AddScreen = ImGui::GetMousePos();
        }

        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        const ImVec2 pan = ImNodes::EditorContextGetPanning();
        const ImVec2 mouseGrid((io.MousePos.x - m_CanvasOrigin.x - pan.x) / m_Zoom, (io.MousePos.y - m_CanvasOrigin.y - pan.y) / m_Zoom);
        const ImVec2 rightDrag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && rightDrag.x * rightDrag.x + rightDrag.y * rightDrag.y < 36.0f)
        {
            int hoveredNode = 0;
            if (!ImNodes::IsNodeHovered(&hoveredNode))
            {
                m_OpenAdd = true;
                m_AddScreen = io.MousePos;
                m_AddGrid = mouseGrid;
                m_PendingLink = -1;
            }
        }
        if (m_OpenAdd && m_PendingLink >= 0)
            m_AddGrid = mouseGrid;
        if (focused && !ImGui::IsAnyItemActive())
        {
            const bool ctrl = io.KeyCtrl || io.KeySuper;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) DeleteSelection();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) CopySelection();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) Paste(mouseGrid);
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) Duplicate();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) Redo(); else Undo(); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Save();
            if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) FrameAll();
            if (!ctrl && hovered && (ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_Tab, false)))
            {
                m_OpenAdd = true;
                m_AddScreen = io.MousePos;
                m_AddGrid = mouseGrid;
                m_PendingLink = -1;
            }
        }
        RenderAddPopup();
        if (m_File.Nodes.empty())
        {
            const char* hint = "Right-click or press Space to add a node";
            const ImVec2 size = ImGui::CalcTextSize(hint);
            const ImVec2 c(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x * 0.5f, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y * 0.5f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.5f), IM_COL32(140, 140, 150, 255), hint);
        }
        ImNodes::SetCurrentContext(previous ? previous : m_Context);
    }

    void NodeAssetPanel::RenderAddPopup()
    {
        if (m_OpenAdd)
        {
            ImGui::OpenPopup("##na_add");
            ImGui::SetNextWindowPos(m_AddScreen);
            m_Search[0] = 0;
            m_OpenAdd = false;
            m_FocusSearch = true;
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(360, 440));
        if (!ImGui::BeginPopup("##na_add"))
        {
            m_PendingLink = -1;
            return;
        }
        if (m_FocusSearch)
        {
            ImGui::SetKeyboardFocusHere();
            m_FocusSearch = false;
        }
        ImGui::SetNextItemWidth(-1);
        const bool enter = ImGui::InputTextWithHint("##q", ICON_FA_MAGNIFYING_GLASS "  Search nodes", m_Search, sizeof(m_Search), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string filter = Lower(m_Search);
        const NodeAssetType* first = nullptr;
        const NodeAssetType* chosen = nullptr;
        std::string category;
        for (const auto& t : m_Schema->Types)
        {
            if (!filter.empty() && Lower(t.Title).find(filter) == std::string::npos && Lower(t.Category).find(filter) == std::string::npos)
                continue;
            if (t.Single)
            {
                bool exists = false;
                for (const auto& n : m_File.Nodes)
                    exists |= n.Type == t.Id;
                if (exists)
                    continue;
            }
            if (t.Category != category)
            {
                category = t.Category;
                if (!category.empty())
                    ImGui::SeparatorText(category.c_str());
            }
            if (!first)
                first = &t;
            if (ImGui::Selectable((t.Title + "##" + t.Id).c_str()))
                chosen = &t;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !t.Description.empty())
                ImGui::SetTooltip("%s", t.Description.c_str());
        }
        if (enter && first)
            chosen = first;
        if (chosen)
        {
            AddNodeAt(chosen->Id, m_AddGrid);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_PendingLink = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
