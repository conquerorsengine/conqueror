#include "ModelLoader.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/ImportActivity.h"
#include <fstream>
#include <yaml-cpp/yaml.h>
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"

#include <chrono>

#include <algorithm>
#include <cmath>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>
#include <assimp/quaternion.h>
#include <Renderer/RHI/Texture.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        constexpr uint32_t kMaxEngineBones = 128;

        std::string NormalizeSeparators(std::string p)
        {
            for (char& c : p)
            {
                if (c == '\\')
                    c = '/';
            }
            return p;
        }

        std::string ResolveExternalTexturePath(const std::string& modelDir, const std::string& relativeFromMaterial)
        {
            namespace fs = std::filesystem;

            std::string rel = NormalizeSeparators(relativeFromMaterial);

            const bool absoluteUnix = !rel.empty() && rel[0] == '/';
            const bool absoluteWin = rel.size() > 2 && rel[1] == ':' && std::isalpha(static_cast<unsigned char>(rel[0]));

            if (!absoluteUnix && !absoluteWin)
            {
                while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
                    rel.erase(rel.begin());
            }

            if (rel.empty())
                return {};

            fs::path primary;
            if (absoluteUnix || absoluteWin)
                primary = fs::path(rel).lexically_normal();
            else if (!modelDir.empty())
                primary = (fs::path(modelDir) / fs::path(rel)).lexically_normal();
            else
                primary = fs::path(rel).lexically_normal();

            const fs::path filenameOnly = fs::path(rel).filename();

            const fs::path candidates[] = {
                primary,
                !modelDir.empty() ? (fs::path(modelDir) / filenameOnly).lexically_normal() : fs::path{},
                !modelDir.empty() ? (fs::path(modelDir) / "sourceimages" / filenameOnly).lexically_normal() : fs::path{},
                !modelDir.empty() ? (fs::path(modelDir).parent_path() / "sourceimages" / filenameOnly).lexically_normal() : fs::path{},
                !modelDir.empty() ? (fs::path(modelDir).parent_path() / "textures" / filenameOnly).lexically_normal() : fs::path{},
                !modelDir.empty() ? (fs::path(modelDir) / "textures" / filenameOnly).lexically_normal() : fs::path{},
            };

            for (const fs::path& cand : candidates)
            {
                if (cand.empty())
                    continue;
                std::error_code ec;
                if (fs::is_regular_file(cand, ec))
                    return cand.string();
            }

            return primary.string();
        }

        // Extract Materials: gömülü doku dizini -> çıkarılan dosya
        const std::unordered_map<int, std::string>* g_EmbeddedTextureFiles = nullptr;
        // Load sırasında modelin malzeme eşlemesi (ad -> çözülmüş .cqmat yolu)
        const std::unordered_map<std::string, std::string>* g_MaterialRemap = nullptr;

        std::string MaterialNameOf(const aiMaterial* material, unsigned index)
        {
            aiString name;
            if (material && material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0)
                return name.C_Str();
            return "Material " + std::to_string(index);
        }

        std::string SafeFileName(std::string n)
        {
            for (char& c : n)
                if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                    c = '_';
            while (!n.empty() && (n.back() == ' ' || n.back() == '.'))
                n.pop_back();
            return n.empty() ? std::string("Material") : n;
        }

        std::shared_ptr<Texture2D> LoadTextureSlot(aiMaterial* material, aiTextureType texType, unsigned texIndex,
            const aiScene* scene, const std::string& directory)
        {
            if (material->GetTextureCount(texType) <= texIndex)
                return nullptr;

            aiString pathStr;
            if (material->GetTexture(texType, texIndex, &pathStr) != AI_SUCCESS)
                return nullptr;

            std::string rel(pathStr.C_Str());
            if (rel.empty())
                return nullptr;

            if (rel[0] == '*')
            {
                const int embedIndex = std::atoi(rel.c_str() + 1);
                // Extract Materials sırasında: dosyaya yazılmış gömülü doku (malzeme dosyası yolu saklayabilsin)
                if (g_EmbeddedTextureFiles)
                    if (auto it = g_EmbeddedTextureFiles->find(embedIndex); it != g_EmbeddedTextureFiles->end())
                        return Texture2D::Create(it->second);
                if (!scene || embedIndex < 0 || static_cast<unsigned>(embedIndex) >= scene->mNumTextures)
                {
                    CQ_CORE_WARN("Invalid embedded texture reference '{0}'", rel);
                    return nullptr;
                }

                const aiTexture* tex = scene->mTextures[embedIndex];
                if (!tex || !tex->pcData)
                    return nullptr;

                if (tex->mHeight == 0)
                    return Texture2D::CreateFromCompressedImageMemory(reinterpret_cast<const unsigned char*>(tex->pcData), tex->mWidth);
                return Texture2D::CreateFromRawBGRA(tex->mWidth, tex->mHeight, reinterpret_cast<const unsigned char*>(tex->pcData));
            }

            const std::string full = ResolveExternalTexturePath(directory, rel);
            if (full.empty())
                return nullptr;

            return Texture2D::Create(full);
        }

        glm::mat4 Mat4FromAi(const aiMatrix4x4& from)
        {
            glm::mat4 to;
            for (unsigned i = 0; i < 4; ++i)
                for (unsigned j = 0; j < 4; ++j)
                    to[j][i] = from[i][j];
            return to;
        }

        /// Assimp node->mTransformation ile aynı TRS sırası (glm::translate*rotate*scale değildir!)
        glm::mat4 ComposeAssimpLocalMatrix(const glm::vec3& s, const glm::quat& r, const glm::vec3& p)
        {
            aiQuaternion aiQ(r.w, r.x, r.y, r.z);
            aiVector3D aiS(s.x, s.y, s.z);
            aiVector3D aiP(p.x, p.y, p.z);
            aiMatrix4x4 m(aiS, aiQ, aiP);
            return Mat4FromAi(m);
        }

        glm::mat4 AccumulatedWorldFrom(aiNode* fromNode)
        {
            std::vector<aiNode*> stack;
            for (aiNode* n = fromNode; n; n = n->mParent)
                stack.push_back(n);
            glm::mat4 cumulative(1.f);
            for (int si = static_cast<int>(stack.size()) - 1; si >= 0; --si)
                cumulative = cumulative * Mat4FromAi(stack[si]->mTransformation);
            return cumulative;
        }

        aiNode* FindAiNode(aiNode* node, const std::string& name)
        {
            if (!node)
                return nullptr;
            if (name == std::string(node->mName.C_Str()))
                return node;
            for (unsigned i = 0; i < node->mNumChildren; ++i)
            {
                aiNode* found = FindAiNode(node->mChildren[i], name);
                if (found)
                    return found;
            }
            return nullptr;
        }

        std::unique_ptr<AnimSceneNode> CloneAnimHierarchy(aiNode* node)
        {
            if (!node)
                return nullptr;

            auto out = std::make_unique<AnimSceneNode>();
            out->Name = node->mName.C_Str();

            out->LocalBindTransform = Mat4FromAi(node->mTransformation);

            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(out->LocalBindTransform, out->BindScale, out->BindRotation, out->BindPosition, skew, perspective);

            out->Children.reserve(node->mNumChildren);
            for (unsigned i = 0; i < node->mNumChildren; ++i)
                out->Children.push_back(CloneAnimHierarchy(node->mChildren[i]));
            return out;
        }

        glm::mat4 FindBoneOffsetMatrix(const aiScene* scene, const std::string& boneName)
        {
            for (unsigned m = 0; m < scene->mNumMeshes; ++m)
            {
                aiMesh* mesh = scene->mMeshes[m];
                if (!mesh->HasBones())
                    continue;
                for (unsigned b = 0; b < mesh->mNumBones; ++b)
                {
                    aiBone* bone = mesh->mBones[b];
                    if (boneName == std::string(bone->mName.C_Str()))
                        return Mat4FromAi(bone->mOffsetMatrix);
                }
            }
            return glm::mat4(1.f);
        }

        int32_t FindParentBoneIndex(const aiScene* scene, const std::unordered_map<std::string, uint32_t>& boneMap,
            const std::string& boneName)
        {
            aiNode* boneNode = FindAiNode(scene->mRootNode, boneName);
            if (!boneNode)
                return -1;
            aiNode* p = boneNode->mParent;
            while (p)
            {
                std::string pname(p->mName.C_Str());
                auto it = boneMap.find(pname);
                if (it != boneMap.end())
                    return static_cast<int32_t>(it->second);
                p = p->mParent;
            }
            return -1;
        }

        void CollectBoneOrder(const aiScene* scene, std::vector<std::string>& outOrder,
            std::unordered_map<std::string, uint32_t>& outMap)
        {
            outOrder.clear();
            outMap.clear();
            for (unsigned m = 0; m < scene->mNumMeshes; ++m)
            {
                aiMesh* mesh = scene->mMeshes[m];
                if (!mesh->HasBones())
                    continue;
                for (unsigned b = 0; b < mesh->mNumBones; ++b)
                {
                    std::string name(mesh->mBones[b]->mName.C_Str());
                    if (outMap.count(name) == 0)
                    {
                        uint32_t idx = static_cast<uint32_t>(outOrder.size());
                        outMap[name] = idx;
                        outOrder.push_back(name);
                    }
                }
            }
        }

        bool SceneHasAnyBones(const aiScene* scene)
        {
            for (unsigned m = 0; m < scene->mNumMeshes; ++m)
                if (scene->mMeshes[m]->HasBones())
                    return true;
            return false;
        }
               // Mesh'in bağlı olduğu node'un global transform'unu bul
        // Assimp referans: globalInverseMeshTransform = GetGlobalTransform(meshNode).Inverse()
        glm::mat4 FindMeshNodeGlobalTransform(const aiScene* scene)
        {
            // İlk skinned mesh'e sahip node'u bul
            for (unsigned m = 0; m < scene->mNumMeshes; ++m)
            {
                if (!scene->mMeshes[m]->HasBones())
                    continue;

                // Bu mesh'i hangi node barındırıyor?
                std::function<aiNode*(aiNode*, unsigned)> findOwner = [&](aiNode* node, unsigned meshIdx) -> aiNode* {
                    for (unsigned i = 0; i < node->mNumMeshes; ++i)
                        if (node->mMeshes[i] == meshIdx)
                            return node;
                    for (unsigned i = 0; i < node->mNumChildren; ++i) {
                        aiNode* found = findOwner(node->mChildren[i], meshIdx);
                        if (found) return found;
                    }
                    return nullptr;
                };

                aiNode* meshNode = findOwner(scene->mRootNode, m);
                if (meshNode)
                    return AccumulatedWorldFrom(meshNode);
            }
            // Fallback: root node
            return AccumulatedWorldFrom(scene->mRootNode);
        }

        void BuildSkeleton(const aiScene* scene, std::shared_ptr<Model> model)
        {
            auto skel = std::make_shared<Skeleton>();

            std::vector<std::string> boneOrder;
            std::unordered_map<std::string, uint32_t> boneMap;
            CollectBoneOrder(scene, boneOrder, boneMap);

            if (boneOrder.empty())
                return;

            if (boneOrder.size() > kMaxEngineBones)
                CQ_CORE_WARN("Model has {0} bones; engine supports {1} — truncating palette sampling.", boneOrder.size(), kMaxEngineBones);

            skel->Bones.resize(boneOrder.size());
            skel->BoneNameToIndex = boneMap;

            for (size_t i = 0; i < boneOrder.size(); ++i)
            {
                skel->Bones[i].Name = boneOrder[i];
                skel->Bones[i].OffsetMatrix = FindBoneOffsetMatrix(scene, boneOrder[i]);
                skel->Bones[i].ParentIndex = FindParentBoneIndex(scene, boneMap, boneOrder[i]);
            }

            // Assimp referansı: mesh node'unun global transform'unun tersini kullan
            glm::mat4 meshNodeGlobal = FindMeshNodeGlobalTransform(scene);
            skel->GlobalInverseRoot = glm::inverse(meshNodeGlobal);

            skel->RootAnimNode = CloneAnimHierarchy(scene->mRootNode);

            model->SkeletonData = skel;
        }

        void LoadAnimationClips(const aiScene* scene, std::shared_ptr<Model> model)
        {
            model->Animations.clear();
            for (unsigned a = 0; a < scene->mNumAnimations; ++a)
            {
                const aiAnimation* anim = scene->mAnimations[a];
                auto clip = std::make_shared<AnimationClip>();
                clip->Name = anim->mName.length ? anim->mName.C_Str() : ("Anim_" + std::to_string(a));
                clip->DurationTicks = static_cast<float>(anim->mDuration);
                clip->TicksPerSecond = anim->mTicksPerSecond > 1e-8 ? static_cast<float>(anim->mTicksPerSecond) : 25.f;

                for (unsigned c = 0; c < anim->mNumChannels; ++c)
                {
                    const aiNodeAnim* ch = anim->mChannels[c];
                    std::string nodeName(ch->mNodeName.C_Str());
                    BoneAnimChannel bc;

                    for (unsigned i = 0; i < ch->mNumPositionKeys; ++i)
                    {
                        const aiVectorKey& k = ch->mPositionKeys[i];
                        Vec3Keyframe vk;
                        vk.TimeTicks = static_cast<float>(k.mTime);
                        vk.Value = glm::vec3(k.mValue.x, k.mValue.y, k.mValue.z);
                        bc.Positions.push_back(vk);
                    }
                    for (unsigned i = 0; i < ch->mNumRotationKeys; ++i)
                    {
                        const aiQuatKey& k = ch->mRotationKeys[i];
                        QuatKeyframe qk;
                        qk.TimeTicks = static_cast<float>(k.mTime);
                        const aiQuaternion& q = k.mValue;
                        qk.Value = glm::quat(q.w, q.x, q.y, q.z);
                        bc.Rotations.push_back(qk);
                    }
                    for (unsigned i = 0; i < ch->mNumScalingKeys; ++i)
                    {
                        const aiVectorKey& k = ch->mScalingKeys[i];
                        Vec3Keyframe vk;
                        vk.TimeTicks = static_cast<float>(k.mTime);
                        vk.Value = glm::vec3(k.mValue.x, k.mValue.y, k.mValue.z);
                        bc.Scales.push_back(vk);
                    }

                    clip->Channels[nodeName] = std::move(bc);
                }

                model->Animations.push_back(std::move(clip));
            }
        }

        void AddBoneInfluence(SkinnedVertex& v, int boneId, float weight)
        {
            if (weight <= 1e-8f || boneId < 0)
                return;

            for (int i = 0; i < 4; ++i)
            {
                if (v.BoneWeights[i] <= 1e-8f)
                {
                    v.BoneIDs[i] = boneId;
                    v.BoneWeights[i] = weight;
                    return;
                }
            }

            int smallest = 0;
            for (int i = 1; i < 4; ++i)
                if (v.BoneWeights[i] < v.BoneWeights[smallest])
                    smallest = i;
            if (weight > v.BoneWeights[smallest])
            {
                v.BoneIDs[smallest] = boneId;
                v.BoneWeights[smallest] = weight;
            }
        }

        void NormalizeVertexBones(SkinnedVertex& v)
        {
            float sum = v.BoneWeights[0] + v.BoneWeights[1] + v.BoneWeights[2] + v.BoneWeights[3];
            if (sum > 1e-6f)
            {
                v.BoneWeights /= sum;
            }
            for (int i = 0; i < 4; ++i)
            {
                if (v.BoneWeights[i] <= 1e-8f)
                {
                    v.BoneIDs[i] = 0;
                    v.BoneWeights[i] = 0.f;
                }
            }
        }
    }

    std::shared_ptr<Model> ModelLoader::Load(const std::string& path)
    {
        auto it = s_Cache.find(path);
        if (it != s_Cache.end())
        {
            if (auto cached = it->second.lock())
                return cached;
        }

        const auto importStart = std::chrono::steady_clock::now();
        Assimp::Importer importer;
        // FBX PreRotation/PostRotation/Pivots'ı ara node olarak tutma,
        // bunları hem node transform'a hem animasyon keyframe'lerine bake et.
        // Yoksa mTransformation ile animasyon keyframe'leri farklı uzayda kalır → spagetti.
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        // Normalsiz dosyalar: 80°'ye kadar yumuşak (Unity / Godot içe aktarma varsayılanı gibi), daha keskinler sert kenar
        importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 80.0f);
        // Aynı köşeler birleştirilir: OBJ / STL gibi biçimlerde üçgen başına ayrı köşe olmaz (bellek, LOD / küme kalitesi)
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_JoinIdenticalVertices |
            aiProcess_CalcTangentSpace |
            // aiProcess_FlipUVs yok: Assimp tüm biçimlerde UV'yi GL düzeninde (v yukarı) verir, dokular da stb ile
            // ters çevrilerek yüklenir; ek çevirme glTF / FBX / OBJ dokularını baş aşağı örnekliyordu
            aiProcess_LimitBoneWeights);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            CQ_CORE_ERROR("Assimp: {0}", importer.GetErrorString());
            ImportActivity::Add(path, "Model", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - importStart).count(),
                                false, importer.GetErrorString());
            return nullptr;
        }
        // Model içe aktarma ayarı: çıkarılmış / atanmış malzemeler (Unity Remapped Materials)
        std::unordered_map<std::string, std::string> remap;
        for (const auto& [name, file] : ReadMaterialRemap(path))
            if (!file.empty())
                remap[name] = SceneSerializer::ResolveAssetPath(file);
        g_MaterialRemap = remap.empty() ? nullptr : &remap;
        struct ClearRemap
        {
            ~ClearRemap() { g_MaterialRemap = nullptr; }
        } clearRemap;

        auto model = std::make_shared<Model>();
        const size_t sep = path.find_last_of("/\\");
        model->Directory = sep != std::string::npos ? path.substr(0, sep) : "";

        model->IsSkinned = SceneHasAnyBones(scene);
        if (model->IsSkinned)
        {
            BuildSkeleton(scene, model);
            LoadAnimationClips(scene, model);
        }

        ProcessNode(scene->mRootNode, scene, model, glm::mat4(1.0f), true);

        if (model->IsSkinned)
        {
            CQ_CORE_INFO("Model loaded (skinned): {0} — skinned meshes: {1}, clips: {2}, bones: {3}",
                path, model->SkinnedMeshes.size(), model->Animations.size(),
                model->SkeletonData ? model->SkeletonData->GetBoneCount() : 0u);
        }
        else
            CQ_CORE_INFO("Model loaded: {0} ({1} meshes)", path, model->Meshes.size());

        s_Cache[path] = model;
        ImportActivity::Add(path, "Model", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - importStart).count(), true,
                            std::to_string(model->Meshes.size() + model->SkinnedMeshes.size()) + " meshes, " + std::to_string(model->Materials.size()) +
                                " materials" + (model->Animations.empty() ? std::string() : ", " + std::to_string(model->Animations.size()) + " animations"));
        return model;
    }

    void ModelLoader::ProcessNode(void* nodePtr, const void* scenePtr, std::shared_ptr<Model> model, const glm::mat4& parentTransform, bool isRoot)
    {
        aiNode* node = static_cast<aiNode*>(nodePtr);
        const aiScene* scene = static_cast<const aiScene*>(scenePtr);
        // Kök düğümün dönüşümü (FBX birim ölçeği / eksen çevirmesi) önceki davranışla uyumlu kalsın diye uygulanmaz
        const glm::mat4 global = isRoot ? parentTransform : parentTransform * Mat4FromAi(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

            if (model->IsSkinned && mesh->HasBones())
                model->SkinnedMeshes.push_back(ProcessSkinnedMesh(mesh, model));
            else
                model->Meshes.push_back(ProcessStaticMesh(mesh, scene, global));

            if (mesh->mMaterialIndex >= 0)
            {
                aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
                std::shared_ptr<Material> remapped;
                if (g_MaterialRemap)
                    if (auto it = g_MaterialRemap->find(MaterialNameOf(material, mesh->mMaterialIndex)); it != g_MaterialRemap->end())
                        remapped = MaterialAsset::Load(it->second);
                model->Materials.push_back(remapped ? remapped : ProcessMaterial(material, scene, model->Directory));
            }
            else
                model->Materials.push_back(Material::CreateDefault());
        }

        for (uint32_t i = 0; i < node->mNumChildren; i++)
            ProcessNode(node->mChildren[i], scene, model, global, false);
    }

    std::shared_ptr<Mesh> ModelLoader::ProcessStaticMesh(void* meshPtr, [[maybe_unused]] const void* scenePtr, const glm::mat4& transform)
    {
        aiMesh* mesh = static_cast<aiMesh*>(meshPtr);
        const bool identity = transform == glm::mat4(1.0f);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        for (uint32_t i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex;

            vertex.Position = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);

            if (mesh->HasNormals())
                vertex.Normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);

            if (mesh->HasTangentsAndBitangents())
                vertex.Tangent = glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);

            if (mesh->mTextureCoords[0])
                vertex.TexCoords = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
            else
                vertex.TexCoords = glm::vec2(0.f);

            if (!identity)
            {
                vertex.Position = glm::vec3(transform * glm::vec4(vertex.Position, 1.0f));
                if (glm::dot(vertex.Normal, vertex.Normal) > 0.0f)
                    vertex.Normal = glm::normalize(normalMatrix * vertex.Normal);
                if (glm::dot(vertex.Tangent, vertex.Tangent) > 0.0f)
                    vertex.Tangent = glm::normalize(glm::mat3(transform) * vertex.Tangent);
            }

            vertices.push_back(vertex);
        }

        for (uint32_t i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for (uint32_t j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<SkinnedMesh> ModelLoader::ProcessSkinnedMesh(void* meshPtr, std::shared_ptr<Model> model)
    {
        aiMesh* mesh = static_cast<aiMesh*>(meshPtr);
        CQ_CORE_ASSERT(model && model->SkeletonData, "Skeleton required for skinned mesh");

        std::vector<SkinnedVertex> vertices(mesh->mNumVertices);
        std::vector<uint32_t> indices;

        for (uint32_t i = 0; i < mesh->mNumVertices; i++)
        {
            SkinnedVertex& vertex = vertices[i];
            vertex.BoneIDs = glm::ivec4(0);
            vertex.BoneWeights = glm::vec4(0.f);

            vertex.Position = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);

            if (mesh->HasNormals())
                vertex.Normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);

            if (mesh->HasTangentsAndBitangents())
                vertex.Tangent = glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);

            if (mesh->mTextureCoords[0])
                vertex.TexCoords = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
            else
                vertex.TexCoords = glm::vec2(0.f);
        }

        for (unsigned b = 0; b < mesh->mNumBones; ++b)
        {
            aiBone* bone = mesh->mBones[b];
            std::string boneName(bone->mName.C_Str());
            auto it = model->SkeletonData->BoneNameToIndex.find(boneName);
            if (it == model->SkeletonData->BoneNameToIndex.end())
                continue;

            int boneIndex = static_cast<int>(it->second);
            for (unsigned w = 0; w < bone->mNumWeights; ++w)
            {
                uint32_t vid = bone->mWeights[w].mVertexId;
                float weight = bone->mWeights[w].mWeight;
                if (vid < vertices.size())
                    AddBoneInfluence(vertices[vid], boneIndex, weight);
            }
        }

        for (auto& v : vertices)
            NormalizeVertexBones(v);

        for (uint32_t i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for (uint32_t j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        return std::make_shared<SkinnedMesh>(vertices, indices);
    }

    std::shared_ptr<Material> ModelLoader::ProcessMaterial(void* materialPtr, const void* scenePtr, const std::string& directory)
    {
        aiMaterial* material = static_cast<aiMaterial*>(materialPtr);
        const aiScene* scene = static_cast<const aiScene*>(scenePtr);

        auto mat = std::make_shared<Material>();

        aiColor3D color(1.0f, 1.0f, 1.0f);
        material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
        mat->Albedo = glm::vec3(color.r, color.g, color.b);

        float shininess = 0.0f;
        material->Get(AI_MATKEY_SHININESS, shininess);
        mat->Roughness = 1.0f - (shininess / 128.0f);

        mat->AlbedoMap = LoadTextureSlot(material, aiTextureType_DIFFUSE, 0, scene, directory);

        mat->NormalMap = LoadTextureSlot(material, aiTextureType_NORMALS, 0, scene, directory);
        if (!mat->NormalMap)
            mat->NormalMap = LoadTextureSlot(material, aiTextureType_HEIGHT, 0, scene, directory);

        mat->MetallicMap = LoadTextureSlot(material, aiTextureType_METALNESS, 0, scene, directory);
        if (!mat->MetallicMap)
            mat->MetallicMap = LoadTextureSlot(material, aiTextureType_SPECULAR, 0, scene, directory);

        mat->RoughnessMap = LoadTextureSlot(material, aiTextureType_DIFFUSE_ROUGHNESS, 0, scene, directory);
        if (!mat->RoughnessMap)
            mat->RoughnessMap = LoadTextureSlot(material, aiTextureType_SHININESS, 0, scene, directory);

        mat->AOMap = LoadTextureSlot(material, aiTextureType_AMBIENT_OCCLUSION, 0, scene, directory);
        if (!mat->AOMap)
            mat->AOMap = LoadTextureSlot(material, aiTextureType_AMBIENT, 0, scene, directory);

        // glTF metal/pürüz iş akışı: çarpanlar, paketli doku kanalları, ışıma, alfa modu, geçirgenlik (cam)
        float metallicFactor = 1.0f;
        if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor) == AI_SUCCESS)
        {
            float roughnessFactor = 1.0f;
            material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor);
            mat->Metallic = metallicFactor;
            mat->Roughness = roughnessFactor;
            mat->MapsScaleFactors = true;

            // Temel renk çarpanı doğrusal; gölgelendirici albedoyu sRGB kabul edip doğrusala çevirir
            aiColor4D base(1.0f, 1.0f, 1.0f, 1.0f);
            if (material->Get(AI_MATKEY_BASE_COLOR, base) == AI_SUCCESS)
            {
                auto toSrgb = [](float c) { return std::pow(std::max(c, 0.0f), 1.0f / 2.2f); };
                mat->Albedo = glm::vec3(toSrgb(base.r), toSrgb(base.g), toSrgb(base.b));
                mat->Opacity = base.a;
            }
            if (!mat->AlbedoMap)
                mat->AlbedoMap = LoadTextureSlot(material, aiTextureType_BASE_COLOR, 0, scene, directory);

            // Tek doku iki kanalda (metal B, pürüz G)
            if (!mat->MetallicMap && !mat->RoughnessMap)
            {
                auto mr = LoadTextureSlot(material, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, scene, directory);
                mat->MetallicMap = mr;
                mat->RoughnessMap = mr;
            }
            if (mat->MetallicMap && mat->MetallicMap == mat->RoughnessMap)
            {
                mat->MetallicChannel = 2;
                mat->RoughnessChannel = 1;
            }
            else if (mat->MetallicMap || mat->RoughnessMap)
            {
                // Ayrı yüklenmiş aynı dosya (Assimp her iki türe de aynı yolu verir)
                aiString a, b;
                if (material->GetTexture(aiTextureType_METALNESS, 0, &a) == AI_SUCCESS &&
                    material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &b) == AI_SUCCESS && a == b)
                {
                    mat->RoughnessMap = mat->MetallicMap;
                    mat->MetallicChannel = 2;
                    mat->RoughnessChannel = 1;
                }
            }

            aiColor3D emissive(0.0f, 0.0f, 0.0f);
            if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS && (emissive.r + emissive.g + emissive.b) > 0.0f)
            {
                float intensity = 1.0f;
                material->Get(AI_MATKEY_EMISSIVE_INTENSITY, intensity);
                mat->EmissionColor = glm::vec3(emissive.r, emissive.g, emissive.b);
                mat->EmissionStrength = intensity;
                mat->EmissionMap = LoadTextureSlot(material, aiTextureType_EMISSIVE, 0, scene, directory);
            }

            aiString alphaMode;
            if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
            {
                const std::string mode = alphaMode.C_Str();
                if (mode == "MASK")
                {
                    mat->Rendering = Material::RenderMode::Cutout;
                    float cutoff = 0.5f;
                    material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff);
                    mat->AlphaClip = cutoff;
                }
                else if (mode == "BLEND")
                    mat->Rendering = Material::RenderMode::Transparent;
            }
            float transmission = 0.0f;
            if (material->Get(AI_MATKEY_TRANSMISSION_FACTOR, transmission) == AI_SUCCESS && transmission > 0.0f)
            {
                // Kırılma yok: geçirgen yüzey yarı saydam, parlak yansımalı cam olarak çizilir
                mat->Rendering = Material::RenderMode::Transparent;
                mat->Opacity = std::min(mat->Opacity, 1.0f - 0.82f * std::min(transmission, 1.0f));
            }
            int twoSided = 0;
            if (material->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS && twoSided)
                mat->Culling = Material::CullMode::Off;
        }

        return mat;
    }

    // ------------------------------------------------------------------ Malzeme çıkarma / eşleme (<model>.meta)

    std::vector<std::pair<std::string, std::string>> ModelLoader::ReadMaterialRemap(const std::string& path)
    {
        std::vector<std::pair<std::string, std::string>> out;
        std::error_code ec;
        const std::string meta = path + ".meta";
        if (!std::filesystem::exists(meta, ec))
            return out;
        try
        {
            YAML::Node root = YAML::LoadFile(meta);
            if (auto remap = root["ModelImport"]["MaterialRemap"]; remap && remap.IsMap())
                for (auto it = remap.begin(); it != remap.end(); ++it)
                    out.push_back({ it->first.as<std::string>(), it->second.as<std::string>() });
        }
        catch (const std::exception& e)
        {
            CQ_CORE_WARN("Model import settings {0}: {1}", meta, e.what());
        }
        return out;
    }

    bool ModelLoader::WriteMaterialRemap(const std::string& path, const std::vector<std::pair<std::string, std::string>>& remap)
    {
        const std::string meta = path + ".meta";
        std::error_code ec;
        bool any = false;
        for (const auto& [name, file] : remap)
            any |= !file.empty();
        if (!any)
        {
            std::filesystem::remove(meta, ec);
            Invalidate(path);
            return true;
        }
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "ModelImport" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "MaterialRemap" << YAML::Value << YAML::BeginMap;
        for (const auto& [name, file] : remap)
            if (!file.empty())
                out << YAML::Key << name << YAML::Value << file;
        out << YAML::EndMap << YAML::EndMap << YAML::EndMap;
        std::ofstream f(meta, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f << "# Conqueror's Engine - model import settings\n" << out.c_str() << "\n";
        Invalidate(path);
        return true;
    }

    std::vector<std::string> ModelLoader::MaterialNames(const std::string& path)
    {
        std::vector<std::string> names;
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, 0);
        if (!scene)
            return names;
        for (unsigned i = 0; i < scene->mNumMaterials; ++i)
        {
            const std::string n = MaterialNameOf(scene->mMaterials[i], i);
            if (std::find(names.begin(), names.end(), n) == names.end())
                names.push_back(n);
        }
        return names;
    }

    bool ModelLoader::ExtractMaterials(const std::string& path, const std::string& outDirectory, std::vector<std::string>& created, std::string* error)
    {
        namespace fs = std::filesystem;
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, 0);
        if (!scene)
        {
            if (error) *error = importer.GetErrorString();
            return false;
        }
        std::error_code ec;
        fs::create_directories(outDirectory, ec);
        auto unique = [](fs::path p) {
            std::error_code e;
            if (!fs::exists(p, e))
                return p;
            for (int i = 1; i < 10000; ++i)
            {
                fs::path c = p.parent_path() / (p.stem().string() + " " + std::to_string(i) + p.extension().string());
                if (!fs::exists(c, e))
                    return c;
            }
            return p;
        };
        const std::string stem = fs::path(path).stem().string();

        // 1) Gömülü dokular -> dosya (Unity Extract Textures)
        std::unordered_map<int, std::string> textureFiles;
        for (unsigned i = 0; i < scene->mNumTextures; ++i)
        {
            const aiTexture* tex = scene->mTextures[i];
            if (!tex || !tex->pcData)
                continue;
            std::string base = tex->mFilename.length > 0 ? fs::path(tex->mFilename.C_Str()).stem().string() : std::string();
            if (base.empty())
                base = stem + " Texture " + std::to_string(i);
            fs::path file;
            if (tex->mHeight == 0)
            {
                std::string ext = tex->achFormatHint[0] ? std::string(tex->achFormatHint) : std::string("png");
                if (ext == "jpeg")
                    ext = "jpg";
                file = unique(fs::path(outDirectory) / (SafeFileName(base) + "." + ext));
                std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(tex->pcData), tex->mWidth);
            }
            else
            {
                // Sıkıştırılmamış BGRA -> TGA (başlık + satırlar, sıkıştırmasız)
                file = unique(fs::path(outDirectory) / (SafeFileName(base) + ".tga"));
                std::ofstream f(file, std::ios::binary);
                unsigned char header[18] = {};
                header[2] = 2;
                header[12] = (unsigned char)(tex->mWidth & 0xFF);
                header[13] = (unsigned char)((tex->mWidth >> 8) & 0xFF);
                header[14] = (unsigned char)(tex->mHeight & 0xFF);
                header[15] = (unsigned char)((tex->mHeight >> 8) & 0xFF);
                header[16] = 32;
                header[17] = 0x28;   // üstten başlayan satırlar, 8 bit alfa
                f.write(reinterpret_cast<const char*>(header), sizeof(header));
                f.write(reinterpret_cast<const char*>(tex->pcData), (std::streamsize)tex->mWidth * tex->mHeight * 4);
            }
            textureFiles[(int)i] = file.string();
            created.push_back(file.string());
        }

        // 2) Malzemeler -> .cqmat (dokular dosya yoluyla)
        g_EmbeddedTextureFiles = &textureFiles;
        std::vector<std::pair<std::string, std::string>> remap;
        const std::string directory = fs::path(path).parent_path().string();
        for (unsigned i = 0; i < scene->mNumMaterials; ++i)
        {
            const std::string name = MaterialNameOf(scene->mMaterials[i], i);
            bool done = false;
            for (const auto& r : remap)
                done |= r.first == name;
            if (done)
                continue;
            auto mat = ProcessMaterial(scene->mMaterials[i], scene, directory);
            if (!mat)
                continue;
            mat->Name = name;
            const fs::path file = unique(fs::path(outDirectory) / (SafeFileName(name) + ".cqmat"));
            if (!MaterialAsset::Create(file.string(), mat.get()))
                continue;
            created.push_back(file.string());
            remap.push_back({ name, SceneSerializer::ToAssetPath(file.string()) });
        }
        g_EmbeddedTextureFiles = nullptr;
        if (remap.empty())
        {
            if (error) *error = "The model has no materials.";
            return false;
        }
        // Var olan eşlemeler korunur, çıkarılanlar üzerine yazılır
        auto existing = ReadMaterialRemap(path);
        for (const auto& r : remap)
        {
            bool replaced = false;
            for (auto& e : existing)
                if (e.first == r.first)
                {
                    e.second = r.second;
                    replaced = true;
                }
            if (!replaced)
                existing.push_back(r);
        }
        return WriteMaterialRemap(path, existing);
    }
}
