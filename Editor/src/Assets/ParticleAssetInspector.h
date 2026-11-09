#pragma once

#include <string>

namespace Conqueror::Editor::ParticleAssetInspector
{
    // Particle System varlığı (.cqparticle): Particle System bileşeniyle aynı ayarlar
    bool IsParticleAsset(const std::string& path);
    void Draw(const std::string& path);
}
