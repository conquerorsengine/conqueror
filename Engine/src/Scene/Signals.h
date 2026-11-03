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

    // Signal Receiver (Unity Timeline Signal Receiver): Signal varlığı (.cqsignal) bu nesneye ulaşınca (Timeline'daki
    // Signal Emitter ya da SignalSend) tepkiler çalışır: hedef nesnenin betik metodu (isteğe bağlı metin argümanıyla).
    struct SignalReaction
    {
        std::string Signal;     // proje göreli .cqsignal
        uint64_t Target = 0;    // hedef nesne UUID'si (0 = bu nesne)
        std::string Method;
        std::string Argument;
    };
    struct SignalReceiverComponent
    {
        std::vector<SignalReaction> Reactions;
    };

    namespace SignalAssets
    {
        constexpr const char* Extension = ".cqsignal";
        CQ_API bool Create(const std::string& path);
        // Nesnedeki Signal Receiver'a sinyal gönder; çalışan tepki sayısı
        CQ_API int Send(Scene& scene, uint32_t receiver, const std::string& signalPath);
        // İki yol aynı sinyal dosyası mı (göreli / mutlak farkı yok sayılır)
        CQ_API bool Same(const std::string& a, const std::string& b);
    }

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
