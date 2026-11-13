#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

// Window (Godot Window düğümü; Unity'de çoklu ekran Display.Activate yalnız ikinci monitöre tam ekran): oyunda ayrı bir işletim
// sistemi penceresi. Camera'daki kamera bu pencereye çizer (kameraya Render Target eklenir, doku pencerede gösterilir); pencere
// taşınır, boyutlanır, kapatma düğmesi close_requested sinyalini yayar ve betiklerde OnCloseRequested çağrılır. OpenGL bağlamı ana
// pencereyle paylaşılır. Düzenleyicide Play'de de gerçek pencere açılır, oyun durunca kapanır.
namespace Conqueror
{
    class Scene;

    struct GameWindowComponent
    {
        std::string Title = "Window";
        glm::vec2 Size{ 640.0f, 360.0f };
        glm::vec2 Position{ 120.0f, 120.0f };   // ekran koordinatı (sistem izin verirse; Wayland pencere yerini kendisi seçer)
        bool Visible = true;
        bool Borderless = false;
        bool AlwaysOnTop = false;
        bool Resizable = true;
        bool HideOnClose = true;                // kapatma düğmesi pencereyi gizler (kapalıysa yalnız sinyal / OnCloseRequested)
        uint64_t Camera = 0;                    // bu pencereye çizen kamera (yalnız bu pencere için; ana kamera olmasın)

        // Çalışma zamanı
        void* Handle = nullptr;                 // GLFWwindow
        bool Open = false;
        bool Focused = false;
        int Frames = 0;                         // gösterilen kare sayısı
        bool ProbeRequest = false;              // test: sonraki gösterimde merkez bölgenin parlaklığı ölçülür
        float ProbeMean = -1.0f;
        glm::vec2 AppliedSize{ 0.0f }, AppliedPosition{ -1e9f };
        std::string AppliedTitle;
        bool AppliedVisible = false, AppliedTop = false;
        uint32_t VertexArray = 0;               // pencere bağlamının VAO'su (VAO bağlamlar arasında paylaşılmaz)
    };

    namespace GameWindows
    {
        // Oyunda her kare: pencereleri aç / güncelle / göster
        CQ_API void Update(Scene& scene);
        // Oyun durunca: bu sahnenin pencereleri kapanır
        CQ_API void ForgetScene(Scene* scene);
        CQ_API bool Show(Scene& scene, uint32_t entity, bool visible);
        CQ_API bool GrabFocus(Scene& scene, uint32_t entity);
        // GPU kaynakları (OpenGL bağlamı kapanmadan önce; statik gölgelendirici çıkışta bağlamsız silinip çöküyordu)
        CQ_API void Shutdown();
    }

    void RegisterGameWindowComponents();
}
