#include "SoundCue.h"

#include "Core/Assets/Graph/NodeAsset.h"
#include "Core/Audio/AudioOneShots.h"
#include "Core/Serialization/SceneSerializer.h"

#include <algorithm>
#include <random>
#include <unordered_map>
#include <vector>

namespace Conqueror::SoundCues
{
    namespace
    {
        struct Voice
        {
            std::string Clip;
            float Volume = 1.0f, Pitch = 1.0f, Delay = 0.0f;
            bool Loop = false;
        };

        struct Pending
        {
            Voice V;
            float Remaining = 0.0f;
        };

        std::vector<Pending>& PendingVoices()
        {
            static std::vector<Pending> p;
            return p;
        }

        std::mt19937& Rng()
        {
            static std::mt19937 rng(std::random_device{}());
            return rng;
        }

        float Uniform(float a, float b)
        {
            return a >= b ? a : std::uniform_real_distribution<float>(a, b)(Rng());
        }

        struct OwnerState
        {
            std::unordered_map<int, int> LastPick;    // Random: son seçilen giriş
            std::unordered_map<int, int> Next;        // Sequence: sıradaki giriş
        };

        std::unordered_map<uint64_t, OwnerState>& States()
        {
            static std::unordered_map<uint64_t, OwnerState> s;
            return s;
        }

        void Evaluate(const NodeAssets::File& file, const NodeAssets::Node& node, OwnerState& state, std::vector<Voice>& out, int depth);

        void Follow(const NodeAssets::File& file, int nodeId, int pin, OwnerState& state, std::vector<Voice>& out, int depth)
        {
            for (const auto* link : file.In(nodeId, pin))
                if (const auto* src = file.Find(link->From))
                {
                    Evaluate(file, *src, state, out, depth + 1);
                    return;
                }
        }

        void Evaluate(const NodeAssets::File& file, const NodeAssets::Node& node, OwnerState& state, std::vector<Voice>& out, int depth)
        {
            if (depth > 64)
                return;
            if (node.Type == "wave")
            {
                const std::string clip = node.Str("Clip");
                if (clip.empty())
                    return;
                Voice v;
                v.Clip = clip;
                v.Loop = node.Bool("Looping");
                out.push_back(std::move(v));
                return;
            }
            if (node.Type == "random" || node.Type == "sequence")
            {
                std::vector<int> pins;
                for (int pin = 0; pin < 4; ++pin)
                    if (!file.In(node.Id, pin).empty())
                        pins.push_back(pin);
                if (pins.empty())
                    return;
                int chosen = pins.front();
                if (node.Type == "sequence")
                {
                    int& next = state.Next[node.Id];
                    chosen = pins[(size_t)next % pins.size()];
                    next = (next + 1) % (int)pins.size();
                }
                else
                {
                    static const char* kWeights[4] = { "Weight A", "Weight B", "Weight C", "Weight D" };
                    int& last = state.LastPick.try_emplace(node.Id, -1).first->second;
                    std::vector<float> weights;
                    float total = 0.0f;
                    for (int pin : pins)
                    {
                        const float w = (node.Bool("No Repeats") && pin == last && pins.size() > 1) ? 0.0f : std::max(0.0f, node.Float(kWeights[pin], 1.0f));
                        weights.push_back(w);
                        total += w;
                    }
                    float roll = Uniform(0.0f, total);
                    for (size_t i = 0; i < pins.size(); ++i)
                    {
                        chosen = pins[i];
                        roll -= weights[i];
                        if (roll <= 0.0f && weights[i] > 0.0f)
                            break;
                    }
                    last = chosen;
                }
                Follow(file, node.Id, chosen, state, out, depth);
                return;
            }
            if (node.Type == "mixer")
            {
                static const char* kVolumes[4] = { "Volume A", "Volume B", "Volume C", "Volume D" };
                for (int pin = 0; pin < 4; ++pin)
                {
                    const size_t before = out.size();
                    Follow(file, node.Id, pin, state, out, depth);
                    for (size_t i = before; i < out.size(); ++i)
                        out[i].Volume *= node.Float(kVolumes[pin], 1.0f);
                }
                return;
            }
            const size_t before = out.size();
            Follow(file, node.Id, 0, state, out, depth);
            if (node.Type == "modulator")
            {
                const float volume = Uniform(node.Float("Volume Min", 1.0f), node.Float("Volume Max", 1.0f));
                const float pitch = Uniform(node.Float("Pitch Min", 1.0f), node.Float("Pitch Max", 1.0f));
                for (size_t i = before; i < out.size(); ++i)
                {
                    out[i].Volume *= volume;
                    out[i].Pitch *= pitch;
                }
            }
            else if (node.Type == "delay")
            {
                const float delay = Uniform(node.Float("Min", 0.0f), node.Float("Max", 0.0f));
                for (size_t i = before; i < out.size(); ++i)
                    out[i].Delay += delay;
            }
            else if (node.Type == "loop")
            {
                for (size_t i = before; i < out.size(); ++i)
                    out[i].Loop = true;
            }
            else if (node.Type == "output")
            {
                for (size_t i = before; i < out.size(); ++i)
                {
                    out[i].Volume *= node.Float("Volume", 1.0f);
                    out[i].Pitch *= node.Float("Pitch", 1.0f);
                }
            }
        }
    }

    bool Next(const std::string& path, uint64_t owner, AudioRandomContainers::Pick& out)
    {
        NodeAssets::File file;
        if (!NodeAssets::Load(path, file))
            return false;
        const NodeAssets::Node* output = nullptr;
        for (const auto& n : file.Nodes)
            if (n.Type == "output")
                output = &n;
        if (!output)
            return false;
        std::vector<Voice> voices;
        Evaluate(file, *output, States()[owner], voices, 0);
        bool main = false;
        for (const auto& v : voices)
        {
            if (!main && v.Delay <= 0.0f)
            {
                out.Clip = v.Clip;
                out.VolumeScale = v.Volume;
                out.PitchScale = v.Pitch;
                out.Loop = v.Loop;
                main = true;
                continue;
            }
            PendingVoices().push_back({ v, v.Delay });
        }
        return main;
    }

    void UpdatePending(float dt)
    {
        auto& pending = PendingVoices();
        for (auto& p : pending)
        {
            p.Remaining -= dt;
            if (p.Remaining <= 0.0f)
                AudioOneShots::Play(SceneSerializer::ResolveAssetPath(p.V.Clip), p.V.Volume, p.V.Pitch);
        }
        pending.erase(std::remove_if(pending.begin(), pending.end(), [](const Pending& p) { return p.Remaining <= 0.0f; }), pending.end());
    }

    void Reset()
    {
        States().clear();
        PendingVoices().clear();
    }
}
