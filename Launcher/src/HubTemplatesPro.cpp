// Unreal tarzı sektör şablonları: Third Person, Top Down, Vehicle (Games), Virtual Production (Film / Video & Live Events),
// Archviz Interior (Architecture), Car Configurator (Automotive), Crowd Simulation (Simulation).
// Varlıklar: Resources/Templates/Kenney (CC0) — kullanılanlar projenin Assets/ klasörüne kopyalanır.

#include "HubTemplates.h"
#include "HubTemplateKit.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "AISystem/Components/NavComponents.h"
#include "Scene/GameplayComponents.h"
#include "Scene/PhysicsComponents.h"
#include "Scene/CameraRig.h"
#include "UI/UIComponents.h"
#include "Scene/ComponentRegistry.h"

#include <map>

namespace Conqueror::Launcher
{
    namespace
    {
        using namespace Kit;

        // ------------------------------------------------------------------ Betikler

        const char* kThirdPersonCharacter = R"CQS(using nativebridge;

// Üçüncü şahıs karakter: WASD kameraya göre yürü, Shift koş, Space zıpla.
// Kamera "Main Camera" (Camera Follow, Orbit: sağ tuşla döndür). Görünüm "Mesh" çocuğunda (CharacterAnimDriver).
script ThirdPersonCharacter : ConquerorScript
{
    public float walkSpeed = 2.6;
    public float sprintSpeed = 6.0;
    public float acceleration = 10.0;
    public float jumpHeight = 1.2;
    public float gravity = 22.0;
    public float turnSharpness = 12.0;
    public string cameraName = "Main Camera";
    // CharacterAnimDriver okur
    public float speed01 = 0.0;
    public bool grounded = true;
    public int jumps = 0;
    var vy = 0.0;
    var yaw = 0.0;
    var speed = 0.0;
    var cam = null;

    func Start()
    {
        yaw = GetRotation().y;
        cam = FindEntity(cameraName);
    }

    func Update(dt)
    {
        var fx = 0.0;
        var fz = -1.0;
        if (cam != null)
        {
            var f = EntityGetForward(cam);
            var l = MathSqrt(f.x * f.x + f.z * f.z);
            if (l > 0.001) { fx = f.x / l; fz = f.z / l; }
        }
        var ix = 0.0;
        var iz = 0.0;
        if (IsKeyPressed("W") || IsKeyPressed("Up")) { iz += 1.0; }
        if (IsKeyPressed("S") || IsKeyPressed("Down")) { iz -= 1.0; }
        if (IsKeyPressed("D") || IsKeyPressed("Right")) { ix += 1.0; }
        if (IsKeyPressed("A") || IsKeyPressed("Left")) { ix -= 1.0; }
        // Sağ = ileri x yukarı
        var mx = fx * iz - fz * ix;
        var mz = fz * iz + fx * ix;
        var ml = MathSqrt(mx * mx + mz * mz);
        var target = 0.0;
        if (ml > 0.001)
        {
            mx /= ml;
            mz /= ml;
            target = walkSpeed;
            if (IsKeyPressed("LeftShift")) { target = sprintSpeed; }
            // Model +Z'ye bakar: yön açısı atan2(x, z)
            var want = MathRadToDeg(MathAtan2(mx, mz));
            yaw = yaw + MathNormalizeAngle(want - yaw) * (1.0 - MathExp(-turnSharpness * dt));
            SetRotation(0, yaw, 0);
        }
        speed = MathLerp(speed, target, 1.0 - MathExp(-acceleration * dt));
        speed01 = speed / sprintSpeed;

        grounded = CharacterIsGrounded();
        if (grounded)
        {
            vy = -2.0;
            if (IsKeyDown("Space"))
            {
                vy = MathSqrt(2.0 * gravity * jumpHeight);
                jumps++;
            }
        }
        else
        {
            vy -= gravity * dt;
        }
        var dirX = 0.0;
        var dirZ = 0.0;
        if (ml > 0.001) { dirX = mx; dirZ = mz; }
        CharacterMove(dirX * speed * dt, vy * dt, dirZ * speed * dt);
    }
}
)CQS";

        const char* kCharacterAnimDriver = R"CQS(using nativebridge;

// Karakter görünümü: üst varlığın betiğindeki speed01 / grounded / jumps alanlarını animasyon parametrelerine aktarır.
script CharacterAnimDriver : ConquerorScript
{
    var owner = -1;
    var lastJumps = 0;

    func Start()
    {
        owner = EntityGetParent();
    }

    func Update(dt)
    {
        if (owner < 0) { return; }
        var s = GetScriptField(owner, "speed01");
        if (s != null) { AnimSetFloat("Speed", s); }
        var g = GetScriptField(owner, "grounded");
        if (g != null) { AnimSetBool("Grounded", g); }
        var j = GetScriptField(owner, "jumps");
        if (j != null && j != lastJumps)
        {
            lastJumps = j;
            AnimSetTrigger("Jump");
        }
    }
}
)CQS";

        const char* kTopDownCharacter = R"CQS(using nativebridge;

// Yukarıdan bakış: sol tıkla yere hedef ver, karakter NavMesh üzerinde yürür (NavMesh Agent). Shift basılıyken koşar.
script TopDownCharacter : ConquerorScript
{
    public float walkSpeed = 3.0;
    public float runSpeed = 6.0;
    public float speed01 = 0.0;
    public bool grounded = true;
    public int jumps = 0;
    var last = null;

    func Update(dt)
    {
        if (IsMouseButtonDown(0))
        {
            var m = GetMousePosition();
            var hit = RaycastFromScreen(m.x, m.y, 500.0);
            if (hit != null)
            {
                NavAgentSetDestination(GetEntityId(), hit.point.x, hit.point.y, hit.point.z);
                var marker = FindEntity("Destination Marker");
                if (marker != null) { EntitySetWorldPosition(marker, hit.point.x, hit.point.y + 0.05, hit.point.z); }
            }
        }
        var top = walkSpeed;
        if (IsKeyPressed("LeftShift")) { top = runSpeed; }
        SetComponentField("NavMeshAgent", "Speed", top);
        var p = GetPosition();
        if (last != null && dt > 0.0)
        {
            var dx = p.x - last.x;
            var dz = p.z - last.z;
            var v = MathSqrt(dx * dx + dz * dz) / dt;
            speed01 = MathLerp(speed01, MathClamp(v / runSpeed, 0.0, 1.0), 1.0 - MathExp(-10.0 * dt));
        }
        last = p;
    }
}
)CQS";

        const char* kShotSequencer = R"CQS(using nativebridge;

// Sanal prodüksiyon: kamera çekimlerini sırayla canlı yapar (Camera Brain en yüksek öncelikli Virtual Camera'ya harmanlar).
// Çekimler "Shot 1", "Shot 2" ... adlı Virtual Camera varlıklarıdır. Space bir sonraki çekime geçer.
script ShotSequencer : ConquerorScript
{
    public float shotLength = 5.0;
    public int shotCount = 4;
    public int currentShot = 1;
    var timer = 0.0;

    func Start()
    {
        Activate(1);
    }

    func Update(dt)
    {
        timer += dt;
        if (timer >= shotLength || IsKeyDown("Space"))
        {
            var next = currentShot + 1;
            if (next > shotCount) { next = 1; }
            Activate(next);
        }
    }

    func Activate(n)
    {
        currentShot = n;
        timer = 0.0;
        var i = 1;
        while (i <= shotCount)
        {
            var cam = FindEntity("Shot " + ToString(i));
            if (cam != null)
            {
                var prio = 10;
                if (i == n) { prio = 20; }
                EntitySetComponentField(cam, "VirtualCamera", "Priority", prio);
            }
            i++;
        }
    }
}
)CQS";

        const char* kCarConfigurator = R"CQS(using nativebridge;

// Araç yapılandırıcı: 1-6 ya da Sol / Sağ ok ile model seç; döner tabla kendiliğinden döner, sağ tuşla kamera döndür.
script CarConfigurator : ConquerorScript
{
    public string models = "Sedan Sports,SUV,Race,Police,Taxi,Hatchback Sports";
    public int selected = 0;
    var names = [];

    func Start()
    {
        names = StringSplit(models, ",");
        Show(selected);
    }

    func Update(dt)
    {
        var i = 0;
        while (i < names.Count)
        {
            if (IsKeyDown(ToString(i + 1))) { Show(i); }
            i++;
        }
        if (IsKeyDown("Right")) { Show((selected + 1) % names.Count); }
        if (IsKeyDown("Left")) { Show((selected + names.Count - 1) % names.Count); }
    }

    func Show(index)
    {
        selected = index;
        var i = 0;
        while (i < names.Count)
        {
            var e = FindEntity(names[i]);
            if (e != null) { EntitySetActive(e, i == index); }
            i++;
        }
        var label = FindEntity("Model Name");
        if (label != null) { UISetText(label, names[index] + "   (1-6 / arrow keys)"); }
    }
}
)CQS";

        const char* kWanderer = R"CQS(using nativebridge;

// Kalabalık benzetimi: yaya rastgele bir kaldırım noktası seçer, NavMesh Agent ile yürür, varınca kısa bekleyip yenisini seçer.
script Wanderer : ConquerorScript
{
    public float areaHalfSize = 22.0;
    public float minWait = 0.5;
    public float maxWait = 3.0;
    public float speed01 = 0.0;
    public bool grounded = true;
    public int jumps = 0;
    var wait = 0.0;
    var last = null;

    func Start()
    {
        SetComponentField("NavMeshAgent", "Speed", MathRandomRange(1.1, 1.8));
        wait = MathRandomRange(0.0, 1.0);
    }

    func Update(dt)
    {
        if (wait > 0.0)
        {
            wait -= dt;
            if (wait <= 0.0) { PickTarget(); }
        }
        else if (NavRemainingDistance() < 0.6)
        {
            wait = MathRandomRange(minWait, maxWait);
        }
        var p = GetPosition();
        if (last != null && dt > 0.0)
        {
            var dx = p.x - last.x;
            var dz = p.z - last.z;
            var v = MathSqrt(dx * dx + dz * dz) / dt;
            speed01 = MathLerp(speed01, MathClamp(v / 6.0, 0.0, 1.0), 1.0 - MathExp(-8.0 * dt));
        }
        last = p;
    }

    func PickTarget()
    {
        var tries = 0;
        while (tries < 8)
        {
            var q = NavSamplePosition(MathRandomRange(-areaHalfSize, areaHalfSize), 0.0, MathRandomRange(-areaHalfSize, areaHalfSize), 2.0);
            if (q != null)
            {
                NavAgentSetDestination(GetEntityId(), q.x, q.y, q.z);
                return;
            }
            tries++;
        }
    }
}
)CQS";

        // Kenney Mini Characters klip sırası: static, idle, walk, sprint, jump, fall, ...
        const char* kCharacterController = R"YAML(AnimationController:
  Version: 2
  Layers:
    - Name: Base Layer
      Weight: 1
      DefaultState: Locomotion
      EntryPosition: [40, 180]
      AnyStatePosition: [40, 320]
      ExitPosition: [900, 180]
      States:
        - Name: Locomotion
          Type: BlendTree
          EditorPosX: 260
          EditorPosY: 170
          BlendType: 0
          BlendParameter: Speed
          BlendChildren:
            - ClipIndex: 1
              ClipName: idle
              Threshold: 0
            - ClipIndex: 2
              ClipName: walk
              Threshold: 0.43
            - ClipIndex: 3
              ClipName: sprint
              Threshold: 1
        - Name: Jump
          ClipIndex: 4
          ClipName: jump
          Loop: false
          EditorPosX: 520
          EditorPosY: 80
        - Name: Fall
          ClipIndex: 5
          ClipName: fall
          EditorPosX: 520
          EditorPosY: 260
      Transitions:
        - From: Any State
          To: Jump
          HasExitTime: false
          Duration: 0.1
          Conditions:
            - Parameter: Jump
              Mode: If
        - From: Jump
          To: Fall
          HasExitTime: true
          ExitTime: 0.85
          Duration: 0.15
        - From: Locomotion
          To: Fall
          HasExitTime: false
          Duration: 0.2
          Conditions:
            - Parameter: Grounded
              Mode: IfNot
        - From: Fall
          To: Locomotion
          HasExitTime: false
          Duration: 0.15
          Conditions:
            - Parameter: Grounded
              Mode: If
  Parameters:
    - Name: Speed
      Type: Float
      Default: 0
    - Name: Grounded
      Type: Bool
      Default: 1
    - Name: Jump
      Type: Trigger
      Default: 0
)YAML";

        // Sahne içi canlandırma: klip adıyla tek durumlu döngü (selamlaşma, oturma ...)
        std::string SingleClipController(int clipIndex, const char* clipName)
        {
            return std::string("AnimationController:\n  Version: 2\n  Layers:\n    - Name: Base Layer\n      Weight: 1\n      DefaultState: ") + clipName +
                   "\n      States:\n        - Name: " + clipName + "\n          ClipIndex: " + std::to_string(clipIndex) + "\n          ClipName: " + clipName +
                   "\n  Parameters: []\n";
        }

        // ------------------------------------------------------------------ Yardımcılar

        // Görünmez statik çarpıştırıcı (MakeBlock ile aynı ölçek kuralı, ağ kaldırılır)
        Entity MakeCollider(Scene& scene, const char* name, const glm::vec3& pos, const glm::vec3& size)
        {
            Entity e = MakeMesh(scene, name, MeshType::Cube, pos, size, glm::vec4(1.0f));
            MakeStatic(e);
            e.RemoveComponent<MeshRendererComponent>();
            return e;
        }

        constexpr float kCharacterScale = 1.75f;   // Kenney karakteri ~1 birim -> ~1.8 m

        // Karakter varlıkları ve denetleyicisini projeye kopyala
        void CopyCharacters(const fs::path& dir)
        {
            for (const char* c : { "male-a", "male-c", "female-a", "female-d" })
                CopyResource(dir, std::string("Templates/Kenney/Characters/character-") + c + ".glb", std::string("Assets/Models/Characters/character-") + c + ".glb");
            CopyResource(dir, "Templates/Kenney/Characters/Textures/colormap.png", "Assets/Models/Characters/Textures/colormap.png");
            WriteText(dir / "Assets/Animations/Character.cqcont", kCharacterController);
            WriteText(dir / "Assets/Scripts/CharacterAnimDriver.cqs", kCharacterAnimDriver);
            CopyResource(dir, "Templates/Kenney/LICENSE.txt", "Assets/Models/LICENSE-Kenney.txt");
        }

        // Görünüm çocuğu: model + animasyon + sürücü betiği. footOffset: kök noktasından ayaklara (kapsül merkezi için -h/2)
        Entity AddCharacterMesh(Scene& scene, Entity owner, const char* variant, float footOffset)
        {
            Entity mesh = MakeModel(scene, "Mesh", std::string("Assets/Models/Characters/character-") + variant + ".glb", { 0.0f, footOffset, 0.0f }, 0.0f,
                                    kCharacterScale);
            mesh.AddComponent<AnimationComponent>().ControllerFilePath = "Assets/Animations/Character.cqcont";
            AddScript(mesh, "Assets/Scripts/CharacterAnimDriver.cqs", "CharacterAnimDriver");
            scene.SetEntityParent(mesh, owner, false);
            return mesh;
        }

        // Sahne süsü: tek klipte döngüde oynayan karakter
        Entity AddActor(Scene& scene, const fs::path& dir, const char* name, const char* variant, const glm::vec3& pos, float yawDeg, int clipIndex, const char* clip)
        {
            const std::string controller = std::string("Assets/Animations/") + clip + ".cqcont";
            if (!fs::exists(dir / controller))
                WriteText(dir / controller, SingleClipController(clipIndex, clip).c_str());
            Entity e = MakeModel(scene, name, std::string("Assets/Models/Characters/character-") + variant + ".glb", pos, yawDeg, kCharacterScale);
            e.AddComponent<AnimationComponent>().ControllerFilePath = controller;
            return e;
        }

        Entity MakePointLight(Scene& scene, const char* name, const glm::vec3& pos, const glm::vec3& color, float intensity, float range)
        {
            Entity e = scene.CreateEntity(name);
            e.GetComponent<TransformComponent>().Position = pos;
            auto& pl = e.AddComponent<PointLightComponent>();
            pl.Color = color;
            pl.Intensity = intensity;
            pl.Range = range;
            return e;
        }

        Entity MakeSpotLight(Scene& scene, const char* name, const glm::vec3& pos, const glm::vec3& target, const glm::vec3& color, float intensity, float range,
                             float cone)
        {
            Entity e = scene.CreateEntity(name);
            e.GetComponent<TransformComponent>().Position = pos;
            auto& sl = e.AddComponent<SpotLightComponent>();
            sl.Direction = glm::normalize(target - pos);
            sl.Color = color;
            sl.Intensity = intensity;
            sl.Range = range;
            sl.InnerConeAngle = cone * 0.7f;
            sl.OuterConeAngle = cone;
            return e;
        }

        // Unreal oynanış şablonlarının ortak test alanı: zemin, rampalar, merdiven, platformlar, itilebilir kutular
        void BuildPlayground(Scene& scene, float half)
        {
            const glm::vec4 floorColor{ 0.36f, 0.38f, 0.4f, 1.0f }, wallColor{ 0.58f, 0.6f, 0.63f, 1.0f };
            const glm::vec4 accentA{ 0.15f, 0.45f, 0.85f, 1.0f }, accentB{ 0.95f, 0.55f, 0.15f, 1.0f };
            MakeBlock(scene, "Floor", { 0.0f, -0.25f, 0.0f }, { half * 2.0f, 0.5f, half * 2.0f }, floorColor);
            MakeBlock(scene, "Wall North", { 0.0f, 1.5f, -half }, { half * 2.0f, 3.0f, 0.5f }, wallColor);
            MakeBlock(scene, "Wall South", { 0.0f, 1.5f, half }, { half * 2.0f, 3.0f, 0.5f }, wallColor);
            MakeBlock(scene, "Wall East", { half, 1.5f, 0.0f }, { 0.5f, 3.0f, half * 2.0f }, wallColor);
            MakeBlock(scene, "Wall West", { -half, 1.5f, 0.0f }, { 0.5f, 3.0f, half * 2.0f }, wallColor);
            // Rampa + üstünde platform
            MakeBlock(scene, "Ramp", { 8.0f, 1.0f, -6.0f }, { 4.0f, 0.4f, 8.0f }, accentA, 0.0f, 14.0f);
            MakeBlock(scene, "Ramp Platform", { 8.0f, 1.9f, -12.0f }, { 6.0f, 0.4f, 4.0f }, accentA);
            // Merdiven (0.3 m basamak: Character Controller'ın adım yüksekliği)
            for (int i = 0; i < 6; ++i)
            {
                std::string n = "Step " + std::to_string(i + 1);
                MakeBlock(scene, n.c_str(), { -8.0f, 0.15f + i * 0.3f, -4.0f - i * 0.6f }, { 3.0f, 0.3f + i * 0.6f, 0.6f }, accentB);
            }
            MakeBlock(scene, "Stair Landing", { -8.0f, 0.9f, -9.0f }, { 3.0f, 1.8f, 3.0f }, accentB);
            // Zıplama blokları
            MakeBlock(scene, "Jump Block 1", { 0.0f, 0.4f, -10.0f }, { 1.6f, 0.8f, 1.6f }, accentA);
            MakeBlock(scene, "Jump Block 2", { 2.2f, 0.8f, -12.0f }, { 1.6f, 1.6f, 1.6f }, accentA);
            MakeBlock(scene, "Jump Block 3", { 4.4f, 1.2f, -14.0f }, { 1.6f, 2.4f, 1.6f }, accentA);
            // İtilebilir kutular
            for (int i = 0; i < 6; ++i)
            {
                std::string n = "Crate " + std::to_string(i + 1);
                Entity c = MakeMesh(scene, n.c_str(), MeshType::Cube, { -3.0f + (i % 3) * 1.1f, 0.5f + (i / 3) * 1.05f, 6.0f }, glm::vec3(1.0f),
                                    { 0.62f, 0.45f, 0.3f, 1.0f });
                c.AddComponent<RigidbodyComponent>().Type = RigidbodyComponent::BodyType::Dynamic;
                c.AddComponent<BoxColliderComponent>();
            }
        }

        // ------------------------------------------------------------------ Şablonlar

        void BuildThirdPerson(Scene& scene, const fs::path& dir)
        {
            CopyCharacters(dir);
            WriteText(dir / "Assets/Scripts/ThirdPersonCharacter.cqs", kThirdPersonCharacter);
            SetupHighQuality(scene);
            MakeSun(scene, { -0.45f, -0.8f, -0.35f }, 3.0f);
            BuildPlayground(scene, 20.0f);

            Entity player = scene.CreateEntity("Player");
            player.GetComponent<TransformComponent>().Position = { 0.0f, 0.95f, 4.0f };
            auto& cc = player.AddComponent<CharacterControllerComponent>();
            cc.Height = 1.8f;
            cc.Radius = 0.35f;
            AddScript(player, "Assets/Scripts/ThirdPersonCharacter.cqs", "ThirdPersonCharacter");
            AddCharacterMesh(scene, player, "male-a", -0.9f);

            Entity cam = MakeCamera(scene, { 0.0f, 3.0f, 10.0f }, { 0.0f, 1.0f, 4.0f });
            auto& follow = cam.AddComponent<CameraFollowComponent>();
            follow.Target = (uint64_t)player.GetComponent<IDComponent>().ID;
            follow.Mode = CameraFollowMode::Orbit;
            follow.OrbitDistance = 5.5f;
            follow.OrbitPitch = 18.0f;
            follow.LookOffset = { 0.0f, 0.6f, 0.0f };
            follow.MouseOrbit = true;
            follow.PositionDamping = 0.08f;
        }

        void BuildTopDown(Scene& scene, const fs::path& dir)
        {
            CopyCharacters(dir);
            WriteText(dir / "Assets/Scripts/TopDownCharacter.cqs", kTopDownCharacter);
            SetupHighQuality(scene);
            MakeSun(scene, { -0.3f, -1.0f, -0.5f }, 3.0f);
            BuildPlayground(scene, 20.0f);
            // NavMesh: oyun başlarken pişirilir
            Entity nav = scene.CreateEntity("NavMesh Surface");
            auto& surf = nav.AddComponent<NavMeshSurfaceComponent>();
            surf.AgentRadius = 0.4f;
            surf.AgentHeight = 1.8f;
            surf.StepHeight = 0.35f;
            surf.BakeOnPlay = true;

            Entity player = scene.CreateEntity("Player");
            player.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 4.0f };
            auto& agent = player.AddComponent<NavMeshAgentComponent>();
            agent.Radius = 0.4f;
            agent.Height = 1.8f;
            agent.Speed = 3.0f;
            agent.AngularSpeed = 720.0f;
            agent.Acceleration = 20.0f;
            agent.StoppingDistance = 0.1f;
            AddScript(player, "Assets/Scripts/TopDownCharacter.cqs", "TopDownCharacter");
            AddCharacterMesh(scene, player, "female-a", 0.0f);

            Entity marker = MakeMesh(scene, "Destination Marker", MeshType::Cylinder, { 0.0f, -10.0f, 0.0f }, { 0.6f, 0.04f, 0.6f }, { 0.2f, 0.9f, 0.5f, 1.0f });
            (void)marker;

            Entity cam = MakeCamera(scene, { 0.0f, 9.0f, 10.5f }, { 0.0f, 0.0f, 4.0f });
            auto& follow = cam.AddComponent<CameraFollowComponent>();
            follow.Target = (uint64_t)player.GetComponent<IDComponent>().ID;
            follow.Mode = CameraFollowMode::TopDown;
            follow.Offset = { 0.0f, 9.0f, 6.5f };
            follow.LookAtTarget = true;
            follow.PositionDamping = 0.15f;
        }

        bool BuildVehicle(Scene& scene, const fs::path& dir, std::string& error)
        {
            if (!InstallAddon("vehicle-controller", dir, error))
                return false;
            for (const char* f : { "sedan-sports-body.glb", "wheel-default.glb", "Textures/colormap.png" })
                CopyResource(dir, std::string("Templates/Kenney/Cars/") + f, std::string("Assets/Models/Cars/") + f);
            CopyResource(dir, "Templates/Kenney/LICENSE.txt", "Assets/Models/LICENSE-Kenney.txt");
            SetupHighQuality(scene);
            MakeSun(scene, { -0.5f, -0.7f, -0.3f }, 3.0f);

            // Pist: geniş zemin, koniler (itilebilir), rampa, eğimli viraj duvarları
            const float half = 60.0f;
            MakeBlock(scene, "Ground", { 0.0f, -0.25f, 0.0f }, { half * 2.0f, 0.5f, half * 2.0f }, { 0.3f, 0.32f, 0.34f, 1.0f });
            MakeBlock(scene, "Track Inner", { 0.0f, 0.4f, 0.0f }, { 30.0f, 0.8f, 50.0f }, { 0.35f, 0.55f, 0.3f, 1.0f });
            MakeBlock(scene, "Ramp", { 24.0f, 0.6f, -20.0f }, { 6.0f, 0.3f, 10.0f }, { 0.95f, 0.6f, 0.15f, 1.0f }, 0.0f, 9.0f);
            for (int side = -1; side <= 1; side += 2)
            {
                MakeBlock(scene, side < 0 ? "Barrier West" : "Barrier East", { side * half, 1.0f, 0.0f }, { 1.0f, 2.0f, half * 2.0f }, { 0.85f, 0.85f, 0.88f, 1.0f });
                MakeBlock(scene, side < 0 ? "Barrier North" : "Barrier South", { 0.0f, 1.0f, side * half }, { half * 2.0f, 2.0f, 1.0f }, { 0.85f, 0.85f, 0.88f, 1.0f });
            }
            for (int i = 0; i < 12; ++i)
            {
                std::string n = "Cone " + std::to_string(i + 1);
                Entity cone = MakeMesh(scene, n.c_str(), MeshType::Cylinder, { -22.0f + (i % 2) * 4.0f, 0.4f, -30.0f + i * 5.0f }, { 0.5f, 0.8f, 0.5f },
                                       { 1.0f, 0.45f, 0.1f, 1.0f });
                auto& rb = cone.AddComponent<RigidbodyComponent>();
                rb.Type = RigidbodyComponent::BodyType::Dynamic;
                rb.Mass = 4.0f;
                cone.AddComponent<BoxColliderComponent>();
            }

            // Araç: addon prefab'ı (Wheel Collider'lar + VehicleController), görünüm Kenney gövdesi + tekerlekleri
            const std::string rel = "Packages/vehicle-controller/Prefabs/Vehicle.cqprefab";
            Entity car = PrefabSerializer::Instantiate(scene, (dir / rel).string());
            if (!car)
            {
                error = "Vehicle prefab missing";
                return false;
            }
            if (car.HasComponent<PrefabInstanceComponent>())
                car.GetComponent<PrefabInstanceComponent>().Prefab = rel;
            car.GetComponent<TransformComponent>().Position = { -20.0f, 0.3f, 30.0f };
            for (const char* n : { "Car Body", "Cabin" })
                if (Entity e = FindByName(scene, n); e && e.HasComponent<MeshRendererComponent>())
                    e.RemoveComponent<MeshRendererComponent>();
            // Kenney gövdesi +Z'ye bakar, motor ileri yönü -Z: 180°. Prefab gövdesi 4 m (Kenney 2.54 m) -> ölçek 1.6
            Entity body = MakeModel(scene, "Body Model", "Assets/Models/Cars/sedan-sports-body.glb", { 0.0f, 0.12f, 0.0f }, 180.0f, 1.6f);
            scene.SetEntityParent(body, car, false);
            for (const char* w : { "WheelFL Mesh", "WheelFR Mesh", "WheelRL Mesh", "WheelRR Mesh" })
            {
                Entity mesh = FindByName(scene, w);
                if (!mesh)
                    continue;
                if (mesh.HasComponent<MeshRendererComponent>())
                    mesh.RemoveComponent<MeshRendererComponent>();
                auto& tc = mesh.GetComponent<TransformComponent>();
                const bool left = std::string(w).find('L', 5) == 5;   // WheelFL / WheelRL
                tc.Rotation = { 0.0f, glm::radians(left ? 180.0f : 0.0f), 0.0f };
                tc.Scale = glm::vec3(1.2f);
                mesh.AddComponent<ModelComponent>().FilePath = "Assets/Models/Cars/wheel-default.glb";
            }
            scene.UpdateWorldTransforms();

            Entity cam = MakeCamera(scene, { -20.0f, 3.0f, 37.0f }, { -20.0f, 1.0f, 30.0f });
            AddScript(cam, "Packages/vehicle-controller/Scripts/ChaseCamera.cqs", "ChaseCamera");
            return true;
        }

        void BuildVirtualProduction(Scene& scene, const fs::path& dir)
        {
            CopyCharacters(dir);
            WriteText(dir / "Assets/Scripts/ShotSequencer.cqs", kShotSequencer);
            auto& rs = scene.GetRenderSettings();
            SetupHighQuality(scene, true);
            rs.Environment.Sky = SkyMode::SolidColor;
            rs.Environment.BackgroundColor = { 0.02f, 0.02f, 0.025f };
            rs.Environment.SkyboxIntensity = 0.25f;
            rs.Post.Vignette = true;
            rs.Post.VignetteIntensity = 0.35f;
            rs.Post.FilmGrain = true;
            rs.Post.FilmGrainIntensity = 0.08f;
            rs.Post.DepthOfField = true;
            rs.Post.FocusDistance = 6.0f;
            rs.Post.Aperture = 2.0f;
            rs.Post.FocalLength = 50.0f;

            // Sahne: parlak siyah zemin, arka plan perdesi (cyclorama), sahne ışıkları
            MakeBlock(scene, "Stage Floor", { 0.0f, -0.1f, 0.0f }, { 16.0f, 0.2f, 12.0f }, { 0.05f, 0.05f, 0.06f, 1.0f });
            if (Entity f = FindByName(scene, "Stage Floor"))
                f.GetComponent<MeshRendererComponent>().Roughness = 0.15f;
            MakeBlock(scene, "Cyclorama", { 0.0f, 3.0f, -6.0f }, { 16.0f, 6.0f, 0.2f }, { 0.12f, 0.14f, 0.2f, 1.0f });
            MakeBlock(scene, "Riser", { 0.0f, 0.2f, -2.0f }, { 6.0f, 0.4f, 3.0f }, { 0.25f, 0.22f, 0.2f, 1.0f });
            MakeSpotLight(scene, "Key Light", { 3.5f, 5.0f, 3.0f }, { 0.0f, 1.0f, -1.5f }, { 1.0f, 0.9f, 0.78f }, 22.0f, 18.0f, 30.0f);
            MakeSpotLight(scene, "Fill Light", { -4.0f, 3.5f, 2.5f }, { 0.0f, 1.0f, -1.5f }, { 0.55f, 0.65f, 1.0f }, 8.0f, 16.0f, 40.0f);
            MakeSpotLight(scene, "Rim Light", { 0.0f, 5.0f, -5.0f }, { 0.0f, 1.2f, -1.5f }, { 1.0f, 0.5f, 0.85f }, 18.0f, 14.0f, 35.0f);
            MakePointLight(scene, "Practical Lamp", { -3.0f, 1.2f, -3.0f }, { 1.0f, 0.65f, 0.3f }, 3.0f, 6.0f);

            // Oyuncular (döngüde canlandırma)
            AddActor(scene, dir, "Actor Presenter", "male-c", { -0.9f, 0.4f, -2.0f }, 20.0f, 11, "emote-yes");
            AddActor(scene, dir, "Actor Guest", "female-d", { 0.9f, 0.4f, -2.0f }, -20.0f, 1, "idle");
            AddActor(scene, dir, "Actor Crew", "male-a", { -5.0f, 0.0f, 1.0f }, 60.0f, 2, "walk");

            // Çekimler: Virtual Camera'lar + Camera Brain (gerçek kamera)
            Entity brainCam = MakeCamera(scene, { 0.0f, 1.6f, 5.0f }, { 0.0f, 1.2f, -2.0f });
            brainCam.AddComponent<CameraBrainComponent>();
            AddScript(brainCam, "Assets/Scripts/ShotSequencer.cqs", "ShotSequencer");
            struct Shot { glm::vec3 Pos, Look; float Fov; };
            const Shot shots[] = {
                { { 0.0f, 1.7f, 5.5f }, { 0.0f, 1.2f, -2.0f }, 40.0f },   // geniş plan
                { { -1.8f, 1.5f, 0.6f }, { 0.9f, 1.3f, -2.0f }, 30.0f },  // omuz üstü
                { { 1.6f, 1.4f, 0.4f }, { -0.9f, 1.3f, -2.0f }, 30.0f },  // ters açı
                { { 4.5f, 3.2f, 3.5f }, { 0.0f, 0.8f, -2.0f }, 35.0f },   // vinç
            };
            int i = 0;
            for (const auto& s : shots)
            {
                std::string n = "Shot " + std::to_string(++i);
                Entity v = scene.CreateEntity(n);
                auto& tc = v.GetComponent<TransformComponent>();
                tc.Position = s.Pos;
                tc.Rotation = Math::EulerForForward(s.Look - s.Pos);
                auto& vc = v.AddComponent<VirtualCameraComponent>();
                vc.Priority = i == 1 ? 20 : 10;
                vc.FieldOfView = s.Fov;
            }
        }

        void BuildArchvizInterior(Scene& scene, const fs::path& dir)
        {
            // Mobilyalar (Kenney Furniture Kit, 1 birim ızgara) x2 ölçek
            const char* items[] = { "floorFull", "wall", "wallWindow", "wallWindowSlide", "wallDoorway", "loungeSofa", "loungeChair", "tableCoffeeGlass",
                                    "rugRectangle", "lampSquareFloor", "lampRoundTable", "pottedPlant", "plantSmall2", "bookcaseOpen", "books", "televisionModern",
                                    "cabinetTelevision", "kitchenCabinet", "kitchenSink", "kitchenStove", "kitchenFridgeLarge", "kitchenBar", "stoolBar", "table",
                                    "chair", "lampSquareCeiling", "sideTable", "speaker", "pillow", "laptop" };
            for (const char* it : items)
                CopyResource(dir, std::string("Templates/Kenney/Furniture/") + it + ".glb", std::string("Assets/Models/Furniture/") + it + ".glb");
            CopyResource(dir, "Templates/Kenney/LICENSE.txt", "Assets/Models/LICENSE-Kenney.txt");
            // FirstPersonController.cqs: HubTemplates.cpp yazar (FPS şablonuyla aynı)
            SetupHighQuality(scene, true, true);
            auto& rs = scene.GetRenderSettings();
            rs.Post.AutoExposure = false;
            rs.Shadows.Distance = 40.0f;

            const float S = 2.0f;   // Kenney 1 birim = 2 m
            auto furn = [&](const char* name, const char* model, float x, float z, float yaw = 0.0f, float y = 0.0f) {
                return MakeModel(scene, name, std::string("Assets/Models/Furniture/") + model + ".glb", { x, y, z }, yaw, S);
            };
            // Döşeme: 5 x 4 karo (10 m x 8 m), karo kökü köşede (x 0..1, z -1..0)
            for (int x = 0; x < 5; ++x)
                for (int z = 0; z < 4; ++z)
                    furn("Floor Tile", "floorFull", -5.0f + x * S, -4.0f + (z + 1) * S);
            // Döşeme çarpıştırıcısı ve tavan
            MakeCollider(scene, "Floor Collider", { 0.0f, -0.1f, 0.0f }, { 10.0f, 0.2f, 8.0f });
            MakeBlock(scene, "Ceiling", { 0.0f, 2.62f, 0.0f }, { 10.0f, 0.1f, 8.0f }, { 0.92f, 0.92f, 0.9f, 1.0f });
            // Duvarlar: kuzey pencereli, doğu sürgülü cam, güney kapılı, batı düz
            for (int i = 0; i < 5; ++i)
            {
                furn("Wall North", i % 2 ? "wallWindow" : "wall", -5.0f + i * S, -4.0f);
                furn("Wall South", i == 2 ? "wallDoorway" : "wall", -5.0f + (i + 1) * S, 4.0f, 180.0f);
            }
            for (int i = 0; i < 4; ++i)
            {
                furn("Wall East", i == 1 || i == 2 ? "wallWindowSlide" : "wall", 5.0f, -4.0f + i * S, 90.0f);
                furn("Wall West", "wall", -5.0f, -4.0f + (i + 1) * S, -90.0f);
            }
            for (const auto& w : { std::pair<glm::vec3, glm::vec3>{ { 0.0f, 1.3f, -4.05f }, { 10.0f, 2.6f, 0.1f } }, { { 0.0f, 1.3f, 4.05f }, { 10.0f, 2.6f, 0.1f } },
                                   { { 5.05f, 1.3f, 0.0f }, { 0.1f, 2.6f, 8.0f } }, { { -5.05f, 1.3f, 0.0f }, { 0.1f, 2.6f, 8.0f } } })
                MakeCollider(scene, "Wall Collider", w.first, w.second);
            // Oturma alanı
            furn("Rug", "rugRectangle", -2.6f, 0.4f);
            furn("Sofa", "loungeSofa", -3.2f, 1.6f, 0.0f);
            furn("Lounge Chair", "loungeChair", 0.4f, -0.4f, 90.0f);
            furn("Coffee Table", "tableCoffeeGlass", -2.4f, -0.3f);
            furn("Floor Lamp", "lampSquareFloor", -4.6f, 2.6f);
            furn("TV Cabinet", "cabinetTelevision", -3.0f, -3.9f);
            furn("Television", "televisionModern", -2.7f, -3.85f, 0.0f, 0.6f);
            furn("Bookcase", "bookcaseOpen", -4.9f, -1.0f, 90.0f);
            furn("Plant", "pottedPlant", 4.2f, 3.4f);
            furn("Plant Small", "plantSmall2", -4.6f, -3.6f);
            // Mutfak ve yemek
            furn("Kitchen Cabinet 1", "kitchenCabinet", 2.0f, -3.9f);
            furn("Kitchen Sink", "kitchenSink", 3.0f, -3.9f);
            furn("Kitchen Stove", "kitchenStove", 4.0f, -3.9f);
            furn("Fridge", "kitchenFridgeLarge", 0.6f, -3.9f);
            furn("Kitchen Bar", "kitchenBar", 2.2f, -1.6f);
            furn("Bar Stool 1", "stoolBar", 2.4f, -0.6f);
            furn("Bar Stool 2", "stoolBar", 3.4f, -0.6f);
            furn("Dining Table", "table", 2.0f, 2.6f);
            furn("Chair 1", "chair", 2.2f, 1.4f, 180.0f);
            furn("Chair 2", "chair", 3.4f, 3.2f);
            furn("Laptop", "laptop", 2.6f, 2.2f, 0.0f, 0.66f);
            // Işıklar: güneş pencerelerden, tavan lambaları, sıcak ortam
            MakeSun(scene, { -0.55f, -0.55f, -0.6f }, 4.0f, { 1.0f, 0.93f, 0.82f });
            for (const glm::vec3& p : { glm::vec3(-2.5f, 2.45f, 0.0f), glm::vec3(2.5f, 2.45f, -2.0f), glm::vec3(2.5f, 2.45f, 2.0f) })
            {
                furn("Ceiling Lamp", "lampSquareCeiling", p.x - 0.3f, p.z + 0.3f, 0.0f, 2.2f);
                MakePointLight(scene, "Ceiling Light", { p.x, 2.2f, p.z }, { 1.0f, 0.85f, 0.65f }, 2.5f, 7.0f);
            }

            // Gezinti: birinci şahıs (HubTemplates.cpp ile aynı betik)
            Entity player = scene.CreateEntity("Player");
            player.GetComponent<TransformComponent>().Position = { 0.0f, 1.0f, 2.8f };
            auto& cc = player.AddComponent<CharacterControllerComponent>();
            cc.Height = 1.7f;
            cc.Radius = 0.3f;
            AddScript(player, "Assets/Scripts/FirstPersonController.cqs", "FirstPersonController");
            Entity cam = scene.CreateEntity("Camera");
            cam.GetComponent<TransformComponent>().Position = { 0.0f, 0.65f, 0.0f };
            auto& camc = cam.AddComponent<CameraComponent>();
            camc.Primary = true;
            scene.SetEntityParent(cam, player, false);
        }

        void BuildCarConfigurator(Scene& scene, const fs::path& dir)
        {
            const char* cars[][2] = { { "sedan-sports", "Sedan Sports" }, { "suv", "SUV" }, { "race", "Race" }, { "police", "Police" }, { "taxi", "Taxi" },
                                      { "hatchback-sports", "Hatchback Sports" } };
            for (auto& c : cars)
                CopyResource(dir, std::string("Templates/Kenney/Cars/") + c[0] + ".glb", std::string("Assets/Models/Cars/") + c[0] + ".glb");
            CopyResource(dir, "Templates/Kenney/Cars/Textures/colormap.png", "Assets/Models/Cars/Textures/colormap.png");
            CopyResource(dir, "Templates/Kenney/LICENSE.txt", "Assets/Models/LICENSE-Kenney.txt");
            WriteText(dir / "Assets/Scripts/CarConfigurator.cqs", kCarConfigurator);
            SetupHighQuality(scene, true);
            auto& rs = scene.GetRenderSettings();
            rs.Environment.Sky = SkyMode::SolidColor;
            rs.Environment.BackgroundColor = { 0.08f, 0.085f, 0.095f };

            // Stüdyo: parlak zemin, döner tabla, yumuşak tepe ışıkları
            MakeBlock(scene, "Studio Floor", { 0.0f, -0.1f, 0.0f }, { 40.0f, 0.2f, 40.0f }, { 0.07f, 0.07f, 0.08f, 1.0f });
            if (Entity f = FindByName(scene, "Studio Floor"))
                f.GetComponent<MeshRendererComponent>().Roughness = 0.12f;
            // Döner tabla: ölçeksiz pivot (arabalar disk ölçeğini miras almasın) + disk çocuğu
            Entity table = scene.CreateEntity("Turntable");
            Entity disc = MakeMesh(scene, "Turntable Disc", MeshType::Cylinder, { 0.0f, 0.05f, 0.0f }, { 7.0f, 0.1f, 7.0f }, { 0.75f, 0.76f, 0.78f, 1.0f });
            disc.GetComponent<MeshRendererComponent>().Metallic = 0.8f;
            disc.GetComponent<MeshRendererComponent>().Roughness = 0.25f;
            scene.SetEntityParent(disc, table, false);
            auto& spin = table.AddComponent<RotatorComponent>();
            spin.RotationRate = { 0.0f, 12.0f, 0.0f };
            spin.RunInEditor = false;
            for (auto& c : cars)
            {
                Entity car = MakeModel(scene, c[1], std::string("Assets/Models/Cars/") + c[0] + ".glb", { 0.0f, 0.1f, 0.0f }, 0.0f, 1.6f);
                scene.SetEntityParent(car, table, false);
            }
            MakeSpotLight(scene, "Top Light", { 0.0f, 7.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 30.0f, 14.0f, 45.0f);
            MakeSpotLight(scene, "Key Light", { 5.0f, 4.0f, 5.0f }, { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.95f, 0.9f }, 20.0f, 16.0f, 35.0f);
            MakeSpotLight(scene, "Rim Light", { -5.0f, 3.0f, -5.0f }, { 0.0f, 0.6f, 0.0f }, { 0.6f, 0.75f, 1.0f }, 16.0f, 16.0f, 35.0f);
            MakeSun(scene, { -0.3f, -1.0f, -0.2f }, 0.6f);

            Entity cam = MakeCamera(scene, { 6.0f, 2.2f, 6.0f }, { 0.0f, 0.6f, 0.0f });
            auto& follow = cam.AddComponent<CameraFollowComponent>();
            follow.Target = (uint64_t)table.GetComponent<IDComponent>().ID;
            follow.Mode = CameraFollowMode::Orbit;
            follow.OrbitDistance = 7.5f;
            follow.OrbitPitch = 14.0f;
            follow.OrbitYaw = 45.0f;
            follow.LookOffset = { 0.0f, 0.7f, 0.0f };

            // Arayüz: model adı + kullanım
            Entity canvas = scene.CreateEntity("UI Canvas");
            canvas.AddComponent<CanvasComponent>();
            Entity label = scene.CreateEntity("Model Name");
            auto& rt = label.AddComponent<RectTransformComponent>();
            rt.AnchorMin = { 0.0f, 0.0f };
            rt.AnchorMax = { 1.0f, 0.0f };
            rt.Pivot = { 0.5f, 0.0f };
            rt.AnchoredPosition = { 0.0f, 30.0f };
            rt.SizeDelta = { 0.0f, 60.0f };
            auto& text = label.AddComponent<UITextComponent>();
            text.Text = "Sedan Sports";
            text.FontSize = 32.0f;
            text.AlignH = UITextAlignH::Center;
            text.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            scene.SetEntityParent(label, canvas, false);
            AddScript(canvas, "Assets/Scripts/CarConfigurator.cqs", "CarConfigurator");
        }

        void BuildCrowdSimulation(Scene& scene, const fs::path& dir)
        {
            CopyCharacters(dir);
            WriteText(dir / "Assets/Scripts/Wanderer.cqs", kWanderer);
            for (const char* b : { "a", "b", "c", "d", "e", "f", "g", "h", "skyscraper-a", "skyscraper-c" })
                CopyResource(dir, std::string("Templates/Kenney/City/building-") + b + ".glb", std::string("Assets/Models/City/building-") + b + ".glb");
            CopyResource(dir, "Templates/Kenney/City/Textures/colormap.png", "Assets/Models/City/Textures/colormap.png");
            SetupHighQuality(scene);
            MakeSun(scene, { -0.5f, -0.8f, -0.4f }, 3.0f);

            // Şehir bloğu: plaza (yürünebilir), dört köşede binalar
            MakeBlock(scene, "Plaza", { 0.0f, -0.25f, 0.0f }, { 60.0f, 0.5f, 60.0f }, { 0.55f, 0.55f, 0.52f, 1.0f });
            const char* kinds[] = { "a", "b", "c", "d", "e", "f", "g", "h", "skyscraper-a", "skyscraper-c" };
            int k = 0;
            for (int bx = -1; bx <= 1; bx += 2)
                for (int bz = -1; bz <= 1; bz += 2)
                    for (int i = 0; i < 2; ++i)
                    {
                        const glm::vec3 pos{ bx * (12.0f + i * 9.0f), 0.0f, bz * 16.0f };
                        const std::string model = std::string("Assets/Models/City/building-") + kinds[k++ % 10] + ".glb";
                        Entity b = MakeModel(scene, "Building", model, pos, bz > 0 ? 180.0f : 0.0f, 9.0f);
                        (void)b;
                        MakeCollider(scene, "Building Collider", { pos.x, 6.0f, pos.z }, { 8.0f, 12.0f, 8.6f });
                    }
            // Çeşme (engel)
            MakeBlock(scene, "Fountain", { 0.0f, 0.4f, 0.0f }, { 4.0f, 0.8f, 4.0f }, { 0.75f, 0.75f, 0.78f, 1.0f });
            Entity nav = scene.CreateEntity("NavMesh Surface");
            auto& surf = nav.AddComponent<NavMeshSurfaceComponent>();
            surf.AgentRadius = 0.35f;
            surf.AgentHeight = 1.8f;
            surf.BakeOnPlay = true;

            // Yayalar
            const char* variants[] = { "male-a", "male-c", "female-a", "female-d" };
            for (int i = 0; i < 24; ++i)
            {
                std::string n = "Pedestrian " + std::to_string(i + 1);
                Entity p = scene.CreateEntity(n);
                p.GetComponent<TransformComponent>().Position = { -18.0f + (i % 6) * 7.0f, 0.0f, -6.0f + (i / 6) * 4.0f };
                auto& agent = p.AddComponent<NavMeshAgentComponent>();
                agent.Radius = 0.35f;
                agent.Height = 1.8f;
                agent.AngularSpeed = 540.0f;
                agent.StoppingDistance = 0.3f;
                AddScript(p, "Assets/Scripts/Wanderer.cqs", "Wanderer");
                AddCharacterMesh(scene, p, variants[i % 4], 0.0f);
            }
            MakeCamera(scene, { 0.0f, 26.0f, 34.0f }, { 0.0f, 0.0f, 0.0f });
        }

        // Her bileşenden bir istasyon (bileşen kaydından üretilir: yeni bileşenler kendiliğinden eklenir). Satırlar kategoriler.
        // Açılıp Play'e basıldığında hepsi birlikte çalışır — motorun kapsamlı duman testi.
        void BuildFeatureTest(Scene& scene)
        {
            SetupHighQuality(scene);
            MakeSun(scene, { -0.4f, -0.9f, -0.3f }, 2.6f).GetComponent<TagComponent>().Tag = "Sun";
            std::map<std::string, std::vector<const ComponentInfo*>> byCategory;
            for (const auto& info : ComponentRegistry::All())
                if (info.Add && info.TypeName != "TransformComponent" && info.TypeName != "PrefabInstanceComponent")
                {
                    const std::string cat = info.Category.empty() ? "Other" : info.Category;
                    byCategory[cat].push_back(&info);
                }
            const float spacing = 3.0f;
            size_t widest = 1;
            for (auto& [cat, list] : byCategory)
                widest = std::max(widest, list.size());
            const float width = widest * spacing + 6.0f, depth = byCategory.size() * spacing * 2.0f + 6.0f;
            MakeBlock(scene, "Floor", { width * 0.5f - 3.0f, -0.25f, -depth * 0.5f + 3.0f }, { width, 0.5f, depth }, { 0.4f, 0.42f, 0.45f, 1.0f });

            Entity canvas = scene.CreateEntity("UI Canvas");
            canvas.AddComponent<CanvasComponent>();
            int row = 0;
            for (auto& [cat, list] : byCategory)
            {
                Entity group = scene.CreateEntity(cat);
                group.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, -row * spacing * 2.0f };
                const bool ui = cat == "UI";
                int col = 0;
                for (const ComponentInfo* info : list)
                {
                    Entity e = scene.CreateEntity(info->Name);
                    if (ui)
                    {
                        if (!e.HasComponent<RectTransformComponent>())
                            e.AddComponent<RectTransformComponent>().AnchoredPosition = { -600.0f + (col % 8) * 150.0f, 250.0f - (col / 8) * 60.0f };
                        scene.SetEntityParent(e, canvas, false);
                    }
                    else
                    {
                        auto& tc = e.GetComponent<TransformComponent>();
                        tc.Position = { col * spacing, 1.0f, 0.0f };
                        // Görünür taban (2D bileşenler, ışıklar ve kameralar hariç)
                        const bool visual = cat != "Physics 2D" && cat != "Lighting" && cat != "Cameras" && cat != "Grid" && info->TypeName != "MeshRendererComponent" &&
                                            info->TypeName != "CameraComponent";
                        if (visual)
                        {
                            tc.Scale = glm::vec3(0.4f);
                            auto& mr = e.AddComponent<MeshRendererComponent>();
                            mr.Type = MeshType::Cube;
                            mr.Color = { 0.3f + 0.05f * (row % 8), 0.55f, 0.85f - 0.05f * (col % 8), 1.0f };
                        }
                        scene.SetEntityParent(e, group, false);
                    }
                    if (!(info->Has && info->Has(e)))
                        ComponentRegistry::AddWithRequirements(*info, e);
                    ++col;
                }
                ++row;
            }
            // Genel bakış kamerası (birincil kamera: Camera bileşeni istasyonu birincil değil)
            const glm::vec3 overviewPos{ width * 0.5f, 18.0f, 14.0f }, overviewLook{ width * 0.5f, 0.0f, -depth * 0.35f };
            MakeCamera(scene, overviewPos, overviewLook, false, 6.0f, "Overview Camera");
            // Sahne geneline etki eden istasyonlar okunabilir kalsın: 2D ışık küresel (3D sahneyi karartmasın),
            // Camera Brain'in seçtiği sanal kamera genel bakışta
            auto setField = [&](const char* entity, const char* component, const char* field, const char* value) {
                Entity e = FindByName(scene, entity);
                const ComponentInfo* info = ComponentRegistry::Find(component);
                if (!e || !info || !info->Get)
                    return;
                if (const ComponentField* f = ComponentRegistry::FindField(*info, field))
                    if (void* c = info->Get(e))
                        ComponentRegistry::FieldFromString(*f, c, value);
            };
            setField("Light 2D", "Light2DComponent", "Type", "Global");
            // Directional Light istasyonu da güneşle aynı yöne baksın (atmosfer en parlak yönlü ışığı güneş sayar)
            if (Entity dl = FindByName(scene, "Directional Light"))
                dl.GetComponent<TransformComponent>().Rotation = Math::EulerForForward(glm::vec3(-0.4f, -0.9f, -0.3f));
            if (Entity vcam = FindByName(scene, "Virtual Camera"))
            {
                scene.RemoveEntityParent(vcam);
                auto& tc = vcam.GetComponent<TransformComponent>();
                tc.Position = overviewPos;
                tc.Rotation = Math::EulerForForward(overviewLook - overviewPos);
                tc.Scale = glm::vec3(1.0f);
            }
        }
    }

    bool IsProTemplate(const std::string& id)
    {
        return id == "thirdperson" || id == "topdown" || id == "vehicle" || id == "virtualproduction" || id == "archviz" || id == "configurator" ||
               id == "crowdsim" || id == "featuretest";
    }

    bool BuildProTemplate(const std::string& id, Scene& scene, const fs::path& dir, std::string& error)
    {
        if (id == "thirdperson") BuildThirdPerson(scene, dir);
        else if (id == "topdown") BuildTopDown(scene, dir);
        else if (id == "vehicle") return BuildVehicle(scene, dir, error);
        else if (id == "virtualproduction") BuildVirtualProduction(scene, dir);
        else if (id == "archviz") BuildArchvizInterior(scene, dir);
        else if (id == "configurator") BuildCarConfigurator(scene, dir);
        else if (id == "crowdsim") BuildCrowdSimulation(scene, dir);
        else if (id == "featuretest") BuildFeatureTest(scene);
        else
        {
            error = "Unknown template " + id;
            return false;
        }
        return true;
    }
}
