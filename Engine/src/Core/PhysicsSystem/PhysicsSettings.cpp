#include "PhysicsSettings.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>

namespace Conqueror
{
    PhysicsSettings& PhysicsSettings::Get()
    {
        static PhysicsSettings s_Settings;
        return s_Settings;
    }

    const char* PhysicsBackendName(PhysicsBackend3D backend)
    {
        switch (backend)
        {
            case PhysicsBackend3D::Jolt: return "Jolt";
            case PhysicsBackend3D::MuJoCo: return "MuJoCo";
        }
        return "Unknown";
    }

    void PhysicsSettings::Serialize(YAML::Emitter& out) const
    {
        out << YAML::Key << "Physics" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Backend" << YAML::Value << PhysicsBackendName(Backend);
        out << YAML::Key << "Gravity" << YAML::Value << YAML::Flow << YAML::BeginSeq << Gravity.x << Gravity.y << Gravity.z << YAML::EndSeq;
        out << YAML::Key << "Gravity2D" << YAML::Value << YAML::Flow << YAML::BeginSeq << Gravity2D.x << Gravity2D.y << YAML::EndSeq;
        out << YAML::Key << "FixedTimestep" << YAML::Value << FixedTimestep;
        out << YAML::Key << "MaxSubSteps" << YAML::Value << MaxSubSteps;
        out << YAML::Key << "CollisionSteps" << YAML::Value << CollisionSteps;
        out << YAML::Key << "VelocityIterations" << YAML::Value << VelocityIterations;
        out << YAML::Key << "PositionIterations" << YAML::Value << PositionIterations;
        out << YAML::Key << "Interpolation" << YAML::Value << Interpolation;
        out << YAML::Key << "DefaultFriction" << YAML::Value << DefaultFriction;
        out << YAML::Key << "DefaultRestitution" << YAML::Value << DefaultRestitution;
        out << YAML::Key << "SleepThreshold" << YAML::Value << SleepThreshold;
        out << YAML::Key << "QueriesHitTriggers" << YAML::Value << QueriesHitTriggers;
        out << YAML::Key << "AutoSyncTransforms" << YAML::Value << AutoSyncTransforms;
        out << YAML::Key << "CollisionMatrix" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (uint32_t row : CollisionMatrix)
            out << row;
        out << YAML::EndSeq;
        out << YAML::Key << "MuJoCoLibraryPath" << YAML::Value << MuJoCoLibraryPath;
        out << YAML::Key << "MuJoCoIntegrator" << YAML::Value << (int)Integrator;
        out << YAML::Key << "MuJoCoIterations" << YAML::Value << MuJoCoIterations;
        out << YAML::Key << "MuJoCoTimestepScale" << YAML::Value << MuJoCoTimestepScale;
        out << YAML::EndMap;
    }

    void PhysicsSettings::Deserialize(const YAML::Node& node)
    {
        *this = PhysicsSettings();
        if (!node || !node.IsMap())
            return;
        try
        {
            if (node["Backend"])
                Backend = node["Backend"].as<std::string>() == "MuJoCo" ? PhysicsBackend3D::MuJoCo : PhysicsBackend3D::Jolt;
            if (auto g = node["Gravity"]; g && g.IsSequence() && g.size() == 3)
                Gravity = { g[0].as<float>(), g[1].as<float>(), g[2].as<float>() };
            if (auto g = node["Gravity2D"]; g && g.IsSequence() && g.size() == 2)
                Gravity2D = { g[0].as<float>(), g[1].as<float>() };
#define CQ_PHYS_IN(key, T) if (node[#key]) key = node[#key].as<T>()
            CQ_PHYS_IN(FixedTimestep, float);
            CQ_PHYS_IN(MaxSubSteps, int);
            CQ_PHYS_IN(CollisionSteps, int);
            CQ_PHYS_IN(VelocityIterations, int);
            CQ_PHYS_IN(PositionIterations, int);
            CQ_PHYS_IN(Interpolation, bool);
            CQ_PHYS_IN(DefaultFriction, float);
            CQ_PHYS_IN(DefaultRestitution, float);
            CQ_PHYS_IN(SleepThreshold, float);
            CQ_PHYS_IN(QueriesHitTriggers, bool);
            CQ_PHYS_IN(AutoSyncTransforms, bool);
            CQ_PHYS_IN(MuJoCoLibraryPath, std::string);
            CQ_PHYS_IN(MuJoCoIterations, int);
            CQ_PHYS_IN(MuJoCoTimestepScale, float);
#undef CQ_PHYS_IN
            if (node["MuJoCoIntegrator"])
                Integrator = (MuJoCoIntegrator)std::clamp(node["MuJoCoIntegrator"].as<int>(), 0, 3);
            if (auto m = node["CollisionMatrix"]; m && m.IsSequence())
                for (size_t i = 0; i < std::min<size_t>(m.size(), MaxLayers); ++i)
                    CollisionMatrix[i] = m[i].as<uint32_t>();
        }
        catch (const YAML::Exception&)
        {
            // Bozuk değerler varsayılanlarda kalır
        }
        FixedTimestep = std::clamp(FixedTimestep, 1.0f / 1000.0f, 0.25f);
        MaxSubSteps = std::clamp(MaxSubSteps, 1, 64);
        CollisionSteps = std::clamp(CollisionSteps, 1, 8);
        VelocityIterations = std::clamp(VelocityIterations, 1, 100);
        PositionIterations = std::clamp(PositionIterations, 1, 100);
    }
}
