#include "Material.h"
#include "Core/ShaderSystem/CQShaderLoader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>
#include <queue>
#include <algorithm>

namespace Conqueror
{
    static std::string ReadFile(const std::string& filepath)
    {
        auto cqSource = CQShaderLoader::Load(filepath);
        return cqSource.Source;
    }

    void Material::CompileShaderGraph()
    {
        CQ_CORE_INFO("[ShaderGraph] CompileShaderGraph started - Material: '{0}'", Name);
        CQ_CORE_INFO("[ShaderGraph] Graph node count: {0}, link count: {1}", GraphNodes.size(), GraphLinks.size());

        std::string pbrSource = ReadFile("Engine/src/Shaders/3D/Forward/PBR.cqsh");
        if (pbrSource.empty())
        {
            CQ_CORE_ERROR("[ShaderGraph] PBR.cqsh could not be read or is empty! CompileShaderGraph aborted.");
            return;
        }
        CQ_CORE_INFO("[ShaderGraph] PBR.cqsh loaded, size: {0} bytes", pbrSource.size());

        ShaderGraphNode* masterNode = nullptr;
        for (auto& node : GraphNodes)
        {
            if (node.Type == ShaderGraphNodeType::MasterOutput)
            {
                masterNode = &node;
                break;
            }
        }
        if (!masterNode)
        {
            CQ_CORE_ERROR("[ShaderGraph] MasterOutput node not found! CompileShaderGraph aborted.");
            return;
        }
        CQ_CORE_INFO("[ShaderGraph] MasterOutput node found, ID: {0}", masterNode->ID);

        std::string injectedUniforms = "";
        std::string injectedEvaluation = "";

        std::unordered_map<int, std::string> nodeOutputs;
        std::set<std::string> declaredUniforms;

        std::unordered_map<int, std::vector<int>> dependencies;
        std::unordered_map<int, ShaderGraphNode*> nodeMap;
        for (auto& node : GraphNodes) nodeMap[node.ID] = &node;

        for (const auto& link : GraphLinks)
        {
            dependencies[link.ToNodeID].push_back(link.FromNodeID);
        }

        std::vector<int> sortedNodes;
        std::set<int> visited;
        std::function<void(int)> visit = [&](int nodeID) {
            if (visited.count(nodeID)) return;
            visited.insert(nodeID);
            for (int dep : dependencies[nodeID]) visit(dep);
            sortedNodes.push_back(nodeID);
        };
        visit(masterNode->ID);

        CQ_CORE_INFO("[ShaderGraph] Topological sort complete, sorted node count: {0}", sortedNodes.size());
        for (int nid : sortedNodes)
        {
            if (nid == masterNode->ID) continue;
            ShaderGraphNode* n = nodeMap[nid];
            CQ_CORE_INFO("[ShaderGraph]   Node {0}: type={1}", nid, (int)n->Type);
        }

        for (int nodeID : sortedNodes)
        {
            if (nodeID == masterNode->ID) continue;

            ShaderGraphNode* node = nodeMap[nodeID];
            std::string nodeVar = "node_" + std::to_string(nodeID);
            std::string code = "";

            auto getInputValue = [&](int pinIndex, const std::string& defaultVal) -> std::string {
                for (const auto& link : GraphLinks)
                {
                    if (link.ToNodeID == nodeID && link.ToPinIndex == pinIndex)
                        return nodeOutputs[link.FromNodeID];
                }
                return defaultVal;
            };

            auto addUniform = [&](const std::string& u) {
                if (declaredUniforms.insert(u).second)
                    injectedUniforms += u;
            };

            switch (node->Type)
            {
                case ShaderGraphNodeType::Color:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform vec4 u_Node" + id + "_Color;\n");
                    code = "vec4 " + nodeVar + " = u_Node" + id + "_Color;\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::Float:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform float u_Node" + id + "_Float;\n");
                    code = "float " + nodeVar + " = u_Node" + id + "_Float;\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::Vec2:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform vec2 u_Node" + id + "_Vec2;\n");
                    code = "vec2 " + nodeVar + " = u_Node" + id + "_Vec2;\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::Vec3:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform vec3 u_Node" + id + "_Vec3;\n");
                    code = "vec3 " + nodeVar + " = u_Node" + id + "_Vec3;\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::TextureSample:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform sampler2D u_Node" + id + "_Tex;\n");
                    std::string uv = getInputValue(0, "fs_in.TexCoord");
                    code = "vec4 " + nodeVar + " = texture(u_Node" + id + "_Tex, " + uv + ");\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::NormalMap:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform sampler2D u_Node" + id + "_Tex;\n");
                    std::string uv = getInputValue(0, "fs_in.TexCoord");
                    code = "vec4 " + nodeVar + " = vec4(vec3(texture(u_Node" + id + "_Tex, " + uv + ").rgb) * 2.0 - 1.0, 1.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::Add:
                    code = "vec4 " + nodeVar + " = vec4(" + getInputValue(0, "vec4(0.0)") + ") + vec4(" + getInputValue(1, "vec4(0.0)") + ");\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Subtract:
                    code = "vec4 " + nodeVar + " = vec4(" + getInputValue(0, "vec4(0.0)") + ") - vec4(" + getInputValue(1, "vec4(0.0)") + ");\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Multiply:
                    code = "vec4 " + nodeVar + " = vec4(" + getInputValue(0, "vec4(1.0)") + ") * vec4(" + getInputValue(1, "vec4(1.0)") + ");\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Divide:
                    code = "vec4 " + nodeVar + " = vec4(" + getInputValue(0, "vec4(1.0)") + ") / (vec4(" + getInputValue(1, "vec4(1.0)") + ") + 0.00001);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Power:
                    code = "vec4 " + nodeVar + " = pow(max(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(0.0)), vec4(" + getInputValue(1, "vec4(1.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Sqrt:
                    code = "vec4 " + nodeVar + " = sqrt(max(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(0.0)));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Abs:
                    code = "vec4 " + nodeVar + " = abs(vec4(" + getInputValue(0, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Sign:
                    code = "vec4 " + nodeVar + " = sign(vec4(" + getInputValue(0, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Ceil:
                    code = "vec4 " + nodeVar + " = ceil(vec4(" + getInputValue(0, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Floor:
                    code = "vec4 " + nodeVar + " = floor(vec4(" + getInputValue(0, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Round:
                    code = "vec4 " + nodeVar + " = floor(vec4(" + getInputValue(0, "vec4(0.0)") + ") + 0.5);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Mod:
                    code = "vec4 " + nodeVar + " = mod(vec4(" + getInputValue(0, "vec4(0.0)") + "), max(vec4(" + getInputValue(1, "vec4(1.0)") + "), vec4(0.00001)));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Min:
                    code = "vec4 " + nodeVar + " = min(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(" + getInputValue(1, "vec4(1.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Max:
                    code = "vec4 " + nodeVar + " = max(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(" + getInputValue(1, "vec4(1.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Step:
                    code = "vec4 " + nodeVar + " = step(vec4(" + getInputValue(0, "vec4(0.5)") + "), vec4(" + getInputValue(1, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Smoothstep:
                    code = "vec4 " + nodeVar + " = smoothstep(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(" + getInputValue(1, "vec4(1.0)") + "), vec4(" + getInputValue(2, "vec4(0.5)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Clamp:
                    code = "vec4 " + nodeVar + " = clamp(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(" + getInputValue(1, "vec4(0.0)") + "), vec4(" + getInputValue(2, "vec4(1.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Saturate:
                    code = "vec4 " + nodeVar + " = clamp(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(0.0), vec4(1.0));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::OneMinus:
                    code = "vec4 " + nodeVar + " = 1.0 - vec4(" + getInputValue(0, "vec4(0.0)") + ");\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Mix:
                    code = "vec4 " + nodeVar + " = mix(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(" + getInputValue(1, "vec4(1.0)") + "), vec4(" + getInputValue(2, "vec4(0.5)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Sin:
                    code = "vec4 " + nodeVar + " = sin(vec4(" + getInputValue(0, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Cos:
                    code = "vec4 " + nodeVar + " = cos(vec4(" + getInputValue(0, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Tan:
                    code = "vec4 " + nodeVar + " = tan(vec4(" + getInputValue(0, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Asin:
                    code = "vec4 " + nodeVar + " = asin(clamp(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(-1.0), vec4(1.0)));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Acos:
                    code = "vec4 " + nodeVar + " = acos(clamp(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(-1.0), vec4(1.0)));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Atan:
                    code = "vec4 " + nodeVar + " = atan(vec4(" + getInputValue(0, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Dot:
                    code = "float " + nodeVar + " = dot(vec3(" + getInputValue(0, "vec3(0.0)") + "), vec3(" + getInputValue(1, "vec3(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Cross:
                    code = "vec3 " + nodeVar + " = cross(vec3(" + getInputValue(0, "vec3(0,1,0)") + "), vec3(" + getInputValue(1, "vec3(0,0,1)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Normalize:
                    code = "vec3 " + nodeVar + " = normalize(vec3(" + getInputValue(0, "vec3(0.0)") + ") + 0.0001);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Length:
                    code = "float " + nodeVar + " = length(vec3(" + getInputValue(0, "vec3(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Distance:
                    code = "float " + nodeVar + " = distance(vec3(" + getInputValue(0, "vec3(0.0)") + "), vec3(" + getInputValue(1, "vec3(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Reflect:
                    code = "vec3 " + nodeVar + " = reflect(vec3(" + getInputValue(0, "vec3(0,1,0)") + "), vec3(" + getInputValue(1, "vec3(0,1,0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Refract:
                    code = "vec3 " + nodeVar + " = refract(vec3(" + getInputValue(0, "vec3(0,1,0)") + "), vec3(" + getInputValue(1, "vec3(0,1,0)") + "), float(" + getInputValue(2, "1.0") + ".r));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::UV:
                    code = "vec2 " + nodeVar + " = fs_in.TexCoord;\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::WorldPosition:
                    code = "vec3 " + nodeVar + " = fs_in.FragPos;\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::ViewDirection:
                    code = "vec3 " + nodeVar + " = normalize(u_CameraPos - fs_in.FragPos);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::NormalVector:
                    code = "vec3 " + nodeVar + " = normalize(fs_in.Normal);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Time:
                    addUniform("uniform float u_Time;\n");
                    code = "float " + nodeVar + " = u_Time;\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Fresnel:
                    code = "float " + nodeVar + " = pow(1.0 - max(dot(normalize(u_CameraPos - fs_in.FragPos), normalize(fs_in.Normal)), 0.0), " + getInputValue(0, "5.0") + ");\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Panner:
                    addUniform("uniform float u_Time;\n");
                    code = "vec2 " + nodeVar + " = vec2(" + getInputValue(0, "fs_in.TexCoord") + ") + vec2(" + getInputValue(1, "vec2(0.1)") + ") * u_Time;\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Noise:
                    code = "vec4 " + nodeVar + " = vec4(vec3(fract(sin(dot(vec3(" + getInputValue(0, "fs_in.FragPos") + "), vec3(12.9898, 78.233, 45.164))) * 43758.5453)), 1.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::TextureObject:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform sampler2D u_Node" + id + "_Tex;\n");
                    code = "vec4 " + nodeVar + " = vec4(1.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::Fract:
                    code = "vec4 " + nodeVar + " = fract(vec4(" + getInputValue(0, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Reciprocal:
                    code = "vec4 " + nodeVar + " = 1.0 / (vec4(" + getInputValue(0, "vec4(1.0)") + ") + 0.00001);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Square:
                    code = "vec4 " + nodeVar + " = vec4(" + getInputValue(0, "vec4(0.0)") + ") * vec4(" + getInputValue(0, "vec4(0.0)") + ");\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Log:
                    code = "vec4 " + nodeVar + " = log(max(vec4(" + getInputValue(0, "vec4(1.0)") + "), vec4(0.00001)));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Exp:
                    code = "vec4 " + nodeVar + " = exp(vec4(" + getInputValue(0, "vec4(0.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::ArcTan2:
                    code = "vec4 " + nodeVar + " = atan(vec4(" + getInputValue(0, "vec4(0.0)") + "), vec4(" + getInputValue(1, "vec4(1.0)") + "));\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::If:
                    code = "vec4 " + nodeVar + " = vec4(\n"
                         "  vec4(" + getInputValue(0, "vec4(0.0)") + ").r > vec4(" + getInputValue(1, "vec4(0.0)") + ").r ? vec4(" + getInputValue(2, "vec4(1.0)") + ").r : vec4(" + getInputValue(3, "vec4(0.0)") + ").r,\n"
                         "  vec4(" + getInputValue(0, "vec4(0.0)") + ").g > vec4(" + getInputValue(1, "vec4(0.0)") + ").g ? vec4(" + getInputValue(2, "vec4(1.0)") + ").g : vec4(" + getInputValue(3, "vec4(0.0)") + ").g,\n"
                         "  vec4(" + getInputValue(0, "vec4(0.0)") + ").b > vec4(" + getInputValue(1, "vec4(0.0)") + ").b ? vec4(" + getInputValue(2, "vec4(1.0)") + ").b : vec4(" + getInputValue(3, "vec4(0.0)") + ").b,\n"
                         "  1.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Append:
                {
                    std::string a = getInputValue(0, "vec4(0.0)");
                    std::string b = getInputValue(1, "vec4(0.0)");
                    code = "vec4 " + nodeVar + " = vec4(vec2(" + a + ").xy, vec2(" + b + ").xy);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::ComponentMask:
                {
                    int mask = node->IntValue;
                    std::string r = (mask & 1) ? ".r" : "0.0";
                    std::string g = (mask & 2) ? ".g" : "0.0";
                    std::string b = (mask & 4) ? ".b" : "0.0";
                    std::string a = (mask & 8) ? ".a" : "1.0";
                    std::string src = getInputValue(0, "vec4(0.0)");
                    code = "vec4 " + nodeVar + " = vec4(" + src + r + ", " + src + g + ", " + src + b + ", " + src + a + ");\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::TextureCoordinate:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform vec2 u_Node" + id + "_Tiling;\n");
                    addUniform("uniform vec2 u_Node" + id + "_Offset;\n");
                    code = "vec2 " + nodeVar + " = fs_in.TexCoord * u_Node" + id + "_Tiling + u_Node" + id + "_Offset;\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::LightVector:
                    code = "vec3 " + nodeVar + " = normalize(-u_DirLight.Direction);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::VertexColor:
                    code = "vec4 " + nodeVar + " = vec4(1.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::Desaturation:
                {
                    std::string src = "vec4(" + getInputValue(0, "vec4(0.0)") + ")";
                    std::string amt = "vec4(" + getInputValue(1, "vec4(0.5)") + ")";
                    code = "vec4 " + nodeVar + " = vec4(mix(" + src + ".rgb, vec3(dot(" + src + ".rgb, vec3(0.2126, 0.7152, 0.0722))), " + amt + ".r), " + src + ".a);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::DepthFade:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform float u_Node" + id + "_Distance;\n");
                    code = "float " + nodeVar + " = clamp((gl_FragCoord.z / gl_FragCoord.w) / u_Node" + id + "_Distance, 0.0, 1.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::SphereMask:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform float u_Node" + id + "_Radius;\n");
                    addUniform("uniform float u_Node" + id + "_Hardness;\n");
                    std::string pos = getInputValue(0, "vec4(fs_in.FragPos, 1.0)");
                    code = "float " + nodeVar + "_d = distance(vec3(" + pos + "), vec3(0.0));\n"
                         "float " + nodeVar + " = clamp((u_Node" + id + "_Radius - " + nodeVar + "_d) / (1.0 - u_Node" + id + "_Hardness + 0.0001), 0.0, 1.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::BoxMask:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform vec3 u_Node" + id + "_Size;\n");
                    addUniform("uniform float u_Node" + id + "_Softness;\n");
                    std::string pos = getInputValue(0, "vec4(fs_in.FragPos, 1.0)");
                    code = "vec3 " + nodeVar + "_abs = abs(vec3(" + pos + "));\n"
                         "vec3 " + nodeVar + "_d = " + nodeVar + "_abs - u_Node" + id + "_Size;\n"
                         "float " + nodeVar + " = clamp(1.0 - (max(max(" + nodeVar + "_d.x, " + nodeVar + "_d.y), " + nodeVar + "_d.z) + u_Node" + id + "_Softness) / (u_Node" + id + "_Softness + 0.0001), 0.0, 1.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::CameraDepthFade:
                {
                    std::string id = std::to_string(nodeID);
                    addUniform("uniform float u_Node" + id + "_NearDistance;\n");
                    addUniform("uniform float u_Node" + id + "_FadeDistance;\n");
                    code = "float " + nodeVar + " = clamp((length(u_CameraPos - fs_in.FragPos) - u_Node" + id + "_NearDistance) / u_Node" + id + "_FadeDistance, 0.0, 1.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                case ShaderGraphNodeType::ObjectPosition:
                    code = "vec3 " + nodeVar + " = vec3(u_Transform[3][0], u_Transform[3][1], u_Transform[3][2]);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::ObjectBounds:
                    code = "vec3 " + nodeVar + " = vec3(1.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                case ShaderGraphNodeType::CustomExpression:
                {
                    std::string custom = node->CustomCode;
                    if (custom.find("vec4") != std::string::npos)
                        code = "vec4 " + nodeVar + " = " + custom + ";\n";
                    else if (custom.find("vec3") != std::string::npos)
                        code = "vec4 " + nodeVar + " = vec4(" + custom + ", 1.0);\n";
                    else
                        code = "vec4 " + nodeVar + " = vec4(" + custom + ");\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
                }
                default:
                    code = "vec4 " + nodeVar + " = vec4(0.0);\n";
                    nodeOutputs[nodeID] = nodeVar;
                    break;
            }
            injectedEvaluation += code;
        }

        auto getMasterInput = [&](int pinIndex) -> std::string {
            for (const auto& link : GraphLinks)
            {
                if (link.ToNodeID == masterNode->ID && link.ToPinIndex == pinIndex)
                    return nodeOutputs[link.FromNodeID];
            }
            return "";
        };

        std::string albedoOut = getMasterInput(0);
        std::string normalOut = getMasterInput(1);
        std::string metallicOut = getMasterInput(2);
        std::string roughnessOut = getMasterInput(3);
        std::string aoOut = getMasterInput(4);
        std::string emissionOut = getMasterInput(5);

        if (!albedoOut.empty()) injectedEvaluation += "albedo = vec3(" + albedoOut + ");\n";
        if (!normalOut.empty()) injectedEvaluation += "N = normalize(fs_in.TBN * (vec3(" + normalOut + ") * 2.0 - 1.0));\n";
        if (!metallicOut.empty()) injectedEvaluation += "metallic = float(" + metallicOut + ".r);\n";
        if (!roughnessOut.empty()) injectedEvaluation += "roughness = float(" + roughnessOut + ".r);\n";
        if (!aoOut.empty()) injectedEvaluation += "ao = float(" + aoOut + ".r);\n";
        if (!emissionOut.empty()) injectedEvaluation += "emission = vec3(" + emissionOut + ");\n";

        CQ_CORE_INFO("[ShaderGraph] Injection complete. Uniform length: {0}, Evaluation length: {1}", injectedUniforms.size(), injectedEvaluation.size());
        CQ_CORE_INFO("[ShaderGraph] Injected Uniforms:\n{0}", injectedUniforms);
        CQ_CORE_INFO("[ShaderGraph] Injected Evaluation:\n{0}", injectedEvaluation);

        size_t uniformPos = pbrSource.find("// #GRAPH_INJECT_UNIFORMS#");
        if (uniformPos != std::string::npos)
        {
            pbrSource.replace(uniformPos, 26, injectedUniforms);
            CQ_CORE_INFO("[ShaderGraph] // #GRAPH_INJECT_UNIFORMS# found and replaced (position: {0})", uniformPos);
        }
        else
        {
            CQ_CORE_WARN("[ShaderGraph] // #GRAPH_INJECT_UNIFORMS# marker not found!");
        }

        size_t evalPos = pbrSource.find("// #GRAPH_INJECT_EVALUATION#");
        if (evalPos != std::string::npos)
        {
            pbrSource.replace(evalPos, 28, injectedEvaluation);
            CQ_CORE_INFO("[ShaderGraph] // #GRAPH_INJECT_EVALUATION# found and replaced (position: {0})", evalPos);
        }
        else
        {
            CQ_CORE_WARN("[ShaderGraph] // #GRAPH_INJECT_EVALUATION# marker not found!");
        }

        std::string typeToken = "#type ";
        size_t pos = pbrSource.find(typeToken, 0);
        std::string vertexSrc = "";
        std::string fragmentSrc = "";
        while (pos != std::string::npos)
        {
            size_t eol = pbrSource.find_first_of("\r\n", pos);
            size_t begin = pos + typeToken.length();
            std::string type = pbrSource.substr(begin, eol - begin);

            size_t nextLinePos = pbrSource.find_first_not_of("\r\n", eol);
            pos = pbrSource.find(typeToken, nextLinePos);

            std::string src = (pos == std::string::npos) ? pbrSource.substr(nextLinePos) : pbrSource.substr(nextLinePos, pos - nextLinePos);

            if (type == "vertex") vertexSrc = src;
            else if (type == "fragment") fragmentSrc = src;
        }

        CQ_CORE_INFO("[ShaderGraph] Vertex src length: {0}, Fragment src length: {1}", vertexSrc.size(), fragmentSrc.size());

        LastCompiledVertexSrc = vertexSrc;
        LastCompiledFragmentSrc = fragmentSrc;

        MaterialShader = Shader::Create("GeneratedShader_" + Name, vertexSrc, fragmentSrc);
        if (MaterialShader)
        {
            CQ_CORE_INFO("[ShaderGraph] Shader created successfully: GeneratedShader_{0}", Name);
        }
        else
        {
            CQ_CORE_ERROR("[ShaderGraph] Shader CREATION FAILED! GeneratedShader_{0}", Name);
        }
    }
}
