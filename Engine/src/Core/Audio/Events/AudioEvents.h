#pragma once

#include "Core/Events/Event.h"

#include <string>

namespace Conqueror
{
    enum class AudioGraphChangeType
    {
        None = 0,
        NodeAdded,
        NodeRemoved,
        LinkAdded,
        LinkRemoved,
        ParameterChanged,
        Rebuilt
    };

    class CQ_API AudioGraphChangedEvent : public Event
    {
    public:
        uint64_t EntityID = 0;
        AudioGraphChangeType ChangeType = AudioGraphChangeType::None;
        std::string Description;

        AudioGraphChangedEvent() = default;
        AudioGraphChangedEvent(uint64_t entityID, AudioGraphChangeType changeType, std::string description = {})
            : EntityID(entityID), ChangeType(changeType), Description(std::move(description)) {}

        EVENT_CLASS_TYPE(AudioGraphChanged)
        EVENT_CLASS_CATEGORY(EventCategoryAudio)

        std::string ToString() const override
        {
            return "AudioGraphChangedEvent: entity=" + std::to_string(EntityID) + " description=" + Description;
        }
    };

    class CQ_API AudioSourceLoadedEvent : public Event
    {
    public:
        uint64_t EntityID = 0;
        std::string FilePath;
        bool Success = false;

        AudioSourceLoadedEvent() = default;
        AudioSourceLoadedEvent(uint64_t entityID, std::string filePath, bool success)
            : EntityID(entityID), FilePath(std::move(filePath)), Success(success) {}

        EVENT_CLASS_TYPE(AudioSourceLoaded)
        EVENT_CLASS_CATEGORY(EventCategoryAudio)

        std::string ToString() const override
        {
            return "AudioSourceLoadedEvent: entity=" + std::to_string(EntityID) + " success=" + (Success ? "true" : "false") + " file=" + FilePath;
        }
    };

    class CQ_API AudioSourcePlaybackStateChangedEvent : public Event
    {
    public:
        uint64_t EntityID = 0;
        bool IsPlaying = false;

        AudioSourcePlaybackStateChangedEvent() = default;
        AudioSourcePlaybackStateChangedEvent(uint64_t entityID, bool isPlaying)
            : EntityID(entityID), IsPlaying(isPlaying) {}

        EVENT_CLASS_TYPE(AudioSourcePlaybackStateChanged)
        EVENT_CLASS_CATEGORY(EventCategoryAudio)

        std::string ToString() const override
        {
            return "AudioSourcePlaybackStateChangedEvent: entity=" + std::to_string(EntityID) + " playing=" + (IsPlaying ? "true" : "false");
        }
    };
}
