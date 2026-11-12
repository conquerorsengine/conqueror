#pragma once

#include "Core/Base/Base.h"
#include "Core/PhysicsSystem/PhysicsSettings.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Motor-bağımsız 3D fizik arka uç arayüzü. PhysicsWorld3D (cephe) sahne verisinden gövde,
// eklem ve karakter tanımları üretir; arka uç (Jolt / MuJoCo) bunları simüle eder.
namespace Conqueror::Physics3D
{
    using Handle = uint64_t;            // 0 = geçersiz
    constexpr uint32_t NoEntity = 0xFFFFFFFFu;

    enum class ShapeType : int { Box = 0, Sphere, Capsule, Cylinder, ConvexHull, TriangleMesh, HeightField };

    struct ShapeDesc
    {
        ShapeType Type = ShapeType::Box;
        glm::vec3 HalfExtents{ 0.5f };  // Box
        float Radius = 0.5f;            // Sphere / Capsule / Cylinder
        float HalfHeight = 0.5f;        // Capsule (silindir kısmı) / Cylinder, Y ekseni boyunca
        glm::vec3 Offset{ 0.0f };       // gövde uzayında
        glm::quat Rotation{ 1, 0, 0, 0 };
        std::vector<glm::vec3> Vertices; // ConvexHull / TriangleMesh (gövde uzayında, ölçek uygulanmış)
        std::vector<uint32_t> Indices;   // TriangleMesh
        // HeightField: Rows x Cols örnek (satır = +Z, sütun = +X), yerel köken köşede;
        // yükseklik = Heights[r * Cols + c] * Size.y
        int HFRows = 0, HFCols = 0;
        std::vector<float> HFHeights;
        glm::vec3 HFSize{ 1.0f };        // X genişliği, en yüksek nokta, Z uzunluğu
        bool IsTrigger = false;
        uint32_t SourceEntity = NoEntity; // çarpıştırıcının ait olduğu varlık (çocuk çarpıştırıcılar)
    };

    enum class MotionType : int { Static = 0, Dynamic, Kinematic };

    // Kilitli serbestlik dereceleri
    enum LockFlags : uint32_t
    {
        LockPosX = 1, LockPosY = 2, LockPosZ = 4,
        LockRotX = 8, LockRotY = 16, LockRotZ = 32
    };

    struct BodyDesc
    {
        uint32_t Entity = NoEntity;
        glm::vec3 Position{ 0.0f };
        glm::quat Rotation{ 1, 0, 0, 0 };
        std::vector<ShapeDesc> Shapes;
        MotionType Motion = MotionType::Dynamic;
        float Mass = 1.0f;
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;
        float GravityFactor = 1.0f;
        float Friction = 0.6f;
        float Restitution = 0.0f;
        float MaxAngularVelocity = 50.0f;
        bool Continuous = false;        // sürekli çarpışma algılama
        bool AllowSleep = true;
        uint32_t Locks = 0;             // LockFlags
        int Layer = 0;                  // 0..31
        glm::vec3 LinearVelocity{ 0.0f };
        glm::vec3 AngularVelocity{ 0.0f };
    };

    enum class JointType : int { Fixed = 0, Hinge, Slider, Spring, Character, Ball, SixDOF };

    // Configurable Joint: 3 öteleme + 3 dönme ekseni (X birincil, Y ikincil, Z = X × Y)
    enum class AxisMotion : int { Locked = 0, Limited, Free };
    struct SixDOFDesc
    {
        AxisMotion Linear[3] = { AxisMotion::Locked, AxisMotion::Locked, AxisMotion::Locked };
        AxisMotion Angular[3] = { AxisMotion::Locked, AxisMotion::Locked, AxisMotion::Locked };
        float LinearLimit = 1.0f;                       // ±m
        float LinearLimitSpring = 0.0f, LinearLimitDamper = 0.0f; // 0 = katı
        float AngularXMin = -0.785f, AngularXMax = 0.785f; // rad
        float AngularYLimit = 0.785f, AngularZLimit = 0.785f; // ±rad (simetrik)
        // Sürücüler: yay (konum) + sönüm; 0/0 = kapalı
        float LinearDriveSpring[3] = { 0, 0, 0 }, LinearDriveDamper[3] = { 0, 0, 0 }, LinearDriveMaxForce[3] = { 1e6f, 1e6f, 1e6f };
        float AngularDriveSpring[3] = { 0, 0, 0 }, AngularDriveDamper[3] = { 0, 0, 0 }, AngularDriveMaxForce[3] = { 1e6f, 1e6f, 1e6f };
        glm::vec3 SecondaryAxisWorld{ 0, 1, 0 };
    };

    struct JointTargets
    {
        glm::vec3 Position{ 0.0f };          // eklem uzayında
        glm::vec3 Velocity{ 0.0f };
        glm::quat Rotation{ 1, 0, 0, 0 };
        glm::vec3 AngularVelocity{ 0.0f };   // rad/s
    };

    struct JointDesc
    {
        JointType Type = JointType::Fixed;
        Handle BodyA = 0;               // eklemin bulunduğu gövde
        Handle BodyB = 0;               // bağlı gövde (0 = dünya)
        glm::vec3 AnchorWorld{ 0.0f };
        glm::vec3 ConnectedAnchorWorld{ 0.0f }; // Spring: B tarafı bağlantı noktası
        glm::vec3 AxisWorld{ 1, 0, 0 };          // menteşe / kaydırma / bükülme ekseni
        glm::vec3 NormalWorld{ 0, 1, 0 };        // eksene dik referans
        // Bağlı gövde (B) tarafının referans çerçevesi (dünya uzayında). UseFrameB ise eklem ilk kurulduğu dinlenme pozunu
        // korur: sonradan yeniden kurulunca (başka gövdeler yeniden kurulunca, ışınlamada) boşluk / limit / referans açı kaymaz.
        // Kapalıysa B tarafı A ile aynı (eklem o anki pozdan başlar).
        bool UseFrameB = false;
        glm::vec3 AnchorWorldB{ 0.0f };
        glm::vec3 AxisWorldB{ 1, 0, 0 };
        glm::vec3 NormalWorldB{ 0, 1, 0 };
        glm::vec3 SecondaryAxisWorldB{ 0, 0, 1 };  // SixDOF ikincil eksen
        float RestLength = -1.0f;                 // Spring: >= 0 ise ilk kurulumdaki doğal boy
        bool EnableCollision = false;
        float BreakForce = 0.0f;        // 0 = kırılmaz (N)
        float BreakTorque = 0.0f;       // 0 = kırılmaz (N·m)

        // Limitler (açılar radyan, mesafeler metre)
        bool UseLimits = false;
        float LimitMin = 0.0f, LimitMax = 0.0f;
        // Yay (Spring eklemi ve limit yumuşaklığı): sertlik + sönüm
        bool UseSpring = false;
        float SpringStiffness = 10.0f, SpringDamping = 0.5f, SpringTarget = 0.0f;
        float MinDistance = 0.0f, MaxDistance = 0.0f;
        // Motor
        bool UseMotor = false;
        float MotorTargetVelocity = 0.0f; // rad/s veya m/s
        float MotorMaxForce = 100.0f;
        // Karakter eklemi (salınım/bükülme)
        float TwistMin = 0.0f, TwistMax = 0.0f, Swing1 = 0.0f, Swing2 = 0.0f;
        SixDOFDesc SixDOF;                       // JointType::SixDOF
    };

    struct CharacterDesc
    {
        uint32_t Entity = NoEntity;
        glm::vec3 Position{ 0.0f };     // varlık konumu (Center ofseti ayrıca)
        glm::quat Rotation{ 1, 0, 0, 0 };
        glm::vec3 Center{ 0.0f };
        float Radius = 0.5f;
        float Height = 2.0f;            // toplam kapsül yüksekliği
        float SlopeLimit = glm::radians(45.0f);
        float StepOffset = 0.3f;
        float SkinWidth = 0.02f;
        float Mass = 70.0f;
        bool PushRigidbodies = true;
        int Layer = 0;
    };

    enum CollisionFlags : uint32_t { CollidedNone = 0, CollidedSides = 1, CollidedAbove = 2, CollidedBelow = 4 };

    struct CharacterState
    {
        glm::vec3 Position{ 0.0f };     // varlık konumu
        glm::vec3 Velocity{ 0.0f };
        glm::vec3 GroundNormal{ 0, 1, 0 };
        uint32_t GroundEntity = NoEntity;
        bool Grounded = false;
        uint32_t Flags = CollidedNone;
    };

    struct RaycastHit
    {
        uint32_t Entity = NoEntity;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0, 1, 0 };
        float Distance = 0.0f;
        bool IsTrigger = false;
    };

    struct ContactEvent
    {
        enum Kind : int { Enter = 0, Stay, Exit };
        Kind Type = Enter;
        uint32_t EntityA = NoEntity, EntityB = NoEntity;
        bool Trigger = false;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0, 1, 0 };   // A'dan B'ye
        float Impulse = 0.0f;
        glm::vec3 RelativeVelocity{ 0.0f };
    };

    // Sorgu filtresi
    struct QueryFilter
    {
        uint32_t LayerMask = 0xFFFFFFFFu;
        bool HitTriggers = true;
        uint32_t IgnoreEntity = NoEntity;
        Handle IgnoreBody = 0;          // bu gövdenin tüm şekillerini yok say (tekerlek ışınları)
    };

    // Kumaş / yumuşak gövde (Cloth): dünya uzayı köşeler + üçgenler; InvMass 0 = sabitlenmiş (dışarıdan sürülür)
    struct SoftBodyDesc
    {
        std::vector<glm::vec3> Vertices;
        std::vector<uint32_t> Triangles;
        std::vector<float> InvMass;
        float Compliance = 1e-4f;          // germe (0 = esnemez)
        float BendCompliance = 1e-2f;      // bükülme (büyük = yumuşak)
        float Damping = 0.1f;
        float Friction = 0.3f;
        float GravityFactor = 1.0f;
        float VertexRadius = 0.02f;
        int Iterations = 6;
        int Layer = 0;
        uint32_t Entity = NoEntity;
    };

    class CQ_API Backend
    {
    public:
        virtual ~Backend() = default;

        // Yumuşak gövdeler (desteklemeyen arka uçta kumaş durağan kalır)
        virtual Handle CreateSoftBody(const SoftBodyDesc& desc) { (void)desc; return 0; }
        virtual void DestroySoftBody(Handle body) { (void)body; }
        virtual bool GetSoftBodyVertices(Handle body, std::vector<glm::vec3>& outWorld) const { (void)body; (void)outWorld; return false; }
        // Sabitlenmiş köşeleri (dünya) yeni konumlarına taşı; serbest köşelere hız ekle (rüzgâr)
        virtual void DriveSoftBody(Handle body, const std::vector<uint32_t>& pinned, const std::vector<glm::vec3>& pinnedWorld, const glm::vec3& addVelocity, float dt)
        {
            (void)body; (void)pinned; (void)pinnedWorld; (void)addVelocity; (void)dt;
        }

        virtual const char* GetName() const = 0;
        virtual bool Init(const PhysicsSettings& settings, std::string* error) = 0;
        virtual void Shutdown() = 0;

        virtual void SetGravity(const glm::vec3& gravity) = 0;
        virtual void Step(float dt) = 0;

        // Gövdeler
        virtual Handle CreateBody(const BodyDesc& desc) = 0;
        virtual void DestroyBody(Handle body) = 0;
        virtual void SetBodyTransform(Handle body, const glm::vec3& position, const glm::quat& rotation, bool resetVelocity) = 0;
        virtual void MoveKinematic(Handle body, const glm::vec3& position, const glm::quat& rotation, float dt) = 0;
        virtual void GetBodyTransform(Handle body, glm::vec3& position, glm::quat& rotation) const = 0;
        virtual bool IsBodySleeping(Handle body) const = 0;
        virtual void WakeBody(Handle body) = 0;

        virtual void AddForce(Handle body, const glm::vec3& force) = 0;
        virtual void AddForceAtPoint(Handle body, const glm::vec3& force, const glm::vec3& worldPoint) = 0;
        virtual void AddImpulse(Handle body, const glm::vec3& impulse) = 0;
        virtual void AddImpulseAtPoint(Handle body, const glm::vec3& impulse, const glm::vec3& worldPoint) = 0;
        virtual void AddTorque(Handle body, const glm::vec3& torque) = 0;
        virtual void AddAngularImpulse(Handle body, const glm::vec3& impulse) = 0;
        virtual void SetLinearVelocity(Handle body, const glm::vec3& velocity) = 0;
        virtual glm::vec3 GetLinearVelocity(Handle body) const = 0;
        virtual void SetAngularVelocity(Handle body, const glm::vec3& velocity) = 0;
        virtual glm::vec3 GetAngularVelocity(Handle body) const = 0;
        virtual float GetMass(Handle body) const = 0;
        // Dünya noktasının hızı (v + ω × r). Varsayılan: doğrusal hız gövde kökünün hızı kabul edilir.
        virtual glm::vec3 GetPointVelocity(Handle body, const glm::vec3& worldPoint) const
        {
            glm::vec3 p;
            glm::quat r;
            GetBodyTransform(body, p, r);
            return GetLinearVelocity(body) + glm::cross(GetAngularVelocity(body), worldPoint - p);
        }

        // Eklemler
        virtual Handle CreateJoint(const JointDesc& desc) = 0;
        virtual void DestroyJoint(Handle joint) = 0;
        // Son adımdaki eklem tepki kuvveti / torku (kırılma denetimi için)
        virtual void GetJointReaction(Handle joint, float& force, float& torque) const = 0;
        virtual void SetJointMotor(Handle joint, bool enabled, float targetVelocity, float maxForce) = 0;
        // Configurable Joint sürücü hedefleri (her sabit adımda güncellenebilir)
        virtual void SetJointTargets(Handle joint, const JointTargets& targets) { (void)joint; (void)targets; }
        // Configurable Joint: bağlı gövdeye göre anlık öteleme (eklem uzayı) ve dönüş (Euler, rad)
        virtual bool GetJointPose(Handle joint, glm::vec3& translation, glm::vec3& eulerRad) const { (void)joint; translation = eulerRad = glm::vec3(0.0f); return false; }
        virtual float GetJointPosition(Handle joint) const = 0; // menteşe açısı (rad) / kaydırma (m)

        // Karakter denetleyicileri
        virtual Handle CreateCharacter(const CharacterDesc& desc) = 0;
        virtual void DestroyCharacter(Handle character) = 0;
        // Yer değiştirmeyi uygula (çarp-kay, basamak, eğim), yeni durumu döndür
        virtual CharacterState MoveCharacter(Handle character, const glm::vec3& displacement, float dt) = 0;
        virtual void SetCharacterPosition(Handle character, const glm::vec3& position) = 0;
        virtual CharacterState GetCharacterState(Handle character) const = 0;

        // Sorgular
        virtual bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, RaycastHit& hit) const = 0;
        virtual void RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, std::vector<RaycastHit>& hits) const = 0;
        virtual bool SphereCast(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, RaycastHit& hit) const = 0;
        virtual void OverlapSphere(const glm::vec3& center, float radius, const QueryFilter& filter, std::vector<uint32_t>& entities) const = 0;
        virtual void OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation, const QueryFilter& filter, std::vector<uint32_t>& entities) const = 0;
        // Şekil süpürme (Godot ShapeCast3D): küre (radius) / kutu (halfExtents) / kapsül (radius, halfHeight: silindir yarısı, Y ekseni)
        // rotation ile origin'den direction * maxDistance boyunca; çarpılanlar yakından uzağa (başta iç içe olanlar uzaklık 0).
        // Varsayılan: kuşatan küreyle en yakın tek isabet (arka uç kendi şekilleriyle geçersiz kılar)
        enum class CastShape : int { Sphere = 0, Box, Capsule };
        virtual void ShapeCastAll(CastShape shape, const glm::vec3& halfExtents, float radius, float halfHeight, const glm::quat& rotation,
                                  const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter,
                                  std::vector<RaycastHit>& hits, int maxResults) const
        {
            (void)rotation; (void)maxResults;
            hits.clear();
            RaycastHit h;
            const float r = shape == CastShape::Sphere ? radius : shape == CastShape::Capsule ? radius + halfHeight : glm::length(halfExtents);
            if (SphereCast(origin, r, direction, maxDistance, filter, h))
                hits.push_back(h);
        }

        // Son Step'ten bu yana biriken temas olayları (ana iş parçacığında)
        virtual void DrainContactEvents(std::vector<ContactEvent>& events) = 0;

        // İstatistik
        virtual int GetBodyCount() const = 0;
        virtual int GetActiveBodyCount() const = 0;
    };

    CQ_API std::unique_ptr<Backend> CreateJoltBackend();
    CQ_API std::unique_ptr<Backend> CreateMuJoCoBackend();
    // MuJoCo kitaplığı bulunabilir mi (ayarlar paneli için); bulunduysa yolu döndürür
    CQ_API bool ProbeMuJoCo(const std::string& overridePath, std::string* foundPath, std::string* version);
}
