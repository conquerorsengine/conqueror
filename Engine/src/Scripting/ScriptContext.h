#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Conqueror
{
    class Scene;

    struct ScriptContactInfo
    {
        bool Valid = false;
        uint32_t Other = 0xFFFFFFFF;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };   // diğer cisimden bu varlığa
        glm::vec3 RelativeVelocity{ 0.0f };
        float Impulse = 0.0f;
    };

    // Dil bağımsız betik bağlamı: native köprüsü fonksiyonları (GetPosition, ApplyForce ...)
    // "geçerli varlık" ve "etkin sahne" bilgisini buradan okur. CQS, Lua, Python, C#, Java ve C++
    // çalışma zamanları bir betik metodunu çağırmadan önce bağlamı ayarlar.
    class CQ_API ScriptContext
    {
    public:
        static constexpr uint32_t NullEntity = 0xFFFFFFFF;

        static Scene* GetActiveScene() { return s_ActiveScene; }
        static void   SetActiveScene(Scene* scene) { s_ActiveScene = scene; }

        static uint32_t GetCurrentEntity() { return s_CurrentEntity; }
        // Önceki varlığı döndürür (geri yüklemek için)
        static uint32_t SetCurrentEntity(uint32_t entity)
        {
            uint32_t previous = s_CurrentEntity;
            s_CurrentEntity = entity;
            return previous;
        }

        // Kapsam süresince varlığı değiştir
        struct EntityScope
        {
            uint32_t Previous;
            explicit EntityScope(uint32_t entity) : Previous(SetCurrentEntity(entity)) {}
            ~EntityScope() { SetCurrentEntity(Previous); }
        };

        // Fizik olayı bağlamı: OnCollisionEnter / OnTriggerEnter ... çağrılırken geçerli temas
        using ContactInfo = ScriptContactInfo;
        static const ContactInfo& GetContact() { return s_Contact; }
        static void SetContact(const ContactInfo& info) { s_Contact = info; }
        static void ClearContact() { s_Contact = ContactInfo{}; }

    private:
        static inline Scene*   s_ActiveScene = nullptr;
        static inline uint32_t s_CurrentEntity = NullEntity;
        static inline ContactInfo s_Contact{};
    };
}
