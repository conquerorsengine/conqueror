#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

// Unity "Skybox" bileşeni: kameraya özel gökyüzü. Yalnızca o kameranın arka planını değiştirir;
// ortam ışığı ve yansımalar sahnenin gökyüzünden gelmeye devam eder.
namespace Conqueror
{
    class Cubemap;

    struct CameraSkyboxComponent
    {
        std::string Skybox;               // HDR / panorama görüntü (equirectangular)
        float Exposure = 1.0f;
        glm::vec3 Tint{ 1.0f };
        // Çalışma zamanı
        std::shared_ptr<Cubemap> Loaded;
        std::string LoadedSkybox;
    };

    namespace CameraSkybox
    {
        // Yüklü küp haritası (yol değişince yeniden yüklenir); yoksa nullptr
        CQ_API std::shared_ptr<Cubemap> Resolve(CameraSkyboxComponent& component);
    }

    void RegisterCameraSkyboxComponents();
}
