#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>

// miniaudio forward declarations
typedef struct ma_engine ma_engine;
typedef struct ma_sound ma_sound;

namespace Conqueror
{
    // Audio Engine - Singleton pattern ile tek instance
    class AudioEngine
    {
    public:
        static void Init();
        static void Shutdown();
        
        static AudioEngine& Get() { return *s_Instance; }
        
        // Engine kontrolü
        void SetMasterVolume(float volume);
        float GetMasterVolume() const;
        
        // Listener pozisyonu (3D ses için)
        void SetListenerPosition(const glm::vec3& position);
        void SetListenerDirection(const glm::vec3& forward, const glm::vec3& up);
        void SetListenerVelocity(const glm::vec3& velocity);
        
        glm::vec3 GetListenerPosition() const { return m_ListenerPosition; }
        glm::vec3 GetListenerForward() const { return m_ListenerForward; }
        glm::vec3 GetListenerUp() const { return m_ListenerUp; }
        
        // Internal
        ma_engine* GetNativeEngine() { return m_Engine; }
        
        // Visualization
        void GetSpectrumData(float* buffer, uint32_t count, bool isPlaying = true, float volume = 1.0f);
        
    private:
        AudioEngine();
        ~AudioEngine();
        
        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;
        
    private:
        static AudioEngine* s_Instance;
        
        ma_engine* m_Engine = nullptr;
        
        glm::vec3 m_ListenerPosition = glm::vec3(0.0f);
        glm::vec3 m_ListenerForward = glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 m_ListenerUp = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 m_ListenerVelocity = glm::vec3(0.0f);
    };
}