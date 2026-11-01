#include "CQSNativeBridge.h"
#include <algorithm>
#include <cmath>
#include "CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Grid/GridSystem.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"

// Izgara / karo haritası betik API'si (ilk argüman: karo haritası varlığı; -1/null = bu varlık)
namespace Conqueror::CQS
{
    namespace
    {
        Entity MapEntity(int argc, Value* args)
        {
            Scene* scene = ScriptContext::GetActiveScene();
            if (!scene)
                return {};
            uint32_t id = ScriptContext::GetCurrentEntity();
            if (argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0)
                id = (uint32_t)(int64_t)args[0].ToNumber();
            entt::entity h = (entt::entity)id;
            if (id == ScriptContext::NullEntity || !scene->m_Registry.valid(h))
                return {};
            Entity e{ h, scene };
            return e.HasComponent<TilemapComponent>() ? e : Entity{};
        }

        int I(int argc, Value* args, int i, int def = 0)
        {
            return i < argc && args[i].IsNumber() ? (int)std::floor(args[i].ToNumber() + 0.5) : def;
        }
        float F(int argc, Value* args, int i, float def = 0.0f)
        {
            return i < argc && args[i].IsNumber() ? (float)args[i].ToNumber() : def;
        }

        Value Cell(const glm::ivec2& c)
        {
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeInt(c.x);
            m->Entries["y"] = Value::MakeInt(c.y);
            return Value::MakeObject(m);
        }

        void Touch(Entity e)
        {
            if (auto* w = e.GetScene()->GetPhysicsWorld2D())
                if (e.HasComponent<TilemapCollider2DComponent>())
                    w->MarkDirty(e);
        }
    }

    void RegisterGridNatives(Scripting::NativeRegistry* vm)
    {
        vm->DefineNative("TilemapSetTile", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e || argc < 4) return Value::MakeBool(false);
            glm::vec4 color(F(argc, args, 4, 1.0f), F(argc, args, 5, 1.0f), F(argc, args, 6, 1.0f), F(argc, args, 7, 1.0f));
            GridSystem::SetTile(e.GetComponent<TilemapComponent>(), { I(argc, args, 1), I(argc, args, 2) }, I(argc, args, 3), color);
            Touch(e);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("TilemapGetTile", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e || argc < 3) return Value::MakeInt(-1);
            return Value::MakeInt(GridSystem::GetTile(e.GetComponent<TilemapComponent>(), { I(argc, args, 1), I(argc, args, 2) }));
        }, 3);
        vm->DefineNative("TilemapClearTile", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e || argc < 3) return Value::MakeNull();
            GridSystem::ClearTile(e.GetComponent<TilemapComponent>(), { I(argc, args, 1), I(argc, args, 2) });
            Touch(e);
            return Value::MakeNull();
        }, 3);
        vm->DefineNative("TilemapClear", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e) return Value::MakeNull();
            auto& map = e.GetComponent<TilemapComponent>();
            map.Cells.clear();
            map.Version++;
            Touch(e);
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("TilemapFill", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e || argc < 6) return Value::MakeNull();
            GridSystem::BoxFill(e.GetComponent<TilemapComponent>(), { I(argc, args, 1), I(argc, args, 2) }, { I(argc, args, 3), I(argc, args, 4) }, I(argc, args, 5));
            Touch(e);
            return Value::MakeNull();
        }, 6);
        vm->DefineNative("TilemapTileCount", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            return Value::MakeInt(e ? (int64_t)e.GetComponent<TilemapComponent>().Cells.size() : 0);
        }, 1);
        vm->DefineNative("TilemapSetCellColor", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e || argc < 7) return Value::MakeBool(false);
            auto& map = e.GetComponent<TilemapComponent>();
            auto it = map.Cells.find(GridKey(I(argc, args, 1), I(argc, args, 2)));
            if (it == map.Cells.end()) return Value::MakeBool(false);
            it->second.Color = { F(argc, args, 3), F(argc, args, 4), F(argc, args, 5), F(argc, args, 6) };
            return Value::MakeBool(true);
        }, 7);
        vm->DefineNative("TilemapGetTileHeight", [](int argc, Value* args) -> Value {
            // 3D karonun üst yüzü (karo yüksekliği + hücre yüksekliği); boş hücre: 0
            Entity e = MapEntity(argc, args);
            if (!e || argc < 3) return Value::MakeFloat(0.0);
            auto& map = e.GetComponent<TilemapComponent>();
            auto it = map.Cells.find(GridKey(I(argc, args, 1), I(argc, args, 2)));
            if (it == map.Cells.end() || it->second.Tile < 0 || it->second.Tile >= (int)map.Palette.size()) return Value::MakeFloat(0.0);
            float h = map.Render3D ? std::max(0.01f, map.Palette[(size_t)it->second.Tile].Height) : 0.0f;
            return Value::MakeFloat(h + it->second.Elevation);
        }, 3);
        vm->DefineNative("TilemapSetCellElevation", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e || argc < 4) return Value::MakeBool(false);
            auto& map = e.GetComponent<TilemapComponent>();
            auto it = map.Cells.find(GridKey(I(argc, args, 1), I(argc, args, 2)));
            if (it == map.Cells.end()) return Value::MakeBool(false);
            it->second.Elevation = F(argc, args, 3);
            map.Version++;
            return Value::MakeBool(true);
        }, 4);
        vm->DefineNative("TilemapFindTileType", [](int argc, Value* args) -> Value {
            // Palet indisini adla bul (yoksa -1)
            Entity e = MapEntity(argc, args);
            if (!e || argc < 2) return Value::MakeInt(-1);
            auto& palette = e.GetComponent<TilemapComponent>().Palette;
            const std::string name = args[1].ToString();
            for (size_t i = 0; i < palette.size(); ++i)
                if (palette[i].Name == name) return Value::MakeInt((int64_t)i);
            return Value::MakeInt(-1);
        }, 2);
        vm->DefineNative("TilemapAddTileType", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e) return Value::MakeInt(-1);
            auto& map = e.GetComponent<TilemapComponent>();
            TileDefinition def;
            def.Name = argc > 1 ? args[1].ToString() : "Tile";
            def.Color = { F(argc, args, 2, 1.0f), F(argc, args, 3, 1.0f), F(argc, args, 4, 1.0f), F(argc, args, 5, 1.0f) };
            map.Palette.push_back(def);
            map.Version++;
            return Value::MakeInt((int64_t)map.Palette.size() - 1);
        }, -1);
        vm->DefineNative("TilemapSetTileType", [](int argc, Value* args) -> Value {
            // TilemapSetTileType(map, index, {Name, Color: {r,g,b,a}, Texture, Solid, Walkable, Cost, Height, Autotile, Terrain})
            Entity e = MapEntity(argc, args);
            int t = I(argc, args, 1, -1);
            if (!e || argc < 3 || !args[2].IsMap()) return Value::MakeBool(false);
            auto& map = e.GetComponent<TilemapComponent>();
            if (t < 0 || t >= (int)map.Palette.size()) return Value::MakeBool(false);
            auto* props = args[2].AsMap();
            if (!props) return Value::MakeBool(false);
            TileDefinition& def = map.Palette[(size_t)t];
            for (const auto& [key, v] : props->Entries)
            {
                if (key == "Name") def.Name = v.ToString();
                else if (key == "Texture") def.Texture = v.ToString();
                else if (key == "Solid") def.Solid = v.IsTruthy();
                else if (key == "Walkable") def.Walkable = v.IsTruthy();
                else if (key == "Cost") def.Cost = std::max(1, (int)v.ToNumber());
                else if (key == "Height") def.Height = (float)v.ToNumber();
                else if (key == "Autotile") def.Autotile = std::clamp((int)v.ToNumber(), 0, 2);
                else if (key == "Terrain") def.Terrain = (int)v.ToNumber();
                else if (key == "Color" && v.IsMap())
                {
                    if (auto* c = v.AsMap())
                    {
                        auto comp = [&](const char* k, float d) { auto it = c->Entries.find(k); return it != c->Entries.end() ? (float)it->second.ToNumber() : d; };
                        def.Color = { comp("r", 1.0f), comp("g", 1.0f), comp("b", 1.0f), comp("a", 1.0f) };
                    }
                }
            }
            map.Version++;
            Touch(e);
            return Value::MakeBool(true);
        }, 3);
        vm->DefineNative("TilemapGetTileName", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            int t = I(argc, args, 1, -1);
            if (!e || t < 0 || t >= (int)e.GetComponent<TilemapComponent>().Palette.size()) return Value::MakeString("");
            return Value::MakeString(e.GetComponent<TilemapComponent>().Palette[(size_t)t].Name);
        }, 2);
        vm->DefineNative("GridCellToWorld", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e || argc < 3) return Value::MakeNull();
            glm::vec3 p = GridSystem::CellToWorld(e, { I(argc, args, 1), I(argc, args, 2) }, F(argc, args, 3));
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(p.x);
            m->Entries["y"] = Value::MakeFloat(p.y);
            m->Entries["z"] = Value::MakeFloat(p.z);
            return Value::MakeObject(m);
        }, -1);
        vm->DefineNative("GridWorldToCell", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e || argc < 4) return Value::MakeNull();
            return Cell(GridSystem::WorldToCell(e, { F(argc, args, 1), F(argc, args, 2), F(argc, args, 3) }));
        }, 4);
        vm->DefineNative("GridScreenToCell", [](int argc, Value* args) -> Value {
            // GridScreenToCell(map, mx, my) -> {x, y} | null: fare altındaki hücre (ızgara düzlemiyle kesişim)
            Entity e = MapEntity(argc, args);
            glm::vec3 o, d, hit;
            if (!e || argc < 3 || !ScreenPointToWorldRay(e.GetScene(), F(argc, args, 1), F(argc, args, 2), o, d))
                return Value::MakeNull();
            if (!GridSystem::RaycastPlane(e, o, d, hit))
                return Value::MakeNull();
            return Cell(GridSystem::WorldToCell(e, hit));
        }, 3);
        vm->DefineNative("GridNeighbors", [](int argc, Value* args) -> Value {
            auto* l = new CQSListObject();
            Entity e = MapEntity(argc, args);
            if (e && argc >= 3)
            {
                bool diagonals = argc > 3 && args[3].IsBool() && args[3].AsBool();
                for (auto& c : GridSystem::Neighbors(GridSystem::GridOf(e), { I(argc, args, 1), I(argc, args, 2) }, diagonals))
                    l->Elements.push_back(Cell(c));
            }
            return Value::MakeObject(l);
        }, -1);
        vm->DefineNative("GridDistance", [](int argc, Value* args) -> Value {
            Entity e = MapEntity(argc, args);
            if (!e || argc < 5) return Value::MakeInt(0);
            return Value::MakeInt(GridSystem::Distance(GridSystem::GridOf(e), { I(argc, args, 1), I(argc, args, 2) }, { I(argc, args, 3), I(argc, args, 4) }));
        }, 5);
        vm->DefineNative("GridFindPath", [](int argc, Value* args) -> Value {
            auto* l = new CQSListObject();
            Entity e = MapEntity(argc, args);
            if (e && argc >= 5)
                for (auto& c : GridSystem::FindPath(GridSystem::GridOf(e), e.GetComponent<TilemapComponent>(), { I(argc, args, 1), I(argc, args, 2) },
                                                    { I(argc, args, 3), I(argc, args, 4) }))
                    l->Elements.push_back(Cell(c));
            return Value::MakeObject(l);
        }, 5);
    }
}
