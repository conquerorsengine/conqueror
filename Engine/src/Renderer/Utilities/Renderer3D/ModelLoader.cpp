#include "ModelLoader.h"
#include "Core/Logging/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
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
        Assimp::Importer importer;
        // FBX PreRotation/PostRotation/Pivots'ı ara node olarak tutma,
        // bunları hem node transform'a hem animasyon keyframe'lerine bake et.
        // Yoksa mTransformation ile animasyon keyframe'leri farklı uzayda kalır → spagetti.
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_FlipUVs |
            aiProcess_LimitBoneWeights);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            CQ_CORE_ERROR("Assimp: {0}", importer.GetErrorString());
            return nullptr;
        }

        auto model = std::make_shared<Model>();
        const size_t sep = path.find_last_of("/\\");
        model->Directory = sep != std::string::npos ? path.substr(0, sep) : "";

        model->IsSkinned = SceneHasAnyBones(scene);
        if (model->IsSkinned)
        {
            BuildSkeleton(scene, model);
            LoadAnimationClips(scene, model);
        }

        ProcessNode(scene->mRootNode, scene, model);

        if (model->IsSkinned)
        {
            CQ_CORE_INFO("Model loaded (skinned): {0} — skinned meshes: {1}, clips: {2}, bones: {3}",
                path, model->SkinnedMeshes.size(), model->Animations.size(),
                model->SkeletonData ? model->SkeletonData->GetBoneCount() : 0u);
        }
        else
            CQ_CORE_INFO("Model loaded: {0} ({1} meshes)", path, model->Meshes.size());

        return model;
    }

    void ModelLoader::ProcessNode(void* nodePtr, const void* scenePtr, std::shared_ptr<Model> model)
    {
        aiNode* node = static_cast<aiNode*>(nodePtr);
        const aiScene* scene = static_cast<const aiScene*>(scenePtr);

        for (uint32_t i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

            if (model->IsSkinned && mesh->HasBones())
                model->SkinnedMeshes.push_back(ProcessSkinnedMesh(mesh, model));
            else
                model->Meshes.push_back(ProcessStaticMesh(mesh, scene));

            if (mesh->mMaterialIndex >= 0)
            {
                aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
                model->Materials.push_back(ProcessMaterial(material, scene, model->Directory));
            }
            else
                model->Materials.push_back(Material::CreateDefault());
        }

        for (uint32_t i = 0; i < node->mNumChildren; i++)
            ProcessNode(node->mChildren[i], scene, model);
    }

    std::shared_ptr<Mesh> ModelLoader::ProcessStaticMesh(void* meshPtr, [[maybe_unused]] const void* scenePtr)
    {
        aiMesh* mesh = static_cast<aiMesh*>(meshPtr);

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

        return mat;
    }
}
