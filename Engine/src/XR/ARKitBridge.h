#pragma once

#include "XR/ARCore.h"

#include <string>
#include <vector>

// iOS / iPadOS: ARCore API'sinin (XR/ARCore.h) Apple ARKit arka ucu (Platform/iOS/ARKitSession.mm). Betikler ve sahne aynı AR
// işlevlerini kullanır (ARCameraPose, ARPlanes, ARRaycast, kamera görüntüsü arka planda); Unity AR Foundation'ın ARKit sağlayıcısı gibi.
// Yalnız iOS derlemesinde vardır
namespace Conqueror::ARKitBridge
{
    // Dünya izleme + yatay / dikey düzlem algılama oturumu. Cihaz desteklemiyorsa false (state "Unavailable", error nedeni)
    bool Start(std::string& state, std::string& error);
    void Stop();
    // Her kare (ana iş parçacığı, GL bağlamı): yeni ARKit karesi varsa poz, görüş açısı, düzlemler ve kamera görüntüsü
    void Update(std::string& state, std::string& error, std::vector<ARCore::Plane>& planes);
    bool IsRunning();
    bool CameraPose(glm::vec3& position, glm::quat& rotation);
    float CameraFovY();
    bool Raycast(float pixelX, float pixelY, ARCore::HitResult& out);
    uint32_t BackgroundTexture();
    uint64_t FramesUpdated();
}
