#pragma once

#include "Core/Base/Base.h"
#include "AudioFilter.h"
#include <glm/glm.hpp>
#include <string>

typedef struct ma_sound ma_sound;

namespace Conqueror
{
    // Attenuation modelleri
    enum class AudioAttenuationModel
    {
        None = 0,
        Inverse,
        Linear,
        Exponential
    };

    // Audio Source - Tek bir ses kaynağını temsil eder
    class AudioSource : public AudioNode
    {
    public:
        AudioSource();
        ~AudioSource();

        // Ses dosyası yükleme
        bool LoadFromFile(const std::string& filepath);
        void Unload();

        // Playback kontrolü
        void Play();
        void Pause();
        void Stop();
        void Rewind(); // Başa sar

        bool IsPlaying() const;
        bool IsLooping() const;
        bool IsAtEnd() const;

        // Temel ayarlar
        void SetVolume(float volume);        // 0.0 - 1.0
        void SetPitch(float pitch);          // 0.5 - 2.0
        void SetPan(float pan);              // -1.0 (sol) - 1.0 (sağ)
        void SetLooping(bool loop);
        void SetMute(bool mute);

        float GetVolume() const;
        float GetPitch() const;
        float GetPan() const;

        // 3D Ses ayarları
        void SetPosition(const glm::vec3& position);
        void SetVelocity(const glm::vec3& velocity); // Doppler için
        void SetDirection(const glm::vec3& direction);
        
        void SetMinDistance(float distance);
        void SetMaxDistance(float distance);
        void SetDopplerFactor(float factor);
        void SetAttenuationModel(AudioAttenuationModel model);
        void SetRolloff(float rolloff);

        glm::vec3 GetPosition() const { return m_Position; }
        float GetMinDistance() const { return m_MinDistance; }
        float GetMaxDistance() const { return m_MaxDistance; }

        // Spatial (3D) ses aktif/pasif
        void SetSpatialization(bool enabled);
        bool IsSpatialized() const { return m_IsSpatialized; }

        // Zaman kontrolü
        float GetLengthInSeconds() const;
        float GetCurrentTimeInSeconds() const;
        void SetCurrentTimeInSeconds(float time);
        const std::string& GetFilePath() const { return m_FilePath; }

        // Internal
        ma_sound* GetNativeSound() { return m_Sound; }
        ma_node_base* GetNativeNode() const override;

    private:
        ma_sound* m_Sound = nullptr;
        std::string m_FilePath;
        
        glm::vec3 m_Position = glm::vec3(0.0f);
        glm::vec3 m_Velocity = glm::vec3(0.0f);
        glm::vec3 m_Direction = glm::vec3(0.0f, 0.0f, -1.0f);
        
        float m_MinDistance = 1.0f;
        float m_MaxDistance = 100.0f;
        float m_DopplerFactor = 1.0f;
        float m_Rolloff = 1.0f;
        
        bool m_IsSpatialized = true;
        bool m_IsLoaded = false;
    };
}
