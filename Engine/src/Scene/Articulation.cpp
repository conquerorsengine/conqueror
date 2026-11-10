#include "Articulation.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/PhysicsComponents.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Logging/Log.h"

#include <IconsFontAwesome6.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    namespace
    {
        JointMotion ToMotion(ArticulationMotion m)
        {
            return m == ArticulationMotion::Free ? JointMotion::Free : m == ArticulationMotion::Limited ? JointMotion::Limited : JointMotion::Locked;
        }

        Entity ParentLink(Entity e)
        {
            for (Entity p = e.GetParent(); p; p = p.GetParent())
                if (p.HasComponent<ArticulationBodyComponent>())
                    return p;
            return {};
        }

        // Sürücü: Configurable Joint'in ilgili ekseni (X: doğrusal ya da açısal)
        void CopyTargets(const ArticulationBodyComponent& a, ConfigurableJointComponent& j)
        {
            switch (a.JointType)
            {
                case ArticulationJointType::Prismatic:
                    j.TargetPosition = glm::vec3(a.XDrive.Target, 0.0f, 0.0f);
                    j.TargetVelocity = glm::vec3(a.XDrive.TargetVelocity, 0.0f, 0.0f);
                    break;
                case ArticulationJointType::Revolute:
                    j.TargetRotation = glm::vec3(a.XDrive.Target, 0.0f, 0.0f);
                    j.TargetAngularVelocity = glm::vec3(a.XDrive.TargetVelocity, 0.0f, 0.0f);
                    break;
                case ArticulationJointType::Spherical:
                    j.TargetRotation = glm::vec3(a.XDrive.Target, a.YDrive.Target, a.ZDrive.Target);
                    j.TargetAngularVelocity = glm::vec3(a.XDrive.TargetVelocity, a.YDrive.TargetVelocity, a.ZDrive.TargetVelocity);
                    break;
                default:
                    break;
            }
        }
    }

    namespace Articulations
    {
        void PrepareRuntime(Scene& scene)
        {
            auto& reg = scene.m_Registry;
            std::vector<entt::entity> links;
            for (auto e : reg.view<ArticulationBodyComponent>())
                links.push_back(e);
            for (auto h : links)
            {
                Entity e{ h, &scene };
                auto& a = e.GetComponent<ArticulationBodyComponent>();
                Entity parent = ParentLink(e);
                a.IsRoot = !parent;
                // Gövde (Unity'de Articulation Body ile Rigidbody aynı nesnede olamaz: varsa onun ayarları kullanılır)
                if (e.HasComponent<RigidbodyComponent>())
                    CQ_CORE_WARN("[Physics] '{0}' has both Articulation Body and Rigidbody; the Rigidbody settings are used.", e.GetComponent<TagComponent>().Tag);
                else
                {
                    auto& rb = e.AddComponent<RigidbodyComponent>();
                    rb.Type = (a.IsRoot && a.Immovable) ? RigidbodyComponent::BodyType::Kinematic : RigidbodyComponent::BodyType::Dynamic;
                    rb.Mass = std::max(a.Mass, 1e-4f);
                    rb.UseGravity = a.UseGravity;
                    rb.LinearDrag = a.LinearDamping;
                    rb.AngularDrag = a.AngularDamping;
                }
                if (a.IsRoot || e.HasComponent<ConfigurableJointComponent>())
                    continue;
                // Üst bağlantıya eklem: eklem çerçevesinin X'i dönme / kayma ekseni
                auto& j = e.AddComponent<ConfigurableJointComponent>();
                j.ConnectedBody = (uint64_t)parent.GetComponent<IDComponent>().ID;
                j.Anchor = a.AnchorPosition;
                const glm::quat r(glm::radians(a.AnchorRotation));
                glm::vec3 axis = r * glm::vec3(1, 0, 0), secondary = r * glm::vec3(0, 1, 0);
                if (a.JointType == ArticulationJointType::Prismatic && a.PrismaticAxis != ArticulationAxis::X)
                {
                    // Kayma ekseni Y ya da Z: eklemin X'i o eksene döner
                    axis = r * (a.PrismaticAxis == ArticulationAxis::Y ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1));
                    secondary = r * (a.PrismaticAxis == ArticulationAxis::Y ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0));
                }
                j.Axis = axis;
                j.SecondaryAxis = secondary;
                j.XMotion = j.YMotion = j.ZMotion = JointMotion::Locked;
                j.AngularXMotion = j.AngularYMotion = j.AngularZMotion = JointMotion::Locked;
                switch (a.JointType)
                {
                    case ArticulationJointType::Prismatic:
                        j.XMotion = ToMotion(a.LinearMotion);
                        // Simetrik sınır (alt / üst sınırın büyüğü)
                        j.LinearLimit = std::max(std::abs(a.XDrive.LowerLimit), std::abs(a.XDrive.UpperLimit));
                        j.XDriveSpring = a.XDrive.Stiffness;
                        j.XDriveDamper = a.XDrive.Damping;
                        j.XDriveMaxForce = a.XDrive.ForceLimit;
                        break;
                    case ArticulationJointType::Revolute:
                        j.AngularXMotion = ToMotion(a.TwistMotion);
                        j.LowAngularXLimit = a.XDrive.LowerLimit;
                        j.HighAngularXLimit = a.XDrive.UpperLimit;
                        j.AngularXDriveSpring = a.XDrive.Stiffness;
                        j.AngularXDriveDamper = a.XDrive.Damping;
                        j.AngularXDriveMaxForce = a.XDrive.ForceLimit;
                        break;
                    case ArticulationJointType::Spherical:
                        j.AngularXMotion = ToMotion(a.TwistMotion);
                        j.AngularYMotion = ToMotion(a.SwingYMotion);
                        j.AngularZMotion = ToMotion(a.SwingZMotion);
                        j.LowAngularXLimit = a.XDrive.LowerLimit;
                        j.HighAngularXLimit = a.XDrive.UpperLimit;
                        j.AngularYLimit = std::max(std::abs(a.YDrive.LowerLimit), std::abs(a.YDrive.UpperLimit));
                        j.AngularZLimit = std::max(std::abs(a.ZDrive.LowerLimit), std::abs(a.ZDrive.UpperLimit));
                        j.AngularXDriveSpring = a.XDrive.Stiffness;
                        j.AngularXDriveDamper = a.XDrive.Damping;
                        j.AngularXDriveMaxForce = a.XDrive.ForceLimit;
                        j.AngularYZDriveSpring = std::max(a.YDrive.Stiffness, a.ZDrive.Stiffness);
                        j.AngularYZDriveDamper = std::max(a.YDrive.Damping, a.ZDrive.Damping);
                        j.AngularYZDriveMaxForce = std::max(a.YDrive.ForceLimit, a.ZDrive.ForceLimit);
                        break;
                    default:
                        break;   // Fixed: hepsi kilitli
                }
                CopyTargets(a, j);
            }
        }

        void PreStep(Scene& scene)
        {
            auto view = scene.m_Registry.view<ArticulationBodyComponent, ConfigurableJointComponent>();
            for (auto e : view)
                CopyTargets(view.get<ArticulationBodyComponent>(e), view.get<ConfigurableJointComponent>(e));
        }

        void PostStep(Scene& scene)
        {
            auto view = scene.m_Registry.view<ArticulationBodyComponent, ConfigurableJointComponent>();
            for (auto e : view)
            {
                auto& a = view.get<ArticulationBodyComponent>(e);
                const auto& j = view.get<ConfigurableJointComponent>(e);
                a.JointPosition = a.JointType == ArticulationJointType::Prismatic ? j.CurrentPosition.x : j.CurrentRotation.x;
            }
        }
    }

    template<> void Scene::OnComponentAdded<ArticulationBodyComponent>(Entity, ArticulationBodyComponent&) {}

    void RegisterArticulationComponents()
    {
        using A = ArticulationBodyComponent;
        auto drive = [](ComponentDef<A>& def, const char* prefix, const char* header, ArticulationDrive A::*member, std::function<bool(const A&)> visible) {
            const std::string p(prefix);
            def.FieldVia((p + "LowerLimit").c_str(), "Lower Limit", [member](A& a) -> float& { return (a.*member).LowerLimit; }).Speed(0.5f).Header(header).VisibleIf(visible)
               .FieldVia((p + "UpperLimit").c_str(), "Upper Limit", [member](A& a) -> float& { return (a.*member).UpperLimit; }).Speed(0.5f).VisibleIf(visible)
               .FieldVia((p + "Stiffness").c_str(), "Stiffness", [member](A& a) -> float& { return (a.*member).Stiffness; }).Range(0.0f, 1.0e7f, 1.0f).VisibleIf(visible)
               .FieldVia((p + "Damping").c_str(), "Damping", [member](A& a) -> float& { return (a.*member).Damping; }).Range(0.0f, 1.0e7f, 0.5f).VisibleIf(visible)
               .FieldVia((p + "ForceLimit").c_str(), "Force Limit", [member](A& a) -> float& { return (a.*member).ForceLimit; }).Range(0.0f, 1.0e9f, 10.0f).VisibleIf(visible)
               .FieldVia((p + "Target").c_str(), "Target", [member](A& a) -> float& { return (a.*member).Target; }).Speed(0.5f).VisibleIf(visible)
               .FieldVia((p + "TargetVelocity").c_str(), "Target Velocity", [member](A& a) -> float& { return (a.*member).TargetVelocity; }).Speed(0.5f).VisibleIf(visible);
        };
        ComponentDef<A> def("Articulation Body", "ArticulationBodyComponent", "Physics", ICON_FA_ROBOT);
        def.Color({ 0.45f, 0.85f, 0.55f, 1.0f })
            .Describe("A link of a jointed chain (robot arm, crane): nest links in the hierarchy; each one joins the Articulation Body above it with "
                      "the chosen joint. The top one is the root (Immovable keeps it in place). Drives pull a joint towards Target with "
                      "Stiffness and Damping. Add colliders to the links for collisions.")
            .Field("Mass", "Mass", &A::Mass).Range(0.0001f, 1.0e6f, 0.1f)
            .Field("Immovable", "Immovable", &A::Immovable).Tooltip("Root only: the root stays fixed in the world.")
            .Field("UseGravity", "Use Gravity", &A::UseGravity)
            .Field("LinearDamping", "Linear Damping", &A::LinearDamping).Range(0.0f, 100.0f, 0.01f)
            .Field("AngularDamping", "Angular Damping", &A::AngularDamping).Range(0.0f, 100.0f, 0.01f)
            .Field("JointType", "Articulation Joint Type", &A::JointType).Options({ "Fixed", "Prismatic", "Revolute", "Spherical" }).Header("Joint")
                .Tooltip("Ignored on the root.")
            .Field("AnchorPosition", "Anchor Position", &A::AnchorPosition).Speed(0.01f)
            .Field("AnchorRotation", "Anchor Rotation", &A::AnchorRotation).Speed(0.5f).Tooltip("Joint frame: revolute turns and prismatic slides along its X.")
            .Field("PrismaticAxis", "Prismatic Axis", &A::PrismaticAxis).Options({ "X", "Y", "Z" })
                .VisibleIf([](const A& a) { return a.JointType == ArticulationJointType::Prismatic; })
            .Field("LinearMotion", "Linear Motion", &A::LinearMotion).Options({ "Locked", "Limited", "Free" })
                .VisibleIf([](const A& a) { return a.JointType == ArticulationJointType::Prismatic; })
            .Field("TwistMotion", "Twist Motion", &A::TwistMotion).Options({ "Locked", "Limited", "Free" })
                .VisibleIf([](const A& a) { return a.JointType == ArticulationJointType::Revolute || a.JointType == ArticulationJointType::Spherical; })
            .Field("SwingYMotion", "Swing Y Motion", &A::SwingYMotion).Options({ "Locked", "Limited", "Free" })
                .VisibleIf([](const A& a) { return a.JointType == ArticulationJointType::Spherical; })
            .Field("SwingZMotion", "Swing Z Motion", &A::SwingZMotion).Options({ "Locked", "Limited", "Free" })
                .VisibleIf([](const A& a) { return a.JointType == ArticulationJointType::Spherical; });
        auto moving = [](const A& a) { return a.JointType != ArticulationJointType::Fixed; };
        auto spherical = [](const A& a) { return a.JointType == ArticulationJointType::Spherical; };
        drive(def, "XDrive", "X Drive", &A::XDrive, moving);
        drive(def, "YDrive", "Y Drive", &A::YDrive, spherical);
        drive(def, "ZDrive", "Z Drive", &A::ZDrive, spherical);
        def.Field("JointPosition", "Joint Position", &A::JointPosition).ReadOnly().Header("Runtime");
    }
}
