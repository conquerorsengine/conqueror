#include "FSR2GL.h"

#include "Core/Logging/Log.h"
#include "Renderer/ShaderCompiler/ShaderCompiler.h"

#include <glad/glad.h>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_glsl.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <vector>

namespace Conqueror::FSR2GL
{
    namespace
    {
        constexpr int kMaxResources = 64;
        constexpr GLuint kUniformBufferBase = 24;   // motorun UBO bağlamalarıyla (0 kare, 1 ışık, 7 son geçiş) çakışmaz

        struct FormatInfo
        {
            GLenum Internal;
            GLenum Format;
            GLenum Type;
            bool Integer;
        };

        FormatInfo MapFormat(FfxSurfaceFormat f)
        {
            switch (f)
            {
                case FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS:
                case FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT: return { GL_RGBA32F, GL_RGBA, GL_FLOAT, false };
                case FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT: return { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, false };
                case FFX_SURFACE_FORMAT_R16G16B16A16_UNORM: return { GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, false };
                case FFX_SURFACE_FORMAT_R32G32_FLOAT:       return { GL_RG32F, GL_RG, GL_FLOAT, false };
                case FFX_SURFACE_FORMAT_R32_UINT:           return { GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, true };
                case FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS:
                case FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:     return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false };
                case FFX_SURFACE_FORMAT_R11G11B10_FLOAT:    return { GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, false };
                case FFX_SURFACE_FORMAT_R16G16_FLOAT:       return { GL_RG16F, GL_RG, GL_HALF_FLOAT, false };
                case FFX_SURFACE_FORMAT_R16G16_UINT:        return { GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, true };
                case FFX_SURFACE_FORMAT_R16_FLOAT:          return { GL_R16F, GL_RED, GL_HALF_FLOAT, false };
                case FFX_SURFACE_FORMAT_R16_UINT:           return { GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, true };
                case FFX_SURFACE_FORMAT_R16_UNORM:          return { GL_R16, GL_RED, GL_UNSIGNED_SHORT, false };
                case FFX_SURFACE_FORMAT_R16_SNORM:          return { GL_R16_SNORM, GL_RED, GL_SHORT, false };
                case FFX_SURFACE_FORMAT_R8_UNORM:           return { GL_R8, GL_RED, GL_UNSIGNED_BYTE, false };
                case FFX_SURFACE_FORMAT_R8_UINT:            return { GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, true };
                case FFX_SURFACE_FORMAT_R8G8_UNORM:         return { GL_RG8, GL_RG, GL_UNSIGNED_BYTE, false };
                case FFX_SURFACE_FORMAT_R32_FLOAT:          return { GL_R32F, GL_RED, GL_FLOAT, false };
                default:                                    return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false };
            }
        }

        FfxSurfaceFormat FromGLFormat(GLenum internalFormat)
        {
            switch (internalFormat)
            {
                case GL_RGBA32F:         return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
                case GL_RGBA16F:         return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
                case GL_RGBA16:          return FFX_SURFACE_FORMAT_R16G16B16A16_UNORM;
                case GL_RG32F:           return FFX_SURFACE_FORMAT_R32G32_FLOAT;
                case GL_R32UI:           return FFX_SURFACE_FORMAT_R32_UINT;
                case GL_RGBA8:           return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
                case GL_R11F_G11F_B10F:  return FFX_SURFACE_FORMAT_R11G11B10_FLOAT;
                case GL_RG16F:           return FFX_SURFACE_FORMAT_R16G16_FLOAT;
                case GL_R16F:            return FFX_SURFACE_FORMAT_R16_FLOAT;
                case GL_R8:              return FFX_SURFACE_FORMAT_R8_UNORM;
                case GL_RG8:             return FFX_SURFACE_FORMAT_R8G8_UNORM;
                case GL_R32F:
                case GL_DEPTH_COMPONENT32F:
                case GL_DEPTH24_STENCIL8:
                case GL_DEPTH32F_STENCIL8:
                case GL_DEPTH_COMPONENT24: return FFX_SURFACE_FORMAT_R32_FLOAT;
                default:                 return FFX_SURFACE_FORMAT_UNKNOWN;
            }
        }

        struct Resource
        {
            GLuint Texture = 0;
            bool Owned = false;
            FfxResourceDescription Desc{};
            GLenum InternalFormat = GL_RGBA8;
            bool Integer = false;
        };

        struct SrvUnit
        {
            GLint Unit;
            bool Linear;
        };

        struct Pipeline
        {
            GLuint Program = 0;
            std::vector<std::vector<SrvUnit>> Srvs;   // FSR 2 SRV sırası -> doku birimleri (birleşik örnekleyici başına bir)
            std::vector<GLuint> Uavs;                 // FSR 2 UAV sırası -> görüntü birimi
            std::vector<GLuint> Cbs;                  // FSR 2 sabit tampon sırası -> UBO bağlama noktası
            GLint MaxTextureUnit = -1;
            GLint MaxImageUnit = -1;
        };

        struct Context
        {
            std::string ShaderDir;
            Resource Resources[kMaxResources];
            int NextStatic = 1;   // 0: FFX_FSR2_RESOURCE_IDENTIFIER_NULL
            int NextDynamic = kMaxResources - 1;
            std::vector<FfxGpuJobDescription> Jobs;
            std::vector<std::unique_ptr<Pipeline>> Pipelines;
            GLuint SamplerPoint = 0;
            GLuint SamplerLinear = 0;
            GLuint Ubos[FFX_MAX_NUM_CONST_BUFFERS] = {};
        };

        Context* Ctx(FfxFsr2Interface* backendInterface)
        {
            return *reinterpret_cast<Context**>(backendInterface->scratchBuffer);
        }

        std::wstring Widen(const std::string& s)
        {
            return std::wstring(s.begin(), s.end());
        }

        void CopyName(wchar_t (&dst)[64], const std::string& s)
        {
            const std::wstring w = Widen(s);
            const size_t n = std::min<size_t>(w.size(), 63);
            std::wmemcpy(dst, w.data(), n);
            dst[n] = 0;
        }

        // #include "..." -> FSR 2 GLSL klasörü
        class Includer : public glslang::TShader::Includer
        {
        public:
            explicit Includer(std::string dir) : m_Dir(std::move(dir)) {}
            IncludeResult* includeLocal(const char* headerName, const char*, size_t) override { return Load(headerName); }
            IncludeResult* includeSystem(const char* headerName, const char*, size_t) override { return Load(headerName); }
            void releaseInclude(IncludeResult* result) override
            {
                if (!result)
                    return;
                delete static_cast<std::string*>(result->userData);
                delete result;
            }

        private:
            IncludeResult* Load(const char* name)
            {
                const std::string path = m_Dir + "/" + name;
                std::ifstream f(path, std::ios::binary);
                if (!f)
                    return nullptr;
                auto* text = new std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                return new IncludeResult(path, text->data(), text->size(), text);
            }

            std::string m_Dir;
        };

        const char* PassFile(FfxFsr2Pass pass)
        {
            switch (pass)
            {
                case FFX_FSR2_PASS_DEPTH_CLIP:                 return "ffx_fsr2_depth_clip_pass.glsl";
                case FFX_FSR2_PASS_RECONSTRUCT_PREVIOUS_DEPTH: return "ffx_fsr2_reconstruct_previous_depth_pass.glsl";
                case FFX_FSR2_PASS_LOCK:                       return "ffx_fsr2_lock_pass.glsl";
                case FFX_FSR2_PASS_ACCUMULATE:
                case FFX_FSR2_PASS_ACCUMULATE_SHARPEN:         return "ffx_fsr2_accumulate_pass.glsl";
                case FFX_FSR2_PASS_RCAS:                       return "ffx_fsr2_rcas_pass.glsl";
                case FFX_FSR2_PASS_COMPUTE_LUMINANCE_PYRAMID:  return "ffx_fsr2_compute_luminance_pyramid_pass.glsl";
                case FFX_FSR2_PASS_GENERATE_REACTIVE:          return "ffx_fsr2_autogen_reactive_pass.glsl";
                case FFX_FSR2_PASS_TCR_AUTOGENERATE:           return "ffx_fsr2_tcr_autogen_pass.glsl";
                default:                                       return nullptr;
            }
        }

        // FSR 2 geçişini derler: Vulkan GLSL -> SPIR-V -> OpenGL GLSL 450 -> GL hesaplama programı
        GLuint CompilePass(Context* c, FfxFsr2Pass pass, uint32_t contextFlags, std::string& error)
        {
            const char* file = PassFile(pass);
            if (!file)
            {
                error = "unknown pass";
                return 0;
            }
            const std::string path = c->ShaderDir + "/" + file;
            std::ifstream f(path, std::ios::binary);
            if (!f)
            {
                error = "cannot read " + path;
                return 0;
            }
            const std::string source((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            // Resmi Vulkan derlemesinin tanımları (External/FidelityFX-FSR2/src/ffx-fsr2-api/CMakeLists.txt) + bu bağlamın permütasyonu.
            // FP16 ve dalga işlemleri kapalı (OpenGL'de alt grup dörtlü işlemleri yok: SPD paylaşımlı bellek yolu). Geçmiş yeniden
            // izdüşümü tablosuz başvuru Lanczos'u (REPROJECT_USE_LANCZOS_TYPE 0: Vulkan arka ucunun 32 / 64 dışı dalga genişliğindeki
            // seçimi); tablo yolu bu arka uçta geçmişi her karede yumuşatıyordu (sabit kamerada 18 karede bulanık)
            std::ostringstream pre;
            pre << "#define FFX_GPU 1\n#define FFX_GLSL 1\n#define FFX_HALF 0\n#define SPD_NO_WAVE_OPERATIONS 1\n"
                << "#define FFX_FSR2_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF 0\n"
                << "#define FFX_FSR2_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF 0\n"
                << "#define FFX_FSR2_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF 1\n"
                << "#define FFX_FSR2_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF 0\n"
                << "#define FFX_FSR2_OPTION_UPSAMPLE_USE_LANCZOS_TYPE 2\n"
                << "#define FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE 0\n"
                << "#define FFX_FSR2_OPTION_HDR_COLOR_INPUT " << ((contextFlags & FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE) ? 1 : 0) << "\n"
                << "#define FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS " << ((contextFlags & FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) ? 0 : 1) << "\n"
                << "#define FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS " << ((contextFlags & FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) ? 1 : 0) << "\n"
                << "#define FFX_FSR2_OPTION_INVERTED_DEPTH " << ((contextFlags & FFX_FSR2_ENABLE_DEPTH_INVERTED) ? 1 : 0) << "\n"
                << "#define FFX_FSR2_OPTION_APPLY_SHARPENING " << (pass == FFX_FSR2_PASS_ACCUMULATE_SHARPEN ? 1 : 0) << "\n";
            const std::string preamble = pre.str();

            ShaderCompiler::Init();
            glslang::TShader shader(EShLangCompute);
            const char* src = source.c_str();
            const char* name = path.c_str();
            shader.setStringsWithLengthsAndNames(&src, nullptr, &name, 1);
            shader.setPreamble(preamble.c_str());
            shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute, glslang::EShClientVulkan, 110);
            shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
            shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
            shader.setEntryPoint("main");
            Includer includer(c->ShaderDir);
            const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
            if (!shader.parse(GetDefaultResources(), 450, false, messages, includer))
            {
                error = std::string(file) + ": " + shader.getInfoLog();
                return 0;
            }
            glslang::TProgram program;
            program.addShader(&shader);
            if (!program.link(messages))
            {
                error = std::string(file) + ": " + program.getInfoLog();
                return 0;
            }
            std::vector<uint32_t> spirv;
            glslang::SpvOptions spvOptions;
            glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), spirv, &spvOptions);

            std::string glsl;
            try
            {
                spirv_cross::CompilerGLSL comp(std::move(spirv));
                spirv_cross::CompilerGLSL::Options options;
                options.version = 450;
                options.es = false;
                options.vulkan_semantics = false;
                options.enable_420pack_extension = true;
                comp.set_common_options(options);
                comp.build_dummy_sampler_for_combined_images();
                comp.build_combined_image_samplers();
                // Birleşik örnekleyici adı: CQFSR2_<doku>_S<L doğrusal | P nokta | N örnekleyicisiz texelFetch> (GLSL "__" içeren adları
                // ayıkladığından tek alt çizgi)
                for (const auto& combined : comp.get_combined_image_samplers())
                {
                    const std::string image = comp.get_name(combined.image_id);
                    const std::string sampler = comp.get_name(combined.sampler_id);
                    const char kind = sampler == "s_LinearClamp" ? 'L' : sampler == "s_PointClamp" ? 'P' : 'N';
                    comp.set_name(combined.combined_id, "CQFSR2_" + image + "_S" + kind);
                    comp.unset_decoration(combined.combined_id, spv::DecorationBinding);
                }
                glsl = comp.compile();
            }
            catch (const std::exception& e)
            {
                error = std::string(file) + ": SPIRV-Cross: " + e.what();
                return 0;
            }

            GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
            const char* glslSrc = glsl.c_str();
            glShaderSource(cs, 1, &glslSrc, nullptr);
            glCompileShader(cs);
            GLint ok = 0;
            glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[4096];
                glGetShaderInfoLog(cs, sizeof(log), nullptr, log);
                error = std::string(file) + ": GL compile: " + log;
                glDeleteShader(cs);
                return 0;
            }
            GLuint prog = glCreateProgram();
            glAttachShader(prog, cs);
            glLinkProgram(prog);
            glDeleteShader(cs);
            glGetProgramiv(prog, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                char log[4096];
                glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
                error = std::string(file) + ": GL link: " + log;
                glDeleteProgram(prog);
                return 0;
            }
            return prog;
        }

        bool IsImageType(GLenum type)
        {
            switch (type)
            {
                case GL_IMAGE_2D:
                case GL_INT_IMAGE_2D:
                case GL_UNSIGNED_INT_IMAGE_2D:
                case GL_IMAGE_1D:
                case GL_UNSIGNED_INT_IMAGE_1D:
                    return true;
                default:
                    return false;
            }
        }

        bool IsSamplerType(GLenum type)
        {
            switch (type)
            {
                case GL_SAMPLER_2D:
                case GL_INT_SAMPLER_2D:
                case GL_UNSIGNED_INT_SAMPLER_2D:
                case GL_SAMPLER_1D:
                case GL_UNSIGNED_INT_SAMPLER_1D:
                    return true;
                default:
                    return false;
            }
        }

        // ---------------------------------------------------------------- FfxFsr2Interface

        FfxErrorCode GetDeviceCapabilitiesGL(FfxFsr2Interface*, FfxDeviceCapabilities* caps, FfxDevice)
        {
            caps->minimumSupportedShaderModel = FFX_SHADER_MODEL_5_1;
            caps->waveLaneCountMin = 32;
            caps->waveLaneCountMax = 64;
            caps->fp16Supported = false;
            caps->raytracingSupported = false;
            return FFX_OK;
        }

        FfxErrorCode CreateBackendContextGL(FfxFsr2Interface* backendInterface, FfxDevice)
        {
            Context* c = Ctx(backendInterface);
            glCreateSamplers(1, &c->SamplerPoint);
            glCreateSamplers(1, &c->SamplerLinear);
            for (GLuint s : { c->SamplerPoint, c->SamplerLinear })
            {
                glSamplerParameteri(s, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glSamplerParameteri(s, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glSamplerParameteri(c->SamplerPoint, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glSamplerParameteri(c->SamplerPoint, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glSamplerParameteri(c->SamplerLinear, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
            glSamplerParameteri(c->SamplerLinear, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glCreateBuffers(FFX_MAX_NUM_CONST_BUFFERS, c->Ubos);
            for (GLuint b : c->Ubos)
                glNamedBufferStorage(b, FFX_MAX_CONST_SIZE * sizeof(uint32_t), nullptr, GL_DYNAMIC_STORAGE_BIT);
            c->NextStatic = 1;
            c->NextDynamic = kMaxResources - 1;
            return FFX_OK;
        }

        FfxErrorCode DestroyBackendContextGL(FfxFsr2Interface* backendInterface)
        {
            Context* c = Ctx(backendInterface);
            for (Resource& r : c->Resources)
            {
                if (r.Owned && r.Texture)
                    glDeleteTextures(1, &r.Texture);
                r = Resource{};
            }
            for (auto& p : c->Pipelines)
                if (p && p->Program)
                    glDeleteProgram(p->Program);
            c->Pipelines.clear();
            if (c->SamplerPoint) glDeleteSamplers(1, &c->SamplerPoint);
            if (c->SamplerLinear) glDeleteSamplers(1, &c->SamplerLinear);
            glDeleteBuffers(FFX_MAX_NUM_CONST_BUFFERS, c->Ubos);
            c->SamplerPoint = c->SamplerLinear = 0;
            std::memset(c->Ubos, 0, sizeof(c->Ubos));
            c->Jobs.clear();
            return FFX_OK;
        }

        FfxErrorCode CreateResourceGL(FfxFsr2Interface* backendInterface, const FfxCreateResourceDescription* desc, FfxResourceInternal* out)
        {
            Context* c = Ctx(backendInterface);
            if (c->NextStatic >= c->NextDynamic)
                return FFX_ERROR_OUT_OF_MEMORY;
            if (desc->resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER || desc->resourceDescription.type == FFX_RESOURCE_TYPE_TEXTURE3D)
                return FFX_ERROR_INVALID_ARGUMENT;
            out->internalIndex = c->NextStatic++;
            Resource& r = c->Resources[out->internalIndex];
            r.Desc = desc->resourceDescription;
            const uint32_t w = std::max(r.Desc.width, 1u);
            const uint32_t h = r.Desc.type == FFX_RESOURCE_TYPE_TEXTURE1D ? 1u : std::max(r.Desc.height, 1u);
            const uint32_t fullChain = (uint32_t)std::floor(std::log2((float)std::max(w, h))) + 1u;
            const uint32_t mips = r.Desc.mipCount ? std::min(r.Desc.mipCount, fullChain) : fullChain;
            r.Desc.mipCount = mips;
            const FormatInfo f = MapFormat(r.Desc.format);
            r.InternalFormat = f.Internal;
            r.Integer = f.Integer;
            r.Owned = true;
            glCreateTextures(GL_TEXTURE_2D, 1, &r.Texture);
            glTextureStorage2D(r.Texture, (GLsizei)mips, f.Internal, (GLsizei)w, (GLsizei)h);
            glTextureParameteri(r.Texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTextureParameteri(r.Texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTextureParameteri(r.Texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(r.Texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (desc->initData && desc->initDataSize)
            {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTextureSubImage2D(r.Texture, 0, 0, 0, (GLsizei)w, (GLsizei)h, f.Format, f.Type, desc->initData);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            }
            else
            {
                for (uint32_t m = 0; m < mips; ++m)
                {
                    if (f.Integer)
                    {
                        const uint32_t zero[4] = {};
                        glClearTexImage(r.Texture, (GLint)m, GL_RGBA_INTEGER, GL_UNSIGNED_INT, zero);
                    }
                    else
                    {
                        const float zero[4] = {};
                        glClearTexImage(r.Texture, (GLint)m, GL_RGBA, GL_FLOAT, zero);
                    }
                }
            }
            return FFX_OK;
        }

        FfxErrorCode RegisterResourceGL(FfxFsr2Interface* backendInterface, const FfxResource* in, FfxResourceInternal* out)
        {
            Context* c = Ctx(backendInterface);
            if (in->resource == nullptr)
            {
                out->internalIndex = FFX_FSR2_RESOURCE_IDENTIFIER_NULL;
                return FFX_OK;
            }
            if (c->NextDynamic <= c->NextStatic)
                return FFX_ERROR_OUT_OF_MEMORY;
            out->internalIndex = c->NextDynamic--;
            Resource& r = c->Resources[out->internalIndex];
            r.Texture = (GLuint)(uintptr_t)in->resource;
            r.Owned = false;
            r.Desc = in->description;
            // descriptorData: GL iç biçimi (GetTextureResource yazar)
            r.InternalFormat = in->descriptorData ? (GLenum)in->descriptorData : MapFormat(in->description.format).Internal;
            r.Integer = MapFormat(in->description.format).Integer;
            return FFX_OK;
        }

        FfxErrorCode UnregisterResourcesGL(FfxFsr2Interface* backendInterface)
        {
            Context* c = Ctx(backendInterface);
            for (int i = c->NextDynamic + 1; i < kMaxResources; ++i)
                c->Resources[i] = Resource{};
            c->NextDynamic = kMaxResources - 1;
            return FFX_OK;
        }

        FfxResourceDescription GetResourceDescriptionGL(FfxFsr2Interface* backendInterface, FfxResourceInternal resource)
        {
            Context* c = Ctx(backendInterface);
            if (resource.internalIndex <= 0 || resource.internalIndex >= kMaxResources)
                return FfxResourceDescription{};
            return c->Resources[resource.internalIndex].Desc;
        }

        FfxErrorCode DestroyResourceGL(FfxFsr2Interface* backendInterface, FfxResourceInternal resource)
        {
            Context* c = Ctx(backendInterface);
            if (resource.internalIndex <= 0 || resource.internalIndex >= kMaxResources)
                return FFX_OK;
            Resource& r = c->Resources[resource.internalIndex];
            if (r.Owned && r.Texture)
                glDeleteTextures(1, &r.Texture);
            r = Resource{};
            return FFX_OK;
        }

        FfxErrorCode CreatePipelineGL(FfxFsr2Interface* backendInterface, FfxFsr2Pass pass, const FfxPipelineDescription* desc, FfxPipelineState* out)
        {
            Context* c = Ctx(backendInterface);
            std::string error;
            const GLuint program = CompilePass(c, pass, desc->contextFlags, error);
            if (!program)
            {
                CQ_CORE_ERROR("FSR 2: {0}", error);
                return FFX_ERROR_BACKEND_API_ERROR;
            }
            auto p = std::make_unique<Pipeline>();
            p->Program = program;
            std::vector<std::string> srvNames, uavNames, cbNames;
            // Doku (birleşik örnekleyici) ve görüntü değişkenleri: birimler sırayla atanır
            GLint count = 0;
            glGetProgramInterfaceiv(program, GL_UNIFORM, GL_ACTIVE_RESOURCES, &count);
            GLint nextTexture = 0, nextImage = 0;
            for (GLint i = 0; i < count; ++i)
            {
                const GLenum props[3] = { GL_TYPE, GL_LOCATION, GL_BLOCK_INDEX };
                GLint values[3] = {};
                glGetProgramResourceiv(program, GL_UNIFORM, (GLuint)i, 3, props, 3, nullptr, values);
                if (values[2] != -1 || values[1] < 0)
                    continue;
                char nameBuf[256];
                glGetProgramResourceName(program, GL_UNIFORM, (GLuint)i, sizeof(nameBuf), nullptr, nameBuf);
                std::string uname = nameBuf;
                const GLenum type = (GLenum)values[0];
                if (IsSamplerType(type) && uname.rfind("CQFSR2_", 0) == 0 && uname.size() > 10 && uname[uname.size() - 2] == 'S' &&
                    uname[uname.size() - 3] == '_')
                {
                    const char kind = uname.back();
                    const std::string image = uname.substr(7, uname.size() - 7 - 3);
                    auto it = std::find(srvNames.begin(), srvNames.end(), image);
                    size_t idx = (size_t)(it - srvNames.begin());
                    if (it == srvNames.end())
                    {
                        srvNames.push_back(image);
                        p->Srvs.emplace_back();
                    }
                    const GLint unit = nextTexture++;
                    glProgramUniform1i(program, values[1], unit);
                    p->Srvs[idx].push_back({ unit, kind == 'L' });
                    p->MaxTextureUnit = std::max(p->MaxTextureUnit, unit);
                }
                else if (IsImageType(type))
                {
                    const GLint unit = nextImage++;
                    glProgramUniform1i(program, values[1], unit);
                    uavNames.push_back(uname);
                    p->Uavs.push_back((GLuint)unit);
                    p->MaxImageUnit = std::max(p->MaxImageUnit, unit);
                }
            }
            GLint blocks = 0;
            glGetProgramInterfaceiv(program, GL_UNIFORM_BLOCK, GL_ACTIVE_RESOURCES, &blocks);
            for (GLint b = 0; b < blocks && b < FFX_MAX_NUM_CONST_BUFFERS; ++b)
            {
                char nameBuf[256];
                glGetProgramResourceName(program, GL_UNIFORM_BLOCK, (GLuint)b, sizeof(nameBuf), nullptr, nameBuf);
                std::string bname = nameBuf;
                if (bname.size() > 2 && bname.compare(bname.size() - 2, 2, "_t") == 0)
                    bname.resize(bname.size() - 2);   // cbFSR2_t -> cbFSR2 (SDK'nın aradığı ad)
                const GLuint binding = kUniformBufferBase + (GLuint)b;
                glUniformBlockBinding(program, (GLuint)b, binding);
                cbNames.push_back(bname);
                p->Cbs.push_back(binding);
            }
            if (srvNames.size() > FFX_MAX_NUM_SRVS || uavNames.size() > FFX_MAX_NUM_UAVS)
            {
                CQ_CORE_ERROR("FSR 2: {0} uses too many resources ({1} SRV / {2} UAV)", PassFile(pass), srvNames.size(), uavNames.size());
                glDeleteProgram(program);
                return FFX_ERROR_BACKEND_API_ERROR;
            }
            std::memset(out, 0, sizeof(*out));
            out->pipeline = p.get();
            out->srvCount = (uint32_t)srvNames.size();
            out->uavCount = (uint32_t)uavNames.size();
            out->constCount = (uint32_t)cbNames.size();
            for (size_t i = 0; i < srvNames.size(); ++i)
            {
                out->srvResourceBindings[i].slotIndex = (uint32_t)i;
                CopyName(out->srvResourceBindings[i].name, srvNames[i]);
            }
            for (size_t i = 0; i < uavNames.size(); ++i)
            {
                out->uavResourceBindings[i].slotIndex = (uint32_t)i;
                CopyName(out->uavResourceBindings[i].name, uavNames[i]);
            }
            for (size_t i = 0; i < cbNames.size(); ++i)
            {
                out->cbResourceBindings[i].slotIndex = (uint32_t)i;
                CopyName(out->cbResourceBindings[i].name, cbNames[i]);
            }
            c->Pipelines.push_back(std::move(p));
            return FFX_OK;
        }

        FfxErrorCode DestroyPipelineGL(FfxFsr2Interface* backendInterface, FfxPipelineState* pipeline)
        {
            Context* c = Ctx(backendInterface);
            if (!pipeline || !pipeline->pipeline)
                return FFX_OK;
            for (auto it = c->Pipelines.begin(); it != c->Pipelines.end(); ++it)
            {
                if (it->get() == pipeline->pipeline)
                {
                    if ((*it)->Program)
                        glDeleteProgram((*it)->Program);
                    c->Pipelines.erase(it);
                    break;
                }
            }
            pipeline->pipeline = nullptr;
            return FFX_OK;
        }

        FfxErrorCode ScheduleGpuJobGL(FfxFsr2Interface* backendInterface, const FfxGpuJobDescription* job)
        {
            Ctx(backendInterface)->Jobs.push_back(*job);
            return FFX_OK;
        }

        void ExecuteClear(Context* c, const FfxClearFloatJobDescription& job)
        {
            if (job.target.internalIndex <= 0 || job.target.internalIndex >= kMaxResources)
                return;
            const Resource& r = c->Resources[job.target.internalIndex];
            if (!r.Texture)
                return;
            for (uint32_t m = 0; m < std::max(r.Desc.mipCount, 1u); ++m)
            {
                if (r.Integer)
                {
                    const uint32_t v[4] = { (uint32_t)job.color[0], (uint32_t)job.color[1], (uint32_t)job.color[2], (uint32_t)job.color[3] };
                    glClearTexImage(r.Texture, (GLint)m, GL_RGBA_INTEGER, GL_UNSIGNED_INT, v);
                }
                else
                    glClearTexImage(r.Texture, (GLint)m, GL_RGBA, GL_FLOAT, job.color);
            }
        }

        void ExecuteCopy(Context* c, const FfxCopyJobDescription& job)
        {
            if (job.src.internalIndex <= 0 || job.dst.internalIndex <= 0 || job.src.internalIndex >= kMaxResources || job.dst.internalIndex >= kMaxResources)
                return;
            const Resource& s = c->Resources[job.src.internalIndex];
            const Resource& d = c->Resources[job.dst.internalIndex];
            if (!s.Texture || !d.Texture)
                return;
            const uint32_t mips = std::min(std::max(s.Desc.mipCount, 1u), std::max(d.Desc.mipCount, 1u));
            for (uint32_t m = 0; m < mips; ++m)
            {
                const GLsizei w = (GLsizei)std::max(std::max(s.Desc.width, 1u) >> m, 1u);
                const GLsizei h = (GLsizei)std::max((s.Desc.type == FFX_RESOURCE_TYPE_TEXTURE1D ? 1u : std::max(s.Desc.height, 1u)) >> m, 1u);
                glCopyImageSubData(s.Texture, GL_TEXTURE_2D, (GLint)m, 0, 0, 0, d.Texture, GL_TEXTURE_2D, (GLint)m, 0, 0, 0, w, h, 1);
            }
        }

        void ExecuteCompute(Context* c, const FfxComputeJobDescription& job, GLint& maxTex, GLint& maxImg)
        {
            const Pipeline* p = static_cast<const Pipeline*>(job.pipeline.pipeline);
            if (!p || !p->Program)
                return;
            glUseProgram(p->Program);
            for (uint32_t s = 0; s < job.pipeline.srvCount && s < p->Srvs.size(); ++s)
            {
                const int idx = job.srvs[s].internalIndex;
                const GLuint tex = (idx > 0 && idx < kMaxResources) ? c->Resources[idx].Texture : 0;
                for (const SrvUnit& u : p->Srvs[s])
                {
                    glBindTextureUnit((GLuint)u.Unit, tex);
                    glBindSampler((GLuint)u.Unit, u.Linear ? c->SamplerLinear : c->SamplerPoint);
                }
            }
            for (uint32_t u = 0; u < job.pipeline.uavCount && u < p->Uavs.size(); ++u)
            {
                const int idx = job.uavs[u].internalIndex;
                if (idx <= 0 || idx >= kMaxResources || !c->Resources[idx].Texture)
                    continue;
                const Resource& r = c->Resources[idx];
                glBindImageTexture(p->Uavs[u], r.Texture, (GLint)job.uavMip[u], GL_FALSE, 0, GL_READ_WRITE, r.InternalFormat);
            }
            for (uint32_t k = 0; k < job.pipeline.constCount && k < p->Cbs.size() && k < FFX_MAX_NUM_CONST_BUFFERS; ++k)
            {
                const uint32_t bytes = std::min<uint32_t>(job.cbs[k].uint32Size, FFX_MAX_CONST_SIZE) * sizeof(uint32_t);
                if (bytes)
                    glNamedBufferSubData(c->Ubos[k], 0, bytes, job.cbs[k].data);
                glBindBufferBase(GL_UNIFORM_BUFFER, p->Cbs[k], c->Ubos[k]);
            }
            glDispatchCompute(std::max(job.dimensions[0], 1u), std::max(job.dimensions[1], 1u), std::max(job.dimensions[2], 1u));
            glMemoryBarrier(GL_ALL_BARRIER_BITS);
            maxTex = std::max(maxTex, p->MaxTextureUnit);
            maxImg = std::max(maxImg, p->MaxImageUnit);
        }

        FfxErrorCode ExecuteGpuJobsGL(FfxFsr2Interface* backendInterface, FfxCommandList)
        {
            Context* c = Ctx(backendInterface);
            GLint maxTex = -1, maxImg = -1;
            for (const FfxGpuJobDescription& job : c->Jobs)
            {
                switch (job.jobType)
                {
                    case FFX_GPU_JOB_CLEAR_FLOAT: ExecuteClear(c, job.clearJobDescriptor); break;
                    case FFX_GPU_JOB_COPY:        ExecuteCopy(c, job.copyJobDescriptor); break;
                    case FFX_GPU_JOB_COMPUTE:     ExecuteCompute(c, job.computeJobDescriptor, maxTex, maxImg); break;
                    default: break;
                }
                glMemoryBarrier(GL_ALL_BARRIER_BITS);
            }
            c->Jobs.clear();
            // Örnekleyici nesneleri ve görüntüler bırakılır: motorun sonraki çizimleri doku parametrelerini kullanır
            for (GLint u = 0; u <= maxTex; ++u)
            {
                glBindSampler((GLuint)u, 0);
                glBindTextureUnit((GLuint)u, 0);
            }
            for (GLint u = 0; u <= maxImg; ++u)
                glBindImageTexture((GLuint)u, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
            for (GLuint k = 0; k < FFX_MAX_NUM_CONST_BUFFERS; ++k)
                glBindBufferBase(GL_UNIFORM_BUFFER, kUniformBufferBase + k, 0);
            glUseProgram(0);
            return FFX_OK;
        }
    }

    size_t GetScratchMemorySize()
    {
        return sizeof(Context*) + 16;
    }

    FfxErrorCode GetInterface(FfxFsr2Interface* out, void* scratchBuffer, size_t scratchBufferSize, const std::string& shaderDirectory)
    {
        if (!out || !scratchBuffer || scratchBufferSize < GetScratchMemorySize())
            return FFX_ERROR_INVALID_ARGUMENT;
        auto* context = new Context();
        context->ShaderDir = shaderDirectory;
        *reinterpret_cast<Context**>(scratchBuffer) = context;
        out->fpGetDeviceCapabilities = GetDeviceCapabilitiesGL;
        out->fpCreateBackendContext = CreateBackendContextGL;
        out->fpDestroyBackendContext = [](FfxFsr2Interface* backendInterface) -> FfxErrorCode {
            const FfxErrorCode r = DestroyBackendContextGL(backendInterface);
            delete Ctx(backendInterface);
            *reinterpret_cast<Context**>(backendInterface->scratchBuffer) = nullptr;
            return r;
        };
        out->fpCreateResource = CreateResourceGL;
        out->fpRegisterResource = RegisterResourceGL;
        out->fpUnregisterResources = UnregisterResourcesGL;
        out->fpGetResourceDescription = GetResourceDescriptionGL;
        out->fpDestroyResource = DestroyResourceGL;
        out->fpCreatePipeline = CreatePipelineGL;
        out->fpDestroyPipeline = DestroyPipelineGL;
        out->fpScheduleGpuJob = ScheduleGpuJobGL;
        out->fpExecuteGpuJobs = ExecuteGpuJobsGL;
        out->scratchBuffer = scratchBuffer;
        out->scratchBufferSize = scratchBufferSize;
        return FFX_OK;
    }

    FfxResource GetTextureResource(uint32_t texture, uint32_t width, uint32_t height, uint32_t glInternalFormat, const wchar_t* name, bool isDepth)
    {
        FfxResource r{};
        r.resource = (void*)(uintptr_t)texture;
        r.state = FFX_RESOURCE_STATE_COMPUTE_READ;
        r.isDepth = isDepth;
        r.descriptorData = glInternalFormat;
        r.description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
        r.description.format = FromGLFormat((GLenum)glInternalFormat);
        r.description.width = width;
        r.description.height = height;
        r.description.depth = 1;
        r.description.mipCount = 1;
        r.description.flags = FFX_RESOURCE_FLAGS_NONE;
        if (name)
        {
            const size_t n = std::min<size_t>(std::wcslen(name), 63);
            std::wmemcpy(r.name, name, n);
            r.name[n] = 0;
        }
        return r;
    }
}
