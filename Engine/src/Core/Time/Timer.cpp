#include "Timer.h"

namespace Conqueror
{
    Timer::Timer(float duration, bool loop)
        : m_Duration(duration), m_Elapsed(0.0f), m_Running(false), m_Finished(false), m_Loop(loop)
    {
    }

    void Timer::Start()
    {
        m_Running = true;
        m_Finished = false;
    }

    void Timer::Stop()
    {
        m_Running = false;
    }

    void Timer::Reset()
    {
        m_Elapsed = 0.0f;
        m_Finished = false;
    }

    void Timer::Restart()
    {
        Reset();
        Start();
    }

    void Timer::Update(float deltaTime)
    {
        if (!m_Running || m_Finished)
            return;

        m_Elapsed += deltaTime;

        if (m_Elapsed >= m_Duration)
        {
            m_Finished = true;
            
            if (m_Callback)
                m_Callback();
            
            if (m_Loop)
            {
                m_Elapsed = 0.0f;
                m_Finished = false;
            }
            else
            {
                m_Running = false;
            }
        }
    }
}
