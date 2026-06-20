#pragma once

#include "Core/Base/Base.h"
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace Conqueror
{
    // Job/Task type
    using Task = std::function<void()>;

    // Thread pool for async task execution
    class CQ_API ThreadPool
    {
    public:
        ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
        ~ThreadPool();

        // Submit task and get future
        template<typename F, typename... Args>
        auto Submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>
        {
            using ReturnType = typename std::invoke_result<F, Args...>::type;

            auto task = std::make_shared<std::packaged_task<ReturnType()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );

            std::future<ReturnType> result = task->get_future();
            
            {
                std::unique_lock<std::mutex> lock(m_QueueMutex);
                
                if (m_Stop)
                    throw std::runtime_error("ThreadPool is stopped");

                m_Tasks.emplace([task]() { (*task)(); });
            }
            
            m_Condition.notify_one();
            return result;
        }

        // Wait for all tasks to complete
        void WaitAll();

        // Get number of threads
        size_t GetThreadCount() const { return m_Workers.size(); }

        // Get number of pending tasks
        size_t GetPendingTaskCount() const;

    private:
        std::vector<std::thread> m_Workers;
        std::queue<Task> m_Tasks;
        
        mutable std::mutex m_QueueMutex;
        std::condition_variable m_Condition;
        bool m_Stop = false;
    };
}
