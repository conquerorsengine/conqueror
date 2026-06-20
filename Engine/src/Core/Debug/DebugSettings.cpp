#include "DebugSettings.h"

namespace Conqueror
{
    static DebugSettings s_Settings;

    DebugSettings& DebugSettings::Get()
    {
        return s_Settings;
    }

    void DebugSettings::Reset()
    {
        s_Settings = DebugSettings{};
    }

    bool DebugSettings::IsCategoryActive(DebugCategory category) const
    {
        if (!Enabled)
            return false;
        if (category == DebugCategory::None)
            return true;
        return HasCategory(ActiveCategories, category);
    }

    void DebugSettings::EnableCategory(DebugCategory category)
    {
        ActiveCategories = ActiveCategories | category;
    }

    void DebugSettings::DisableCategory(DebugCategory category)
    {
        ActiveCategories = static_cast<DebugCategory>(
            static_cast<uint32_t>(ActiveCategories) & ~static_cast<uint32_t>(category));
    }

    void DebugSettings::SetCategory(DebugCategory category, bool enabled)
    {
        if (enabled)
            EnableCategory(category);
        else
            DisableCategory(category);
    }
}
