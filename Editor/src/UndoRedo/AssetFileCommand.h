#pragma once

#include "EditorCommand.h"

#include <string>

namespace Conqueror::Editor
{
    // Varlık dosyası düzenlemesi (Unity'de Inspector'dan değiştirilen varlıklar Ctrl+Z ile geri alınır): dosyanın önceki ve
    // sonraki metni saklanır; geri al / yinele dosyayı o metinle yazar (açık Inspector değişen dosyayı yeniden okur).
    class AssetFileCommand : public EditorCommand
    {
    public:
        AssetFileCommand(std::string path, std::string before, std::string after, std::string description);
        void Execute() override;
        void Unexecute() override;
        std::string GetDescription() const override { return m_Description; }

        static std::string ReadFile(const std::string& path);

    private:
        std::string m_Path, m_Before, m_After, m_Description;
    };
}
