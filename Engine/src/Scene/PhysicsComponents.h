#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

// Unity tarzı fizik bileşenleri (Character Controller, eklemler, Constant Force).
// Alanlar ComponentRegistry ile serileştirilir, denetçide çizilir ve betiklere açılır.
namespace Conqueror
{
    struct CharacterControllerComponent
    {
        float SlopeLimit = 45.0f;        // derece
        float StepOffset = 0.3f;
        float SkinWidth = 0.02f;
        float MinMoveDistance = 0.0f;
        glm::vec3 Center{ 0.0f, 0.0f, 0.0f };
        float Radius = 0.5f;
        float Height = 2.0f;
        float Mass = 70.0f;              // dinamik gövdeleri itme gücü
        bool PushRigidbodies = true;

        // Çalışma zamanı (serileştirilmez)
        bool IsGrounded = false;
        glm::vec3 Velocity{ 0.0f };
        int CollisionFlags = 0;          // 1 yanlar, 2 üst, 4 alt
    };

    // Ortak eklem alanları
    struct JointCommon
    {
        uint64_t ConnectedBody = 0;      // bağlı varlığın UUID'si (0 = dünya)
        glm::vec3 Anchor{ 0.0f };        // yerel uzayda
        float BreakForce = 0.0f;         // 0 = sonsuz
        float BreakTorque = 0.0f;
        bool EnableCollision = false;
        bool Broken = false;             // çalışma zamanı
    };

    struct FixedJointComponent : JointCommon
    {
    };

    struct HingeJointComponent : JointCommon
    {
        glm::vec3 Axis{ 1.0f, 0.0f, 0.0f };
        bool UseLimits = false;
        float MinAngle = -90.0f;         // derece
        float MaxAngle = 90.0f;
        bool UseSpring = false;
        float Spring = 50.0f;
        float Damper = 5.0f;
        float TargetAngle = 0.0f;
        bool UseMotor = false;
        float MotorTargetVelocity = 90.0f; // derece/saniye
        float MotorForce = 100.0f;
        float CurrentAngle = 0.0f;       // çalışma zamanı (derece)
    };

    struct SliderJointComponent : JointCommon
    {
        glm::vec3 Axis{ 1.0f, 0.0f, 0.0f };
        bool UseLimits = true;
        float MinDistance = -1.0f;
        float MaxDistance = 1.0f;
        bool UseMotor = false;
        float MotorTargetSpeed = 1.0f;
        float MotorForce = 100.0f;
        float CurrentPosition = 0.0f;    // çalışma zamanı
    };

    struct SpringJointComponent : JointCommon
    {
        glm::vec3 ConnectedAnchor{ 0.0f }; // bağlı gövdenin yerel uzayında (dünya ise dünya uzayında)
        bool AutoConfigureConnectedAnchor = true;
        float Spring = 10.0f;
        float Damper = 0.2f;
        float MinDistance = 0.0f;
        float MaxDistance = 0.0f;        // 0 = başlangıç mesafesi
    };

    struct CharacterJointComponent : JointCommon
    {
        glm::vec3 Axis{ 1.0f, 0.0f, 0.0f };       // bükülme ekseni
        glm::vec3 SwingAxis{ 0.0f, 1.0f, 0.0f };
        float LowTwistLimit = -20.0f;    // derece
        float HighTwistLimit = 70.0f;
        float Swing1Limit = 40.0f;
        float Swing2Limit = 40.0f;
    };

    struct BallJointComponent : JointCommon
    {
    };

    // Unity ConfigurableJoint: her eksen kilitli / sınırlı / serbest; sürücüler (yay + sönüm) hedef konum ve dönüşe çeker.
    // Eksen = X, İkincil Eksen = Y, Z = X × Y (eklemin yerel uzayında).
    enum class JointMotion : int { Locked = 0, Limited, Free };

    struct ConfigurableJointComponent : JointCommon
    {
        glm::vec3 Axis{ 1.0f, 0.0f, 0.0f };
        glm::vec3 SecondaryAxis{ 0.0f, 1.0f, 0.0f };
        JointMotion XMotion = JointMotion::Locked, YMotion = JointMotion::Locked, ZMotion = JointMotion::Locked;
        JointMotion AngularXMotion = JointMotion::Locked, AngularYMotion = JointMotion::Locked, AngularZMotion = JointMotion::Locked;
        float LinearLimit = 1.0f;            // ± m
        float LinearLimitSpring = 0.0f;      // 0 = katı sınır
        float LinearLimitDamper = 0.0f;
        float LowAngularXLimit = -45.0f;     // derece
        float HighAngularXLimit = 45.0f;
        float AngularYLimit = 45.0f;         // ± derece
        float AngularZLimit = 45.0f;

        glm::vec3 TargetPosition{ 0.0f };    // eklem uzayında
        glm::vec3 TargetVelocity{ 0.0f };
        float XDriveSpring = 0.0f, XDriveDamper = 0.0f, XDriveMaxForce = 1.0e6f;
        float YDriveSpring = 0.0f, YDriveDamper = 0.0f, YDriveMaxForce = 1.0e6f;
        float ZDriveSpring = 0.0f, ZDriveDamper = 0.0f, ZDriveMaxForce = 1.0e6f;
        glm::vec3 TargetRotation{ 0.0f };    // derece (Euler)
        glm::vec3 TargetAngularVelocity{ 0.0f }; // derece/s
        float AngularXDriveSpring = 0.0f, AngularXDriveDamper = 0.0f, AngularXDriveMaxForce = 1.0e6f;
        float AngularYZDriveSpring = 0.0f, AngularYZDriveDamper = 0.0f, AngularYZDriveMaxForce = 1.0e6f;

        // Çalışma zamanı (serileştirilmez)
        glm::vec3 CurrentPosition{ 0.0f };
        glm::vec3 CurrentRotation{ 0.0f };   // derece
    };

    // Unity WheelCollider: ışınla süspansiyon + lastik sürtünme eğrileri. Rigidbody'li bir atanın çocuğuna eklenir;
    // kendisi çarpışma şekli değildir. Motor/fren torku ve direksiyon açısı betikten ayarlanır.
    struct WheelFrictionCurve
    {
        float ExtremumSlip = 0.4f;
        float ExtremumValue = 1.0f;
        float AsymptoteSlip = 0.8f;
        float AsymptoteValue = 0.5f;
        float Stiffness = 1.0f;
    };

    // Unity Cloth: Jolt yumuşak gövdesiyle simüle edilen kumaş ızgarası (bayrak, perde, pelerin, masa örtüsü).
    // Mesh Renderer'ın mesh'i kumaşın kendisiyle değiştirilir; sabitlenen kenar varlıkla birlikte hareket eder.
    enum class ClothOrientation : int { Vertical = 0, Horizontal };
    enum class ClothPin : int { None = 0, TopEdge, TopCorners, LeftEdge, AllCorners };

    struct ClothComponent
    {
        glm::vec2 Size{ 2.0f, 2.0f };
        int Resolution = 16;                   // kenar başına hücre
        ClothOrientation Orientation = ClothOrientation::Vertical;
        ClothPin Pin = ClothPin::TopEdge;
        float StretchingStiffness = 0.95f;     // 0..1
        float BendingStiffness = 0.2f;         // 0..1
        float Damping = 0.1f;
        float Friction = 0.3f;
        float Thickness = 0.02f;               // çarpışma yarıçapı
        float GravityScale = 1.0f;
        int SolverIterations = 6;
        float WindInfluence = 1.0f;            // Wind Zone etkisi
        // Çalışma zamanı
        uint64_t BuiltSignature = 0;
    };

    struct WheelColliderComponent
    {
        float Mass = 20.0f;              // tekerlek kütlesi (dönme ataleti)
        float Radius = 0.4f;
        float WheelDampingRate = 0.25f;  // serbest dönme sönümü
        float SuspensionDistance = 0.3f; // süspansiyon kurs boyu
        float ForceAppPointDistance = 0.0f; // kuvvetin uygulandığı nokta (tekerlek merkezinden yukarı)
        glm::vec3 Center{ 0.0f };        // yerel uzayda

        float Spring = 35000.0f;         // N/m
        float Damper = 4500.0f;          // N·s/m
        float TargetPosition = 0.5f;     // 0 tam açık, 1 tam sıkışık

        float FwdExtremumSlip = 0.4f, FwdExtremumValue = 1.0f, FwdAsymptoteSlip = 0.8f, FwdAsymptoteValue = 0.5f, FwdStiffness = 1.0f;
        float SideExtremumSlip = 0.2f, SideExtremumValue = 1.0f, SideAsymptoteSlip = 0.5f, SideAsymptoteValue = 0.75f, SideStiffness = 1.0f;

        float MotorTorque = 0.0f;        // N·m
        float BrakeTorque = 0.0f;        // N·m
        float SteerAngle = 0.0f;         // derece (yerel Y ekseni çevresinde)
        uint64_t WheelModel = 0;         // görsel tekerlek (konumu ve dönüşü otomatik güncellenir)

        // Çalışma zamanı (serileştirilmez)
        bool IsGrounded = false;
        float RPM = 0.0f;
        float Compression = 0.0f;        // 0..1
        float ForwardSlip = 0.0f;
        float SidewaysSlip = 0.0f;
        uint64_t HitEntity = 0;
        glm::vec3 HitPoint{ 0.0f };
        glm::vec3 HitNormal{ 0.0f, 1.0f, 0.0f };
        float SuspensionForce = 0.0f;
        float AngularVelocity = 0.0f;    // rad/s (iç)
        float SpinAngle = 0.0f;          // rad (görsel)
        float LastCompression = -1.0f;   // m (sönüm için)
        glm::quat ModelRotOffset{ 1.0f, 0.0f, 0.0f, 0.0f };
        bool ModelOffsetCaptured = false;
    };

    struct ConstantForceComponent
    {
        glm::vec3 Force{ 0.0f };
        glm::vec3 RelativeForce{ 0.0f };
        glm::vec3 Torque{ 0.0f };
        glm::vec3 RelativeTorque{ 0.0f };
    };

    // ════════════════════════════════════════
    //  2D (Box2D)
    // ════════════════════════════════════════
    struct CapsuleCollider2DComponent
    {
        glm::vec2 Offset{ 0.0f };
        glm::vec2 Size{ 0.5f, 1.0f };
        int Direction = 0;               // 0 dikey, 1 yatay
        float Density = 1.0f;
        float Friction = 0.4f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
    };

    struct PolygonCollider2DComponent
    {
        glm::vec2 Offset{ 0.0f };
        std::vector<glm::vec2> Points{ { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
        float Density = 1.0f;
        float Friction = 0.4f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
    };

    struct EdgeCollider2DComponent
    {
        glm::vec2 Offset{ 0.0f };
        std::vector<glm::vec2> Points{ { -1.0f, 0.0f }, { 1.0f, 0.0f } };
        float EdgeRadius = 0.0f;
        float Friction = 0.4f;
        float Restitution = 0.0f;
        bool Loop = false;
    };

    struct Joint2DCommon
    {
        uint64_t ConnectedBody = 0;      // UUID (0 = dünya)
        glm::vec2 Anchor{ 0.0f };        // yerel
        glm::vec2 ConnectedAnchor{ 0.0f };
        bool AutoConfigureConnectedAnchor = true;
        bool EnableCollision = false;
        float BreakForce = 0.0f;         // 0 = sonsuz
        float BreakTorque = 0.0f;
        bool Broken = false;             // çalışma zamanı
    };

    struct DistanceJoint2DComponent : Joint2DCommon
    {
        bool AutoConfigureDistance = true;
        float Distance = 1.0f;
        bool MaxDistanceOnly = false;    // ip: yalnızca en fazla mesafe
    };

    struct SpringJoint2DComponent : Joint2DCommon
    {
        bool AutoConfigureDistance = true;
        float Distance = 1.0f;
        float Frequency = 1.0f;          // Hz
        float DampingRatio = 0.0f;
    };

    struct HingeJoint2DComponent : Joint2DCommon
    {
        bool UseMotor = false;
        float MotorSpeed = 0.0f;         // derece/s
        float MaxMotorTorque = 10000.0f;
        bool UseLimits = false;
        float LowerAngle = 0.0f;         // derece
        float UpperAngle = 359.0f;
        float CurrentAngle = 0.0f;       // çalışma zamanı
    };

    struct SliderJoint2DComponent : Joint2DCommon
    {
        float Angle = 0.0f;              // kayma yönü (derece, yerel)
        bool UseMotor = false;
        float MotorSpeed = 0.0f;
        float MaxMotorForce = 10000.0f;
        bool UseLimits = false;
        float LowerTranslation = 0.0f;
        float UpperTranslation = 1.0f;
        float CurrentTranslation = 0.0f; // çalışma zamanı
    };

    struct WheelJoint2DComponent : Joint2DCommon
    {
        float Angle = 90.0f;             // süspansiyon ekseni (derece)
        float SuspensionFrequency = 2.0f;
        float SuspensionDamping = 0.7f;
        bool UseMotor = false;
        float MotorSpeed = 0.0f;         // derece/s
        float MaxMotorTorque = 10000.0f;
    };

    struct FixedJoint2DComponent : Joint2DCommon
    {
        float Frequency = 0.0f;          // 0 = katı
        float DampingRatio = 0.0f;
    };

    // Unity FrictionJoint2D: iki gövde (ya da gövde ile dünya) arasındaki göreli hareketi kuvvet/tork sınırıyla yavaşlatır
    // (üstten görünüm oyunlarında zemin sürtünmesi)
    struct FrictionJoint2DComponent : Joint2DCommon
    {
        float MaxForce = 1.0f;           // N
        float MaxTorque = 1.0f;          // N·m
    };

    // Unity RelativeJoint2D: iki gövdeyi belirli bir göreli konum ve açıda tutar (yaylı, kuvvet/tork sınırlı)
    struct RelativeJoint2DComponent : Joint2DCommon
    {
        float MaxForce = 10000.0f;
        float MaxTorque = 10000.0f;
        float CorrectionScale = 0.3f;    // 0..1: hatayı düzeltme hızı
        bool AutoConfigureOffset = true; // başlangıçtaki göreli konumu koru
        glm::vec2 LinearOffset{ 0.0f };  // bağlı gövdenin yerel uzayında
        float AngularOffset = 0.0f;      // derece
    };

    // Unity TargetJoint2D: gövdenin bir noktasını dünyadaki hedefe yayla çeker (fareyle sürükleme, kanca)
    struct TargetJoint2DComponent
    {
        glm::vec2 Anchor{ 0.0f };        // yerel
        glm::vec2 Target{ 0.0f };        // dünya (çalışırken değiştirilebilir)
        bool AutoConfigureTarget = true; // başlangıçta hedef = çapanın dünya konumu
        float MaxForce = 10000.0f;
        float DampingRatio = 1.0f;
        float Frequency = 5.0f;          // Hz
        float BreakForce = 0.0f;         // 0 = sonsuz
        bool Broken = false;             // çalışma zamanı
    };

    // Unity CompositeCollider2D: kendisindeki ve alt varlıklarındaki (bir sonraki Rigidbody 2D'ye kadar) Box / Circle /
    // Capsule / Polygon / Tilemap çarpıştırıcılarını tek şekilde birleştirir. Outlines: tek yönlü zincirler (karo
    // kenarlarında takılma olmaz), Polygons: birleşimi örten kutular (tetikleyici / kütleli gövdeler için).
    enum class CompositeGeometryType : int { Outlines = 0, Polygons };

    struct CompositeCollider2DComponent
    {
        CompositeGeometryType GeometryType = CompositeGeometryType::Outlines;
        float VertexDistance = 0.05f;    // dikdörtgen olmayan şekillerde örnekleme aralığı (metre)
        float Density = 1.0f;
        float Friction = 0.4f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        // Çalışma zamanı (bilgi): son birleşimdeki kontur / kutu sayısı
        int OutlineCount = 0;
        int ShapeCount = 0;
    };

    // Unity ConstantForce2D: her fizik adımında sabit kuvvet ve tork
    struct ConstantForce2DComponent
    {
        glm::vec2 Force{ 0.0f };         // dünya uzayında
        glm::vec2 RelativeForce{ 0.0f }; // gövdenin yerel uzayında
        float Torque = 0.0f;
    };

    struct PlatformEffector2DComponent
    {
        bool UseOneWay = true;           // aşağıdan geçilebilir platform
        float SurfaceArc = 180.0f;       // katı yüzey yayı (derece)
        float RotationalOffset = 0.0f;
    };

    struct AreaEffector2DComponent
    {
        float ForceAngle = 0.0f;         // derece
        bool UseGlobalAngle = false;
        float ForceMagnitude = 0.0f;
        float Drag = 0.0f;
        float AngularDrag = 0.0f;
    };

    struct PointEffector2DComponent
    {
        float ForceMagnitude = 0.0f;     // negatif = çekim
        float DistanceScale = 1.0f;
        int ForceMode = 1;               // 0 sabit, 1 ters doğrusal, 2 ters kare
        float Drag = 0.0f;
        float AngularDrag = 0.0f;
    };

    struct SurfaceEffector2DComponent
    {
        float Speed = 1.0f;              // taşıma bandı hızı
    };

    struct BuoyancyEffector2DComponent
    {
        float SurfaceLevel = 0.0f;       // yerel Y
        float Density = 2.0f;
        float LinearDrag = 1.0f;
        float AngularDrag = 1.0f;
        float FlowAngle = 0.0f;
        float FlowMagnitude = 0.0f;
    };
}
