#include "AISystem/Components/NavComponents.h"
#include "AISystem/Navigation/NavMesh.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/ScenePicking.h"
#include "Scene/Entity.h"

#include <IconsFontAwesome6.h>

namespace Conqueror
{
    void RegisterNavigationComponents()
    {
        const std::vector<std::string> areas = { "Walkable", "Not Walkable", "Jump", "User 3", "User 4", "User 5", "User 6", "User 7" };

        using A = NavMeshAgentComponent;
        ComponentDef<A>("NavMesh Agent", "NavMeshAgentComponent", "AI & Navigation", ICON_FA_PERSON_WALKING)
            .Describe("Moves along the baked NavMesh to a destination: acceleration, braking, turning, links (jumps) and avoidance of other agents.")
            .Field("Speed", "Speed", &A::Speed).Range(0.0f, 100.0f, 0.05f)
            .Field("AngularSpeed", "Angular Speed", &A::AngularSpeed).Range(0.0f, 2000.0f, 1.0f).Format("%.0f°/s")
            .Field("Acceleration", "Acceleration", &A::Acceleration).Range(0.0f, 1000.0f, 0.1f)
            .Field("StoppingDistance", "Stopping Distance", &A::StoppingDistance).Range(0.0f, 100.0f, 0.01f)
            .Field("AutoBraking", "Auto Braking", &A::AutoBraking)
            .Field("Radius", "Radius", &A::Radius).Range(0.01f, 100.0f, 0.01f).Header("Obstacle Avoidance")
            .Field("Height", "Height", &A::Height).Range(0.01f, 100.0f, 0.01f)
            .Field("BaseOffset", "Base Offset", &A::BaseOffset).Speed(0.01f)
            .Field("AvoidOtherAgents", "Avoid Other Agents", &A::AvoidOtherAgents)
            .Field("AvoidancePriority", "Priority", &A::AvoidancePriority).Range(0.0f, 99.0f, 1.0f)
            .Field("AutoTraverseLinks", "Auto Traverse Links", &A::AutoTraverseLinks).Header("Path Finding")
            .Field("AreaMask", "Area Mask", &A::AreaMask)
            .Field("IsStopped", "Is Stopped", &A::IsStopped)
            .Field("RemainingDistance", "Remaining Distance", &A::RemainingDistance).ReadOnly()
            .Field("HasPath", "Has Path", &A::HasPath).ReadOnly();

        using O = NavMeshObstacleComponent;
        ComponentDef<O>("NavMesh Obstacle", "NavMeshObstacleComponent", "AI & Navigation", ICON_FA_BAN)
            .Describe("A moving obstacle for agents. Carve cuts a hole in the NavMesh so agents path around it.")
            .OnAddedInEditor([](Entity e) {
                // Unity gibi: modelin / mesh'in sınırlarına otur
                glm::vec3 mn, mx;
                if (ScenePicking::GetLocalBounds(e, mn, mx))
                {
                    auto& o = e.GetComponent<NavMeshObstacleComponent>();
                    o.Center = (mn + mx) * 0.5f;
                    o.Size = glm::max(mx - mn, glm::vec3(0.01f));
                }
            })
            .Field("Carve", "Carve", &O::Carve)
            .Field("Center", "Center", &O::Center).Speed(0.01f)
            .Field("Size", "Size", &O::Size).Speed(0.01f)
            .Field("Radius", "Radius", &O::Radius).Range(0.0f, 100.0f, 0.01f);

        using S = NavMeshSurfaceComponent;
        ComponentDef<S>("NavMesh Surface", "NavMeshSurfaceComponent", "AI & Navigation", ICON_FA_MAP)
            .Describe("Bakes the walkable NavMesh from the scene geometry (voxelized: slopes, steps, ceilings and agent radius). Bake from Window > Navigation or automatically on Play.")
            .Field("CollectObjects", "Collect Objects", &S::CollectObjects).Options({ "All Game Objects", "Children" })
            .Field("AreaType", "Default Area", &S::AreaType).Options(areas)
            .Field("AgentRadius", "Agent Radius", &S::AgentRadius).Range(0.0f, 50.0f, 0.01f).Header("Agent")
            .Field("AgentHeight", "Agent Height", &S::AgentHeight).Range(0.1f, 100.0f, 0.01f)
            .Field("MaxSlope", "Max Slope", &S::MaxSlope).Range(0.0f, 89.0f, 0.5f).Format("%.1f°")
            .Field("StepHeight", "Step Height", &S::StepHeight).Range(0.0f, 10.0f, 0.01f)
            .Field("OverrideVoxelSize", "Override Voxel Size", &S::OverrideVoxelSize).Header("Advanced")
            .Field("VoxelSize", "Voxel Size", &S::VoxelSize).Range(0.02f, 4.0f, 0.005f).VisibleIf([](const S& s) { return s.OverrideVoxelSize; })
            .Field("BakeOnPlay", "Bake On Play", &S::BakeOnPlay);

        using M = NavMeshModifierComponent;
        ComponentDef<M>("NavMesh Modifier", "NavMeshModifierComponent", "AI & Navigation", ICON_FA_PEN_RULER)
            .Describe("Changes the area of this object's geometry (and its children) in the NavMesh, or leaves it out of the bake.")
            .Field("IgnoreFromBuild", "Ignore From Build", &M::IgnoreFromBuild)
            .Field("OverrideArea", "Override Area", &M::OverrideArea)
            .Field("Area", "Area Type", &M::Area).Options(areas).VisibleIf([](const M& m) { return m.OverrideArea; })
            .Field("ApplyToChildren", "Apply To Children", &M::ApplyToChildren);

        using V = NavMeshModifierVolumeComponent;
        ComponentDef<V>("NavMesh Modifier Volume", "NavMeshModifierVolumeComponent", "AI & Navigation", ICON_FA_CUBE)
            .Describe("Marks the NavMesh inside this box with an area: water and mud (expensive), forbidden zones (Not Walkable).")
            .Field("Size", "Size", &V::Size).Speed(0.01f)
            .Field("Center", "Center", &V::Center).Speed(0.01f)
            .Field("Area", "Area Type", &V::Area).Options(areas);

        using L = NavMeshLinkComponent;
        ComponentDef<L>("NavMesh Link", "NavMeshLinkComponent", "AI & Navigation", ICON_FA_LINK)
            .Describe("Connects two points of the NavMesh (gaps, ledges, ladders); agents traverse it automatically, jumping on Jump links.")
            .Field("StartPoint", "Start Point", &L::StartPoint).Speed(0.01f)
            .Field("EndPoint", "End Point", &L::EndPoint).Speed(0.01f)
            .Field("Width", "Width", &L::Width).Range(0.0f, 100.0f, 0.01f)
            .Field("Bidirectional", "Bidirectional", &L::Bidirectional)
            .Field("CostModifier", "Cost Modifier", &L::CostModifier).Range(-1.0f, 1000.0f, 0.01f).Tooltip("-1 = use the area cost.")
            .Field("Area", "Area Type", &L::Area).Options(areas);
    }
}
