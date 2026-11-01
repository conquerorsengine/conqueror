#include "Scene/ComponentRegistry.h"
#include "TimelineSystem.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

namespace Conqueror
{
    void RegisterTimelineComponents()
    {
        ComponentDef<PlayableDirectorComponent>("Playable Director", "PlayableDirectorComponent", "Animation", ICON_FA_FILM)
            .Color({ 0.55f, 0.85f, 0.65f, 1.0f })
            .Describe("Plays a Timeline (.cqtimeline): cutscenes, camera moves, activation, property and audio sequences. "
                      "Edit it in Window > Timeline; scripts use TimelinePlay / TimelinePause / TimelineSetTime.")
            .Field("Timeline", "Timeline", &PlayableDirectorComponent::Timeline, FieldKind::Asset).Filter(".cqtimeline")
            .Field("PlayOnAwake", "Play On Awake", &PlayableDirectorComponent::PlayOnAwake)
            .Field("WrapMode", "Wrap Mode", &PlayableDirectorComponent::WrapMode).Options({ "Hold", "Loop", "None" })
            .Field("Speed", "Speed", &PlayableDirectorComponent::Speed).Range(-10.0f, 10.0f, 0.01f)
            // Parça bağlantıları: parça sırasına göre varlık UUID'leri
            .CustomData(
                [](YAML::Emitter& out, PlayableDirectorComponent& d) {
                    out << YAML::Key << "Bindings" << YAML::Value << YAML::Flow << YAML::BeginSeq;
                    for (uint64_t id : d.Bindings)
                        out << id;
                    out << YAML::EndSeq;
                },
                [](const YAML::Node& node, PlayableDirectorComponent& d) {
                    d.Bindings.clear();
                    if (auto b = node["Bindings"]; b && b.IsSequence())
                        for (const auto& id : b)
                            d.Bindings.push_back(id.as<uint64_t>(0));
                });
    }
}
