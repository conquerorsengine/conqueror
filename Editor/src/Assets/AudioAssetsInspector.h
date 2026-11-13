#pragma once

#include <functional>
#include <string>

namespace Conqueror::Editor::AudioAssetsInspector
{
    // Audio Mixer varlığı (.cqmixer): grupları, çıkış grubu, mikser penceresini aç
    void DrawMixer(const std::string& path);
    // Audio Random Container (.cqaudiorandom): klipler, rastgele ses / perde, sıra, tekrar, deneme çalma
    void DrawRandomContainer(const std::string& path);
    // Etkileşimli müzik (Godot Audio Stream Playlist / Synchronized / Interactive): parçalar, katmanlar, klipler, geçiş tablosu, önizleme
    void DrawMusic(const std::string& path);
    // Audio Mixer penceresini aç (EditorLayer bağlar)
    void SetOpenMixerWindow(std::function<void()> open);
}
