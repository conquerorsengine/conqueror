#pragma once

#include "Scene/Entity.h"

#include <string>

namespace Conqueror::Editor::VolumeProfileInspector
{
    // Volume Profile varlığı (.cqvolume) Inspector'ı
    void DrawAsset(const std::string& path);
    // Post Process Volume bileşeninin altı: Profile New / Clone ve profil ayarlarının yerinde düzenlenmesi (Unity Volume)
    void DrawVolumeExtra(Entity entity);
}
