#include "HubTemplates.h"
#include "HubTemplateKit.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/EditorCamera.h"
#include "Grid/GridSystem.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/MeshRendererMaterial.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Project/Project.h"
#include <yaml-cpp/yaml.h>
#include "Core/Project/ProjectSerializer.h"
#include "Core/PhysicsSystem/PhysicsSettings.h"
#include "Core/Math/Math.h"
#include "Core/Logging/Log.h"
#include "Addons/AddonManager.h"
#include "Renderer/RHI/Texture.h"

#include <IconsFontAwesome6.h>

#include <cctype>
#include <cmath>
#include <fstream>

namespace Conqueror::Launcher
{
    namespace
    {
        namespace fs = std::filesystem;

        // ------------------------------------------------------------------ Betikler

        const char* kPlayerController2D = R"CQS(using nativebridge;

// 2D platform oyuncusu: A / D ya da oklar ile yürü (ivmeli, kayma yok), Space / W ile zıpla.
// Zıplama: tuşu erken bırakırsan alçak zıplar, düşüşte yerçekimi artar (havada süzülmez), en fazla düşme hızı sınırlı.
// Yer denetimi ayakların iki köşesinden kısa ışınla; "coyote time" kenardan düşerken, "jump buffer" yere değmeden
// az önce basılan zıplamayı sayar. Paralar (Coin) toplanır, Goal bayrağına değince bölüm biter.
script PlayerController2D : ConquerorScript
{
    public float moveSpeed = 6.5;
    public float acceleration = 60.0;        // yerde (m/s²)
    public float airAcceleration = 35.0;
    public float jumpHeight = 2.6;           // m (yaklaşık iki buçuk karo)
    public float fallMultiplier = 1.7;       // düşerken ek yerçekimi
    public float lowJumpMultiplier = 2.5;    // tuş bırakılınca kısa zıplama
    public float maxFallSpeed = 18.0;
    public float coyoteTime = 0.1;
    public float jumpBuffer = 0.12;
    public int coins = 0;
    public int totalCoins = 0;
    public bool grounded = false;
    public bool finished = false;
    var halfHeight = 0.5;
    var halfWidth = 0.5;
    var sinceGrounded = 1.0;
    var sinceJumpPressed = 1.0;
    var vx = 0.0;
    var startPos = null;

    func Start()
    {
        var size = GetComponentField("BoxCollider2D", "Size");
        var s = GetScale();
        if (size != null)
        {
            halfHeight = size.y * MathAbs(s.y) * 0.5;
            halfWidth = size.x * MathAbs(s.x) * 0.5;
        }
        startPos = GetPosition();
        foreach (var e in FindEntitiesWithComponent("CircleCollider2D"))
        {
            if (StringStartsWith(EntityGetName(e), "Coin")) { totalCoins++; }
        }
        UpdateText();
    }

    func Gravity()
    {
        var g = GetComponentField("Rigidbody2D", "GravityScale");
        return 9.81 * (g == null ? 1.0 : g);
    }

    func UpdateText()
    {
        var t = FindEntity("Coin Counter");
        if (t != null && t >= 0)
        {
            EntitySetComponentField(t, "TextRenderer", "Text", finished ? "LEVEL COMPLETE!  " + coins + " / " + totalCoins + " COINS" : "COINS " + coins + " / " + totalCoins);
        }
    }

    func Update(dt)
    {
        var p = GetPosition();
        var left = Raycast2D(p.x - halfWidth * 0.8, p.y - halfHeight + 0.02, 0, -1, 0.12, -1, true);
        var right = Raycast2D(p.x + halfWidth * 0.8, p.y - halfHeight + 0.02, 0, -1, 0.12, -1, true);
        var v = GetLinearVelocity2D();
        grounded = (left != null || right != null) && v.y <= 0.1;
        if (grounded) { sinceGrounded = 0.0; } else { sinceGrounded += dt; }

        var dir = 0.0;
        if (!finished)
        {
            if (IsKeyPressed("A") || IsKeyPressed("Left")) { dir -= 1.0; }
            if (IsKeyPressed("D") || IsKeyPressed("Right")) { dir += 1.0; }
        }
        var jumpDown = !finished && (IsKeyDown("Space") || IsKeyDown("W") || IsKeyDown("Up"));
        var jumpHeld = IsKeyPressed("Space") || IsKeyPressed("W") || IsKeyPressed("Up");
        sinceJumpPressed = jumpDown ? 0.0 : sinceJumpPressed + dt;

        // Yatay: hedef hıza ivmeyle (inişte ve dönüşte kayma yok, tuş bırakılınca hemen durur)
        var target = dir * moveSpeed;
        var a = (grounded ? acceleration : airAcceleration) * dt;
        vx = vx + MathClamp(target - vx, -a, a);
        if (grounded && dir == 0.0) { vx = vx * MathExp(-25.0 * dt); }

        var g = Gravity();
        var vy = v.y;
        if (sinceGrounded <= coyoteTime && sinceJumpPressed <= jumpBuffer)
        {
            vy = MathSqrt(2.0 * g * jumpHeight);
            sinceGrounded = 1.0;
            sinceJumpPressed = 1.0;
        }
        else if (vy < 0.0) { vy -= g * (fallMultiplier - 1.0) * dt; }
        else if (vy > 0.0 && !jumpHeld) { vy -= g * (lowJumpMultiplier - 1.0) * dt; }
        vy = MathMax(vy, -maxFallSpeed);
        SetLinearVelocity2D(vx, vy);
        if (dir != 0.0) { SetComponentField("SpriteRenderer", "FlipX", dir < 0.0); }

        // Düşerse başa dön
        if (p.y < -12.0 && startPos != null)
        {
            SetPosition(startPos.x, startPos.y, startPos.z);
            SetLinearVelocity2D(0, 0);
            vx = 0.0;
        }
    }

    func OnTriggerEnter2D(other, name)
    {
        if (StringStartsWith(name, "Coin"))
        {
            coins++;
            EntityDestroy(other);
            UpdateText();
        }
        else if (name == "Goal" && !finished)
        {
            finished = true;
            UpdateText();
        }
    }
}
)CQS";

        const char* kFirstPersonController = R"CQS(using nativebridge;

// Birinci şahıs denetleyici: WASD yürü, Shift koş, Space zıpla.
// Fareyle bakış: sağ tuş basılıyken (lockCursor açıksa her zaman). Kamera "Camera" adlı çocuktur.
script FirstPersonController : ConquerorScript
{
    public float walkSpeed = 4.5;
    public float sprintMultiplier = 1.7;
    public float jumpHeight = 1.1;
    public float gravity = 20.0;
    public float mouseSensitivity = 0.15;
    public bool lockCursor = false;
    public bool grounded = false;
    var yaw = 0.0;
    var pitch = 0.0;
    var vy = 0.0;
    var cam = -1;

    func Start()
    {
        cam = EntityFindChild(GetEntityId(), "Camera");
        var r = GetRotation();
        yaw = r.y;
        if (lockCursor) { SetCursorMode(2); }
    }

    func Update(dt)
    {
        if (lockCursor || IsMouseButtonPressed(1))
        {
            var md = GetMouseDelta();
            yaw -= md.x * mouseSensitivity;
            pitch = MathClamp(pitch - md.y * mouseSensitivity, -85.0, 85.0);
        }
        SetRotation(0, yaw, 0);
        if (cam >= 0) { EntitySetRotation(cam, pitch, 0, 0); }

        var f = GetForward();
        var r = GetRight();
        var ix = 0.0;
        var iz = 0.0;
        if (IsKeyPressed("W")) { iz += 1.0; }
        if (IsKeyPressed("S")) { iz -= 1.0; }
        if (IsKeyPressed("D")) { ix += 1.0; }
        if (IsKeyPressed("A")) { ix -= 1.0; }
        var len = MathSqrt(ix * ix + iz * iz);
        if (len > 1.0) { ix /= len; iz /= len; }
        var speed = walkSpeed;
        if (IsKeyPressed("LeftShift")) { speed *= sprintMultiplier; }
        var fx = f.x;
        var fz = f.z;
        var fl = MathSqrt(fx * fx + fz * fz);
        if (fl > 0.0001) { fx /= fl; fz /= fl; }
        var mx = (fx * iz + r.x * ix) * speed;
        var mz = (fz * iz + r.z * ix) * speed;

        grounded = CharacterIsGrounded();
        if (grounded)
        {
            vy = -2.0;
            if (IsKeyDown("Space")) { vy = MathSqrt(2.0 * gravity * jumpHeight); }
        }
        else
        {
            vy -= gravity * dt;
        }
        CharacterMove(mx * dt, vy * dt, mz * dt);
    }
}
)CQS";

        // ------------------------------------------------------------------ Sahne yardımcıları

        using namespace Kit;

        void SetupEditorView(const std::string& templateId, EditorCamera& view)
        {
            auto view2D = [&](const glm::vec2& center, float orthoSize) {
                view.SetCameraMode(EditorCameraMode::Orthographic2D);
                view.SetOrthographicSize(orthoSize);
                view.SetDistance(10.0f);
                view.SetFocalPoint({ center.x, center.y, 0.0f });
            };
            auto view3D = [&](const glm::vec3& focus, float distance, float pitchDeg, float yawDeg) {
                view.SetCameraMode(EditorCameraMode::Perspective3D);
                view.SetPitch(glm::radians(pitchDeg));
                view.SetYaw(glm::radians(yawDeg));
                view.SetDistance(distance);
                view.SetFocalPoint(focus);
            };
            if (templateId == "basic2d") view2D({ 0.0f, 0.0f }, 6.0f);
            else if (templateId == "platformer2d") view2D({ 6.0f, 1.5f }, 9.0f);
            else if (templateId == "fps") view3D({ 2.0f, 0.0f, -4.0f }, 22.0f, 30.0f, 30.0f);
            else if (templateId == "boardgame") view3D({ 0.0f, 0.0f, 0.0f }, 17.0f, 45.0f, 20.0f);
            else if (templateId == "empty") view3D({ 0.0f, 0.0f, 0.0f }, 10.0f, 30.0f, 45.0f);
            else view3D({ 0.0f, 0.5f, 0.0f }, 11.0f, 25.0f, 30.0f);
        }

        // ------------------------------------------------------------------ Şablon sahneleri

        void BuildEmpty(Scene& scene)
        {
            MakeCamera(scene, { 0.0f, 2.0f, 8.0f }, { 0.0f, 0.5f, 0.0f });
            MakeSun(scene);
        }

        // Unity 3D (Core) şablonu gibi: yalnızca Main Camera (0, 1, 10; -Z'ye bakar) ve Directional Light (50°, -30°),
        // varsayılan gökyüzü. Sahne boş; içerik kullanıcıya kalır.
        void BuildBasic3D(Scene& scene)
        {
            MakeCamera(scene, { 0.0f, 1.0f, 10.0f }, { 0.0f, 1.0f, 0.0f });
            const float pitch = glm::radians(50.0f), yaw = glm::radians(-30.0f);
            MakeSun(scene, glm::normalize(glm::vec3(std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch))), 1.0f,
                    glm::vec3(1.0f, 0.957f, 0.839f));
        }

        // Unity 2D (Core) şablonu gibi: yalnızca ortografik Main Camera (boyut 5 = 10 birim yükseklik), Unity'nin koyu mavi arka planı
        void BuildBasic2D(Scene& scene)
        {
            MakeCamera(scene, { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, 0.0f }, true, 10.0f);
            Setup2DRendering(scene, { 0.192f, 0.302f, 0.475f });
        }

        void BuildPlatformer(Scene& scene)
        {
            // Küçük ama tam bir bölüm (~100 karo): düzlükler, çukurlar, basamaklar, yüzen platformlar, kuleler,
            // 16 para ve sonda bitiş bayrağı. Karo = 1 m; zemin üstü y = -1.
            Entity grid = scene.CreateEntity("Level Grid");
            grid.AddComponent<GridComponent>();
            Entity tiles = scene.CreateEntity("Level Tiles");
            scene.SetEntityParent(tiles, grid, false);
            auto& map = tiles.AddComponent<TilemapComponent>();
            // Palet: 0 çim, 1 toprak, 2 taş, 3 tuğla (hepsi katı)
            auto tile = [](const char* name, const glm::vec4& color) {
                TileDefinition t;
                t.Name = name;
                t.Color = color;
                t.Solid = true;
                return t;
            };
            map.Palette = { tile("Grass", { 0.36f, 0.66f, 0.3f, 1.0f }), tile("Dirt", { 0.52f, 0.36f, 0.24f, 1.0f }),
                            tile("Stone", { 0.5f, 0.5f, 0.54f, 1.0f }), tile("Brick", { 0.74f, 0.42f, 0.28f, 1.0f }) };
            auto ground = [&](int x0, int x1, int top) {
                GridSystem::BoxFill(map, { x0, -6 }, { x1, top - 1 }, 1);
                GridSystem::BoxFill(map, { x0, top }, { x1, top }, 0);
            };
            auto block = [&](int x0, int y0, int x1, int y1, int tile) { GridSystem::BoxFill(map, { x0, y0 }, { x1, y1 }, tile); };
            ground(-16, 14, -2);                   // başlangıç düzlüğü
            block(-16, -1, -16, 10, 2);            // sol duvar
            block(5, -1, 6, 0, 3);                 // küçük engel
            ground(18, 30, -2);                    // ilk çukurun ötesi
            ground(31, 32, -1);                    // basamaklar
            ground(33, 34, 0);
            ground(35, 37, 1);
            block(40, 2, 42, 2, 2);                // ikinci çukur üstü yüzen platformlar
            block(45, 3, 47, 3, 2);
            block(50, 2, 52, 2, 2);
            ground(55, 72, -2);
            block(58, 1, 61, 1, 3);                // üstte tuğla sıra (paralar)
            block(64, -1, 65, 2, 2);               // kule
            block(68, 3, 71, 3, 2);
            ground(76, 100, -2);                   // son düzlük
            block(80, -1, 80, -1, 2);              // bitiş merdiveni
            block(81, -1, 81, 0, 2);
            block(82, -1, 82, 1, 2);
            block(83, -1, 83, 2, 2);
            block(100, -1, 100, 10, 2);            // sağ duvar
            tiles.AddComponent<TilemapCollider2DComponent>();

            // Oyuncu: ~1 x 1 m (256 px doku); yerçekimi 3x (kısa, keskin zıplama)
            Entity player = MakeSprite(scene, "Player", "Resources/test/Square.png", { -12.0f, 0.0f, 0.0f }, { 0.4f, 0.4f, 1.0f }, { 0.95f, 0.35f, 0.3f, 1.0f });
            auto& rb = player.AddComponent<RigidBody2DComponent>();
            rb.Type = RigidBody2DComponent::BodyType::Dynamic;
            rb.FixedRotation = true;
            rb.GravityScale = 3.0f;
            player.AddComponent<BoxCollider2DComponent>();
            AddScript(player, "Assets/Scripts/PlayerController2D.cqs", "PlayerController2D");

            // Paralar (tetikleyici)
            const glm::vec2 coins[] = { { -6.5f, 0.0f }, { -3.5f, 0.0f }, { 5.5f, 2.0f }, { 16.0f, 2.0f }, { 24.0f, 0.0f },
                                        { 33.5f, 2.0f }, { 36.0f, 3.0f }, { 41.0f, 4.0f }, { 46.0f, 5.0f }, { 51.0f, 4.0f },
                                        { 59.5f, 3.0f }, { 60.5f, 3.0f }, { 64.5f, 4.0f }, { 69.5f, 5.0f }, { 74.0f, 2.0f }, { 83.0f, 4.0f } };
            int i = 0;
            for (const auto& c : coins)
            {
                std::string name = "Coin " + std::to_string(++i);
                Entity coin = MakeSprite(scene, name.c_str(), "Resources/test/Circle.png", { c.x, c.y, 0.0f }, { 0.25f, 0.25f, 1.0f }, { 1.0f, 0.82f, 0.2f, 1.0f });
                auto& cc = coin.AddComponent<CircleCollider2DComponent>();
                cc.IsTrigger = true;
                auto& spin = coin.AddComponent<RotatorComponent>();
                spin.RotationRate = { 0.0f, 180.0f, 0.0f };
            }

            // Bitiş bayrağı: direk + bayrak, tetikleyici "Goal"
            MakeSprite(scene, "Flag Pole", "Resources/test/Square.png", { 90.0f, 1.0f, 0.0f }, { 0.06f, 1.6f, 1.0f }, { 0.9f, 0.9f, 0.92f, 1.0f });
            MakeSprite(scene, "Flag", "Resources/test/Square.png", { 90.6f, 2.6f, 0.0f }, { 0.45f, 0.3f, 1.0f }, { 0.25f, 0.8f, 0.35f, 1.0f });
            Entity goal = scene.CreateEntity("Goal");
            goal.GetComponent<TransformComponent>().Position = { 90.0f, 1.0f, 0.0f };
            auto& gb = goal.AddComponent<BoxCollider2DComponent>();
            gb.Size = { 1.2f, 4.0f };
            gb.IsTrigger = true;
            goal.AddComponent<RigidBody2DComponent>().Type = RigidBody2DComponent::BodyType::Static;

            // Kamera: oyuncuyu yandan izler; ekranın üstünde para sayacı (kameranın çocuğu)
            Entity cam = MakeCamera(scene, { -12.0f, 1.5f, 10.0f }, { -12.0f, 1.5f, 0.0f }, true, 12.0f);
            Setup2DRendering(scene, { 0.44f, 0.68f, 0.92f });
            auto& follow = cam.AddComponent<CameraFollowComponent>();
            follow.Target = (uint64_t)player.GetComponent<IDComponent>().ID;
            follow.Mode = CameraFollowMode::SideScroller;
            follow.Offset = { 0.0f, 1.5f, 10.0f };
            follow.LookAtTarget = false;
            follow.PositionDamping = 0.12f;
            Entity counter = scene.CreateEntity("Coin Counter");
            scene.SetEntityParent(counter, cam, false);
            counter.GetComponent<TransformComponent>().Position = { 0.0f, 5.0f, -9.0f };
            auto& text = counter.AddComponent<TextRendererComponent>();
            text.Text = "COINS 0 / 16";
            text.FontSize = 0.7f;
        }

        void BuildFirstPerson(Scene& scene)
        {
            MakeSun(scene);
            Entity ground = MakeMesh(scene, "Ground", MeshType::Cube, { 0.0f, -0.25f, 0.0f }, { 40.0f, 0.5f, 40.0f }, { 0.4f, 0.44f, 0.47f, 1.0f });
            MakeStatic(ground);
            // Engeller, rampa ve itilebilir kutular
            Entity wall = MakeMesh(scene, "Wall", MeshType::Cube, { 0.0f, 1.5f, -10.25f }, { 18.0f, 3.0f, 0.5f }, { 0.75f, 0.72f, 0.68f, 1.0f });
            MakeStatic(wall);
            Entity ramp = MakeMesh(scene, "Ramp", MeshType::Cube, { 6.0f, 0.6f, -4.0f }, { 3.0f, 0.3f, 6.0f }, { 0.9f, 0.6f, 0.25f, 1.0f });
            ramp.GetComponent<TransformComponent>().Rotation = { glm::radians(15.0f), 0.0f, 0.0f };   // -Z ucu (platforma bakan) yukarıda
            MakeStatic(ramp);
            Entity platform = MakeMesh(scene, "Platform", MeshType::Cube, { 6.0f, 1.4f, -8.5f }, { 3.0f, 0.3f, 3.0f }, { 0.9f, 0.6f, 0.25f, 1.0f });
            MakeStatic(platform);
            for (int i = 0; i < 4; ++i)
            {
                std::string name = "Crate " + std::to_string(i + 1);
                Entity crate = MakeMesh(scene, name.c_str(), MeshType::Cube, { -4.0f + (i % 2) * 1.05f, 0.5f + (i / 2) * 1.0f, -5.0f }, glm::vec3(1.0f),
                                        { 0.62f, 0.45f, 0.3f, 1.0f });
                crate.AddComponent<RigidbodyComponent>().Type = RigidbodyComponent::BodyType::Dynamic;
                crate.AddComponent<BoxColliderComponent>();
            }
            // Oyuncu: Character Controller + kamera çocuğu
            Entity player = scene.CreateEntity("Player");
            player.GetComponent<TransformComponent>().Position = { 0.0f, 1.1f, 4.0f };
            auto& cc = player.AddComponent<CharacterControllerComponent>();
            cc.Height = 1.8f;
            cc.Radius = 0.35f;
            AddScript(player, "Assets/Scripts/FirstPersonController.cqs", "FirstPersonController");
            Entity cam = scene.CreateEntity("Camera");
            cam.GetComponent<TransformComponent>().Position = { 0.0f, 0.7f, 0.0f };
            cam.AddComponent<CameraComponent>().Primary = true;
            scene.SetEntityParent(cam, player, false);
        }

        bool BuildBoardGame(Scene& scene, const fs::path& projectDir, std::string& error)
        {
            MakeCamera(scene, { 0.0f, 10.0f, 10.0f }, { 0.0f, 0.0f, 0.0f });
            auto spawn = [&](const char* rel, const glm::vec3& pos) -> Entity {
                fs::path abs = projectDir / rel;
                Entity e = PrefabSerializer::Instantiate(scene, abs.string());
                if (e)
                {
                    e.GetComponent<TransformComponent>().Position = pos;
                    // Taşınabilir (proje göreli) prefab yolu
                    if (e.HasComponent<PrefabInstanceComponent>())
                        e.GetComponent<PrefabInstanceComponent>().Prefab = rel;
                }
                return e;
            };
            if (!spawn("Packages/hex-board-kit/Prefabs/HexBoard.cqprefab", glm::vec3(0.0f)))
            {
                error = "Hex Board Kit addon is missing from the engine catalog";
                return false;
            }
            spawn("Packages/hex-board-kit/Prefabs/HexUnit.cqprefab", glm::vec3(0.0f));
            if (!spawn("Packages/day-night-cycle/Prefabs/Sun.cqprefab", { 0.0f, 8.0f, 0.0f }))
                MakeSun(scene);
            scene.UpdateWorldTransforms();
            return true;
        }

    }

    const std::vector<HubTemplate>& GetTemplates()
    {
        static const std::vector<HubTemplate> s_Templates = {
            { "basic3d", "3D Core", "Core", "An empty 3D scene with a main camera and a directional light, like Unity's 3D (Core) template.",
              ICON_FA_CUBE, { 0.18f, 0.45f, 0.95f, 1.0f }, { 0.08f, 0.16f, 0.38f, 1.0f }, {},
              { "Main Camera + Directional Light", "Physical sky, PBR lighting", "Jolt physics (MuJoCo selectable)" } },
            { "basic2d", "2D Core", "Core", "An empty 2D scene with an orthographic main camera, like Unity's 2D (Core) template.",
              ICON_FA_SHAPES, { 0.95f, 0.5f, 0.2f, 1.0f }, { 0.45f, 0.18f, 0.08f, 1.0f }, {},
              { "Orthographic Main Camera", "2D renderer (sprites, tilemaps, 2D lights)", "Box2D physics" } },
            { "platformer2d", "2D Platformer", "Sample", "A small side-scrolling level: tilemap with pits, steps and floating platforms, 16 coins and a goal flag.",
              ICON_FA_PERSON_RUNNING, { 0.3f, 0.78f, 0.45f, 1.0f }, { 0.08f, 0.3f, 0.16f, 1.0f }, { "quick-tools" },
              { "Responsive controller (coyote time, jump buffer, variable jump)", "Tilemap level + collider", "Coins, coin counter, goal flag", "Side-scroller camera" } },
            { "fps", "First Person", "Sample", "First-person arms holding a pistol and a marksman rifle on a shooting range: aim down sights, recoil, reload, pop-up targets.",
              ICON_FA_CROSSHAIRS, { 0.75f, 0.3f, 0.95f, 1.0f }, { 0.28f, 0.1f, 0.4f, 1.0f }, {},
              { "First-person arms (IK hands on the weapon)", "Pistol + marksman rifle: ADS, recoil, reload", "Scoring pop-up targets", "Ramps, stairs, push-able cubes" } },
            { "arenashooter", "Arena Shooter", "Sample", "Fast first-person free-for-all deathmatch: you and the AI bots all fight each other on a NavMesh arena; first to 15 kills wins.",
              ICON_FA_SKULL, { 0.95f, 0.3f, 0.25f, 1.0f }, { 0.4f, 0.08f, 0.07f, 1.0f }, {},
              { "Free-for-all AI bots (hunt, search, combat)", "Health, damage, spawn protection, respawn", "Kill feed, leader, score limit, timer", "Pistol + rifle with impacts" } },
            { "tpsshooter", "Third Person Shooter", "Sample", "Over-the-shoulder free-for-all shooter: the mannequin aims and fires a pistol while the AI bots fight everyone, including each other.",
              ICON_FA_PERSON_RIFLE, { 0.95f, 0.6f, 0.2f, 1.0f }, { 0.4f, 0.22f, 0.05f, 1.0f }, {},
              { "Shoulder camera with aim zoom", "Animated aiming and firing", "Free-for-all AI bots", "Crosshair, hitmarker, health HUD" } },
            { "lasertag", "Laser Tag", "Sample", "Blue versus red team laser tag: glowing taggers, team bases and AI team mates and opponents.",
              ICON_FA_BOLT, { 0.25f, 0.75f, 1.0f, 1.0f }, { 0.06f, 0.2f, 0.4f, 1.0f }, {},
              { "Two teams with AI players", "Laser taggers, tag-and-respawn", "Team score to win", "Neon arena" } },
            { "boardgame", "3D Board Game", "Sample", "A hex island strategy board on the sea: a new island every play with forests, villages and snowy peaks, a chess knight that walks A* paths with path preview, an RTS camera and a day / night sun.",
              ICON_FA_CHESS_BOARD, { 0.95f, 0.75f, 0.2f, 1.0f }, { 0.4f, 0.28f, 0.05f, 1.0f }, { "hex-board-kit", "day-night-cycle", "scene-stats" },
              { "Procedural hex islands (Hex Board Kit)", "A* moves with path preview", "RTS orbit camera", "Day / night cycle" } },
            { "empty", "Empty", "Core", "Just a camera and a light. Build everything yourself.",
              ICON_FA_FILE, { 0.45f, 0.5f, 0.58f, 1.0f }, { 0.15f, 0.17f, 0.2f, 1.0f }, {}, { "Camera + light" } },
            { "thirdperson", "Third Person", "Sample", "An animated character that walks, sprints and jumps around a test arena, with an orbiting follow camera.",
              ICON_FA_PERSON_WALKING, { 0.25f, 0.6f, 0.95f, 1.0f }, { 0.07f, 0.2f, 0.4f, 1.0f }, {},
              { "Animated human (idle / walk / sprint / jump / fall)", "Character Controller + camera-relative movement", "Orbit camera (right mouse)",
                "Ramps, stairs, jump blocks, push-able crates" } },
            { "topdown", "Top Down", "Sample", "Click or hold on the ground to walk there along a NavMesh path, or steer with WASD: an animated character, a cursor ring on the ground and a zoomable top-down follow camera.",
              ICON_FA_LOCATION_CROSSHAIRS, { 0.3f, 0.85f, 0.6f, 1.0f }, { 0.07f, 0.3f, 0.2f, 1.0f }, {},
              { "Click / hold to move (NavMesh path)", "WASD walk, Shift run", "Animated mannequin, cursor decal", "Follow camera with wheel zoom" } },
            { "vehicle", "Vehicle", "Sample", "A concept sports car on a kerbed race track (3-lap time trial with checkpoints) and in an off-road valley.",
              ICON_FA_CAR, { 0.95f, 0.35f, 0.3f, 1.0f }, { 0.4f, 0.1f, 0.08f, 1.0f }, {},
              { "Time Trial: laps, checkpoints, best lap", "Editable spline track (drag the Racing Line knots)", "Off-road terrain valley with jumps",
                "Suspension, gearbox, engine sound, autopilot" } },
            { "virtualproduction", "Virtual Production", "Sample", "An LED volume stage: two actors and a concept car in front of a 270 degree LED wall (forest or sunset). A seven shot short film with dialogue, subtitles, focus pulls and title cards plays on the cine camera.",
              ICON_FA_VIDEO, { 0.85f, 0.35f, 0.75f, 1.0f }, { 0.3f, 0.08f, 0.28f, 1.0f }, {},
              { "270 degree LED wall + ceiling (1 / 2 switch content)", "7-shot film: dialogue, subtitles, title cards", "Talking actors, eye lines, focus pulls", "Space next shot, Tab director view" },
              "Film / Video & Live Events" },
            { "archviz", "Archviz Interior", "Sample", "A furnished apartment with a garden view: sunlight through tall windows, linen curtains, global illumination and reflections, day and evening lighting and a first-person walkthrough with viewpoints.",
              ICON_FA_HOUSE, { 0.9f, 0.75f, 0.45f, 1.0f }, { 0.35f, 0.26f, 0.12f, 1.0f }, {},
              { "Living room, dining area and kitchen", "Screen-space GI and reflections", "Day / evening lighting (G)", "Walk, fly and 4 viewpoints" }, "Architecture" },
            { "configurator", "Car Configurator", "Sample", "A photo studio presenting a concept car on a turntable: paint, interior and wheel options, camera presets and the driver's seat.",
              ICON_FA_CAR_SIDE, { 0.55f, 0.6f, 0.7f, 1.0f }, { 0.15f, 0.17f, 0.22f, 1.0f }, {},
              { "Paint, interior and wheel variants", "Camera presets + cockpit view", "HDR studio lighting and reflections", "Turntable, headlights, option panel" }, "Automotive" },
            { "crowdsim", "Crowd Simulation", "Sample", "A living city plaza: pedestrians on the NavMesh wander, window-shop, sit on benches, meet and chat, and come and go through the side streets. Follow anyone with the camera and change the crowd size live.",
              ICON_FA_PEOPLE_GROUP, { 0.45f, 0.75f, 0.95f, 1.0f }, { 0.1f, 0.25f, 0.38f, 1.0f }, {},
              { "36+ animated pedestrians with tasks", "Benches, shop windows, conversations", "City plaza with shops and fountain", "Orbit / follow camera, live crowd size" }, "Simulation" },
            { "vr", "Virtual Reality", "Sample", "A VR room with teleport arc, snap turn, grab and throw, a pistol to pick up and pop-up targets. Without a headset it runs in the XR Device Simulator (mouse = head, right hand points where you look).",
              ICON_FA_VR_CARDBOARD, { 0.35f, 0.85f, 0.95f, 1.0f }, { 0.08f, 0.28f, 0.34f, 1.0f }, {},
              { "XR Origin: head + two motion controllers", "Teleport arc with fade, 45 degree snap turn", "Grab, throw, pick up and fire a pistol", "XR Device Simulator, two-eye preview" } },
            { "ar", "Augmented Reality", "Sample", "Handheld AR furniture placement: the AR session finds surfaces as you move the device, tap a plane to place a chair, plant or shelf, then move, resize and rotate it. Without an AR camera a furnished room stands in for the real world (AR Simulation).",
              ICON_FA_MOBILE_SCREEN, { 0.95f, 0.55f, 0.85f, 1.0f }, { 0.35f, 0.1f, 0.3f, 1.0f }, {},
              { "Plane detection with feature points", "Tap to place, drag, pinch-resize, twist-rotate", "Furniture catalog (Poly Haven)", "AR Simulation room, simulated device" } },
            { "shortfilm", "Short Film", "Sample", "\"The Lantern\": a one-minute cinematic told entirely with the Timeline. At dusk Leo waits on a bench with a small box; Mira opens it, a paper lantern rises and the plaza lights up. Press Play to watch it, or Record with Fixed Time Step for a smooth MP4.",
              ICON_FA_FILM, { 1.0f, 0.68f, 0.35f, 1.0f }, { 0.36f, 0.16f, 0.06f, 1.0f }, {},
              { "Timeline: camera cuts, lens and focus pulls", "Two actors with blended animation clips", "Lights, titles and music on tracks", "Offline recording (Fixed Time Step) to MP4" }, "Film / Video & Live Events" },
            { "featuretest", "Feature Test", "Test", "Eighteen labelled stations that run the engine's systems side by side: materials, lights, rigid bodies, cloth, destruction, particles, animation, ragdolls, navigation, splines, joints, IK, render targets, instancing and LOD, surfaces, CSG, triggers and forces. Press Play and fly between them.",
              ICON_FA_FLASK, { 0.6f, 0.45f, 0.95f, 1.0f }, { 0.2f, 0.12f, 0.38f, 1.0f }, {},
              { "18 stations with floor labels", "Joints, IK, CSG, render targets, LOD", "Triggers, raycasts, explosion forces", "Fly camera: Tab / PgUp / PgDn / 0-9" } },
        };
        return s_Templates;
    }

    const std::vector<std::string>& GetIndustries()
    {
        static const std::vector<std::string> s_Industries = { "Games", "Film / Video & Live Events", "Architecture", "Automotive", "Simulation" };
        return s_Industries;
    }

    const HubTemplate* FindTemplate(const std::string& id)
    {
        for (auto& t : GetTemplates())
            if (t.Id == id)
                return &t;
        return nullptr;
    }

    bool IsValidProjectName(const std::string& name, std::string* why)
    {
        if (name.empty())
        {
            if (why) *why = "Enter a project name";
            return false;
        }
        for (unsigned char c : name)
            if (!(std::isalnum(c) || c == ' ' || c == '_' || c == '-' || c >= 0x80))
            {
                if (why) *why = "Use letters, numbers, spaces, '-' or '_'";
                return false;
            }
        if (name.front() == ' ' || name.back() == ' ')
        {
            if (why) *why = "The name cannot start or end with a space";
            return false;
        }
        return true;
    }

    // Klasör şablonları (Unreal'deki gibi hazır içerik): Resources/Templates/Projects/<id>/ projeye kopyalanır,
    // Template.yaml'daki "Shared" paketleri Resources/Templates/Shared/<paket> -> Assets/<paket>. Sahneler
    // Tools/Templates/build_templates.py ile önceden üretilir.
    static fs::path FolderTemplateDir(const std::string& id)
    {
        return fs::path("Resources") / "Templates" / "Projects" / id;
    }

    static bool CreateFolderProject(const NewProjectOptions& o, const fs::path& dir, std::string& error)
    {
        const fs::path src = FolderTemplateDir(o.TemplateId);
        std::error_code ec;
        YAML::Node meta;
        try { meta = YAML::LoadFile((src / "Template.yaml").string()); }
        catch (const std::exception& e) { error = std::string("Template.yaml: ") + e.what(); return false; }
        fs::create_directories(dir, ec);
        for (const auto& entry : fs::directory_iterator(src, ec))
        {
            const auto name = entry.path().filename().string();
            if (name == "Template.yaml" || entry.path().extension() == ".cqproj" || name == "Library" || name == "Builds")
                continue;
            fs::copy(entry.path(), dir / name, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) { error = "Cannot copy " + name + ": " + ec.message(); return false; }
        }
        if (auto shared = meta["Shared"])
            for (const auto& pkg : shared)
            {
                const std::string p = pkg.as<std::string>();
                fs::copy(fs::path("Resources") / "Templates" / "Shared" / p, dir / "Assets" / p,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                if (ec) { error = "Cannot copy shared package " + p + ": " + ec.message(); return false; }
            }
        for (const char* sub : { "Packages", "Library" })
            fs::create_directories(dir / sub, ec);
        // Şablonun kullandığı eklentiler (Template.yaml "Addons"): sahne bunların prefab / betiklerine başvurur
        if (auto addons = meta["Addons"])
            for (const auto& a : addons)
                if (!InstallAddon(a.as<std::string>(), dir, error))
                    return false;
        auto project = std::make_shared<Project>();
        auto& cfg = project->GetConfig();
        cfg.Name = o.Name;
        cfg.StartScene = meta["StartScene"] ? meta["StartScene"].as<std::string>() : "Assets/Scenes/Main.cqscene";
        cfg.AssetDirectory = "Assets";
        cfg.TargetPlatform.clear();
        if (auto scenes = meta["Scenes"])
            for (const auto& sc : scenes)
                cfg.Build.Scenes.push_back({ sc.as<std::string>(), true });
        PhysicsSettings saved = PhysicsSettings::Get();
        PhysicsSettings::Get() = PhysicsSettings{};
        PhysicsSettings::Get().Backend = o.PhysicsBackend == 1 ? PhysicsBackend3D::MuJoCo : PhysicsBackend3D::Jolt;
        ProjectSerializer(project).Serialize(dir / (o.Name + ".cqproj"));
        PhysicsSettings::Get() = saved;
        return true;
    }

    bool CreateProject(const NewProjectOptions& o, fs::path& outProjectDir, std::string& error)
    {
        if (!IsValidProjectName(o.Name, &error))
            return false;
        const HubTemplate* tmpl = FindTemplate(o.TemplateId);
        if (!tmpl)
        {
            error = "Unknown template " + o.TemplateId;
            return false;
        }
        std::error_code ec;
        const fs::path dir = o.Location / o.Name;
        if (fs::exists(dir, ec) && !fs::is_empty(dir, ec))
        {
            error = "A non-empty folder already exists at " + dir.string();
            return false;
        }
        if (fs::exists(FolderTemplateDir(o.TemplateId) / "Template.yaml", ec))
        {
            if (!CreateFolderProject(o, dir, error))
                return false;
            outProjectDir = dir;
            CQ_CORE_INFO("[Hub] Created project '{0}' ({1}) at {2}", o.Name, tmpl->Name, dir.string());
            return true;
        }
        for (const char* sub : { "Assets/Scenes", "Assets/Scripts", "Assets/Textures", "Assets/Materials", "Assets/Models", "Assets/Audio",
                                 "Assets/Prefabs", "Packages", "Library" })
            fs::create_directories(dir / sub, ec);
        if (ec)
        {
            error = "Cannot create the project folder: " + ec.message();
            return false;
        }

        // Addon'lar (şablon sahnesi prefab'larını kullanabilir: sahneden önce)
        if (o.InstallAddons || o.TemplateId == "boardgame")
            for (const auto& addon : tmpl->Addons)
                if (!InstallAddon(addon, dir, error))
                    return false;

        // Betikler
        if (o.TemplateId == "platformer2d")
            WriteText(dir / "Assets/Scripts/PlayerController2D.cqs", kPlayerController2D);
        if (o.TemplateId == "fps" || o.TemplateId == "archviz")
            WriteText(dir / "Assets/Scripts/FirstPersonController.cqs", kFirstPersonController);

        // Sahne
        auto scene = std::make_shared<Scene>();
        if (o.TemplateId == "empty") BuildEmpty(*scene);
        else if (o.TemplateId == "basic2d") BuildBasic2D(*scene);
        else if (o.TemplateId == "platformer2d") BuildPlatformer(*scene);
        else if (o.TemplateId == "fps") BuildFirstPerson(*scene);
        else if (o.TemplateId == "boardgame")
        {
            if (!BuildBoardGame(*scene, dir, error))
                return false;
        }
        else if (IsProTemplate(o.TemplateId))
        {
            if (!BuildProTemplate(o.TemplateId, *scene, dir, error))
                return false;
        }
        else BuildBasic3D(*scene);
        scene->UpdateWorldTransforms();
        // Renk / doku verilmiş nesnelerin malzemeleri Assets/Materials/*.cqmat dosyaları olur (Mesh Renderer yalnız malzemeye başvurur)
        MeshRendererMaterial::BakeInstances(*scene, dir / "Assets/Materials", nullptr, dir);

        // Sahne görünümünün ilk bakış açısı (sahneyle kaydedilir): 2D şablonlar 2D modda açılır
        EditorCamera view(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
        SetupEditorView(o.TemplateId, view);
        SceneSerializer serializer(scene);
        serializer.SetEditorCamera(&view);
        serializer.Serialize(dir / "Assets/Scenes/Main.cqscene");

        // Proje dosyası (seçilen fizik motoru dahil)
        auto project = std::make_shared<Project>();
        auto& cfg = project->GetConfig();
        cfg.Name = o.Name;
        cfg.Organization = "";
        cfg.StartScene = "Assets/Scenes/Main.cqscene";
        cfg.AssetDirectory = "Assets";
        cfg.TargetPlatform.clear();   // boş = projeyi açan editörün platformu (Windows/Linux/macOS arasında taşınabilir)
        PhysicsSettings saved = PhysicsSettings::Get();
        PhysicsSettings::Get() = PhysicsSettings{};
        PhysicsSettings::Get().Backend = o.PhysicsBackend == 1 ? PhysicsBackend3D::MuJoCo : PhysicsBackend3D::Jolt;
        ProjectSerializer(project).Serialize(dir / (o.Name + ".cqproj"));
        PhysicsSettings::Get() = saved;

        outProjectDir = dir;
        CQ_CORE_INFO("[Hub] Created project '{0}' ({1}) at {2}", o.Name, tmpl->Name, dir.string());
        return true;
    }
}
