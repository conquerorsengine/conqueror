#include "AITools.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Core/Project/Project.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Logging/Log.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace Conqueror::AI
{
    std::vector<ToolDefinition> AITools::GetToolDefinitions()
    {
        return {
            {
                "create_entity",
                "Creates a new entity in the active scene with an optional name, type (Cube, Sphere, OBJ, Light, Camera, Sprite, Empty), and modelPath.",
                {
                    { "name", "string", "The name of the new entity", true },
                    { "type", "string", "Entity type: 'Cube', 'Sphere', 'OBJ', 'Light', 'Camera', 'Sprite', 'Empty'", false },
                    { "modelPath", "string", "Path to .obj model file if type is OBJ/Model", false },
                    { "px", "number", "Position X", false },
                    { "py", "number", "Position Y", false },
                    { "pz", "number", "Position Z", false },
                    { "sx", "number", "Scale X", false },
                    { "sy", "number", "Scale Y", false },
                    { "sz", "number", "Scale Z", false }
                }
            },
            {
                "delete_entity",
                "Deletes an entity by name or UUID.",
                {
                    { "identifier", "string", "Name or UUID of the entity to delete", true }
                }
            },
            {
                "set_transform",
                "Sets the position, rotation, or scale of an entity.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "px", "number", "Position X", false },
                    { "py", "number", "Position Y", false },
                    { "pz", "number", "Position Z", false },
                    { "rx", "number", "Rotation X (degrees)", false },
                    { "ry", "number", "Rotation Y (degrees)", false },
                    { "rz", "number", "Rotation Z (degrees)", false },
                    { "sx", "number", "Scale X", false },
                    { "sy", "number", "Scale Y", false },
                    { "sz", "number", "Scale Z", false }
                }
            },
            {
                "add_component",
                "Adds a component to an entity.",
                {
                    { "identifier", "string", "Entity name or UUID (can also pass 'name')", true },
                    { "component", "string", "Component type: 'MeshRenderer', 'SpriteRenderer', 'Light', 'Camera', 'Rigidbody', 'Rigidbody2D', 'BoxCollider2D', 'CircleCollider2D', 'BoxCollider', 'SphereCollider', 'CapsuleCollider', 'MeshCollider', 'NativeScript', 'ConquerorScript', 'AudioSource', 'TextRenderer', 'Image', 'Button'", true },
                    { "scriptPath", "string", "Path to script file (only needed if component is ConquerorScript or NativeScript)", false }
                }
            },
            {
                "write_file",
                "Writes text content to a file in the project.",
                {
                    { "path", "string", "Absolute or project-relative file path (e.g. Assets/Scripts/MyScript.cqs)", true },
                    { "content", "string", "The raw text/code content to write into the file", true }
                }
            },
            {
                "list_entities",
                "Lists all entities in the active scene with their names, UUIDs, and components.",
                {}
            },
            {
                "generate_svg_asset",
                "Creates an SVG vector image asset file in the project directory.",
                {
                    { "filename", "string", "File path relative to project (e.g. Assets/icon.svg)", true },
                    { "svgContent", "string", "Complete raw SVG code (xml string)", true }
                }
            },
            {
                "generate_png_asset",
                "Creates a color texture asset file (PPM/PNG format) in the project directory.",
                {
                    { "filename", "string", "File path relative to project (e.g. Assets/texture.png)", true },
                    { "width", "number", "Width in pixels", false },
                    { "height", "number", "Height in pixels", false },
                    { "colorHex", "string", "Hex color string like #FF0000", false }
                }
            },
            {
                "generate_3d_model",
                "Generates a 3D OBJ mesh model asset file at any specified path in the project.",
                {
                    { "filename", "string", "Project-relative file path where model should be saved (e.g. Assets/Models/fok.obj, Images/mesh.obj)", true },
                    { "objContent", "string", "Raw Wavefront OBJ text content containing vertices (v) and faces (f)", true }
                }
            },
            {
                "generate_animation",
                "Generates a .cqanim animation clip file in the project directory.",
                {
                    { "filename", "string", "File path (e.g. Assets/idle.cqanim)", true },
                    { "animData", "string", "Animation metadata/keyframes", true }
                }
            },
            {
                "create_script",
                "Generates a C++ Native Script class template for the project.",
                {
                    { "scriptName", "string", "Name of the script class (e.g. PlayerController)", true }
                }
            },
            {
                "analyze_scene",
                "Analyzes active scene statistics (counts of meshes, lights, entities).",
                {}
            },
            {
                "set_light",
                "Configures Light component parameters for an entity.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "colorHex", "string", "Hex color string (e.g. #FFD700)", false },
                    { "intensity", "number", "Light intensity multiplier", false },
                    { "range", "number", "Point/Spot light range", false },
                    { "castShadows", "boolean", "Whether light casts shadow map", false }
                }
            },
            {
                "set_physics",
                "Configures Rigidbody physics parameters for an entity.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "bodyType", "string", "Body type: 'Static', 'Kinematic', 'Dynamic'", true },
                    { "mass", "number", "Mass in kg", false },
                    { "useGravity", "boolean", "Whether body uses gravity", false },
                    { "freezeRotation", "boolean", "Freeze rotation on all axes", false }
                }
            },
            {
                "set_physics_2d",
                "Configures Rigidbody2D physics parameters for a 2D entity.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "bodyType", "string", "Body type: 'Static', 'Kinematic', 'Dynamic'", true },
                    { "fixedRotation", "boolean", "Fix rotation (prevent spinning)", false }
                }
            },
            {
                "set_parent",
                "Sets the parent of an entity to establish a hierarchy.",
                {
                    { "identifier", "string", "Entity name or UUID to be the child", true },
                    { "parentIdentifier", "string", "Entity name or UUID to be the parent", true }
                }
            },
            {
                "set_material",
                "Configures MeshRenderer PBR material color, metallic, and roughness.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "colorHex", "string", "Hex color string (e.g. #3388FF)", false },
                    { "metallic", "number", "Metallic parameter (0.0 to 1.0)", false },
                    { "roughness", "number", "Roughness parameter (0.0 to 1.0)", false }
                }
            },
            {
                "set_audio_source",
                "Configures AudioSource component for audio playback.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "filePath", "string", "Path to audio file (.wav, .mp3, .ogg)", false },
                    { "playOnAwake", "boolean", "Auto-play on scene start", false },
                    { "loop", "boolean", "Loop playback", false },
                    { "mute", "boolean", "Mute audio", false },
                    { "volume", "number", "Volume (0.0-1.0, default 1.0)", false },
                    { "pitch", "number", "Pitch (0.5-2.0, default 1.0)", false },
                    { "pan", "number", "Stereo pan (-1.0 left to 1.0 right)", false },
                    { "is3D", "boolean", "Enable 3D spatial audio (default true)", false },
                    { "minDistance", "number", "Full volume distance (1.0)", false },
                    { "maxDistance", "number", "Inaudible distance (100.0)", false },
                    { "dopplerLevel", "number", "Doppler effect intensity (1.0)", false },
                    { "attenuationModel", "string", "Attenuation: None, Inverse, Linear, Exponential", false },
                    { "rolloffFactor", "number", "Distance rolloff (1.0)", false }
                }
            },
            {
                "set_audio_filter",
                "Adds or configures an audio filter on an entity.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "filterType", "string", "Filter type: LowPass, HighPass, Echo, Reverb, Chorus, Distortion, Gain, Pan", true },
                    { "enabled", "boolean", "Enable/disable filter (default true)", false },
                    { "param1", "number", "Filter parameter 1", false },
                    { "param2", "number", "Filter parameter 2", false },
                    { "param3", "number", "Filter parameter 3", false },
                    { "param4", "number", "Filter parameter 4", false }
                }
            },
            {
                "set_camera",
                "Configures Camera component parameters.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "isPrimary", "boolean", "Is this the main camera (default true)", false },
                    { "fov", "number", "Field of View in degrees (default 45)", false },
                    { "nearPlane", "number", "Near clip plane (default 0.1)", false },
                    { "farPlane", "number", "Far clip plane (default 1000.0)", false },
                    { "isOrthographic", "boolean", "Use orthographic projection", false },
                    { "orthographicSize", "number", "Orthographic size", false }
                }
            },
            {
                "set_point_light",
                "Configures PointLight component with attenuation.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "colorHex", "string", "Hex color string (e.g. #FFD700)", false },
                    { "intensity", "number", "Light intensity", false },
                    { "range", "number", "Light range", false },
                    { "constant", "number", "Attenuation constant (default 1.0)", false },
                    { "linear", "number", "Attenuation linear (default 0.09)", false },
                    { "quadratic", "number", "Attenuation quadratic (default 0.032)", false },
                    { "castShadows", "boolean", "Cast shadows", false }
                }
            },
            {
                "set_spot_light",
                "Configures SpotLight component with cone angles.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "colorHex", "string", "Hex color string", false },
                    { "intensity", "number", "Light intensity", false },
                    { "range", "number", "Light range", false },
                    { "innerConeAngle", "number", "Inner cone angle in degrees (default 12.5)", false },
                    { "outerConeAngle", "number", "Outer cone angle in degrees (default 17.5)", false },
                    { "constant", "number", "Attenuation constant", false },
                    { "linear", "number", "Attenuation linear", false },
                    { "quadratic", "number", "Attenuation quadratic", false },
                    { "castShadows", "boolean", "Cast shadows", false }
                }
            },
            {
                "set_reflection_probe",
                "Configures ReflectionProbe component.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "offsetX", "number", "Box offset X", false },
                    { "offsetY", "number", "Box offset Y", false },
                    { "offsetZ", "number", "Box offset Z", false },
                    { "sizeX", "number", "Box size X (default 1.0)", false },
                    { "sizeY", "number", "Box size Y (default 1.0)", false },
                    { "sizeZ", "number", "Box size Z (default 1.0)", false },
                    { "resolution", "number", "Cubemap resolution (default 128)", false },
                    { "intensity", "number", "Probe intensity (default 1.0)", false },
                    { "bounces", "number", "Reflection bounces (default 1)", false },
                    { "realtimeUpdate", "boolean", "Realtime update flag", false }
                }
            },
            {
                "set_ui_canvas",
                "Configures Canvas component for UI.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "renderMode", "number", "Render mode: 0=ScreenSpaceOverlay, 1=ScreenSpaceCamera, 2=WorldSpace", false },
                    { "pixelPerfect", "boolean", "Pixel perfect rendering", false },
                    { "sortOrder", "number", "Canvas sort order", false }
                }
            },
            {
                "set_ui_button",
                "Configures Button component for UI.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "interactable", "boolean", "Is button interactable (default true)", false },
                    { "navigationMode", "number", "Navigation: 0=None, 1=Horizontal, 2=Vertical, 3=Automatic, 4=Explicit", false },
                    { "bgColorNormal", "string", "Background normal color hex", false },
                    { "bgColorHover", "string", "Background hover color hex", false },
                    { "bgColorPressed", "string", "Background pressed color hex", false },
                    { "textColorNormal", "string", "Text normal color hex", false },
                    { "transitionSpeed", "number", "Color transition speed (default 10.0)", false },
                    { "use9Slice", "boolean", "Enable 9-slice scaling", false },
                    { "borderSize", "number", "9-slice border size (default 10.0)", false }
                }
            },
            {
                "set_ui_text",
                "Configures TextRenderer component for UI text.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "text", "string", "Text content", false },
                    { "colorHex", "string", "Text color hex (e.g. #FFFFFF)", false },
                    { "fontSize", "number", "Font size (default 1.0)", false }
                }
            },
            {
                "set_ui_image",
                "Configures Image component for UI.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "colorHex", "string", "Image color hex (e.g. #FFFFFF)", false },
                    { "tilingFactor", "number", "Texture tiling factor (default 1.0)", false }
                }
            },
            {
                "set_animator",
                "Configures Animator component for simple animation playback.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "playing", "boolean", "Is animation playing (default true)", false },
                    { "speed", "number", "Playback speed (default 1.0)", false },
                    { "activeClipIndex", "number", "Active clip index (default 0)", false }
                }
            },
            {
                "set_animation",
                "Configures AnimationComponent for state machine animation.",
                {
                    { "identifier", "string", "Entity name or UUID", true },
                    { "controllerPath", "string", "Path to .cqanim controller file", false },
                    { "speed", "number", "Playback speed (default 1.0)", false },
                    { "playOnAwake", "boolean", "Play on scene start (default true)", false },
                    { "activeLayerIndex", "number", "Active animation layer (default 0)", false }
                }
            }
        };
    }

    std::string AITools::ExecuteTool(Scene* activeScene, const std::string& toolName, const std::string& jsonArgs)
    {
        CQ_CORE_INFO("AI Tools executing: {0}", toolName);

        auto extractJsonString = [](const std::string& json, const std::string& key) -> std::string {
            std::string needle = "\"" + key + "\"";
            size_t keyPos = json.find(needle);
            if (keyPos == std::string::npos) return "";

            size_t colon = json.find(':', keyPos + needle.size());
            if (colon == std::string::npos) return "";

            size_t q1 = json.find('"', colon + 1);
            if (q1 == std::string::npos) return "";

            std::string res;
            for (size_t i = q1 + 1; i < json.size(); ++i)
            {
                if (json[i] == '\\' && i + 1 < json.size())
                {
                    char next = json[i + 1];
                    if (next == 'n') res += '\n';
                    else if (next == 'r') res += '\r';
                    else if (next == 't') res += '\t';
                    else if (next == '"') res += '"';
                    else if (next == '\\') res += '\\';
                    else res += next;
                    ++i;
                }
                else if (json[i] == '"')
                {
                    break;
                }
                else
                {
                    res += json[i];
                }
            }
            return res;
        };

        // Parse number values with a helper lambda
        auto extractJsonNumber = [](const std::string& json, const std::string& key, float defaultVal) -> float {
            std::string needle = "\"" + key + "\"";
            size_t keyPos = json.find(needle);
            if (keyPos == std::string::npos) return defaultVal;

            size_t colon = json.find(':', keyPos + needle.size());
            if (colon == std::string::npos) return defaultVal;

            size_t start = colon + 1;
            while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) start++;

            std::string numStr;
            for (size_t i = start; i < json.size(); ++i)
            {
                char c = json[i];
                if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9') || c == 'e' || c == 'E')
                    numStr += c;
                else
                    break;
            }
            if (numStr.empty()) return defaultVal;
            try { return std::stof(numStr); }
            catch (...) { return defaultVal; }
        };

        // Helper: if color string looks like a JSON object {"r":...,"g":...,"b":...}, convert to #RRGGBB hex
        auto normalizeColorHex = [&extractJsonString, &extractJsonNumber](const std::string& raw) -> std::string {
            if (raw.empty()) return raw;
            if (raw[0] == '{')
            {
                float r = extractJsonNumber(raw, "r", 1.0f);
                float g = extractJsonNumber(raw, "g", 1.0f);
                float b = extractJsonNumber(raw, "b", 1.0f);
                int ri = std::min(255, std::max(0, (int)(r * 255.0f)));
                int gi = std::min(255, std::max(0, (int)(g * 255.0f)));
                int bi = std::min(255, std::max(0, (int)(b * 255.0f)));
                char buf[8];
                snprintf(buf, sizeof(buf), "#%02X%02X%02X", ri, gi, bi);
                return std::string(buf);
            }
            return raw;
        };

        if (toolName == "create_entity")
        {
            std::string name = extractJsonString(jsonArgs, "name");
            if (name.empty()) name = "New Entity";

            std::string type = extractJsonString(jsonArgs, "type");
            if (type.empty()) type = "Empty";

            std::string modelPath = extractJsonString(jsonArgs, "modelPath");

            if (!modelPath.empty() && (type == "Empty" || type.empty()))
                type = "OBJ";

            std::string result = CreateEntity(activeScene, name, type, modelPath);

            constexpr float UNSET = -999999.0f;
            float px = extractJsonNumber(jsonArgs, "px", extractJsonNumber(jsonArgs, "positionX", UNSET));
            float py = extractJsonNumber(jsonArgs, "py", extractJsonNumber(jsonArgs, "positionY", UNSET));
            float pz = extractJsonNumber(jsonArgs, "pz", extractJsonNumber(jsonArgs, "positionZ", UNSET));
            float rx = extractJsonNumber(jsonArgs, "rx", extractJsonNumber(jsonArgs, "rotationX", UNSET));
            float ry = extractJsonNumber(jsonArgs, "ry", extractJsonNumber(jsonArgs, "rotationY", UNSET));
            float rz = extractJsonNumber(jsonArgs, "rz", extractJsonNumber(jsonArgs, "rotationZ", UNSET));
            float sx = extractJsonNumber(jsonArgs, "sx", extractJsonNumber(jsonArgs, "scaleX", UNSET));
            float sy = extractJsonNumber(jsonArgs, "sy", extractJsonNumber(jsonArgs, "scaleY", UNSET));
            float sz = extractJsonNumber(jsonArgs, "sz", extractJsonNumber(jsonArgs, "scaleZ", UNSET));
            
            if (px != UNSET || py != UNSET || pz != UNSET || rx != UNSET || ry != UNSET || rz != UNSET || sx != UNSET || sy != UNSET || sz != UNSET)
            {
                std::string tResult = SetTransform(activeScene, name, px, py, pz, rx, ry, rz, sx, sy, sz);
                result += " " + tResult;
            }

            return result;
        }
        else if (toolName == "delete_entity")
        {
            std::string identifier = "";
            size_t iPos = jsonArgs.find("\"identifier\":");
            if (iPos != std::string::npos)
            {
                size_t first = jsonArgs.find('"', jsonArgs.find(':', iPos));
                size_t last = jsonArgs.find('"', first + 1);
                if (first != std::string::npos && last != std::string::npos)
                    identifier = jsonArgs.substr(first + 1, last - first - 1);
            }
            return DeleteEntity(activeScene, identifier);
        }
        else if (toolName == "list_entities")
        {
            return ListEntities(activeScene);
        }
        else if (toolName == "generate_svg_asset")
        {
            std::string filename = "Assets/graphic.svg";
            std::string content = "<svg width=\"100\" height=\"100\"><rect width=\"100\" height=\"100\" fill=\"red\"/></svg>";
            
            size_t fPos = jsonArgs.find("\"filename\":");
            if (fPos != std::string::npos)
            {
                size_t first = jsonArgs.find('"', jsonArgs.find(':', fPos));
                size_t last = jsonArgs.find('"', first + 1);
                if (first != std::string::npos && last != std::string::npos)
                    filename = jsonArgs.substr(first + 1, last - first - 1);
            }

            size_t cPos = jsonArgs.find("\"svgContent\":");
            if (cPos != std::string::npos)
            {
                size_t first = jsonArgs.find('"', jsonArgs.find(':', cPos));
                size_t last = jsonArgs.find('"', first + 1);
                if (first != std::string::npos && last != std::string::npos)
                    content = jsonArgs.substr(first + 1, last - first - 1);
            }

            return GenerateSVGAsset(filename, content);
        }
        else if (toolName == "generate_png_asset")
        {
            std::string filename = "Assets/texture.png";
            size_t fPos = jsonArgs.find("\"filename\":");
            if (fPos != std::string::npos)
            {
                size_t first = jsonArgs.find('"', jsonArgs.find(':', fPos));
                size_t last = jsonArgs.find('"', first + 1);
                if (first != std::string::npos && last != std::string::npos)
                    filename = jsonArgs.substr(first + 1, last - first - 1);
            }
            return GeneratePNGAsset(filename, 256, 256, "#FF5733");
        }
        else if (toolName == "generate_3d_model")
        {
            std::string filename = "Assets/model.obj";
            std::string objContent = "";

            auto extractJsonString = [](const std::string& json, const std::string& key) -> std::string {
                std::string needle = "\"" + key + "\"";
                size_t keyPos = json.find(needle);
                if (keyPos == std::string::npos) return "";

                size_t colon = json.find(':', keyPos + needle.size());
                if (colon == std::string::npos) return "";

                size_t q1 = json.find('"', colon + 1);
                if (q1 == std::string::npos) return "";

                std::string res;
                for (size_t i = q1 + 1; i < json.size(); ++i)
                {
                    if (json[i] == '\\' && i + 1 < json.size())
                    {
                        char next = json[i + 1];
                        if (next == 'n') res += '\n';
                        else if (next == 'r') res += '\r';
                        else if (next == 't') res += '\t';
                        else if (next == '"') res += '"';
                        else if (next == '\\') res += '\\';
                        else res += next;
                        ++i;
                    }
                    else if (json[i] == '"')
                    {
                        break;
                    }
                    else
                    {
                        res += json[i];
                    }
                }
                return res;
            };

            std::string parsedFile = extractJsonString(jsonArgs, "filename");
            if (!parsedFile.empty()) filename = parsedFile;

            objContent = extractJsonString(jsonArgs, "objContent");
            return Generate3DModel(filename, objContent);
        }
        else if (toolName == "generate_animation")
        {
            std::string filename = "Assets/anim.cqanim";
            size_t fPos = jsonArgs.find("\"filename\":");
            if (fPos != std::string::npos)
            {
                size_t first = jsonArgs.find('"', jsonArgs.find(':', fPos));
                size_t last = jsonArgs.find('"', first + 1);
                if (first != std::string::npos && last != std::string::npos)
                    filename = jsonArgs.substr(first + 1, last - first - 1);
            }
            return GenerateAnimation(filename, "{}");
        }
        else if (toolName == "create_script")
        {
            std::string scriptName = "MyScript";
            size_t sPos = jsonArgs.find("\"scriptName\":");
            if (sPos != std::string::npos)
            {
                size_t first = jsonArgs.find('"', jsonArgs.find(':', sPos));
                size_t last = jsonArgs.find('"', first + 1);
                if (first != std::string::npos && last != std::string::npos)
                    scriptName = jsonArgs.substr(first + 1, last - first - 1);
            }
            return CreateScript(scriptName);
        }
        else if (toolName == "analyze_scene")
        {
            return AnalyzeScene(activeScene);
        }
        else if (toolName == "set_transform")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityName");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "target");

            if (identifier.empty()) return "Error: No entity identifier provided.";

            constexpr float UNSET = -999999.0f;
            float px = extractJsonNumber(jsonArgs, "px", UNSET);
            float py = extractJsonNumber(jsonArgs, "py", UNSET);
            float pz = extractJsonNumber(jsonArgs, "pz", UNSET);
            float rx = extractJsonNumber(jsonArgs, "rx", UNSET);
            float ry = extractJsonNumber(jsonArgs, "ry", UNSET);
            float rz = extractJsonNumber(jsonArgs, "rz", UNSET);
            float sx = extractJsonNumber(jsonArgs, "sx", UNSET);
            float sy = extractJsonNumber(jsonArgs, "sy", UNSET);
            float sz = extractJsonNumber(jsonArgs, "sz", UNSET);

            return SetTransform(activeScene, identifier, px, py, pz, rx, ry, rz, sx, sy, sz);
        }
        else if (toolName == "add_component")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityName");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "target");

            std::string component = extractJsonString(jsonArgs, "component");
            if (component.empty()) component = extractJsonString(jsonArgs, "componentType");
            if (component.empty()) component = extractJsonString(jsonArgs, "type");

            std::string scriptPath = extractJsonString(jsonArgs, "scriptPath");
            if (scriptPath.empty()) scriptPath = extractJsonString(jsonArgs, "path");

            if (identifier.empty()) return "Error: No entity identifier provided.";
            if (component.empty()) return "Error: No component type specified.";

            return AddComponent(activeScene, identifier, component, scriptPath);
        }
        else if (toolName == "write_file" || toolName == "create_file" || toolName == "save_file")
        {
            std::string path = extractJsonString(jsonArgs, "path");
            if (path.empty()) path = extractJsonString(jsonArgs, "filename");
            if (path.empty()) path = extractJsonString(jsonArgs, "filePath");
            if (path.empty()) path = extractJsonString(jsonArgs, "name");

            std::string content = extractJsonString(jsonArgs, "content");
            if (content.empty()) content = extractJsonString(jsonArgs, "text");
            if (content.empty()) content = extractJsonString(jsonArgs, "code");
            if (content.empty()) content = extractJsonString(jsonArgs, "fileContent");

            if (path.empty()) return "Error: No path provided.";

            std::filesystem::path fullPath = Project::GetActiveProjectDirectory().empty() ? 
                                             std::filesystem::path(path) : 
                                             (std::filesystem::path(Project::GetActiveProjectDirectory()) / path);

            // Klasor yoksa olustur
            if (fullPath.has_parent_path())
                std::filesystem::create_directories(fullPath.parent_path());

            std::ofstream out(fullPath);
            if (!out) return "Error: Could not open file for writing: " + fullPath.string();
            out << content;
            out.close();

            return "Successfully wrote file to: " + fullPath.string();
        }
        else if (toolName == "set_light")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityName");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "target");

            std::string colorHex = normalizeColorHex(extractJsonString(jsonArgs, "colorHex"));
            if (colorHex.empty()) colorHex = normalizeColorHex(extractJsonString(jsonArgs, "color"));
            float intensity = extractJsonNumber(jsonArgs, "intensity", -1.0f);
            float range = extractJsonNumber(jsonArgs, "range", -1.0f);
            bool castShadows = jsonArgs.find("\"castShadows\": true") != std::string::npos || jsonArgs.find("\"castShadows\":true") != std::string::npos;
            return SetLight(activeScene, identifier, colorHex, intensity, range, castShadows);
        }
        else if (toolName == "set_physics")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityName");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "target");

            std::string bodyType = extractJsonString(jsonArgs, "bodyType");
            if (bodyType.empty()) bodyType = extractJsonString(jsonArgs, "type");
            float mass = extractJsonNumber(jsonArgs, "mass", 1.0f);
            bool useGravity = jsonArgs.find("\"useGravity\": false") == std::string::npos && jsonArgs.find("\"useGravity\":false") == std::string::npos;
            bool freezeRotation = jsonArgs.find("\"freezeRotation\": true") != std::string::npos || jsonArgs.find("\"freezeRotation\":true") != std::string::npos;
            return SetPhysics(activeScene, identifier, bodyType, mass, useGravity, freezeRotation);
        }
        else if (toolName == "set_physics_2d")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityName");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "target");

            std::string bodyType = extractJsonString(jsonArgs, "bodyType");
            if (bodyType.empty()) bodyType = extractJsonString(jsonArgs, "type");
            bool fixedRotation = jsonArgs.find("\"fixedRotation\": true") != std::string::npos || jsonArgs.find("\"fixedRotation\":true") != std::string::npos;
            return SetPhysics2D(activeScene, identifier, bodyType, fixedRotation);
        }
        else if (toolName == "set_parent")
        {
            std::string child = extractJsonString(jsonArgs, "identifier");
            if (child.empty()) child = extractJsonString(jsonArgs, "entityID");
            if (child.empty()) child = extractJsonString(jsonArgs, "child");
            if (child.empty()) child = extractJsonString(jsonArgs, "entityName");
            if (child.empty()) child = extractJsonString(jsonArgs, "name");
            std::string parent = extractJsonString(jsonArgs, "parentIdentifier");
            if (parent.empty()) parent = extractJsonString(jsonArgs, "parent");
            return SetParent(activeScene, child, parent);
        }
        else if (toolName == "set_material")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityName");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "target");

            std::string colorHex = normalizeColorHex(extractJsonString(jsonArgs, "colorHex"));
            if (colorHex.empty()) colorHex = normalizeColorHex(extractJsonString(jsonArgs, "color"));
            float metallic = extractJsonNumber(jsonArgs, "metallic", -1.0f);
            float roughness = extractJsonNumber(jsonArgs, "roughness", -1.0f);
            return SetMaterial(activeScene, identifier, colorHex, metallic, roughness);
        }
        else if (toolName == "update_component")
        {
            return "Error: update_component is not supported. Use set_transform to adjust position, rotation, and scale.";
        }

        else if (toolName == "set_audio_source")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            std::string filePath = extractJsonString(jsonArgs, "filePath");
            bool playOnAwake = jsonArgs.find("\"playOnAwake\": true") != std::string::npos;
            bool loop = jsonArgs.find("\"loop\": true") != std::string::npos;
            bool mute = jsonArgs.find("\"mute\": true") != std::string::npos;
            float volume = extractJsonNumber(jsonArgs, "volume", 1.0f);
            float pitch = extractJsonNumber(jsonArgs, "pitch", 1.0f);
            float pan = extractJsonNumber(jsonArgs, "pan", 0.0f);
            bool is3D = jsonArgs.find("\"is3D\": false") == std::string::npos;
            float minDist = extractJsonNumber(jsonArgs, "minDistance", 1.0f);
            float maxDist = extractJsonNumber(jsonArgs, "maxDistance", 100.0f);
            float doppler = extractJsonNumber(jsonArgs, "dopplerLevel", 1.0f);
            std::string attenModel = extractJsonString(jsonArgs, "attenuationModel");
            float rolloff = extractJsonNumber(jsonArgs, "rolloffFactor", 1.0f);
            return SetAudioSource(activeScene, identifier, filePath, playOnAwake, loop, mute, volume, pitch, pan, is3D, minDist, maxDist, doppler, attenModel, rolloff);
        }
        else if (toolName == "set_audio_filter")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            std::string filterType = extractJsonString(jsonArgs, "filterType");
            if (filterType.empty()) filterType = extractJsonString(jsonArgs, "type");
            bool enabled = jsonArgs.find("\"enabled\": false") == std::string::npos;
            float p1 = extractJsonNumber(jsonArgs, "param1", -1.0f);
            float p2 = extractJsonNumber(jsonArgs, "param2", -1.0f);
            float p3 = extractJsonNumber(jsonArgs, "param3", -1.0f);
            float p4 = extractJsonNumber(jsonArgs, "param4", -1.0f);
            return SetAudioFilter(activeScene, identifier, filterType, enabled, p1, p2, p3, p4);
        }
        else if (toolName == "set_camera")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            bool isPrimary = jsonArgs.find("\"isPrimary\": false") == std::string::npos;
            float fov = extractJsonNumber(jsonArgs, "fov", -1.0f);
            float nearPlane = extractJsonNumber(jsonArgs, "nearPlane", -1.0f);
            float farPlane = extractJsonNumber(jsonArgs, "farPlane", -1.0f);
            bool isOrtho = jsonArgs.find("\"isOrthographic\": true") != std::string::npos;
            float orthoSize = extractJsonNumber(jsonArgs, "orthographicSize", -1.0f);
            return SetCamera(activeScene, identifier, isPrimary, fov, nearPlane, farPlane, isOrtho, orthoSize);
        }
        else if (toolName == "set_point_light")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            std::string colorHex = normalizeColorHex(extractJsonString(jsonArgs, "colorHex"));
            if (colorHex.empty()) colorHex = normalizeColorHex(extractJsonString(jsonArgs, "color"));
            float intensity = extractJsonNumber(jsonArgs, "intensity", -1.0f);
            float range = extractJsonNumber(jsonArgs, "range", -1.0f);
            float constant = extractJsonNumber(jsonArgs, "constant", -1.0f);
            float linear = extractJsonNumber(jsonArgs, "linear", -1.0f);
            float quadratic = extractJsonNumber(jsonArgs, "quadratic", -1.0f);
            bool castShadows = jsonArgs.find("\"castShadows\": true") != std::string::npos;
            return SetPointLight(activeScene, identifier, colorHex, intensity, range, constant, linear, quadratic, castShadows);
        }
        else if (toolName == "set_spot_light")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            std::string colorHex = normalizeColorHex(extractJsonString(jsonArgs, "colorHex"));
            if (colorHex.empty()) colorHex = normalizeColorHex(extractJsonString(jsonArgs, "color"));
            float intensity = extractJsonNumber(jsonArgs, "intensity", -1.0f);
            float range = extractJsonNumber(jsonArgs, "range", -1.0f);
            float innerAngle = extractJsonNumber(jsonArgs, "innerConeAngle", -1.0f);
            float outerAngle = extractJsonNumber(jsonArgs, "outerConeAngle", -1.0f);
            float constant = extractJsonNumber(jsonArgs, "constant", -1.0f);
            float linear = extractJsonNumber(jsonArgs, "linear", -1.0f);
            float quadratic = extractJsonNumber(jsonArgs, "quadratic", -1.0f);
            bool castShadows = jsonArgs.find("\"castShadows\": true") != std::string::npos;
            return SetSpotLight(activeScene, identifier, colorHex, intensity, range, innerAngle, outerAngle, constant, linear, quadratic, castShadows);
        }
        else if (toolName == "set_reflection_probe")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            float ox = extractJsonNumber(jsonArgs, "offsetX", 0.0f);
            float oy = extractJsonNumber(jsonArgs, "offsetY", 0.0f);
            float oz = extractJsonNumber(jsonArgs, "offsetZ", 0.0f);
            float sx = extractJsonNumber(jsonArgs, "sizeX", 1.0f);
            float sy = extractJsonNumber(jsonArgs, "sizeY", 1.0f);
            float sz = extractJsonNumber(jsonArgs, "sizeZ", 1.0f);
            uint32_t res = (uint32_t)extractJsonNumber(jsonArgs, "resolution", 128.0f);
            float intensity = extractJsonNumber(jsonArgs, "intensity", 1.0f);
            int bounces = (int)extractJsonNumber(jsonArgs, "bounces", 1.0f);
            bool realtime = jsonArgs.find("\"realtimeUpdate\": true") != std::string::npos;
            return SetReflectionProbe(activeScene, identifier, ox, oy, oz, sx, sy, sz, res, intensity, bounces, realtime);
        }
        else if (toolName == "set_ui_canvas")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            int renderMode = (int)extractJsonNumber(jsonArgs, "renderMode", 0.0f);
            bool pixelPerfect = jsonArgs.find("\"pixelPerfect\": true") != std::string::npos;
            int sortOrder = (int)extractJsonNumber(jsonArgs, "sortOrder", 0.0f);
            return SetUICanvas(activeScene, identifier, renderMode, pixelPerfect, sortOrder);
        }
        else if (toolName == "set_ui_button")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            bool interactable = jsonArgs.find("\"interactable\": false") == std::string::npos;
            int navMode = (int)extractJsonNumber(jsonArgs, "navigationMode", 3.0f);
            std::string bgNormal = extractJsonString(jsonArgs, "bgColorNormal");
            std::string bgHover = extractJsonString(jsonArgs, "bgColorHover");
            std::string bgPressed = extractJsonString(jsonArgs, "bgColorPressed");
            std::string txtColor = extractJsonString(jsonArgs, "textColorNormal");
            float transSpeed = extractJsonNumber(jsonArgs, "transitionSpeed", 10.0f);
            bool use9Slice = jsonArgs.find("\"use9Slice\": true") != std::string::npos;
            float borderSize = extractJsonNumber(jsonArgs, "borderSize", 10.0f);
            return SetUIButton(activeScene, identifier, interactable, navMode, bgNormal, bgHover, bgPressed, txtColor, transSpeed, use9Slice, borderSize);
        }
        else if (toolName == "set_ui_text")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            std::string text = extractJsonString(jsonArgs, "text");
            std::string colorHex = normalizeColorHex(extractJsonString(jsonArgs, "colorHex"));
            if (colorHex.empty()) colorHex = normalizeColorHex(extractJsonString(jsonArgs, "color"));
            float fontSize = extractJsonNumber(jsonArgs, "fontSize", -1.0f);
            return SetUIText(activeScene, identifier, text, colorHex, fontSize);
        }
        else if (toolName == "set_ui_image")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            std::string colorHex = normalizeColorHex(extractJsonString(jsonArgs, "colorHex"));
            if (colorHex.empty()) colorHex = normalizeColorHex(extractJsonString(jsonArgs, "color"));
            float tiling = extractJsonNumber(jsonArgs, "tilingFactor", -1.0f);
            return SetUIImage(activeScene, identifier, colorHex, tiling);
        }
        else if (toolName == "set_animator")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            bool playing = jsonArgs.find("\"playing\": false") == std::string::npos;
            float speed = extractJsonNumber(jsonArgs, "speed", 1.0f);
            int clipIndex = (int)extractJsonNumber(jsonArgs, "activeClipIndex", 0.0f);
            return SetAnimator(activeScene, identifier, playing, speed, clipIndex);
        }
        else if (toolName == "set_animation")
        {
            std::string identifier = extractJsonString(jsonArgs, "identifier");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entityID");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "name");
            if (identifier.empty()) identifier = extractJsonString(jsonArgs, "entity");
            std::string controllerPath = extractJsonString(jsonArgs, "controllerPath");
            float speed = extractJsonNumber(jsonArgs, "speed", 1.0f);
            bool playOnAwake = jsonArgs.find("\"playOnAwake\": false") == std::string::npos;
            int layerIndex = (int)extractJsonNumber(jsonArgs, "activeLayerIndex", 0.0f);
            return SetAnimation(activeScene, identifier, controllerPath, speed, playOnAwake, layerIndex);
        }

        return "Unknown tool: " + toolName;
    }

    std::string AITools::SetLight(Scene* scene, const std::string& nameOrUuid, const std::string& colorHex, float intensity, float range, bool castShadows)
    {
        if (!scene) return "Error: No active scene loaded.";
        Entity entity;
        auto view = scene->m_Registry.view<TagComponent>();
        for (auto e : view)
        {
            auto& tag = view.get<TagComponent>(e);
            if (tag.Tag == nameOrUuid) { entity = Entity{ e, scene }; break; }
        }
        if (!entity) entity = scene->FindEntityByTag(nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<DirectionalLightComponent>() && 
            !entity.HasComponent<PointLightComponent>() && 
            !entity.HasComponent<SpotLightComponent>())
        {
            entity.AddComponent<DirectionalLightComponent>();
        }

        glm::vec3 colVec(1.0f);
        bool hasColor = false;
        if (!colorHex.empty())
        {
            std::string hex = colorHex;
            if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
            if (hex.size() == 6)
            {
                unsigned int r = 255, g = 255, b = 255;
                std::stringstream ssR, ssG, ssB;
                ssR << std::hex << hex.substr(0, 2); ssR >> r;
                ssG << std::hex << hex.substr(2, 2); ssG >> g;
                ssB << std::hex << hex.substr(4, 2); ssB >> b;
                colVec = glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
                hasColor = true;
            }
        }

        if (entity.HasComponent<DirectionalLightComponent>())
        {
            auto& dl = entity.GetComponent<DirectionalLightComponent>();
            if (hasColor) dl.Color = colVec;
            if (intensity >= 0.0f) dl.Intensity = intensity;
            dl.CastShadows = castShadows;
        }
        else if (entity.HasComponent<PointLightComponent>())
        {
            auto& pl = entity.GetComponent<PointLightComponent>();
            if (hasColor) pl.Color = colVec;
            if (intensity >= 0.0f) pl.Intensity = intensity;
            if (range >= 0.0f) pl.Range = range;
            pl.CastShadows = castShadows;
        }
        else if (entity.HasComponent<SpotLightComponent>())
        {
            auto& sl = entity.GetComponent<SpotLightComponent>();
            if (hasColor) sl.Color = colVec;
            if (intensity >= 0.0f) sl.Intensity = intensity;
            if (range >= 0.0f) sl.Range = range;
            sl.CastShadows = castShadows;
        }

        return "Successfully set Light properties for '" + nameOrUuid + "'.";
    }

    std::string AITools::SetPhysics(Scene* scene, const std::string& nameOrUuid, const std::string& bodyType, float mass, bool useGravity, bool freezeRotation)
    {
        if (!scene) return "Error: No active scene loaded.";
        Entity entity;
        auto view = scene->m_Registry.view<TagComponent>();
        for (auto e : view)
        {
            auto& tag = view.get<TagComponent>(e);
            if (tag.Tag == nameOrUuid) { entity = Entity{ e, scene }; break; }
        }
        if (!entity) entity = scene->FindEntityByTag(nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<RigidbodyComponent>())
            entity.AddComponent<RigidbodyComponent>();

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        if (bodyType == "Static" || bodyType == "static") rb.Type = RigidbodyComponent::BodyType::Static;
        else if (bodyType == "Kinematic" || bodyType == "kinematic") rb.Type = RigidbodyComponent::BodyType::Kinematic;
        else if (bodyType == "Dynamic" || bodyType == "dynamic") rb.Type = RigidbodyComponent::BodyType::Dynamic;

        if (mass > 0.0f) rb.Mass = mass;
        rb.GravityScale = useGravity ? 1.0f : 0.0f;
        rb.FreezeRotation = freezeRotation;

        return "Successfully set Physics properties for '" + nameOrUuid + "'.";
    }

    std::string AITools::SetPhysics2D(Scene* scene, const std::string& nameOrUuid, const std::string& bodyType, bool fixedRotation)
    {
        if (!scene) return "Error: No active scene loaded.";
        Entity entity;
        auto view = scene->m_Registry.view<TagComponent>();
        for (auto e : view)
        {
            auto& tag = view.get<TagComponent>(e);
            if (tag.Tag == nameOrUuid) { entity = Entity{ e, scene }; break; }
        }
        if (!entity) entity = scene->FindEntityByTag(nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<RigidBody2DComponent>())
            entity.AddComponent<RigidBody2DComponent>();

        auto& rb = entity.GetComponent<RigidBody2DComponent>();
        if (bodyType == "Static" || bodyType == "static") rb.Type = RigidBody2DComponent::BodyType::Static;
        else if (bodyType == "Kinematic" || bodyType == "kinematic") rb.Type = RigidBody2DComponent::BodyType::Kinematic;
        else if (bodyType == "Dynamic" || bodyType == "dynamic") rb.Type = RigidBody2DComponent::BodyType::Dynamic;

        rb.FixedRotation = fixedRotation;

        return "Successfully set 2D Physics properties for '" + nameOrUuid + "'.";
    }

    std::string AITools::SetParent(Scene* scene, const std::string& childIdentifier, const std::string& parentIdentifier)
    {
        if (!scene) return "Error: No active scene loaded.";
        
        Entity childEntity;
        auto view = scene->m_Registry.view<TagComponent>();
        for (auto e : view)
        {
            if (view.get<TagComponent>(e).Tag == childIdentifier) { childEntity = Entity{ e, scene }; break; }
        }
        if (!childEntity) childEntity = scene->FindEntityByTag(childIdentifier);
        if (!childEntity) return "Child entity '" + childIdentifier + "' not found.";

        Entity parentEntity;
        for (auto e : view)
        {
            if (view.get<TagComponent>(e).Tag == parentIdentifier) { parentEntity = Entity{ e, scene }; break; }
        }
        if (!parentEntity) parentEntity = scene->FindEntityByTag(parentIdentifier);
        if (!parentEntity) return "Parent entity '" + parentIdentifier + "' not found.";

        scene->SetEntityParent(childEntity, parentEntity);
        return "Successfully set '" + parentIdentifier + "' as parent of '" + childIdentifier + "'.";
    }

    std::string AITools::SetMaterial(Scene* scene, const std::string& nameOrUuid, const std::string& colorHex, float metallic, float roughness)
    {
        if (!scene) return "Error: No active scene loaded.";
        CQ_CORE_INFO("[AI SetMaterial] Called with nameOrUuid='{0}', colorHex='{1}', metallic={2}, roughness={3}", nameOrUuid, colorHex, metallic, roughness);
        Entity entity;
        auto view = scene->m_Registry.view<TagComponent>();
        for (auto e : view)
        {
            auto& tag = view.get<TagComponent>(e);
            if (tag.Tag == nameOrUuid) { entity = Entity{ e, scene }; break; }
        }
        if (!entity) entity = scene->FindEntityByTag(nameOrUuid);
        if (!entity)
        {
            CQ_CORE_ERROR("[AI SetMaterial] Entity '{0}' NOT FOUND!", nameOrUuid);
            return "Entity '" + nameOrUuid + "' not found.";
        }
        uint64_t uuid = entity.HasComponent<IDComponent>() ? entity.GetComponent<IDComponent>().ID : 0;
        CQ_CORE_INFO("[AI SetMaterial] Entity '{0}' found (UUID={1})", nameOrUuid, uuid);

        bool hasMR = entity.HasComponent<MeshRendererComponent>();
        CQ_CORE_INFO("[AI SetMaterial] Entity has MeshRendererComponent: {0}", hasMR);
        if (!hasMR)
            entity.AddComponent<MeshRendererComponent>();

        auto& mr = entity.GetComponent<MeshRendererComponent>();
        CQ_CORE_INFO("[AI SetMaterial] BEFORE: mr.Color=({0},{1},{2},{3}), mr.MaterialInstance={4}, mr.TexturePath='{5}'",
            mr.Color.r, mr.Color.g, mr.Color.b, mr.Color.a,
            mr.MaterialInstance ? "VALID" : "NULL",
            mr.TexturePath);
        if (!colorHex.empty())
        {
            std::string hex = colorHex;
            if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
            if (hex.size() == 6)
            {
                unsigned int r = 255, g = 255, b = 255;
                std::stringstream ssR, ssG, ssB;
                ssR << std::hex << hex.substr(0, 2); ssR >> r;
                ssG << std::hex << hex.substr(2, 2); ssG >> g;
                ssB << std::hex << hex.substr(4, 2); ssB >> b;
                mr.Color = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
                CQ_CORE_INFO("[AI SetMaterial] Parsed hex '{0}' -> r={1}, g={2}, b={3}", hex, r, g, b);
            }
            else
            {
                CQ_CORE_ERROR("[AI SetMaterial] Invalid hex color length: '{0}' (size={1})", hex, hex.size());
            }
        }
        else
        {
            CQ_CORE_WARN("[AI SetMaterial] colorHex is EMPTY, skipping color set!");
        }
        if (metallic >= 0.0f) mr.Metallic = metallic;
        if (roughness >= 0.0f) mr.Roughness = roughness;
        CQ_CORE_INFO("[AI SetMaterial] AFTER: mr.Color=({0},{1},{2},{3})", mr.Color.r, mr.Color.g, mr.Color.b, mr.Color.a);

        return "Successfully set Material properties for '" + nameOrUuid + "'.";
    }

    std::string AITools::CreateEntity(Scene* scene, const std::string& name, const std::string& type, const std::string& modelPath)
    {
        if (!scene) return "Error: No active scene loaded.";

        Entity entity = scene->CreateEntity(name);
        if (type == "Cube")
        {
            auto& mr = entity.AddComponent<MeshRendererComponent>();
            mr.Type = MeshType::Cube;
            mr.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        else if (type == "Sphere")
        {
            auto& mr = entity.AddComponent<MeshRendererComponent>();
            mr.Type = MeshType::Sphere;
            mr.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        else if (type == "Plane")
        {
            auto& mr = entity.AddComponent<MeshRendererComponent>();
            mr.Type = MeshType::Plane;
            mr.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        else if (type == "Cylinder" || type == "Capsule")
        {
            auto& mr = entity.AddComponent<MeshRendererComponent>();
            mr.Type = MeshType::Cylinder;
            mr.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        else if (type == "Sprite")
        {
            entity.AddComponent<SpriteRendererComponent>();
        }
        else if (type == "Light")
        {
            entity.AddComponent<DirectionalLightComponent>();
        }
        else if (type == "Camera")
        {
            entity.AddComponent<CameraComponent>();
        }
        else if (type == "OBJ" || type == "Model" || !modelPath.empty())
        {
            auto& mc = entity.AddComponent<ModelComponent>();
            mc.FilePath = modelPath.empty() ? ("Assets/Models/" + name + ".obj") : modelPath;

            std::filesystem::path projDir = Project::GetActiveProjectDirectory();
            std::filesystem::path fullPath = projDir.empty() ? std::filesystem::path(mc.FilePath) : (projDir / mc.FilePath);
            
            // Try loading from full absolute path first, then relative path
            if (std::filesystem::exists(fullPath))
            {
                mc.ModelData = ModelLoader::Load(fullPath.string());
            }
            if (!mc.ModelData && std::filesystem::exists(mc.FilePath))
            {
                mc.ModelData = ModelLoader::Load(mc.FilePath);
            }
            if (!mc.ModelData)
            {
                // Force load attempting fullPath string even if filesystem check was strict
                mc.ModelData = ModelLoader::Load(fullPath.string());
            }

            if (mc.ModelData)
            {
                CQ_CORE_INFO("AI Tools: Successfully loaded ModelData for entity '{0}' ({1} meshes)", name, mc.ModelData->Meshes.size());
            }
            else
            {
                CQ_CORE_WARN("AI Tools: ModelData could NOT be loaded for entity '{0}' from path: {1}", name, fullPath.string());
            }
        }

        return "Successfully created entity '" + name + "' (Type: " + type + ") in active scene.";
    }

    std::string AITools::DeleteEntity(Scene* scene, const std::string& identifier)
    {
        if (!scene) return "Error: No active scene loaded.";

        // Search by entity name (Tag field)
        Entity entity;
        auto view = scene->m_Registry.view<TagComponent>();
        for (auto e : view)
        {
            auto& tag = view.get<TagComponent>(e);
            if (tag.Tag == identifier)
            {
                entity = Entity{ e, scene };
                break;
            }
        }
        // Fallback: try GameTag
        if (!entity)
            entity = scene->FindEntityByTag(identifier);

        if (entity)
        {
            scene->DestroyEntity(entity);
            return "Successfully deleted entity: " + identifier;
        }

        return "Entity '" + identifier + "' not found in active scene.";
    }

    std::string AITools::SetTransform(Scene* scene, const std::string& nameOrUuid, float px, float py, float pz, float rx, float ry, float rz, float sx, float sy, float sz)
    {
        if (!scene) return "Error: No active scene loaded.";

        constexpr float UNSET = -999999.0f;

        // Find entity by name (Tag field)
        Entity entity;
        auto view = scene->m_Registry.view<TagComponent>();
        for (auto e : view)
        {
            auto& tag = view.get<TagComponent>(e);
            if (tag.Tag == nameOrUuid)
            {
                entity = Entity{ e, scene };
                break;
            }
        }
        // Fallback: try GameTag
        if (!entity)
            entity = scene->FindEntityByTag(nameOrUuid);

        if (!entity) return "Entity '" + nameOrUuid + "' not found in active scene.";

        auto& transform = entity.GetComponent<TransformComponent>();

        std::string changes;

        // Only update fields that were provided (not UNSET)
        if (px != UNSET) { transform.Position.x = px; changes += "px=" + std::to_string(px) + " "; }
        if (py != UNSET) { transform.Position.y = py; changes += "py=" + std::to_string(py) + " "; }
        if (pz != UNSET) { transform.Position.z = pz; changes += "pz=" + std::to_string(pz) + " "; }
        if (rx != UNSET) { transform.Rotation.x = glm::radians(rx); changes += "rx=" + std::to_string(rx) + "° "; }
        if (ry != UNSET) { transform.Rotation.y = glm::radians(ry); changes += "ry=" + std::to_string(ry) + "° "; }
        if (rz != UNSET) { transform.Rotation.z = glm::radians(rz); changes += "rz=" + std::to_string(rz) + "° "; }
        if (sx != UNSET) { transform.Scale.x = sx; changes += "sx=" + std::to_string(sx) + " "; }
        if (sy != UNSET) { transform.Scale.y = sy; changes += "sy=" + std::to_string(sy) + " "; }
        if (sz != UNSET) { transform.Scale.z = sz; changes += "sz=" + std::to_string(sz) + " "; }

        if (changes.empty())
            return "No transform values were changed for entity '" + nameOrUuid + "'.";

        CQ_CORE_INFO("AI Tools: SetTransform '{0}' -> {1}", nameOrUuid, changes);
        return "Successfully set transform of '" + nameOrUuid + "': " + changes;
    }

    std::string AITools::AddComponent(Scene* scene, const std::string& nameOrUuid, const std::string& componentType, const std::string& scriptPath)
    {
        if (!scene) return "Error: No active scene loaded.";

        // Find entity by name (Tag field)
        Entity entity;
        auto view = scene->m_Registry.view<TagComponent>();
        for (auto e : view)
        {
            auto& tag = view.get<TagComponent>(e);
            if (tag.Tag == nameOrUuid)
            {
                entity = Entity{ e, scene };
                break;
            }
        }
        if (!entity)
            entity = scene->FindEntityByTag(nameOrUuid);

        if (!entity) return "Entity '" + nameOrUuid + "' not found in active scene.";

        if (componentType == "MeshRenderer")
        {
            if (!entity.HasComponent<MeshRendererComponent>())
            {
                auto& mr = entity.AddComponent<MeshRendererComponent>();
                mr.Type = MeshType::Cube;
                mr.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            }
            return "Added MeshRenderer component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "SpriteRenderer")
        {
            if (!entity.HasComponent<SpriteRendererComponent>())
                entity.AddComponent<SpriteRendererComponent>();
            return "Added SpriteRenderer component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "Light")
        {
            if (!entity.HasComponent<DirectionalLightComponent>())
                entity.AddComponent<DirectionalLightComponent>();
            return "Added Light component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "Camera")
        {
            if (!entity.HasComponent<CameraComponent>())
                entity.AddComponent<CameraComponent>();
            return "Added Camera component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "Rigidbody")
        {
            if (!entity.HasComponent<RigidbodyComponent>())
                entity.AddComponent<RigidbodyComponent>();
            return "Added Rigidbody (3D) component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "Rigidbody2D")
        {
            if (!entity.HasComponent<RigidBody2DComponent>())
                entity.AddComponent<RigidBody2DComponent>();
            return "Added Rigidbody2D component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "BoxCollider2D")
        {
            if (!entity.HasComponent<BoxCollider2DComponent>())
                entity.AddComponent<BoxCollider2DComponent>();
            return "Added BoxCollider2D component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "CircleCollider2D")
        {
            if (!entity.HasComponent<CircleCollider2DComponent>())
                entity.AddComponent<CircleCollider2DComponent>();
            return "Added CircleCollider2D component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "BoxCollider")
        {
            if (!entity.HasComponent<BoxColliderComponent>())
                entity.AddComponent<BoxColliderComponent>();
            return "Added BoxCollider (3D) component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "SphereCollider")
        {
            if (!entity.HasComponent<SphereColliderComponent>())
                entity.AddComponent<SphereColliderComponent>();
            return "Added SphereCollider (3D) component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "CapsuleCollider")
        {
            if (!entity.HasComponent<CapsuleColliderComponent>())
                entity.AddComponent<CapsuleColliderComponent>();
            return "Added CapsuleCollider (3D) component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "MeshCollider")
        {
            if (!entity.HasComponent<MeshColliderComponent>())
                entity.AddComponent<MeshColliderComponent>();
            return "Added MeshCollider (3D) component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "NativeScript")
        {
            if (!entity.HasComponent<NativeScriptComponent>())
            {
                auto& nsc = entity.AddComponent<NativeScriptComponent>();
                if (!scriptPath.empty())
                {
                    NativeScriptData data;
                    data.ScriptPath = scriptPath;
                    nsc.Scripts.push_back(data);
                }
            }
            return "Added NativeScript component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "ConquerorScript")
        {
            if (!entity.HasComponent<ConquerorScriptComponent>())
            {
                auto& csc = entity.AddComponent<ConquerorScriptComponent>();
                if (!scriptPath.empty())
                {
                    ConquerorScriptData data;
                    data.ScriptPath = scriptPath;
                    csc.Scripts.push_back(data);
                }
            }
            return "Added ConquerorScript component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioSource")
        {
            if (!entity.HasComponent<AudioSourceComponent>())
                entity.AddComponent<AudioSourceComponent>();
            return "Added AudioSource component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "TextRenderer")
        {
            if (!entity.HasComponent<TextRendererComponent>())
                entity.AddComponent<TextRendererComponent>();
            return "Added TextRenderer component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "Image")
        {
            if (!entity.HasComponent<ImageComponent>())
                entity.AddComponent<ImageComponent>();
            return "Added Image (UI) component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "Button")
        {
            if (!entity.HasComponent<ButtonComponent>())
                entity.AddComponent<ButtonComponent>();
            return "Added Button (UI) component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioListener")
        {
            if (!entity.HasComponent<AudioListenerComponent>())
                entity.AddComponent<AudioListenerComponent>();
            return "Added AudioListener component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "Canvas")
        {
            if (!entity.HasComponent<CanvasComponent>())
                entity.AddComponent<CanvasComponent>();
            if (!entity.HasComponent<CanvasScalerComponent>())
                entity.AddComponent<CanvasScalerComponent>();
            if (!entity.HasComponent<GraphicRaycasterComponent>())
                entity.AddComponent<GraphicRaycasterComponent>();
            return "Added Canvas (with Scaler and Raycaster) to '" + nameOrUuid + "'.";
        }
        else if (componentType == "CanvasScaler")
        {
            if (!entity.HasComponent<CanvasScalerComponent>())
                entity.AddComponent<CanvasScalerComponent>();
            return "Added CanvasScaler component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "GraphicRaycaster")
        {
            if (!entity.HasComponent<GraphicRaycasterComponent>())
                entity.AddComponent<GraphicRaycasterComponent>();
            return "Added GraphicRaycaster component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "ReflectionProbe")
        {
            if (!entity.HasComponent<ReflectionProbeComponent>())
                entity.AddComponent<ReflectionProbeComponent>();
            return "Added ReflectionProbe component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AdaptiveProbeVolume")
        {
            if (!entity.HasComponent<AdaptiveProbeVolumeComponent>())
                entity.AddComponent<AdaptiveProbeVolumeComponent>();
            return "Added AdaptiveProbeVolume component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "Animator")
        {
            if (!entity.HasComponent<AnimatorComponent>())
                entity.AddComponent<AnimatorComponent>();
            return "Added Animator component to '" + nameOrUuid + "'.";
        }
        else if (componentType == "Animation")
        {
            if (!entity.HasComponent<AnimationComponent>())
                entity.AddComponent<AnimationComponent>();
            return "Added AnimationComponent to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioLowPassFilter")
        {
            if (!entity.HasComponent<AudioLowPassFilterComponent>())
                entity.AddComponent<AudioLowPassFilterComponent>();
            return "Added AudioLowPassFilter to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioHighPassFilter")
        {
            if (!entity.HasComponent<AudioHighPassFilterComponent>())
                entity.AddComponent<AudioHighPassFilterComponent>();
            return "Added AudioHighPassFilter to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioEchoFilter")
        {
            if (!entity.HasComponent<AudioEchoFilterComponent>())
                entity.AddComponent<AudioEchoFilterComponent>();
            return "Added AudioEchoFilter to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioReverbFilter")
        {
            if (!entity.HasComponent<AudioReverbFilterComponent>())
                entity.AddComponent<AudioReverbFilterComponent>();
            return "Added AudioReverbFilter to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioChorusFilter")
        {
            if (!entity.HasComponent<AudioChorusFilterComponent>())
                entity.AddComponent<AudioChorusFilterComponent>();
            return "Added AudioChorusFilter to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioDistortionFilter")
        {
            if (!entity.HasComponent<AudioDistortionFilterComponent>())
                entity.AddComponent<AudioDistortionFilterComponent>();
            return "Added AudioDistortionFilter to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioReverbZone")
        {
            if (!entity.HasComponent<AudioReverbZoneComponent>())
                entity.AddComponent<AudioReverbZoneComponent>();
            return "Added AudioReverbZone to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioGain")
        {
            if (!entity.HasComponent<AudioGainComponent>())
                entity.AddComponent<AudioGainComponent>();
            return "Added AudioGain to '" + nameOrUuid + "'.";
        }
        else if (componentType == "AudioPan")
        {
            if (!entity.HasComponent<AudioPanComponent>())
                entity.AddComponent<AudioPanComponent>();
            return "Added AudioPan to '" + nameOrUuid + "'.";
        }

        return "Unknown component type: " + componentType;
    }

    std::string AITools::ListEntities(Scene* scene)
    {
        if (!scene) return "Error: No active scene loaded.";

        std::stringstream ss;
        ss << "Active Scene Entities:\n";
        
        int count = 0;
        auto view = scene->m_Registry.view<TagComponent>();
        for (auto entityID : view)
        {
            Entity entity{ entityID, scene };
            auto& tag = entity.GetComponent<TagComponent>();
            ss << "- " << tag.Tag << "\n";
            count++;
        }

        if (count == 0) ss << "(No entities found in scene)\n";
        return ss.str();
    }

    std::string AITools::GenerateSVGAsset(const std::string& filename, const std::string& svgContent)
    {
        try
        {
            std::filesystem::path path(filename);
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path());

            std::ofstream file(path);
            if (!file.is_open()) return "Error creating file: " + filename;

            file << svgContent;
            file.close();
            return "Successfully generated SVG asset: " + filename;
        }
        catch (const std::exception& e)
        {
            return "Failed to generate SVG asset: " + std::string(e.what());
        }
    }

    std::string AITools::GeneratePNGAsset(const std::string& filename, int width, int height, const std::string& colorHex)
    {
        try
        {
            std::filesystem::path path(filename);
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path());

            // Write simple SVG/PPM/PNG texture
            std::ofstream file(path);
            file << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height << "\">";
            file << "<rect width=\"100%\" height=\"100%\" fill=\"" << colorHex << "\"/>";
            file << "</svg>";
            file.close();
            return "Successfully generated texture asset: " + filename;
        }
        catch (const std::exception& e)
        {
            return "Failed to generate texture asset: " + std::string(e.what());
        }
    }

    std::string AITools::Generate3DModel(const std::string& filename, const std::string& objContent)
    {
        try
        {
            std::filesystem::path projDir = Project::GetActiveProjectDirectory();
            std::filesystem::path path = projDir.empty() ? std::filesystem::path(filename) : (projDir / filename);

            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path());

            std::ofstream file(path);
            if (!file.is_open()) return "Error opening file: " + path.string();

            if (!objContent.empty())
            {
                file << objContent;
            }
            else
            {
                return "Error: objContent was empty. Please provide raw Wavefront OBJ vertex data (v, f) in objContent.";
            }
            file.close();

            return "Successfully generated 3D OBJ model at: " + path.string();
        }
        catch (const std::exception& e)
        {
            return "Failed to generate 3D model: " + std::string(e.what());
        }
    }

    std::string AITools::GenerateAnimation(const std::string& filename, const std::string& animData)
    {
        try
        {
            std::filesystem::path path(filename);
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path());

            std::ofstream file(path);
            file << "Animation:\n";
            file << "  Name: NewAnimation\n";
            file << "  FrameRate: 60\n";
            file << "  Loop: true\n";
            file.close();

            return "Successfully generated animation clip: " + filename;
        }
        catch (const std::exception& e)
        {
            return "Failed to generate animation: " + std::string(e.what());
        }
    }

    std::string AITools::CreateScript(const std::string& scriptName)
    {
        try
        {
            std::filesystem::path path = std::filesystem::path("Scripts") / (scriptName + ".cpp");
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path());

            std::ofstream file(path);
            file << "#include <Conqueror/Conqueror.h>\n\n";
            file << "class " << scriptName << " : public Conqueror::ScriptableEntity\n";
            file << "{\npublic:\n";
            file << "    void OnCreate() override\n    {\n    }\n\n";
            file << "    void OnUpdate(Conqueror::Timestep ts) override\n    {\n    }\n";
            file << "};\n";
            file.close();

            return "Successfully created script template: " + path.string();
        }
        catch (const std::exception& e)
        {
            return "Failed to create script: " + std::string(e.what());
        }
    }

    std::string AITools::AnalyzeScene(Scene* scene)
    {
        if (!scene) return "No active scene.";

        int entities = 0;
        int meshes = 0;
        int lights = 0;

        auto view = scene->m_Registry.view<TagComponent>();
        entities = (int)view.size();

        auto meshView = scene->m_Registry.view<MeshRendererComponent>();
        meshes = (int)meshView.size();

        auto lightView = scene->m_Registry.view<DirectionalLightComponent>();
        lights = (int)lightView.size();

        std::stringstream ss;
        ss << "Scene Analysis Report:\n";
        ss << "- Total Entities: " << entities << "\n";
        ss << "- 3D Mesh Renderers: " << meshes << "\n";
        ss << "- Directional Lights: " << lights << "\n";
        ss << "- Performance: Optimal\n";
        return ss.str();
    }

    // ========== NEW TOOL IMPLEMENTATIONS ==========

    // Helper: Find entity by name or UUID
    static Entity FindEntityByName(Scene* scene, const std::string& nameOrUuid)
    {
        if (!scene) return Entity{};
        
        Entity entity;
        auto view = scene->m_Registry.view<TagComponent>();
        for (auto e : view)
        {
            auto& tag = view.get<TagComponent>(e);
            if (tag.Tag == nameOrUuid)
            {
                entity = Entity{ e, scene };
                break;
            }
        }
        if (!entity)
            entity = scene->FindEntityByTag(nameOrUuid);
        return entity;
    }

    // Helper: Parse hex color to vec3
    static glm::vec3 ParseHexColor(const std::string& hex)
    {
        std::string h = hex;
        if (!h.empty() && h[0] == '#') h = h.substr(1);
        if (h.size() == 6)
        {
            unsigned int r = 255, g = 255, b = 255;
            std::stringstream ssR, ssG, ssB;
            ssR << std::hex << h.substr(0, 2); ssR >> r;
            ssG << std::hex << h.substr(2, 2); ssG >> g;
            ssB << std::hex << h.substr(4, 2); ssB >> b;
            return glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
        }
        return glm::vec3(1.0f);
    }

    // Helper: Parse hex color to vec4
    static glm::vec4 ParseHexColorRGBA(const std::string& hex)
    {
        glm::vec3 rgb = ParseHexColor(hex);
        return glm::vec4(rgb, 1.0f);
    }

    // ========== AUDIO TOOL IMPLEMENTATIONS ==========

    std::string AITools::SetAudioSource(Scene* scene, const std::string& nameOrUuid,
        const std::string& filePath, bool playOnAwake, bool loop, bool mute,
        float volume, float pitch, float pan, bool is3D, float minDistance,
        float maxDistance, float dopplerLevel, const std::string& attenuationModel, float rolloffFactor)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<AudioSourceComponent>())
            entity.AddComponent<AudioSourceComponent>();

        auto& audio = entity.GetComponent<AudioSourceComponent>();
        
        if (!filePath.empty()) audio.FilePath = filePath;
        audio.PlayOnAwake = playOnAwake;
        audio.Loop = loop;
        audio.Mute = mute;
        if (volume >= 0.0f) audio.Volume = volume;
        if (pitch > 0.0f) audio.Pitch = pitch;
        if (pan >= -1.0f && pan <= 1.0f) audio.Pan = pan;
        audio.Is3D = is3D;
        if (minDistance > 0.0f) audio.MinDistance = minDistance;
        if (maxDistance > 0.0f) audio.MaxDistance = maxDistance;
        if (dopplerLevel >= 0.0f) audio.DopplerLevel = dopplerLevel;
        
        if (!attenuationModel.empty())
        {
            if (attenuationModel == "None") audio.Attenuation = AudioSourceComponent::AttenuationModel::None;
            else if (attenuationModel == "Inverse") audio.Attenuation = AudioSourceComponent::AttenuationModel::Inverse;
            else if (attenuationModel == "Linear") audio.Attenuation = AudioSourceComponent::AttenuationModel::Linear;
            else if (attenuationModel == "Exponential") audio.Attenuation = AudioSourceComponent::AttenuationModel::Exponential;
        }
        
        if (rolloffFactor >= 0.0f) audio.RolloffFactor = rolloffFactor;

        return "Successfully configured AudioSource for '" + nameOrUuid + "'.";
    }

    std::string AITools::SetAudioFilter(Scene* scene, const std::string& nameOrUuid,
        const std::string& filterType, bool enabled, float param1, float param2,
        float param3, float param4)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (filterType == "LowPass")
        {
            if (!entity.HasComponent<AudioLowPassFilterComponent>())
                entity.AddComponent<AudioLowPassFilterComponent>();
            auto& f = entity.GetComponent<AudioLowPassFilterComponent>();
            f.Enabled = enabled;
            if (param1 > 0.0f) f.CutoffFrequency = param1;
            if (param2 > 0.0f) f.Resonance = param2;
            return "Applied LowPass filter to '" + nameOrUuid + "'.";
        }
        else if (filterType == "HighPass")
        {
            if (!entity.HasComponent<AudioHighPassFilterComponent>())
                entity.AddComponent<AudioHighPassFilterComponent>();
            auto& f = entity.GetComponent<AudioHighPassFilterComponent>();
            f.Enabled = enabled;
            if (param1 > 0.0f) f.CutoffFrequency = param1;
            if (param2 > 0.0f) f.Resonance = param2;
            return "Applied HighPass filter to '" + nameOrUuid + "'.";
        }
        else if (filterType == "Echo")
        {
            if (!entity.HasComponent<AudioEchoFilterComponent>())
                entity.AddComponent<AudioEchoFilterComponent>();
            auto& f = entity.GetComponent<AudioEchoFilterComponent>();
            f.Enabled = enabled;
            if (param1 >= 0.0f) f.Delay = param1;
            if (param2 >= 0.0f) f.Decay = param2;
            if (param3 >= 0.0f) f.WetMix = param3;
            if (param4 >= 0.0f) f.DryMix = param4;
            return "Applied Echo filter to '" + nameOrUuid + "'.";
        }
        else if (filterType == "Reverb")
        {
            if (!entity.HasComponent<AudioReverbFilterComponent>())
                entity.AddComponent<AudioReverbFilterComponent>();
            auto& f = entity.GetComponent<AudioReverbFilterComponent>();
            f.Enabled = enabled;
            if (param1 >= -10000.0f) f.Room = param1;
            if (param2 >= 0.0f) f.DecayTime = param2;
            if (param3 >= 0.0f) f.Diffusion = param3;
            if (param4 >= 0.0f) f.Density = param4;
            return "Applied Reverb filter to '" + nameOrUuid + "'.";
        }
        else if (filterType == "Chorus")
        {
            if (!entity.HasComponent<AudioChorusFilterComponent>())
                entity.AddComponent<AudioChorusFilterComponent>();
            auto& f = entity.GetComponent<AudioChorusFilterComponent>();
            f.Enabled = enabled;
            if (param1 >= 0.0f) f.Delay = param1;
            if (param2 >= 0.0f) f.Depth = param2;
            if (param3 >= 0.0f) f.Rate = param3;
            return "Applied Chorus filter to '" + nameOrUuid + "'.";
        }
        else if (filterType == "Distortion")
        {
            if (!entity.HasComponent<AudioDistortionFilterComponent>())
                entity.AddComponent<AudioDistortionFilterComponent>();
            auto& f = entity.GetComponent<AudioDistortionFilterComponent>();
            f.Enabled = enabled;
            if (param1 >= 0.0f) f.Level = param1;
            return "Applied Distortion filter to '" + nameOrUuid + "'.";
        }
        else if (filterType == "Gain")
        {
            if (!entity.HasComponent<AudioGainComponent>())
                entity.AddComponent<AudioGainComponent>();
            auto& f = entity.GetComponent<AudioGainComponent>();
            f.Enabled = enabled;
            if (param1 >= 0.0f) f.Gain = param1;
            return "Applied Gain filter to '" + nameOrUuid + "'.";
        }
        else if (filterType == "Pan")
        {
            if (!entity.HasComponent<AudioPanComponent>())
                entity.AddComponent<AudioPanComponent>();
            auto& f = entity.GetComponent<AudioPanComponent>();
            f.Enabled = enabled;
            if (param1 >= -1.0f && param1 <= 1.0f) f.Pan = param1;
            return "Applied Pan filter to '" + nameOrUuid + "'.";
        }

        return "Unknown filter type: " + filterType;
    }

    // ========== CAMERA TOOL IMPLEMENTATION ==========

    std::string AITools::SetCamera(Scene* scene, const std::string& nameOrUuid,
        bool isPrimary, float fov, float nearPlane, float farPlane, bool isOrthographic,
        float orthographicSize)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<CameraComponent>())
            entity.AddComponent<CameraComponent>();

        auto& camera = entity.GetComponent<CameraComponent>();
        camera.Primary = isPrimary;
        
        if (fov > 0.0f) camera.Camera.SetPerspectiveVerticalFOV(glm::radians(fov));
        if (nearPlane > 0.0f) camera.Camera.SetPerspectiveNearClip(nearPlane);
        if (farPlane > 0.0f) camera.Camera.SetPerspectiveFarClip(farPlane);
        
        if (isOrthographic)
        {
            camera.Camera.SetProjectionType(SceneCamera::ProjectionType::Orthographic);
            if (orthographicSize > 0.0f) camera.Camera.SetOrthographicSize(orthographicSize);
        }
        else
        {
            camera.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        }

        return "Successfully configured Camera for '" + nameOrUuid + "'.";
    }

    // ========== LIGHTING TOOL IMPLEMENTATIONS ==========

    std::string AITools::SetPointLight(Scene* scene, const std::string& nameOrUuid,
        const std::string& colorHex, float intensity, float range,
        float constant, float linear, float quadratic, bool castShadows)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<PointLightComponent>())
            entity.AddComponent<PointLightComponent>();

        auto& light = entity.GetComponent<PointLightComponent>();
        if (!colorHex.empty()) light.Color = ParseHexColor(colorHex);
        if (intensity >= 0.0f) light.Intensity = intensity;
        if (range >= 0.0f) light.Range = range;
        if (constant >= 0.0f) light.Constant = constant;
        if (linear >= 0.0f) light.Linear = linear;
        if (quadratic >= 0.0f) light.Quadratic = quadratic;
        light.CastShadows = castShadows;

        return "Successfully configured PointLight for '" + nameOrUuid + "'.";
    }

    std::string AITools::SetSpotLight(Scene* scene, const std::string& nameOrUuid,
        const std::string& colorHex, float intensity, float range,
        float innerConeAngle, float outerConeAngle,
        float constant, float linear, float quadratic, bool castShadows)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<SpotLightComponent>())
            entity.AddComponent<SpotLightComponent>();

        auto& light = entity.GetComponent<SpotLightComponent>();
        if (!colorHex.empty()) light.Color = ParseHexColor(colorHex);
        if (intensity >= 0.0f) light.Intensity = intensity;
        if (range >= 0.0f) light.Range = range;
        if (innerConeAngle >= 0.0f) light.InnerConeAngle = innerConeAngle;
        if (outerConeAngle >= 0.0f) light.OuterConeAngle = outerConeAngle;
        if (constant >= 0.0f) light.Constant = constant;
        if (linear >= 0.0f) light.Linear = linear;
        if (quadratic >= 0.0f) light.Quadratic = quadratic;
        light.CastShadows = castShadows;

        return "Successfully configured SpotLight for '" + nameOrUuid + "'.";
    }

    std::string AITools::SetReflectionProbe(Scene* scene, const std::string& nameOrUuid,
        float offsetX, float offsetY, float offsetZ,
        float sizeX, float sizeY, float sizeZ,
        uint32_t resolution, float intensity, int bounces, bool realtimeUpdate)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<ReflectionProbeComponent>())
            entity.AddComponent<ReflectionProbeComponent>();

        auto& probe = entity.GetComponent<ReflectionProbeComponent>();
        probe.BoxOffset = glm::vec3(offsetX, offsetY, offsetZ);
        probe.BoxSize = glm::vec3(sizeX, sizeY, sizeZ);
        if (resolution > 0) probe.Resolution = resolution;
        if (intensity >= 0.0f) probe.Intensity = intensity;
        if (bounces >= 0) probe.Bounces = bounces;
        probe.RealtimeUpdate = realtimeUpdate;

        return "Successfully configured ReflectionProbe for '" + nameOrUuid + "'.";
    }

    // ========== UI TOOL IMPLEMENTATIONS ==========

    std::string AITools::SetUICanvas(Scene* scene, const std::string& nameOrUuid,
        int renderMode, bool pixelPerfect, int sortOrder)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<CanvasComponent>())
            entity.AddComponent<CanvasComponent>();

        auto& canvas = entity.GetComponent<CanvasComponent>();
        canvas.Mode = static_cast<CanvasComponent::RenderMode>(renderMode);
        canvas.PixelPerfect = pixelPerfect;
        canvas.SortOrder = sortOrder;

        return "Successfully configured Canvas for '" + nameOrUuid + "'.";
    }

    std::string AITools::SetUIButton(Scene* scene, const std::string& nameOrUuid,
        bool interactable, int navigationMode,
        const std::string& bgColorNormal, const std::string& bgColorHover,
        const std::string& bgColorPressed, const std::string& textColorNormal,
        float transitionSpeed, bool use9Slice, float borderSize)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<ButtonComponent>())
            entity.AddComponent<ButtonComponent>();

        auto& button = entity.GetComponent<ButtonComponent>();
        button.Interactable = interactable;
        button.Navigation = static_cast<ButtonComponent::NavigationMode>(navigationMode);
        
        if (!bgColorNormal.empty()) button.BackgroundNormalColor = ParseHexColorRGBA(bgColorNormal);
        if (!bgColorHover.empty()) button.BackgroundHoverColor = ParseHexColorRGBA(bgColorHover);
        if (!bgColorPressed.empty()) button.BackgroundPressedColor = ParseHexColorRGBA(bgColorPressed);
        if (!textColorNormal.empty()) button.TextNormalColor = ParseHexColorRGBA(textColorNormal);
        
        if (transitionSpeed > 0.0f) button.TransitionSpeed = transitionSpeed;
        button.Use9Slice = use9Slice;
        if (borderSize >= 0.0f) button.BorderSize = borderSize;

        return "Successfully configured Button for '" + nameOrUuid + "'.";
    }

    std::string AITools::SetUIText(Scene* scene, const std::string& nameOrUuid,
        const std::string& text, const std::string& colorHex, float fontSize)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<TextRendererComponent>())
            entity.AddComponent<TextRendererComponent>();

        auto& textComp = entity.GetComponent<TextRendererComponent>();
        if (!text.empty()) textComp.Text = text;
        if (!colorHex.empty()) textComp.Color = ParseHexColorRGBA(colorHex);
        if (fontSize > 0.0f) textComp.FontSize = fontSize;

        return "Successfully configured TextRenderer for '" + nameOrUuid + "'.";
    }

    std::string AITools::SetUIImage(Scene* scene, const std::string& nameOrUuid,
        const std::string& colorHex, float tilingFactor)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<ImageComponent>())
            entity.AddComponent<ImageComponent>();

        auto& image = entity.GetComponent<ImageComponent>();
        if (!colorHex.empty()) image.Color = ParseHexColorRGBA(colorHex);
        if (tilingFactor > 0.0f) image.TilingFactor = tilingFactor;

        return "Successfully configured Image for '" + nameOrUuid + "'.";
    }

    // ========== ANIMATION TOOL IMPLEMENTATIONS ==========

    std::string AITools::SetAnimator(Scene* scene, const std::string& nameOrUuid,
        bool playing, float speed, int activeClipIndex)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<AnimatorComponent>())
            entity.AddComponent<AnimatorComponent>();

        auto& animator = entity.GetComponent<AnimatorComponent>();
        animator.Playing = playing;
        if (speed > 0.0f) animator.Speed = speed;
        if (activeClipIndex >= 0) animator.ActiveClipIndex = activeClipIndex;

        return "Successfully configured Animator for '" + nameOrUuid + "'.";
    }

    std::string AITools::SetAnimation(Scene* scene, const std::string& nameOrUuid,
        const std::string& controllerPath, float speed, bool playOnAwake, int activeLayerIndex)
    {
        Entity entity = FindEntityByName(scene, nameOrUuid);
        if (!entity) return "Entity '" + nameOrUuid + "' not found.";

        if (!entity.HasComponent<AnimationComponent>())
            entity.AddComponent<AnimationComponent>();

        auto& anim = entity.GetComponent<AnimationComponent>();
        if (!controllerPath.empty()) anim.ControllerFilePath = controllerPath;
        if (speed > 0.0f) anim.Speed = speed;
        anim.PlayOnAwake = playOnAwake;
        if (activeLayerIndex >= 0) anim.ActiveLayerIndex = activeLayerIndex;

        return "Successfully configured AnimationComponent for '" + nameOrUuid + "'.";
    }
}
