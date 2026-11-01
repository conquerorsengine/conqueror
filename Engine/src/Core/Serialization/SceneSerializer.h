#pragma once

#include "Scene/Scene.h"
#include <filesystem>

namespace Conqueror
{
    class CQ_API SceneSerializer
    {
    public:
        SceneSerializer(const std::shared_ptr<Scene>& scene);

        void Serialize(const std::filesystem::path& filepath);
        void SerializeRuntime(const std::filesystem::path& filepath);

        bool Deserialize(const std::filesystem::path& filepath);
        bool DeserializeRuntime(const std::filesystem::path& filepath);
        
        // Stream-based serialization (scene kopyalama için)
        void SerializeToStream(std::ostream& stream);
        bool DeserializeFromStream(std::istream& stream);
        
        void SetEditorCamera(class EditorCamera* camera) { m_EditorCamera = camera; }

        // Taşınabilir varlık yolları: proje göreli ya da "CQ:Resources/..." (motor kaynakları)
        static std::string ToAssetPath(const std::string& absolutePath);
        // Tek varlığın YAML metni (değişiklik algılama: geri alma, kirli işareti)
        static std::string SerializeEntityToString(Entity entity);
        static std::string ResolveAssetPath(const std::string& path);
        class EditorCamera* GetEditorCamera() const { return m_EditorCamera; }

    private:
        bool DeserializeFromStreamImpl(std::istream& stream);

        std::shared_ptr<Scene> m_Scene;
        class EditorCamera* m_EditorCamera = nullptr;
    };
}
