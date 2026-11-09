#pragma once

#include <string>

namespace Conqueror::Editor::CsvGridInspector
{
    // Data Table (.cqdatatable) ve Curve Table (.cqcurvetable): ızgara düzenleyici (ilk sütun satır adı)
    bool IsGrid(const std::string& path);
    void Draw(const std::string& path);
}
