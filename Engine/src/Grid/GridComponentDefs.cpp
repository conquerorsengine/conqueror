#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Grid/GridComponents.h"
#include "Grid/GridSystem.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>

namespace Conqueror
{
    namespace
    {
        void SaveTilemap(YAML::Emitter& out, TilemapComponent& map)
        {
            out << YAML::Key << "Palette" << YAML::Value << YAML::BeginSeq;
            for (const auto& t : map.Palette)
                GridSystem::EmitTileDefinition(out, t);
            out << YAML::EndSeq;
            // Hücreler: [x, y, karo, r, g, b, a, yükseklik] (sıralı: kararlı dosya farkları)
            std::vector<std::pair<uint64_t, TileCell>> cells(map.Cells.begin(), map.Cells.end());
            std::sort(cells.begin(), cells.end(), [](auto& a, auto& b) {
                glm::ivec2 ca = GridKeyToCell(a.first), cb = GridKeyToCell(b.first);
                return ca.y != cb.y ? ca.y < cb.y : ca.x < cb.x;
            });
            out << YAML::Key << "Cells" << YAML::Value << YAML::BeginSeq;
            for (const auto& [key, cell] : cells)
            {
                glm::ivec2 c = GridKeyToCell(key);
                out << YAML::Flow << YAML::BeginSeq << c.x << c.y << cell.Tile;
                if (cell.Color != glm::vec4(1.0f) || cell.Elevation != 0.0f)
                    out << cell.Color.r << cell.Color.g << cell.Color.b << cell.Color.a << cell.Elevation;
                out << YAML::EndSeq;
            }
            out << YAML::EndSeq;
        }

        void LoadTilemap(const YAML::Node& node, TilemapComponent& map)
        {
            if (auto palette = node["Palette"]; palette && palette.IsSequence())
            {
                map.Palette.clear();
                for (auto t : palette)
                {
                    TileDefinition def;
                    GridSystem::ReadTileDefinition(t, def);
                    map.Palette.push_back(def);
                }
            }
            if (auto cells = node["Cells"]; cells && cells.IsSequence())
            {
                map.Cells.clear();
                for (auto c : cells)
                {
                    if (!c.IsSequence() || c.size() < 3) continue;
                    TileCell cell;
                    cell.Tile = c[2].as<int>();
                    if (c.size() >= 7)
                        cell.Color = { c[3].as<float>(), c[4].as<float>(), c[5].as<float>(), c[6].as<float>() };
                    if (c.size() >= 8)
                        cell.Elevation = c[7].as<float>();
                    map.Cells[GridKey(c[0].as<int>(), c[1].as<int>())] = cell;
                }
            }
            map.Version++;
        }
    }

    void RegisterGridComponents()
    {
        const glm::vec4 kGridColor{ 0.45f, 0.85f, 0.75f, 1.0f };
        ComponentDef<GridComponent>("Grid", "GridComponent", "Grid", ICON_FA_BORDER_ALL).Color(kGridColor)
            .Describe("Cell layout for child Tilemaps: rectangle, hexagon (pointy / flat top) or isometric.")
            .Field("Layout", "Cell Layout", &GridComponent::Layout).Options({ "Rectangle", "Hexagon (Pointy Top)", "Hexagon (Flat Top)", "Isometric" })
            .Field("CellSize", "Cell Size", &GridComponent::CellSize).Speed(0.01f)
            .Field("CellGap", "Cell Gap", &GridComponent::CellGap).Speed(0.005f)
            .Field("Plane", "Plane", &GridComponent::Plane).Options({ "XY (2D)", "XZ (3D Board)" })
            .Field("ShowGridInEditor", "Show Grid In Editor", &GridComponent::ShowGridInEditor).Header("Editor")
            .Field("EditorGridExtent", "Editor Grid Extent", &GridComponent::EditorGridExtent).Range(1.0f, 64.0f, 1.0f);

        ComponentDef<TilemapComponent>("Tilemap", "TilemapComponent", "Grid", ICON_FA_TABLE_CELLS).Color(kGridColor)
            .Describe("Paints tiles from a palette onto its Grid (Window > Tile Palette). Supports pathfinding and colliders.")
            .Field("Tint", "Color", &TilemapComponent::Tint, FieldKind::Color4)
            .Field("OrderInLayer", "Order In Layer", &TilemapComponent::OrderInLayer).Range(-32768.0f, 32767.0f, 1.0f)
            .Field("Render3D", "Render As 3D Tiles", &TilemapComponent::Render3D)
                .Tooltip("Draws each tile as a lit prism (hex/box) using the tile's Height - great for 3D board games.")
            .Field("Outline", "Cell Outlines", &TilemapComponent::Outline)
            .CustomData(SaveTilemap, LoadTilemap);

        ComponentDef<TilemapCollider2DComponent>("Tilemap Collider 2D", "TilemapCollider2DComponent", "Grid", ICON_FA_VECTOR_SQUARE).Color(kGridColor)
            .Requires("TilemapComponent")
            .Describe("Generates static 2D colliders for every Solid tile.")
            .Field("IsTrigger", "Is Trigger", &TilemapCollider2DComponent::IsTrigger)
            .Field("PhysicsMaterial", "Material", &TilemapCollider2DComponent::PhysicsMaterial, FieldKind::Asset).Filter(".cqphysmat2d")
            .Field("Friction", "Friction", &TilemapCollider2DComponent::Friction).Range(0.0f, 1.0f, 0.01f)
            .Field("Restitution", "Bounciness", &TilemapCollider2DComponent::Restitution).Range(0.0f, 1.0f, 0.01f);
    }
}
