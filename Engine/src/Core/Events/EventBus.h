#pragma once

#include "Event.h"
#include "Core/Base/Base.h"

#include <functional>
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>

namespace Conqueror
{
    // Event callback type
    using EventCallback = std::function<bool(Event&)>;

    // Event listener with priority
    struct EventListener
    {
        EventCallback Callback;
        int Priority = 0;
        bool Enabled = true;

        bool operator<(const EventListener& other) const
        {
            return Priority > other.Priority; // Higher priority first
        }
    };

    // Global event bus for decoupled event handling
    class CQ_API EventBus
    {
    public:
        static EventBus& Get()
        {
            static EventBus instance;
            return instance;
        }

        // Subscribe to specific event type
        template<typename T>
        void Subscribe(EventCallback callback, int priority = 0)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            EventType type = T::GetStaticType();
            
            EventListener listener;
            listener.Callback = callback;
            listener.Priority = priority;
            listener.Enabled = true;
            
            m_Listeners[type].push_back(listener);
            
            // Sort by priority
            std::sort(m_Listeners[type].begin(), m_Listeners[type].end());
        }

        // Publish event immediately (synchronous)
        void Publish(Event& event)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            auto it = m_Listeners.find(event.GetEventType());
            if (it != m_Listeners.end())
            {
                for (auto& listener : it->second)
                {
                    if (listener.Enabled && !event.Handled)
                    {
                        event.Handled = listener.Callback(event);
                    }
                }
            }
        }

        // Queue event for later processing (asynchronous)
        void QueueEvent(Ref<Event> event)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_EventQueue.push(event);
        }

        // Process all queued events
        void ProcessEvents()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            while (!m_EventQueue.empty())
            {
                Ref<Event> event = m_EventQueue.front();
                m_EventQueue.pop();
                
                auto it = m_Listeners.find(event->GetEventType());
                if (it != m_Listeners.end())
                {
                    for (auto& listener : it->second)
                    {
                        if (listener.Enabled && !event->Handled)
                        {
                            event->Handled = listener.Callback(*event);
                        }
                    }
                }
            }
        }

        // Clear all listeners
        void Clear()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Listeners.clear();
            
            // Clear queue
            while (!m_EventQueue.empty())
                m_EventQueue.pop();
        }

        // Clear listeners for specific event type
        void ClearListeners(EventType type)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Listeners.erase(type);
        }

    private:
        EventBus() = default;
        ~EventBus() = default;
        EventBus(const EventBus&) = delete;
        EventBus& operator=(const EventBus&) = delete;

        std::unordered_map<EventType, std::vector<EventListener>> m_Listeners;
        std::queue<Ref<Event>> m_EventQueue;
        std::mutex m_Mutex;
    };
}
