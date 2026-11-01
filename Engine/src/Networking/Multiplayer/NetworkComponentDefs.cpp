#include "Scene/ComponentRegistry.h"
#include "NetworkComponents.h"

#include <IconsFontAwesome6.h>

#include <string>

namespace Conqueror
{
    void RegisterNetworkComponents()
    {
        const glm::vec4 kNetColor{ 0.45f, 0.85f, 0.95f, 1.0f };

        ComponentDef<NetworkManagerComponent>("Network Manager", "NetworkManagerComponent", "Network", ICON_FA_TOWER_BROADCAST).Color(kNetColor)
            .Describe("Multiplayer session settings: transport, address, tick rate and the player prefab spawned for every client. "
                      "Start a session from the Multiplayer window, scripts (NetStartHost / NetStartClient) or Auto Start.")
            .Field("Transport", "Transport", &NetworkManagerComponent::Transport).Options({ "UDP (ENet)", "Steam P2P" })
            .Tooltip("Steam P2P needs the Steam client and steam_api next to the game; the address is then the host's SteamID.")
            .Field("Address", "Address", &NetworkManagerComponent::Address)
            .Field("Port", "Port", &NetworkManagerComponent::Port).Range(1.0f, 65535.0f, 1.0f)
            .Field("MaxClients", "Max Clients", &NetworkManagerComponent::MaxClients).Range(1.0f, 256.0f, 1.0f)
            .Field("AutoStart", "Auto Start", &NetworkManagerComponent::AutoStart).Options({ "None", "Host", "Server", "Client" })
            .Tooltip("Starts the session when the game starts. Built games also accept --host, --server and --connect <address>.")
            .Field("TickRate", "Tick Rate", &NetworkManagerComponent::TickRate).Range(1.0f, 128.0f, 0.5f).Format("%.0f Hz").Header("Synchronization")
            .Field("InterpolationDelay", "Interpolation Delay", &NetworkManagerComponent::InterpolationDelay).Range(0.0f, 1.0f, 0.005f).Format("%.3f s")
            .Tooltip("Clients render remote objects this far in the past so movement stays smooth despite jitter.")
            .Field("PlayerPrefab", "Player Prefab", &NetworkManagerComponent::PlayerPrefab, FieldKind::Asset).Filter(".cqprefab").Header("Players")
            .Field("SpawnPoint", "Spawn Point", &NetworkManagerComponent::SpawnPoint)
            .Field("SpawnSpacing", "Spawn Spacing", &NetworkManagerComponent::SpawnSpacing).Range(0.0f, 100.0f, 0.05f);

        ComponentDef<NetworkObjectComponent>("Network Object", "NetworkObjectComponent", "Network", ICON_FA_GLOBE).Color(kNetColor)
            .Describe("Makes this object exist on every machine in a multiplayer session. Scene objects are matched automatically; "
                      "objects spawned at runtime need a prefab.")
            .Field("Prefab", "Prefab", &NetworkObjectComponent::Prefab, FieldKind::Asset).Filter(".cqprefab")
            .Field("DestroyWithOwner", "Destroy With Owner", &NetworkObjectComponent::DestroyWithOwner)
            .Property("NetworkId", "Network ID", FieldKind::String,
                      [](const NetworkObjectComponent& c) { return c.Spawned ? std::to_string(c.NetworkId) : std::string("(not spawned)"); },
                      [](NetworkObjectComponent&, const std::string&) {}).ReadOnly()
            .Property("Owner", "Owner Client", FieldKind::String,
                      [](const NetworkObjectComponent& c) { return c.Spawned ? (c.OwnerClientId == 0 ? std::string("Server / Host") : "Client " + std::to_string(c.OwnerClientId)) : std::string("-"); },
                      [](NetworkObjectComponent&, const std::string&) {}).ReadOnly();

        ComponentDef<NetworkTransformComponent>("Network Transform", "NetworkTransformComponent", "Network", ICON_FA_ARROWS_TO_DOT).Color(kNetColor)
            .Describe("Synchronizes position, rotation and scale. Server authority by default; Owner authority lets the owning client drive it "
                      "(player characters).")
            .Requires("NetworkObjectComponent")
            .Field("SyncPosition", "Sync Position", &NetworkTransformComponent::SyncPosition)
            .Field("SyncRotation", "Sync Rotation", &NetworkTransformComponent::SyncRotation)
            .Field("SyncScale", "Sync Scale", &NetworkTransformComponent::SyncScale)
            .Field("Authority", "Authority", &NetworkTransformComponent::Authority).Options({ "Server", "Owner" })
            .Field("Interpolate", "Interpolate", &NetworkTransformComponent::Interpolate)
            .Field("PositionThreshold", "Position Threshold", &NetworkTransformComponent::PositionThreshold).Range(0.0f, 10.0f, 0.0005f).Header("Bandwidth")
            .Field("RotationThreshold", "Rotation Threshold", &NetworkTransformComponent::RotationThreshold).Range(0.0f, 45.0f, 0.01f).Format("%.2f deg");

        ComponentDef<NetworkRigidbodyComponent>("Network Rigidbody", "NetworkRigidbodyComponent", "Network", ICON_FA_CIRCLE_NODES).Color(kNetColor)
            .Describe("Copies of this body on peers without authority become kinematic and follow the Network Transform instead of simulating.")
            .Requires("NetworkTransformComponent")
            .Field("AutoUpdateKinematicState", "Auto Update Kinematic State", &NetworkRigidbodyComponent::AutoUpdateKinematicState);

        ComponentDef<NetworkAnimatorComponent>("Network Animator", "NetworkAnimatorComponent", "Network", ICON_FA_PERSON_RUNNING).Color(kNetColor)
            .Describe("Synchronizes the Animation (state machine) parameters from the authority to everyone else: floats, ints, bools and triggers.")
            .Requires("NetworkObjectComponent")
            .Field("SyncFloats", "Floats", &NetworkAnimatorComponent::SyncFloats)
            .Field("SyncInts", "Ints", &NetworkAnimatorComponent::SyncInts)
            .Field("SyncBools", "Bools", &NetworkAnimatorComponent::SyncBools)
            .Field("SyncTriggers", "Triggers", &NetworkAnimatorComponent::SyncTriggers)
            .Field("FloatThreshold", "Float Threshold", &NetworkAnimatorComponent::FloatThreshold).Range(0.0f, 10.0f, 0.001f);
    }
}
