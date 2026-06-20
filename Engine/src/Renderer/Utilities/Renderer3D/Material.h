#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/RHI/Shader.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    // ── Shader Graph veri yapıları ──────────────────────────────────────
    enum class ShaderGraphNodeType
    {
        MasterOutput = 0,
        
        // Data Types
        Color,
        Float,
        Vec2,
        Vec3,
        TextureSample,
        TextureObject,
        
        // Math - Basic
        Add,
        Subtract,
        Multiply,
        Divide,
        Power,
        Sqrt,
        
        // Math - Advanced
        Abs,
        Sign,
        Ceil,
        Floor,
        Round,
        Mod,
        Min,
        Max,
        Clamp,
        Saturate,
        Mix,
        Step,
        Smoothstep,
        OneMinus,
        Fract,
        Reciprocal,
        Square,
        Log,
        Exp,
        ArcTan2,
        
        // Trig
        Sin,
        Cos,
        Tan,
        Asin,
        Acos,
        Atan,
        
        // Logic
        If,
        
        // Vectors
        Dot,
        Cross,
        Normalize,
        Length,
        Distance,
        Reflect,
        Refract,
        Append,
        ComponentMask,
        
        // Coordinates
        UV,
        WorldPosition,
        ViewDirection,
        NormalVector,
        TextureCoordinate,
        LightVector,
        
        // Special
        Time,
        Fresnel,
        NormalMap,
        Panner,
        Noise,
        VertexColor,
        Desaturation,
        DepthFade,
        SphereMask,
        BoxMask,
        CameraDepthFade,
        ObjectPosition,
        ObjectBounds,
        CustomExpression
    };

    struct ShaderGraphNode
    {
        int ID;
        ShaderGraphNodeType Type;
        glm::vec2 EditorPosition;

        glm::vec4 ColorValue   = glm::vec4(1.0f);
        float     FloatValue   = 0.0f;
        glm::vec2 Vec2Value    = glm::vec2(0.0f);
        glm::vec3 Vec3Value    = glm::vec3(0.0f);
        int       IntValue     = 0;
        std::string TexturePath;
        std::string CustomCode;
        std::shared_ptr<Texture2D> TextureRef;

        ShaderGraphNode() = default;
        ShaderGraphNode(int id, ShaderGraphNodeType type, const glm::vec2& pos = {0.0f, 0.0f})
            : ID(id), Type(type), EditorPosition(pos) {}
    };

    struct ShaderGraphLink
    {
        int ID;
        int FromNodeID;
        int FromPinIndex;   // Hangi output pin
        int ToNodeID;
        int ToPinIndex;     // Hangi input pin

        ShaderGraphLink() = default;
        ShaderGraphLink(int id, int fromNode, int fromPin, int toNode, int toPin)
            : ID(id), FromNodeID(fromNode), FromPinIndex(fromPin),
              ToNodeID(toNode), ToPinIndex(toPin) {}
    };

    // ── Material sınıfı ────────────────────────────────────────────────
    class Material
    {
    public:
        Material();
        ~Material() = default;

        // Material ismi
        std::string Name = "Default-Material";

        // Shader referansı (nullptr ise default PBR shader kullanılır)
        std::shared_ptr<Shader> MaterialShader;

        // PBR Parametreleri
        glm::vec3 Albedo = glm::vec3(1.0f);
        float Metallic  = 0.0f;
        float Roughness = 0.5f;
        float AO        = 1.0f;

        // Emission
        glm::vec3 EmissionColor    = glm::vec3(0.0f);
        float     EmissionStrength = 0.0f;

        // Tiling & Offset
        glm::vec2 Tiling = glm::vec2(1.0f);
        glm::vec2 Offset = glm::vec2(0.0f);

        // Render state
        enum class RenderMode { Opaque = 0, Cutout, Transparent };
        enum class CullMode   { Back = 0, Front, Off };
        RenderMode Rendering = RenderMode::Opaque;
        CullMode   Culling   = CullMode::Back;
        float AlphaClip = 0.5f;

        // Texture haritaları
        std::shared_ptr<Texture2D> AlbedoMap;
        std::shared_ptr<Texture2D> NormalMap;
        std::shared_ptr<Texture2D> MetallicMap;
        std::shared_ptr<Texture2D> RoughnessMap;
        std::shared_ptr<Texture2D> AOMap;
        std::shared_ptr<Texture2D> EmissionMap;

        // Genel uniform override'ları
        std::unordered_map<std::string, float>     FloatProperties;
        std::unordered_map<std::string, int>       IntProperties;
        std::unordered_map<std::string, glm::vec3> Vec3Properties;
        std::unordered_map<std::string, glm::vec4> Vec4Properties;

        // ── Shader Graph (editör verisi) ───────────────────────────────
        std::vector<ShaderGraphNode> GraphNodes;
        std::vector<ShaderGraphLink> GraphLinks;
        int  NextGraphNodeID  = 1;
        int  NextGraphLinkID  = 1;
        bool GraphInitialized = false;

        // ── Fonksiyonlar ───────────────────────────────────────────────
        // Verilen shader'a tüm parametreleri gönderir
        void Bind(std::shared_ptr<Shader> shader) const;

        // MaterialShader kullanarak bind eder
        void BindSelf() const;

        // Graph node uniform'larini shader'a gonderir
        void BindGraphUniforms(std::shared_ptr<Shader> shader) const;

        // Graph bağlantılarına göre PBR parametrelerini günceller
        void ApplyGraphToProperties();

        // Graph'ı dinamik olarak GLSL'e derler ve MaterialShader oluşturur
        void CompileShaderGraph();

        // Son derlenen shader kaynak kodu (editörde gösterim için)
        std::string LastCompiledVertexSrc;
        std::string LastCompiledFragmentSrc;

        // Fabrika
        static std::shared_ptr<Material> CreateDefault();

    private:
        void CreateDefaultTextures();

    private:
        std::shared_ptr<Texture2D> m_WhiteTexture;
        std::shared_ptr<Texture2D> m_DefaultNormalMap;
        std::shared_ptr<Texture2D> m_BlackTexture;
    };
}
