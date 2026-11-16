#pragma once

// HarmonyOS: uygulamanın stdout'u görünmez; motor günlükleri hilog'a gider ("hdc shell hilog -T Conqueror"). spdlog'da hilog
// hedefi yok: satır biçimlenir ve OH_LOG_Print ile yazılır (%{public}: gizlilik maskesi olmadan).
#if defined(__OHOS__)

#include <hilog/log.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

#include <mutex>
#include <string>

namespace Conqueror
{
    template<typename Mutex>
    class HilogSink final : public spdlog::sinks::base_sink<Mutex>
    {
    public:
        explicit HilogSink(std::string tag) : m_Tag(std::move(tag)) {}

    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            spdlog::memory_buf_t formatted;
            this->formatter_->format(msg, formatted);
            std::string line(formatted.data(), formatted.size());
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            OH_LOG_Print(::LOG_APP, Level(msg.level), 0x3C51, m_Tag.c_str(), "%{public}s", line.c_str());
        }

        void flush_() override {}

    private:
        static ::LogLevel Level(spdlog::level::level_enum level)
        {
            switch (level)
            {
                case spdlog::level::trace:
                case spdlog::level::debug: return ::LOG_DEBUG;
                case spdlog::level::info: return ::LOG_INFO;
                case spdlog::level::warn: return ::LOG_WARN;
                case spdlog::level::err: return ::LOG_ERROR;
                case spdlog::level::critical: return ::LOG_FATAL;
                default: return ::LOG_INFO;
            }
        }

        std::string m_Tag;
    };

    using HilogSinkMt = HilogSink<std::mutex>;
}

#endif
