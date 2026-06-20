#pragma once

#include "Core/Application.h"

namespace Conqueror::Editor
{
    class EditorApp : public Application
    {
    public:
        EditorApp(const ApplicationSpecification& spec);
        ~EditorApp() override;
    };
}
