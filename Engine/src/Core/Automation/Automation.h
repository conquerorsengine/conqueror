#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Conqueror
{
    // Script-driven UI automation used for automated editor tests, demo
    // recordings and CI screenshots.
    //
    // Activated with the CQ_AUTOMATION environment variable or the
    // --automation=<script> command line flag. A script is a text file with one
    // command per line ('#' starts a comment, quotes group arguments):
    //
    //   wait 30                       # wait N frames
    //   click_item "Lighting"         # click a widget by its label
    //   click_item "Bake" "Lighting"  # ...restricted to a window
    //   key ctrl+s                    # keyboard shortcut
    //   screenshot out/lighting.png   # main window capture (PNG)
    //   quit
    //
    // Layers can register their own commands (e.g. the editor's "select",
    // "play", "window") with RegisterCommand().
    class CQ_API Automation
    {
    public:
        using CommandArgs = std::vector<std::string>;
        // Return false and fill 'error' to report a failed command.
        using CommandHandler = std::function<bool(const CommandArgs& args, std::string& error)>;

        // Reads CQ_AUTOMATION / --automation=. Safe to call when inactive.
        static void Init(int argc, char** argv);
        static void Shutdown();

        static bool IsActive();
        static bool WantsQuit();

        static void RegisterCommand(const std::string& name, CommandHandler handler);
        static void UnregisterCommand(const std::string& name);
        // Kayıtlı (editör) komutlarını doğrudan çalıştırır (Console panelinin "/" komutları)
        static bool ExecuteCommandLine(const std::string& line, std::string& error);
        static std::vector<std::string> GetCommandNames();

        // Frame hooks (called by Application / ImGuiLayer)
        static void OnFrameBegin();
        static void BeforeImGuiNewFrame();
        static void InjectImGuiInput();
        static void OnFrameRendered(uint32_t framebufferWidth, uint32_t framebufferHeight);

        // Writes the current back buffer of the bound default framebuffer.
        static bool CaptureBackbuffer(const std::string& path, uint32_t width, uint32_t height);

        // Used by commands that need the script to pause for N frames.
        static void WaitFrames(int frames);
        // Mouse input in absolute ImGui screen coordinates (e.g. painting cells in the viewport).
        static void QueueMouseMove(float x, float y);
        static void QueueMouseClick(float x, float y, int button = 0);
        static void QueueMouseDrag(float x0, float y0, float x1, float y1, int steps = 12, int button = 0);
        static void Report(const std::string& line);
    };
}
