#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

// Articulation Body (Unity): robot kolu, vinç, araç süspansiyonu gibi birbirine eklemlerle bağlı gövde zinciri. Her bağlantı (link)
// bir nesnedir; hiyerarşide üstteki Articulation Body'ye eklem tipiyle (Fixed / Prismatic / Revolute / Spherical) bağlanır.
// Kök Immovable ise yerinde sabit kalır. Sürücüler (Drive) eklemi hedef konuma / hıza PD kuvvetiyle çeker.
// Oyun başlarken her bağlantı bir gövde ve üst bağlantıya altı serbestlik dereceli eklem olarak kurulur.
namespace Conqueror
{
    class Scene;

    enum class ArticulationJointType : int { Fixed = 0, Prismatic, Revolute, Spherical };
    enum class ArticulationMotion : int { Locked = 0, Limited, Free };
    enum class ArticulationAxis : int { X = 0, Y, Z };

    struct ArticulationDrive
    {
        float LowerLimit = -90.0f;            // derece (prismatic: m)
        float UpperLimit = 90.0f;
        float Stiffness = 0.0f;
        float Damping = 0.0f;
        float ForceLimit = 1.0e6f;
        float Target = 0.0f;                  // derece (prismatic: m)
        float TargetVelocity = 0.0f;          // derece/s (prismatic: m/s)
    };

    struct ArticulationBodyComponent
    {
        float Mass = 1.0f;
        bool Immovable = false;               // yalnız kök: yerinde sabit
        bool UseGravity = true;
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;
        ArticulationJointType JointType = ArticulationJointType::Revolute;
        glm::vec3 AnchorPosition{ 0.0f };     // bu nesnenin yerel uzayında eklem noktası
        glm::vec3 AnchorRotation{ 0.0f };     // derece: eklem çerçevesi (X = dönme / kayma ekseni)
        ArticulationAxis PrismaticAxis = ArticulationAxis::X;
        ArticulationMotion LinearMotion = ArticulationMotion::Limited;   // prismatic
        ArticulationMotion TwistMotion = ArticulationMotion::Limited;    // revolute / spherical X
        ArticulationMotion SwingYMotion = ArticulationMotion::Limited;   // spherical
        ArticulationMotion SwingZMotion = ArticulationMotion::Limited;
        ArticulationDrive XDrive;             // revolute / prismatic / spherical bükülme
        ArticulationDrive YDrive;             // spherical salınım Y
        ArticulationDrive ZDrive;             // spherical salınım Z

        // Çalışma zamanı (bilgi)
        float JointPosition = 0.0f;           // ana serbestlik derecesi: derece (prismatic: m)
        bool IsRoot = false;
    };

    namespace Articulations
    {
        // Oyun başlarken (fizik kurulmadan önce): bağlantılar için gövde ve eklemler
        CQ_API void PrepareRuntime(Scene& scene);
        // Her fizik adımından önce: sürücü hedefleri; sonra: eklem konumları
        CQ_API void PreStep(Scene& scene);
        CQ_API void PostStep(Scene& scene);
    }

    void RegisterArticulationComponents();
}
