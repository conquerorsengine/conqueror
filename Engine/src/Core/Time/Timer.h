#pragma once

#include "Core/Base/Base.h"
#include <functional>

namespace Conqueror
{
    // Geri sayım timer
    class CQ_API Timer
    {
    public:
        Timer(float duration = 1.0f, bool loop = false);
        
        void Start();
        void Stop();
        void Reset();
        void Restart();
        
        void Update(float deltaTime);
        
        bool IsRunning() const { return m_Running; }
        bool IsFinished() const { return m_Finished; }
        
        float GetElapsed() const { return m_Elapsed; }
        float GetRemaining() const { return m_Duration - m_Elapsed; }
        float GetDuration() const { return m_Duration; }
        float GetProgress() const { return m_Elapsed / m_Duration; }
        
        void SetDuration(float duration) { m_Duration = duration; }
        void SetLoop(bool loop) { m_Loop = loop; }
        
        // Callback timer bittiğinde çağrılır
        void SetCallback(std::function<void()> callback) { m_Callback = callback; }
        
    private:
        float m_Duration;
        float m_Elapsed;
        bool m_Running;
        bool m_Finished;
        bool m_Loop;
        std::function<void()> m_Callback;
    };
}
