#pragma once

#include "Scene/OcclusionCulling.h"

#include <string>

namespace Conqueror { class Scene; }

namespace Conqueror::Editor
{

    // Window > Occlusion Culling (Unity): pişirme ayarları, Bake / Clear, görselleştirme
    class OcclusionPanel
    {
    public:
        void SetContext(Scene* scene) { m_Context = scene; }
        void OnImGuiRender(bool* open);
        // Her kare (pencere kapalı olsa da): süren pişirmeyi ilerletir
        void Tick();
        bool StartBake(std::string& error);
        void Clear();
        bool IsBaking() const { return m_Baker.IsRunning(); }
        OcclusionBakeSettings& Settings() { return m_Settings; }
        const std::string& Status() const { return m_Status; }

    private:
        Scene* m_Context = nullptr;
        Scene* m_BakeScene = nullptr;
        OcclusionBaker m_Baker;
        OcclusionBakeSettings m_Settings;
        std::string m_Status;
    };
}
