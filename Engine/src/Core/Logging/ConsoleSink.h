#pragma once

#include <spdlog/sinks/base_sink.h>
#include <mutex>
#include <functional>

namespace Conqueror
{
    // spdlog için custom sink - ConsolePanel'e mesaj göndermek için
    template<typename Mutex>
    class ConsoleSink : public spdlog::sinks::base_sink<Mutex>
    {
    public:
        using MessageCallback = std::function<void(const std::string&, int)>; // message, level
        
        void SetCallback(MessageCallback callback)
        {
            m_Callback = callback;
        }

    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            if (!m_Callback)
                return;

            // Mesajı formatla
            spdlog::memory_buf_t formatted;
            spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
            std::string message = fmt::to_string(formatted);

            // Level'ı int'e çevir (0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=critical)
            int level = static_cast<int>(msg.level);

            // Callback'i çağır
            m_Callback(message, level);
        }

        void flush_() override
        {
            // Flush işlemi gerekirse burada yapılır
        }

    private:
        MessageCallback m_Callback;
    };

    using ConsoleSink_mt = ConsoleSink<std::mutex>;
    using ConsoleSink_st = ConsoleSink<spdlog::details::null_mutex>;
}
