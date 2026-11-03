#pragma once

#include <functional>
#include <string>

// Animation Clip (.cqanim) Inspector: uzunluk, kare hızı (Samples), Loop Time; eğri ve olay listesi; Animation penceresinde aç
namespace Conqueror::Editor::AnimationClipInspector
{
    void Draw(const std::string& path);

    inline std::function<void(const std::string&)> s_OpenWindow;   // EditorLayer: Animation penceresi
    inline std::function<void()> s_ClipChanged;                     // Animation penceresi açık klibi yeniden okur
}
