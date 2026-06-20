#include "ThreadPool.h"
#include "Core/Logging/Log.h"

namespace Conqueror
{
    ThreadPool::ThreadPool(size_t numThreads)
        : m_Stop(false)
    {
        for (size_t i = 0; i < numThreads; ++i)
        {
            m_Workers.emplace_back([this]() {
                for (;;)
                {
                    Task task;
                    
                    {
                        std::unique_lock<std::mutex> lock(m_QueueMutex);
                        m_Condition.wait(lock, [this]() { return m_Stop || !m_Tasks.empty(); });
                        
                        if (m_Stop && m_Tasks.empty())
                            return;
                        
                        task = std::move(m_Tasks.front());
                        m_Tasks.pop();
                    }
                    
                    task();
                }
            });
        }
    }

    ThreadPool::~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(m_QueueMutex);
            m_Stop = true;
        }
        
        m_Condition.notify_all();
        
        for (std::thread& worker : m_Workers)
        {
            if (worker.joinable())
                worker.join();
        }
    }

    void ThreadPool::WaitAll()
    {
        std::unique_lock<std::mutex> lock(m_QueueMutex);
        m_Condition.wait(lock, [this]() { return m_Tasks.empty(); });
    }

    size_t ThreadPool::GetPendingTaskCount() const
    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        return m_Tasks.size();
    }
}