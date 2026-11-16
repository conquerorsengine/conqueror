#pragma once

#include "Project.h"

namespace Conqueror {

    class CQ_API ProjectSerializer
    {
    public:
        ProjectSerializer(Ref<Project> project);

        bool Serialize(const std::filesystem::path& filepath);
        std::string SerializeText();
        bool Deserialize(const std::filesystem::path& filepath);
        // Dosya yerine metinden (ör. taşınabilir oyun paketindeki .cqproj); filepath proje klasörünü belirler
        bool DeserializeText(const std::string& text, const std::filesystem::path& filepath);

    private:
        Ref<Project> m_Project;
    };
}
