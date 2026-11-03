#include "AssetFileCommand.h"

#include <fstream>
#include <sstream>

namespace Conqueror::Editor
{
    AssetFileCommand::AssetFileCommand(std::string path, std::string before, std::string after, std::string description)
        : m_Path(std::move(path)), m_Before(std::move(before)), m_After(std::move(after)), m_Description(std::move(description))
    {
    }

    std::string AssetFileCommand::ReadFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    void AssetFileCommand::Execute()
    {
        // İlk Execute: dosya zaten bu metinde (düzenleme yazıldıktan sonra kaydedilir); yine de yazmak zararsız
        if (ReadFile(m_Path) != m_After)
            std::ofstream(m_Path, std::ios::binary) << m_After;
    }

    void AssetFileCommand::Unexecute()
    {
        std::ofstream(m_Path, std::ios::binary) << m_Before;
    }
}
