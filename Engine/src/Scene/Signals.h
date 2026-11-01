#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Conqueror
{
    class Scene;
    namespace CQS { class Value; }

    // Godot sinyalleri: bir nesne adlandırılmış bir olay yayar, ona bağlanan nesnelerin betik metotları çağrılır.
    // Bağlantılar betikten (SignalConnect) ya da kalıcı olarak Signal Connections bileşeninden kurulur.
    // Motorun kendi yaydığı sinyaller: body_entered / body_exited (tetikleyiciler), collided, pressed (UI düğmesi).
    struct SignalConnectionsComponent
    {
        // Satır başına bir bağlantı: "sinyal -> HedefNesne.Metot" (sonuna " oneshot" eklenebilir; hedef "self" olabilir)
        std::string Connections;

        // Çalışma zamanı
        std::string AppliedFrom;
        bool Applied = false;
    };

    namespace Signals
    {
        CQ_API bool Connect(Scene& scene, uint32_t source, const std::string& signal, uint32_t target, const std::string& method, bool oneShot = false);
        CQ_API bool Disconnect(Scene& scene, uint32_t source, const std::string& signal, uint32_t target, const std::string& method);
        CQ_API bool IsConnected(Scene& scene, uint32_t source, const std::string& signal, uint32_t target, const std::string& method);
        // Bağlı tüm metotları çağır; çağrılan alıcı sayısı
        CQ_API int Emit(Scene& scene, uint32_t source, const std::string& signal, const CQS::Value* args, int argc);
        CQ_API int ConnectionCount(Scene& scene, uint32_t source, const std::string& signal);
        // Kalıcı bağlantıları (bileşen) uygula; silinen varlıkların bağlantılarını temizle
        CQ_API void Update(Scene& scene);
        CQ_API void ForgetScene(Scene* scene);
    }

    void RegisterSignalComponents();
}
