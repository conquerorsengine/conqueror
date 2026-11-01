#include "GLProfile.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Conqueror::GLProfile
{
    namespace
    {
        Kind s_Requested = Kind::Auto;
        Info s_Info;

        // ══════════════════════════════════════════ Özgün işlev göstericileri (sarmalananlar)
        PFNGLBINDTEXTUREPROC o_BindTexture = nullptr;
        PFNGLTEXIMAGE2DPROC o_TexImage2D = nullptr;
        PFNGLTEXSUBIMAGE2DPROC o_TexSubImage2D = nullptr;
        PFNGLENABLEPROC o_Enable = nullptr;
        PFNGLDISABLEPROC o_Disable = nullptr;
        PFNGLISENABLEDPROC o_IsEnabled = nullptr;
        PFNGLTEXSTORAGE2DPROC o_TexStorage2D = nullptr;
        PFNGLTEXPARAMETERIPROC o_TexParameteri = nullptr;
        PFNGLDRAWBUFFERSPROC o_DrawBuffers = nullptr;
        bool s_FloatLinear = true;   // ES: OES_texture_float_linear (32 bit float dokular doğrusal filtrelenebilir mi)
        PFNGLTEXSTORAGE2DMULTISAMPLEPROC o_TexStorage2DMultisample = nullptr;
        typedef void (APIENTRYP PFNQUERYCOUNTEREXTPROC)(GLuint id, GLenum target);
        typedef void (APIENTRYP PFNGETQUERYOBJECTUI64VEXTPROC)(GLuint id, GLenum pname, GLuint64* params);
        PFNQUERYCOUNTEREXTPROC o_QueryCounterEXT = nullptr;
        PFNGETQUERYOBJECTUI64VEXTPROC o_GetQueryObjectui64vEXT = nullptr;

        // Doku hedefleri (DSA çağrıları hedef almaz) ve boyut kaydı (ES 3.0'da glGetTexLevelParameteriv yok)
        struct TextureRecord
        {
            GLenum Target = GL_TEXTURE_2D;
            GLsizei Width = 0, Height = 0;
            GLenum InternalFormat = 0;
            // İstenen filtreler (ES'de filtrelenemeyen biçimlerde sürücüye NEAREST karşılığı gider)
            GLint MinFilter = GL_NEAREST_MIPMAP_LINEAR, MagFilter = GL_LINEAR;
            bool Compare = false;
        };
        std::unordered_map<GLuint, TextureRecord> s_Textures;

        GLenum TargetOf(GLuint texture)
        {
            auto it = s_Textures.find(texture);
            return it != s_Textures.end() ? it->second.Target : GL_TEXTURE_2D;
        }

        GLenum BindingQuery(GLenum target)
        {
            switch (target)
            {
                case GL_TEXTURE_CUBE_MAP: return GL_TEXTURE_BINDING_CUBE_MAP;
                case GL_TEXTURE_3D: return GL_TEXTURE_BINDING_3D;
                case GL_TEXTURE_2D_ARRAY: return GL_TEXTURE_BINDING_2D_ARRAY;
                case GL_TEXTURE_2D_MULTISAMPLE: return GL_TEXTURE_BINDING_2D_MULTISAMPLE;
                default: return GL_TEXTURE_BINDING_2D;
            }
        }

        // Geçici bağlama: hedefin önceki bağını geri yükler
        struct TextureScope
        {
            GLenum Target;
            GLint Previous = 0;
            TextureScope(GLuint texture)
                : Target(TargetOf(texture))
            {
                glGetIntegerv(BindingQuery(Target), &Previous);
                o_BindTexture(Target, texture);
            }
            ~TextureScope() { o_BindTexture(Target, (GLuint)Previous); }
        };

        struct FramebufferScope
        {
            GLint Draw = 0, Read = 0;
            FramebufferScope(GLenum target, GLuint fb)
            {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &Draw);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &Read);
                glad_glBindFramebuffer(target, fb);
            }
            ~FramebufferScope()
            {
                glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)Draw);
                glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)Read);
            }
        };

        struct BufferScope
        {
            GLint Previous = 0;
            BufferScope(GLuint buffer)
            {
                glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &Previous);
                glad_glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
            }
            ~BufferScope() { glad_glBindBuffer(GL_COPY_WRITE_BUFFER, (GLuint)Previous); }
        };

        // Biçim -> piksel boyutu (okuma / temizleme yardımcıları)
        int ComponentCount(GLenum format)
        {
            switch (format)
            {
                case GL_RED: case GL_RED_INTEGER: case GL_DEPTH_COMPONENT: return 1;
                case GL_RG: case GL_RG_INTEGER: return 2;
                case GL_RGB: case GL_BGR: case GL_RGB_INTEGER: return 3;
                default: return 4;
            }
        }
        int ComponentSize(GLenum type)
        {
            switch (type)
            {
                case GL_UNSIGNED_BYTE: case GL_BYTE: return 1;
                case GL_UNSIGNED_SHORT: case GL_SHORT: case GL_HALF_FLOAT: return 2;
                default: return 4;
            }
        }

        // ══════════════════════════════════════════ Sarmalayıcılar
        void APIENTRY W_BindTexture(GLenum target, GLuint texture)
        {
            if (texture)
                s_Textures[texture].Target = target;
            o_BindTexture(target, texture);
        }

        GLenum BindingTarget(GLenum target)
        {
            return (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) ? GL_TEXTURE_CUBE_MAP : target;
        }

        GLuint BoundTexture(GLenum target)
        {
            GLint id = 0;
            glGetIntegerv(BindingQuery(BindingTarget(target)), &id);
            return (GLuint)id;
        }

        // OpenGL ES 3.x'te doğrusal filtrelenemeyen biçimler: derinlik (karşılaştırma kipi kapalıyken), 32 bit float
        // (OES_texture_float_linear yoksa) ve tamsayı. Doğrusal filtre istenirse doku "eksik" sayılır ve örnekleme
        // 0 döndürür (SSR, sis, hareket bulanıklığı derinliği hiç göremez) -> sürücüye NEAREST karşılığı verilir.
        bool NonFilterable(const TextureRecord& r)
        {
            if (!s_Info.ES)
                return false;
            switch (r.InternalFormat)
            {
                case GL_DEPTH_COMPONENT16: case GL_DEPTH_COMPONENT24: case GL_DEPTH_COMPONENT32: case GL_DEPTH_COMPONENT32F:
                case GL_DEPTH24_STENCIL8: case GL_DEPTH32F_STENCIL8:
                    return !r.Compare;
                case GL_R32F: case GL_RG32F: case GL_RGB32F: case GL_RGBA32F:
                    return !s_FloatLinear;
                case GL_R8I: case GL_R8UI: case GL_R16I: case GL_R16UI: case GL_R32I: case GL_R32UI:
                case GL_RG32I: case GL_RG32UI: case GL_RGBA32I: case GL_RGBA32UI: case GL_RGBA8UI: case GL_RGBA16UI:
                    return true;
                default:
                    return false;
            }
        }

        GLint NearestOf(GLint filter)
        {
            switch (filter)
            {
                case GL_LINEAR: return GL_NEAREST;
                case GL_LINEAR_MIPMAP_LINEAR: case GL_LINEAR_MIPMAP_NEAREST: case GL_NEAREST_MIPMAP_LINEAR: return GL_NEAREST_MIPMAP_NEAREST;
                default: return filter;
            }
        }

        // Bağlı dokunun etkin filtrelerini (kayıttaki isteğe göre) sürücüye yeniden yazar
        void ApplyFilters(GLenum target, const TextureRecord& r)
        {
            if (!o_TexParameteri)
                return;
            const bool nearest = NonFilterable(r);
            o_TexParameteri(target, GL_TEXTURE_MIN_FILTER, nearest ? NearestOf(r.MinFilter) : r.MinFilter);
            o_TexParameteri(target, GL_TEXTURE_MAG_FILTER, nearest ? NearestOf(r.MagFilter) : r.MagFilter);
        }

        void RecordSize(GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h)
        {
            if (level != 0) return;
            GLuint id = BoundTexture(target);
            if (!id) return;
            auto& r = s_Textures[id];
            r.Target = BindingTarget(target);
            r.Width = w;
            r.Height = h;
            r.InternalFormat = (GLenum)internalFormat;
            if (NonFilterable(r))
                ApplyFilters(r.Target, r);
        }

        void APIENTRY W_TexParameteri(GLenum target, GLenum pname, GLint param)
        {
            if (pname != GL_TEXTURE_MIN_FILTER && pname != GL_TEXTURE_MAG_FILTER && pname != GL_TEXTURE_COMPARE_MODE)
            {
                o_TexParameteri(target, pname, param);
                return;
            }
            const GLuint id = BoundTexture(target);
            if (!id)
            {
                o_TexParameteri(target, pname, param);
                return;
            }
            auto& r = s_Textures[id];
            if (pname == GL_TEXTURE_COMPARE_MODE)
            {
                o_TexParameteri(target, pname, param);
                r.Compare = param == GL_COMPARE_REF_TO_TEXTURE;
                ApplyFilters(BindingTarget(target), r);
                return;
            }
            (pname == GL_TEXTURE_MIN_FILTER ? r.MinFilter : r.MagFilter) = param;
            o_TexParameteri(target, pname, NonFilterable(r) ? NearestOf(param) : param);
        }

        // ES: BGRA yok -> CPU'da RGBA'ya çevir
        const void* SwizzleBGRA(GLenum& format, GLenum type, GLsizei w, GLsizei h, const void* data, std::vector<unsigned char>& storage)
        {
            if (!s_Info.ES || format != GL_BGRA || !data || type != GL_UNSIGNED_BYTE)
                return data;
            const size_t n = (size_t)w * (size_t)h;
            storage.assign((const unsigned char*)data, (const unsigned char*)data + n * 4);
            for (size_t i = 0; i < n; ++i)
                std::swap(storage[i * 4], storage[i * 4 + 2]);
            format = GL_RGBA;
            return storage.data();
        }

        // ES iç biçim kuralları: masaüstüne özgü iç biçimler en yakın ES karşılığına
        GLint MapInternalFormat(GLint internalFormat)
        {
            if (!s_Info.ES) return internalFormat;
            switch (internalFormat)
            {
                case GL_RGB16: return GL_RGB16F;
                case GL_RGBA16: return GL_RGBA16F;
                case GL_R16: return GL_R16F;
                case GL_RG16: return GL_RG16F;
                case GL_DEPTH_COMPONENT32: return GL_DEPTH_COMPONENT32F;
                case GL_DEPTH_COMPONENT: return GL_DEPTH_COMPONENT24;
                default: return internalFormat;
            }
        }

        void APIENTRY W_TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                                   GLenum format, GLenum type, const void* pixels)
        {
            std::vector<unsigned char> storage;
            pixels = SwizzleBGRA(format, type, width, height, pixels, storage);
            if (s_Info.ES && internalformat == GL_RGBA8 && format == GL_BGRA) format = GL_RGBA;
            o_TexImage2D(target, level, MapInternalFormat(internalformat), width, height, border, format, type, pixels);
            RecordSize(target, level, internalformat, width, height);
        }

        void APIENTRY W_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                                      GLenum format, GLenum type, const void* pixels)
        {
            std::vector<unsigned char> storage;
            pixels = SwizzleBGRA(format, type, width, height, pixels, storage);
            o_TexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
        }

        // glTexStorage2D yoksa (eski masaüstü sürücü): her mip düzeyi için glTexImage2D
        void APIENTRY S_TexStorage2DFallback(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
        {
            GLenum format = GL_RGBA, type = GL_UNSIGNED_BYTE;
            switch (internalformat)
            {
                case GL_DEPTH_COMPONENT16: case GL_DEPTH_COMPONENT24: case GL_DEPTH_COMPONENT32: format = GL_DEPTH_COMPONENT; type = GL_UNSIGNED_INT; break;
                case GL_DEPTH_COMPONENT32F: format = GL_DEPTH_COMPONENT; type = GL_FLOAT; break;
                case GL_DEPTH24_STENCIL8: format = GL_DEPTH_STENCIL; type = GL_UNSIGNED_INT_24_8; break;
                case GL_R8: format = GL_RED; break;
                case GL_RG8: format = GL_RG; break;
                case GL_RGB8: format = GL_RGB; break;
                case GL_R16F: case GL_R32F: format = GL_RED; type = GL_FLOAT; break;
                case GL_RG16F: case GL_RG32F: format = GL_RG; type = GL_FLOAT; break;
                case GL_RGB16F: case GL_RGB32F: case GL_R11F_G11F_B10F: format = GL_RGB; type = GL_FLOAT; break;
                case GL_RGBA16F: case GL_RGBA32F: type = GL_FLOAT; break;
                case GL_R32I: format = GL_RED_INTEGER; type = GL_INT; break;
                case GL_R32UI: format = GL_RED_INTEGER; type = GL_UNSIGNED_INT; break;
                default: break;
            }
            for (GLsizei level = 0; level < levels; ++level)
            {
                const GLsizei w = std::max(1, width >> level), h = std::max(1, height >> level);
                if (target == GL_TEXTURE_CUBE_MAP)
                    for (int face = 0; face < 6; ++face)
                        o_TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, (GLint)internalformat, w, h, 0, format, type, nullptr);
                else
                    o_TexImage2D(target, level, (GLint)internalformat, w, h, 0, format, type, nullptr);
            }
            glad_glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, levels - 1);
        }

        void APIENTRY W_TexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
        {
            const GLenum mapped = (GLenum)MapInternalFormat((GLint)internalformat);
            if (o_TexStorage2D) o_TexStorage2D(target, levels, mapped, width, height);
            else S_TexStorage2DFallback(target, levels, mapped, width, height);
            RecordSize(target, 0, (GLint)internalformat, width, height);
        }

        // ES: masaüstüne özgü yetenekler sessizce yok sayılır
        bool IgnoredCap(GLenum cap)
        {
            if (!s_Info.ES) return false;
            switch (cap)
            {
                case GL_TEXTURE_CUBE_MAP_SEAMLESS: case GL_PROGRAM_POINT_SIZE: case GL_LINE_SMOOTH: case GL_MULTISAMPLE:
                case GL_FRAMEBUFFER_SRGB: case GL_DEPTH_CLAMP: case GL_POLYGON_OFFSET_LINE: case GL_POLYGON_OFFSET_POINT:
                case GL_POLYGON_SMOOTH: case GL_CLIP_DISTANCE0:
                    return true;
                default:
                    return false;
            }
        }
        void APIENTRY W_Enable(GLenum cap) { if (!IgnoredCap(cap)) o_Enable(cap); }
        void APIENTRY W_Disable(GLenum cap) { if (!IgnoredCap(cap)) o_Disable(cap); }
        GLboolean APIENTRY W_IsEnabled(GLenum cap) { return IgnoredCap(cap) ? GL_FALSE : o_IsEnabled(cap); }

        // ══════════════════════════════════════════ DSA taklitleri (4.5 öncesi / ES)
        void APIENTRY S_CreateTextures(GLenum target, GLsizei n, GLuint* textures)
        {
            glad_glGenTextures(n, textures);
            for (GLsizei i = 0; i < n; ++i)
            {
                s_Textures[textures[i]].Target = target;
                GLint prev = 0;
                glGetIntegerv(BindingQuery(target), &prev);
                o_BindTexture(target, textures[i]);   // ad nesneye ancak bağlanınca dönüşür
                o_BindTexture(target, (GLuint)prev);
            }
        }

        void APIENTRY S_TextureStorage2D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
        {
            TextureScope scope(texture);
            W_TexStorage2D(scope.Target, levels, internalformat, width, height);
            auto& r = s_Textures[texture];
            r.Width = width;
            r.Height = height;
            r.InternalFormat = internalformat;
        }

        void APIENTRY S_TextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                                          GLenum format, GLenum type, const void* pixels)
        {
            TextureScope scope(texture);
            W_TexSubImage2D(scope.Target, level, xoffset, yoffset, width, height, format, type, pixels);
        }

        void APIENTRY S_TextureParameteri(GLuint texture, GLenum pname, GLint param)
        {
            if (s_Info.ES && param == GL_CLAMP_TO_BORDER && s_Info.Major == 3 && s_Info.Minor < 2)
                param = GL_CLAMP_TO_EDGE;
            TextureScope scope(texture);
            glad_glTexParameteri(scope.Target, pname, param);
        }

        void APIENTRY S_TextureParameterf(GLuint texture, GLenum pname, GLfloat param)
        {
            TextureScope scope(texture);
            glad_glTexParameterf(scope.Target, pname, param);
        }

        void APIENTRY S_TextureParameterfv(GLuint texture, GLenum pname, const GLfloat* params)
        {
            if (s_Info.ES && pname == GL_TEXTURE_BORDER_COLOR && (s_Info.Major == 3 && s_Info.Minor < 2))
                return;
            TextureScope scope(texture);
            glad_glTexParameterfv(scope.Target, pname, params);
        }

        void APIENTRY S_BindTextureUnit(GLuint unit, GLuint texture)
        {
            GLint active = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            glad_glActiveTexture(GL_TEXTURE0 + unit);
            if (texture)
                o_BindTexture(TargetOf(texture), texture);
            else
            {
                o_BindTexture(GL_TEXTURE_2D, 0);
                o_BindTexture(GL_TEXTURE_CUBE_MAP, 0);
            }
            glad_glActiveTexture((GLenum)active);
        }

        void APIENTRY S_GenerateTextureMipmap(GLuint texture)
        {
            TextureScope scope(texture);
            glad_glGenerateMipmap(scope.Target);
        }

        void APIENTRY S_CreateBuffers(GLsizei n, GLuint* buffers)
        {
            glad_glGenBuffers(n, buffers);
            for (GLsizei i = 0; i < n; ++i)
                BufferScope scope(buffers[i]);
        }

        void APIENTRY S_NamedBufferData(GLuint buffer, GLsizeiptr size, const void* data, GLenum usage)
        {
            BufferScope scope(buffer);
            glad_glBufferData(GL_COPY_WRITE_BUFFER, size, data, usage);
        }

        void APIENTRY S_NamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data)
        {
            BufferScope scope(buffer);
            glad_glBufferSubData(GL_COPY_WRITE_BUFFER, offset, size, data);
        }

        // Köşe dizisi biçimleri (4.3 attrib binding) -> klasik glVertexAttribPointer
        struct VaoState
        {
            struct Binding { GLuint Buffer = 0; GLintptr Offset = 0; GLsizei Stride = 0; GLuint Divisor = 0; };
            struct Attrib { bool HasFormat = false; GLint Size = 4; GLenum Type = GL_FLOAT; GLboolean Normalized = GL_FALSE; GLuint RelativeOffset = 0; bool Integer = false; GLuint Binding = 0; };
            Binding Bindings[16];
            Attrib Attribs[16];
        };
        std::unordered_map<GLuint, VaoState> s_Vaos;

        struct VaoScope
        {
            GLint PrevVao = 0, PrevArray = 0;
            VaoScope(GLuint vao)
            {
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &PrevVao);
                glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &PrevArray);
                glad_glBindVertexArray(vao);
            }
            ~VaoScope()
            {
                glad_glBindVertexArray((GLuint)PrevVao);
                glad_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)PrevArray);
            }
        };

        void ApplyAttrib(GLuint vao, GLuint attrib)
        {
            if (attrib >= 16) return;
            VaoState& s = s_Vaos[vao];
            const auto& a = s.Attribs[attrib];
            if (!a.HasFormat || a.Binding >= 16) return;
            const auto& b = s.Bindings[a.Binding];
            if (!b.Buffer) return;
            VaoScope scope(vao);
            glad_glBindBuffer(GL_ARRAY_BUFFER, b.Buffer);
            const void* offset = (const void*)(uintptr_t)(b.Offset + a.RelativeOffset);
            if (a.Integer) glad_glVertexAttribIPointer(attrib, a.Size, a.Type, b.Stride, offset);
            else glad_glVertexAttribPointer(attrib, a.Size, a.Type, a.Normalized, b.Stride, offset);
            glad_glVertexAttribDivisor(attrib, b.Divisor);
        }

        void APIENTRY S_CreateVertexArrays(GLsizei n, GLuint* arrays)
        {
            glad_glGenVertexArrays(n, arrays);
            for (GLsizei i = 0; i < n; ++i)
            {
                VaoScope scope(arrays[i]);
                s_Vaos[arrays[i]] = VaoState{};
            }
        }

        void APIENTRY S_VertexArrayVertexBuffer(GLuint vao, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)
        {
            if (bindingindex >= 16) return;
            auto& b = s_Vaos[vao].Bindings[bindingindex];
            b.Buffer = buffer;
            b.Offset = offset;
            b.Stride = stride;
            for (GLuint a = 0; a < 16; ++a)
                if (s_Vaos[vao].Attribs[a].Binding == bindingindex)
                    ApplyAttrib(vao, a);
        }

        void APIENTRY S_VertexArrayAttribFormat(GLuint vao, GLuint attrib, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset)
        {
            if (attrib >= 16) return;
            auto& a = s_Vaos[vao].Attribs[attrib];
            a.HasFormat = true;
            a.Size = size;
            a.Type = type;
            a.Normalized = normalized;
            a.RelativeOffset = relativeoffset;
            a.Integer = false;
            ApplyAttrib(vao, attrib);
        }

        void APIENTRY S_VertexArrayAttribIFormat(GLuint vao, GLuint attrib, GLint size, GLenum type, GLuint relativeoffset)
        {
            if (attrib >= 16) return;
            auto& a = s_Vaos[vao].Attribs[attrib];
            a.HasFormat = true;
            a.Size = size;
            a.Type = type;
            a.RelativeOffset = relativeoffset;
            a.Integer = true;
            ApplyAttrib(vao, attrib);
        }

        void APIENTRY S_VertexArrayAttribBinding(GLuint vao, GLuint attrib, GLuint bindingindex)
        {
            if (attrib >= 16) return;
            s_Vaos[vao].Attribs[attrib].Binding = bindingindex;
            ApplyAttrib(vao, attrib);
        }

        void APIENTRY S_VertexArrayBindingDivisor(GLuint vao, GLuint bindingindex, GLuint divisor)
        {
            if (bindingindex >= 16) return;
            s_Vaos[vao].Bindings[bindingindex].Divisor = divisor;
            for (GLuint a = 0; a < 16; ++a)
                if (s_Vaos[vao].Attribs[a].Binding == bindingindex)
                    ApplyAttrib(vao, a);
        }

        void APIENTRY S_EnableVertexArrayAttrib(GLuint vao, GLuint index)
        {
            VaoScope scope(vao);
            glad_glEnableVertexAttribArray(index);
        }

        void APIENTRY S_DisableVertexArrayAttrib(GLuint vao, GLuint index)
        {
            VaoScope scope(vao);
            glad_glDisableVertexAttribArray(index);
        }

        void APIENTRY S_VertexArrayElementBuffer(GLuint vao, GLuint buffer)
        {
            GLint prev = 0;
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev);
            glad_glBindVertexArray(vao);
            glad_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
            glad_glBindVertexArray((GLuint)prev);
        }

        void APIENTRY S_CreateFramebuffers(GLsizei n, GLuint* framebuffers)
        {
            glad_glGenFramebuffers(n, framebuffers);
            for (GLsizei i = 0; i < n; ++i)
                FramebufferScope scope(GL_FRAMEBUFFER, framebuffers[i]);
        }

        void APIENTRY S_NamedFramebufferTexture(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level)
        {
            FramebufferScope scope(GL_FRAMEBUFFER, framebuffer);
            const GLenum target = texture ? TargetOf(texture) : GL_TEXTURE_2D;
            if (target == GL_TEXTURE_CUBE_MAP)
                glad_glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_CUBE_MAP_POSITIVE_X, texture, level);
            else
                glad_glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, target, texture, level);
        }

        void APIENTRY S_NamedFramebufferTextureLayer(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint layer)
        {
            FramebufferScope scope(GL_FRAMEBUFFER, framebuffer);
            if (TargetOf(texture) == GL_TEXTURE_CUBE_MAP)
                glad_glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, texture, level);
            else
                glad_glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, texture, level, layer);
        }

        void APIENTRY S_NamedFramebufferRenderbuffer(GLuint framebuffer, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
        {
            FramebufferScope scope(GL_FRAMEBUFFER, framebuffer);
            glad_glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, renderbuffertarget, renderbuffer);
        }

        // ES: glDrawBuffers listesinin i. öğesi ya GL_COLOR_ATTACHMENTi ya GL_NONE olmalı (masaüstünde herhangi bir ek).
        // {ATTACHMENT1} gibi bir liste ES'de geçersiz sayılır ve çizim hâlâ ATTACHMENT0'a gider (MSAA çözümünde normal
        // eki sahne renginin üstüne kopyalanıyordu) -> her ek kendi konumuna, aradakiler GL_NONE.
        void APIENTRY W_DrawBuffersES(GLsizei n, const GLenum* bufs)
        {
            GLint fbo = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
            if (fbo == 0 || n <= 0 || !bufs)
            {
                o_DrawBuffers(n, bufs);
                return;
            }
            GLenum mapped[8];
            GLsizei count = 0;
            for (GLsizei i = 0; i < 8; ++i)
                mapped[i] = GL_NONE;
            for (GLsizei i = 0; i < n; ++i)
            {
                if (bufs[i] < GL_COLOR_ATTACHMENT0 || bufs[i] > GL_COLOR_ATTACHMENT7)
                    continue;
                const GLsizei slot = (GLsizei)(bufs[i] - GL_COLOR_ATTACHMENT0);
                mapped[slot] = bufs[i];
                count = std::max(count, slot + 1);
            }
            if (count == 0)
            {
                const GLenum none = GL_NONE;
                o_DrawBuffers(1, &none);
                return;
            }
            o_DrawBuffers(count, mapped);
        }

        void APIENTRY S_NamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n, const GLenum* bufs)
        {
            FramebufferScope scope(GL_DRAW_FRAMEBUFFER, framebuffer);
            glad_glDrawBuffers(n, bufs);
        }

        void APIENTRY S_NamedFramebufferDrawBuffer(GLuint framebuffer, GLenum buf)
        {
            FramebufferScope scope(GL_DRAW_FRAMEBUFFER, framebuffer);
            glad_glDrawBuffers(1, &buf);
        }

        void APIENTRY S_NamedFramebufferReadBuffer(GLuint framebuffer, GLenum src)
        {
            FramebufferScope scope(GL_READ_FRAMEBUFFER, framebuffer);
            glad_glReadBuffer(src);
        }

        void APIENTRY S_BlitNamedFramebuffer(GLuint readFramebuffer, GLuint drawFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                             GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)
        {
            GLint draw = 0, read = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
            glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
            glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
            glad_glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
            glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
            glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
        }

        void APIENTRY S_GetNamedFramebufferAttachmentParameteriv(GLuint framebuffer, GLenum attachment, GLenum pname, GLint* params)
        {
            FramebufferScope scope(GL_FRAMEBUFFER, framebuffer);
            glad_glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, attachment, pname, params);
        }

        GLenum APIENTRY S_CheckNamedFramebufferStatus(GLuint framebuffer, GLenum target)
        {
            FramebufferScope scope(GL_FRAMEBUFFER, framebuffer);
            (void)target;
            return glad_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        }

        void APIENTRY S_CreateRenderbuffers(GLsizei n, GLuint* renderbuffers)
        {
            glad_glGenRenderbuffers(n, renderbuffers);
            GLint prev = 0;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev);
            for (GLsizei i = 0; i < n; ++i)
                glad_glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[i]);
            glad_glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev);
        }

        void APIENTRY S_NamedRenderbufferStorage(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height)
        {
            GLint prev = 0;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev);
            glad_glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
            glad_glRenderbufferStorage(GL_RENDERBUFFER, (GLenum)MapInternalFormat((GLint)internalformat), width, height);
            glad_glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev);
        }

        void APIENTRY S_GetNamedRenderbufferParameteriv(GLuint renderbuffer, GLenum pname, GLint* params)
        {
            GLint prev = 0;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev);
            glad_glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
            glad_glGetRenderbufferParameteriv(GL_RENDERBUFFER, pname, params);
            glad_glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev);
        }

        // Okuma: geçici çerçeve arabelleği + glReadPixels (ES'de glGetTexImage yok; masaüstünde 4.5 öncesi
        // glGetTextureSubImage yok). Renk dokuları için.
        GLuint s_ReadFbo = 0;

        bool ReadTexture(GLuint texture, GLenum face, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* pixels)
        {
            if (!s_ReadFbo) glad_glGenFramebuffers(1, &s_ReadFbo);
            GLint draw = 0, read = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
            glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, s_ReadFbo);
            glad_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, face, texture, level);
            const bool ok = glad_glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            if (ok)
            {
                glad_glReadBuffer(GL_COLOR_ATTACHMENT0);
                GLint alignment = 4;
                glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
                glad_glPixelStorei(GL_PACK_ALIGNMENT, 1);
                if (s_Info.ES && format != GL_RGBA && format != GL_RGBA_INTEGER)
                {
                    // ES yalnızca RGBA okumayı garanti eder: oku ve istenen bileşenlere indir
                    const int comps = ComponentCount(format), size = ComponentSize(type);
                    std::vector<unsigned char> tmp((size_t)w * h * 4 * size);
                    glad_glReadPixels(x, y, w, h, GL_RGBA, type, tmp.data());
                    for (size_t i = 0; i < (size_t)w * h; ++i)
                        std::memcpy((unsigned char*)pixels + i * comps * size, tmp.data() + i * 4 * size, (size_t)comps * size);
                }
                else
                    glad_glReadPixels(x, y, w, h, format, type, pixels);
                glad_glPixelStorei(GL_PACK_ALIGNMENT, alignment);
            }
            glad_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
            glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
            glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
            return ok;
        }

        void TextureLevelSize(GLuint texture, GLint level, GLsizei& w, GLsizei& h)
        {
            w = h = 0;
            if (!s_Info.ES || s_Info.Minor >= 1)
            {
                TextureScope scope(texture);
                const GLenum q = scope.Target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : scope.Target;
                GLint iw = 0, ih = 0;
                glad_glGetTexLevelParameteriv(q, level, GL_TEXTURE_WIDTH, &iw);
                glad_glGetTexLevelParameteriv(q, level, GL_TEXTURE_HEIGHT, &ih);
                w = iw;
                h = ih;
            }
            if (w == 0)
            {
                auto it = s_Textures.find(texture);
                if (it != s_Textures.end())
                {
                    w = std::max(1, it->second.Width >> level);
                    h = std::max(1, it->second.Height >> level);
                }
            }
        }

        void APIENTRY S_GetTextureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, void* pixels)
        {
            (void)bufSize;
            const GLenum target = TargetOf(texture);
            if (!s_Info.ES)
            {
                TextureScope scope(texture);
                if (target == GL_TEXTURE_CUBE_MAP)
                {
                    GLsizei w, h;
                    TextureLevelSize(texture, level, w, h);
                    const size_t faceBytes = (size_t)w * h * ComponentCount(format) * ComponentSize(type);
                    for (int face = 0; face < 6; ++face)
                        glad_glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, format, type, (unsigned char*)pixels + face * faceBytes);
                }
                else
                    glad_glGetTexImage(target, level, format, type, pixels);
                return;
            }
            GLsizei w, h;
            TextureLevelSize(texture, level, w, h);
            if (target == GL_TEXTURE_CUBE_MAP)
            {
                const size_t faceBytes = (size_t)w * h * ComponentCount(format) * ComponentSize(type);
                for (int face = 0; face < 6; ++face)
                    ReadTexture(texture, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, 0, 0, w, h, format, type, (unsigned char*)pixels + face * faceBytes);
            }
            else
                ReadTexture(texture, GL_TEXTURE_2D, level, 0, 0, w, h, format, type, pixels);
        }

        void APIENTRY S_GetTextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height,
                                           GLsizei depth, GLenum format, GLenum type, GLsizei bufSize, void* pixels)
        {
            (void)depth;
            (void)bufSize;
            const GLenum target = TargetOf(texture);
            const GLenum face = target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + zoffset : GL_TEXTURE_2D;
            ReadTexture(texture, face, level, xoffset, yoffset, width, height, format, type, pixels);
        }

        // ES: glGetTexImage(hedef, ...) -> çerçeve arabelleğiyle
        void APIENTRY S_GetTexImageES(GLenum target, GLint level, GLenum format, GLenum type, void* pixels)
        {
            const GLuint texture = BoundTexture(target);
            if (!texture) return;
            GLsizei w, h;
            TextureLevelSize(texture, level, w, h);
            ReadTexture(texture, target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : target, level, 0, 0, w, h, format, type, pixels);
        }

        void APIENTRY S_GetTextureLevelParameteriv(GLuint texture, GLint level, GLenum pname, GLint* params)
        {
            if (!s_Info.ES || s_Info.Minor >= 1)
            {
                TextureScope scope(texture);
                const GLenum q = scope.Target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : scope.Target;
                glad_glGetTexLevelParameteriv(q, level, pname, params);
                return;
            }
            GLsizei w, h;
            TextureLevelSize(texture, level, w, h);
            auto it = s_Textures.find(texture);
            switch (pname)
            {
                case GL_TEXTURE_WIDTH: *params = w; break;
                case GL_TEXTURE_HEIGHT: *params = h; break;
                case GL_TEXTURE_INTERNAL_FORMAT: *params = it != s_Textures.end() ? (GLint)it->second.InternalFormat : GL_RGBA8; break;
                default: *params = 0; break;
            }
        }

        // glClearTexImage (4.4): çerçeve arabelleği temizliği
        void APIENTRY S_ClearTexImage(GLuint texture, GLint level, GLenum format, GLenum type, const void* data)
        {
            if (!s_ReadFbo) glad_glGenFramebuffers(1, &s_ReadFbo);
            GLint draw = 0, read = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
            glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_ReadFbo);
            const bool depth = format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL;
            const GLenum attachment = depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
            const GLenum target = TargetOf(texture) == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : TargetOf(texture);
            glad_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, target, texture, level);
            if (depth)
            {
                GLfloat d = 1.0f;
                if (data && type == GL_FLOAT) d = *(const float*)data;
                glad_glClearBufferfv(GL_DEPTH, 0, &d);
            }
            else
            {
                GLenum none = GL_COLOR_ATTACHMENT0;
                glad_glDrawBuffers(1, &none);
                GLfloat color[4] = { 0, 0, 0, 0 };
                if (data)
                {
                    const int comps = ComponentCount(format);
                    for (int c = 0; c < comps && c < 4; ++c)
                    {
                        if (type == GL_FLOAT) color[c] = ((const float*)data)[c];
                        else if (type == GL_UNSIGNED_BYTE) color[c] = ((const unsigned char*)data)[c] / 255.0f;
                        else if (type == GL_INT) color[c] = (float)((const int*)data)[c];
                        else if (type == GL_UNSIGNED_INT) color[c] = (float)((const unsigned int*)data)[c];
                    }
                }
                if (format == GL_RED_INTEGER || format == GL_RG_INTEGER || format == GL_RGBA_INTEGER)
                {
                    GLint ic[4] = { (GLint)color[0], (GLint)color[1], (GLint)color[2], (GLint)color[3] };
                    glad_glClearBufferiv(GL_COLOR, 0, ic);
                }
                else
                    glad_glClearBufferfv(GL_COLOR, 0, color);
            }
            glad_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
            glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
            glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
        }

        void APIENTRY S_MultiDrawElements(GLenum mode, const GLsizei* count, GLenum type, const void* const* indices, GLsizei drawcount)
        {
            for (GLsizei i = 0; i < drawcount; ++i)
                if (count[i] > 0)
                    glad_glDrawElements(mode, count[i], type, indices[i]);
        }

        void APIENTRY S_PolygonModeES(GLenum, GLenum) {}
        void APIENTRY S_DrawBufferES(GLenum buf) { glad_glDrawBuffers(1, &buf); }

        void APIENTRY S_TexImage2DMultisampleES(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixed)
        {
            if (o_TexStorage2DMultisample)
                o_TexStorage2DMultisample(target, samples, (GLenum)MapInternalFormat((GLint)internalformat), width, height, fixed);
        }

        void APIENTRY S_QueryCounterES(GLuint id, GLenum target)
        {
            if (o_QueryCounterEXT) o_QueryCounterEXT(id, target);
        }
        void APIENTRY S_GetQueryObjectui64vES(GLuint id, GLenum pname, GLuint64* params)
        {
            if (o_GetQueryObjectui64vEXT) o_GetQueryObjectui64vEXT(id, pname, params);
            else if (params) *params = 0;
        }
        void APIENTRY S_GetQueryObjectivES(GLuint id, GLenum pname, GLint* params)
        {
            GLuint v = 0;
            glad_glGetQueryObjectuiv(id, pname, &v);
            if (params) *params = (GLint)v;
        }
        void APIENTRY S_DebugMessageCallbackNoop(GLDEBUGPROC, const void*) {}
        void APIENTRY S_DebugMessageControlNoop(GLenum, GLenum, GLenum, GLsizei, const GLuint*, GLboolean) {}

        // ══════════════════════════════════════════ Yükleme
        template<typename T>
        void Load(T& slot, void* (*getProc)(const char*), const char* name)
        {
            if (void* p = getProc(name))
                slot = (T)p;
        }

        bool HasExtension(const char* name)
        {
            GLint count = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &count);
            for (GLint i = 0; i < count; ++i)
                if (const char* e = (const char*)glad_glGetStringi(GL_EXTENSIONS, (GLuint)i); e && std::strcmp(e, name) == 0)
                    return true;
            return false;
        }

        // ES bağlamında glad sürümü "3.2" (masaüstü) sanıp 3.3+ işlevleri (glBindSampler, glTexStorage2D ...) yüklemez:
        // GL_VERSION'ı "4.6" gösteren geçici glGetString ile glad ikinci kez çalıştırılır, her işlevi adla yükler
        PFNGLGETSTRINGPROC s_RealGetString = nullptr;
        void* (*s_LoaderProc)(const char*) = nullptr;

        const GLubyte* APIENTRY FakeGetString(GLenum name)
        {
            if (name == GL_VERSION)
                return (const GLubyte*)"4.6.0 Conqueror ES loader";
            return s_RealGetString(name);
        }

        void* LoaderWithFakeVersion(const char* name)
        {
            if (std::strcmp(name, "glGetString") == 0)
                return (void*)&FakeGetString;
            return s_LoaderProc(name);
        }

        void LoadAllByName(void* (*getProc)(const char*))
        {
            s_RealGetString = (PFNGLGETSTRINGPROC)getProc("glGetString");
            s_LoaderProc = getProc;
            if (s_RealGetString)
                gladLoadGLLoader((GLADloadproc)&LoaderWithFakeVersion);
            glad_glGetString = s_RealGetString;
        }

        // Kullandığımız işlevler adla bir kez daha (güvenlik ağı)
        void LoadESFunctions(void* (*getProc)(const char*))
        {
#define CQ_LOAD(fn) Load(glad_##fn, getProc, #fn)
            CQ_LOAD(glActiveTexture); CQ_LOAD(glAttachShader); CQ_LOAD(glBeginQuery); CQ_LOAD(glBindBuffer); CQ_LOAD(glBindBufferBase);
            CQ_LOAD(glBindFramebuffer); CQ_LOAD(glBindRenderbuffer); CQ_LOAD(glBindTexture); CQ_LOAD(glBindVertexArray);
            CQ_LOAD(glBlendFunc); CQ_LOAD(glBlendFuncSeparate); CQ_LOAD(glBlitFramebuffer); CQ_LOAD(glBufferData); CQ_LOAD(glBufferSubData);
            CQ_LOAD(glCheckFramebufferStatus); CQ_LOAD(glClear); CQ_LOAD(glClearBufferfv); CQ_LOAD(glClearBufferiv); CQ_LOAD(glClearColor);
            CQ_LOAD(glColorMask); CQ_LOAD(glColorMaski); CQ_LOAD(glCompileShader); CQ_LOAD(glCreateProgram); CQ_LOAD(glCreateShader);
            CQ_LOAD(glCullFace); CQ_LOAD(glDeleteBuffers); CQ_LOAD(glDeleteFramebuffers); CQ_LOAD(glDeleteProgram); CQ_LOAD(glDeleteQueries);
            CQ_LOAD(glDeleteRenderbuffers); CQ_LOAD(glDeleteShader); CQ_LOAD(glDeleteTextures); CQ_LOAD(glDeleteVertexArrays);
            CQ_LOAD(glDepthFunc); CQ_LOAD(glDepthMask); CQ_LOAD(glDetachShader); CQ_LOAD(glDisable); CQ_LOAD(glDisableVertexAttribArray);
            CQ_LOAD(glDrawArrays); CQ_LOAD(glDrawArraysInstanced); CQ_LOAD(glDrawBuffers); CQ_LOAD(glDrawElements);
            CQ_LOAD(glDrawElementsInstanced); CQ_LOAD(glEnable); CQ_LOAD(glEnableVertexAttribArray); CQ_LOAD(glEndQuery);
            CQ_LOAD(glFramebufferRenderbuffer); CQ_LOAD(glFramebufferTexture2D); CQ_LOAD(glFramebufferTextureLayer); CQ_LOAD(glGenBuffers);
            CQ_LOAD(glGenFramebuffers); CQ_LOAD(glGenQueries); CQ_LOAD(glGenRenderbuffers); CQ_LOAD(glGenTextures); CQ_LOAD(glGenVertexArrays);
            CQ_LOAD(glGenerateMipmap); CQ_LOAD(glGetBooleanv); CQ_LOAD(glGetFloatv); CQ_LOAD(glGetFramebufferAttachmentParameteriv);
            CQ_LOAD(glGetIntegerv); CQ_LOAD(glGetProgramInfoLog); CQ_LOAD(glGetProgramiv); CQ_LOAD(glGetQueryObjectuiv);
            CQ_LOAD(glGetRenderbufferParameteriv); CQ_LOAD(glGetShaderInfoLog); CQ_LOAD(glGetShaderiv); CQ_LOAD(glGetString);
            CQ_LOAD(glGetStringi); CQ_LOAD(glGetUniformBlockIndex); CQ_LOAD(glGetUniformLocation); CQ_LOAD(glIsEnabled);
            CQ_LOAD(glLineWidth); CQ_LOAD(glLinkProgram); CQ_LOAD(glPixelStorei); CQ_LOAD(glPolygonOffset); CQ_LOAD(glReadBuffer);
            CQ_LOAD(glReadPixels); CQ_LOAD(glRenderbufferStorage); CQ_LOAD(glRenderbufferStorageMultisample); CQ_LOAD(glScissor);
            CQ_LOAD(glShaderSource); CQ_LOAD(glStencilFunc); CQ_LOAD(glStencilMask); CQ_LOAD(glStencilOp); CQ_LOAD(glTexImage2D);
            CQ_LOAD(glTexImage3D); CQ_LOAD(glTexParameterf); CQ_LOAD(glTexParameterfv); CQ_LOAD(glTexParameteri); CQ_LOAD(glTexStorage2D);
            CQ_LOAD(glTexStorage3D); CQ_LOAD(glTexSubImage2D); CQ_LOAD(glTexSubImage3D); CQ_LOAD(glUniform1f); CQ_LOAD(glUniform1fv);
            CQ_LOAD(glUniform1i); CQ_LOAD(glUniform1iv); CQ_LOAD(glUniform2f); CQ_LOAD(glUniform3f); CQ_LOAD(glUniform3fv);
            CQ_LOAD(glUniform4f); CQ_LOAD(glUniform4fv); CQ_LOAD(glUniformBlockBinding); CQ_LOAD(glUniformMatrix3fv); CQ_LOAD(glUniformMatrix4fv);
            CQ_LOAD(glUseProgram); CQ_LOAD(glVertexAttribDivisor); CQ_LOAD(glVertexAttribIPointer); CQ_LOAD(glVertexAttribPointer);
            CQ_LOAD(glViewport); CQ_LOAD(glGetTexLevelParameteriv); CQ_LOAD(glCopyTexSubImage2D);
#undef CQ_LOAD
        }
    }

    // ══════════════════════════════════════════ Genel
    void SetRequested(Kind kind) { s_Requested = kind; }
    Kind GetRequested() { return s_Requested; }

    bool Parse(const std::string& textIn, Kind& out)
    {
        std::string text;
        for (char c : textIn) text += (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        if (text == "4.6" || text == "46" || text == "gl4.6") { out = Kind::GL46; return true; }
        if (text == "4.5" || text == "45" || text == "gl4.5") { out = Kind::GL45; return true; }
        if (text == "4.1" || text == "41" || text == "gl4.1" || text == "macos") { out = Kind::GL41; return true; }
        if (text == "3.3" || text == "33" || text == "gl3.3") { out = Kind::GL33; return true; }
        if (text == "es" || text == "es3" || text == "es3.2" || text == "gles" || text == "gles3.2") { out = Kind::GLES32; return true; }
        if (text == "es3.1" || text == "gles3.1") { out = Kind::GLES31; return true; }
        if (text == "es3.0" || text == "gles3.0" || text == "webgl2") { out = Kind::GLES30; return true; }
        if (text == "auto") { out = Kind::Auto; return true; }
        return false;
    }

    const char* KindName(Kind kind)
    {
        switch (kind)
        {
            case Kind::GL46: return "OpenGL 4.6";
            case Kind::GL45: return "OpenGL 4.5";
            case Kind::GL41: return "OpenGL 4.1 core";
            case Kind::GL33: return "OpenGL 3.3 core";
            case Kind::GLES32: return "OpenGL ES 3.2";
            case Kind::GLES31: return "OpenGL ES 3.1";
            case Kind::GLES30: return "OpenGL ES 3.0 (WebGL 2 level)";
            default: return "Auto";
        }
    }

    Kind Resolve(int argc, char** argv)
    {
        Kind kind = Kind::Auto;
        for (int i = 1; i < argc && argv; ++i)
        {
            const char* a = argv[i];
            if (!a) continue;
            if (std::strncmp(a, "--gl=", 5) == 0 && Parse(a + 5, kind)) return kind;
            if (std::strcmp(a, "--gl") == 0 && i + 1 < argc && argv[i + 1] && Parse(argv[i + 1], kind)) return kind;
            if (std::strcmp(a, "--gles") == 0) return Kind::GLES32;
        }
        if (const char* env = std::getenv("CQ_GL_PROFILE"); env && Parse(env, kind))
            return kind;
#if defined(CQ_DEFAULT_GL_PROFILE)
        // Derleme seçeneği (ör. AGL / gömülü Linux görüntüsü: -DCQ_DEFAULT_GL_PROFILE=es3.2)
        if (Parse(CQ_DEFAULT_GL_PROFILE, kind))
            return kind;
#endif
#if defined(__ANDROID__) || defined(__QNX__)
        // Android / QNX: her zaman OpenGL ES (bağlam 3.2 -> 3.1 -> 3.0 sırasıyla denenir, sürüm bağlamdan okunur)
        return Kind::GLES32;
#else
        return Kind::Auto;
#endif
    }

    void ApplyWindowHints()
    {
        auto desktop = [](int major, int minor, bool core) {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
            if (core)
            {
                glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
                glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            }
        };
        auto es = [](int minor) {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
            glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
        };
        switch (s_Requested)
        {
            case Kind::GL46: desktop(4, 6, false); break;
            case Kind::GL45: desktop(4, 5, false); break;
            case Kind::GL41: desktop(4, 1, true); break;
            case Kind::GL33: desktop(3, 3, true); break;
            case Kind::GLES32: es(2); break;
            case Kind::GLES31: es(1); break;
            case Kind::GLES30: es(0); break;
            default: break;
        }
    }

    void OnContextCreated(void* (*getProc)(const char* name))
    {
        const char* version = (const char*)glad_glGetString(GL_VERSION);
        s_Info = Info{};
        s_Info.ES = version && std::strncmp(version, "OpenGL ES", 9) == 0;
        if (s_Info.ES)
        {
            LoadAllByName(getProc);
            LoadESFunctions(getProc);
        }
        GLint major = 0, minor = 0;
        glad_glGetIntegerv(GL_MAJOR_VERSION, &major);
        glad_glGetIntegerv(GL_MINOR_VERSION, &minor);
        // ES isteği bağlamdan düşükse (Mesa 3.0 isteğine 3.2 verir) istenen sürümün kısıtlarıyla çalış (WebGL 2 sınaması)
        if (s_Info.ES && major == 3)
        {
            if (s_Requested == Kind::GLES30) minor = 0;
            else if (s_Requested == Kind::GLES31) minor = std::min<GLint>(minor, 1);
        }
        s_Info.Major = major;
        s_Info.Minor = minor;
        s_Info.Description = version ? version : "?";
        // Profil isteği daha düşük bir sürümse (ör. 4.6 sürücüde --gl=3.3 compat) yine de istenen yolu sına
        int effective = major * 10 + minor;
        switch (s_Requested)
        {
            case Kind::GL45: effective = std::min(effective, 45); break;
            case Kind::GL41: effective = std::min(effective, 41); break;
            case Kind::GL33: effective = std::min(effective, 33); break;
            default: break;
        }
        s_Info.HasDSA = !s_Info.ES && effective >= 45 && glad_glCreateTextures != nullptr;
        if (s_Info.ES)
        {
            s_Info.GLSLES = true;
            s_Info.GLSLVersion = 300 + minor * 10;
            s_Info.UniformBindingLayouts = minor >= 1;
            s_Info.UniformInitializers = false;
            s_Info.MultisampleTextures = minor >= 1;
            s_Info.PolygonMode = false;
        }
        else
        {
            s_Info.GLSLVersion = effective >= 45 ? 450 : effective >= 43 ? 430 : effective >= 42 ? 420 : effective >= 41 ? 410 : 330;
            s_Info.UniformBindingLayouts = effective >= 42;
            if (!s_Info.UniformBindingLayouts && HasExtension("GL_ARB_shading_language_420pack"))
            {
                s_Info.UniformBindingLayouts = true;
                s_Info.Use420Pack = true;
            }
        }

        if (!s_Info.HasDSA)
        {
            o_BindTexture = glad_glBindTexture;
            o_TexImage2D = glad_glTexImage2D;
            o_TexSubImage2D = glad_glTexSubImage2D;
            o_Enable = glad_glEnable;
            o_Disable = glad_glDisable;
            o_IsEnabled = glad_glIsEnabled;
            o_TexStorage2D = glad_glTexStorage2D;
            if (!o_TexStorage2D) Load(o_TexStorage2D, getProc, "glTexStorage2D");
            o_TexParameteri = glad_glTexParameteri;
            if (s_Info.ES)
            {
                s_FloatLinear = HasExtension("GL_OES_texture_float_linear");
                glad_glTexParameteri = &W_TexParameteri;
            }
            Load(o_TexStorage2DMultisample, getProc, "glTexStorage2DMultisample");
            glad_glBindTexture = &W_BindTexture;
            glad_glTexImage2D = &W_TexImage2D;
            glad_glTexSubImage2D = &W_TexSubImage2D;
            glad_glTexStorage2D = &W_TexStorage2D;
            glad_glEnable = &W_Enable;
            glad_glDisable = &W_Disable;
            glad_glIsEnabled = &W_IsEnabled;

            glad_glCreateTextures = &S_CreateTextures;
            glad_glTextureStorage2D = &S_TextureStorage2D;
            glad_glTextureSubImage2D = &S_TextureSubImage2D;
            glad_glTextureParameteri = &S_TextureParameteri;
            glad_glTextureParameterf = &S_TextureParameterf;
            glad_glTextureParameterfv = &S_TextureParameterfv;
            glad_glBindTextureUnit = &S_BindTextureUnit;
            glad_glGenerateTextureMipmap = &S_GenerateTextureMipmap;
            glad_glGetTextureImage = &S_GetTextureImage;
            glad_glGetTextureSubImage = &S_GetTextureSubImage;
            glad_glGetTextureLevelParameteriv = &S_GetTextureLevelParameteriv;
            glad_glCreateBuffers = &S_CreateBuffers;
            glad_glNamedBufferData = &S_NamedBufferData;
            glad_glNamedBufferSubData = &S_NamedBufferSubData;
            glad_glCreateVertexArrays = &S_CreateVertexArrays;
            glad_glVertexArrayVertexBuffer = &S_VertexArrayVertexBuffer;
            glad_glVertexArrayAttribFormat = &S_VertexArrayAttribFormat;
            glad_glVertexArrayAttribIFormat = &S_VertexArrayAttribIFormat;
            glad_glVertexArrayAttribBinding = &S_VertexArrayAttribBinding;
            glad_glVertexArrayBindingDivisor = &S_VertexArrayBindingDivisor;
            glad_glEnableVertexArrayAttrib = &S_EnableVertexArrayAttrib;
            glad_glDisableVertexArrayAttrib = &S_DisableVertexArrayAttrib;
            glad_glVertexArrayElementBuffer = &S_VertexArrayElementBuffer;
            glad_glCreateFramebuffers = &S_CreateFramebuffers;
            glad_glNamedFramebufferTexture = &S_NamedFramebufferTexture;
            glad_glNamedFramebufferTextureLayer = &S_NamedFramebufferTextureLayer;
            glad_glNamedFramebufferRenderbuffer = &S_NamedFramebufferRenderbuffer;
            glad_glNamedFramebufferDrawBuffers = &S_NamedFramebufferDrawBuffers;
            glad_glNamedFramebufferDrawBuffer = &S_NamedFramebufferDrawBuffer;
            glad_glNamedFramebufferReadBuffer = &S_NamedFramebufferReadBuffer;
            glad_glBlitNamedFramebuffer = &S_BlitNamedFramebuffer;
            glad_glGetNamedFramebufferAttachmentParameteriv = &S_GetNamedFramebufferAttachmentParameteriv;
            glad_glCheckNamedFramebufferStatus = &S_CheckNamedFramebufferStatus;
            glad_glCreateRenderbuffers = &S_CreateRenderbuffers;
            glad_glNamedRenderbufferStorage = &S_NamedRenderbufferStorage;
            glad_glGetNamedRenderbufferParameteriv = &S_GetNamedRenderbufferParameteriv;
            if (effective < 44 || s_Info.ES)
                glad_glClearTexImage = &S_ClearTexImage;
        }
        if (s_Info.ES)
        {
            o_DrawBuffers = glad_glDrawBuffers;
            if (o_DrawBuffers)
                glad_glDrawBuffers = &W_DrawBuffersES;
            glad_glMultiDrawElements = &S_MultiDrawElements;
            glad_glPolygonMode = &S_PolygonModeES;
            glad_glDrawBuffer = &S_DrawBufferES;
            glad_glGetTexImage = &S_GetTexImageES;
            glad_glTexImage2DMultisample = &S_TexImage2DMultisampleES;
            Load(o_QueryCounterEXT, getProc, "glQueryCounterEXT");
            Load(o_GetQueryObjectui64vEXT, getProc, "glGetQueryObjectui64vEXT");
            s_Info.TimerQueries = o_QueryCounterEXT != nullptr;
            glad_glQueryCounter = &S_QueryCounterES;
            glad_glGetQueryObjectui64v = &S_GetQueryObjectui64vES;
            glad_glGetQueryObjectiv = &S_GetQueryObjectivES;
            if (minor < 2)
            {
                glad_glDebugMessageCallback = &S_DebugMessageCallbackNoop;
                glad_glDebugMessageControl = &S_DebugMessageControlNoop;
            }
            else
            {
                Load(glad_glDebugMessageCallback, getProc, "glDebugMessageCallback");
                Load(glad_glDebugMessageControl, getProc, "glDebugMessageControl");
            }
        }
        else if (!glad_glMultiDrawElements)
            glad_glMultiDrawElements = &S_MultiDrawElements;
        if (!glad_glDebugMessageCallback) glad_glDebugMessageCallback = &S_DebugMessageCallbackNoop;
        if (!glad_glDebugMessageControl) glad_glDebugMessageControl = &S_DebugMessageControlNoop;

        CQ_CORE_INFO("OpenGL profile: {0} (requested {1}), GLSL target {2}{3}{4}", s_Info.Description, KindName(s_Requested), s_Info.GLSLVersion,
                     s_Info.GLSLES ? " es" : "", s_Info.HasDSA ? "" : ", DSA emulated");
    }

    const Info& Current() { return s_Info; }

    const char* ImGuiGLSLVersion()
    {
        if (s_Info.ES) return "#version 300 es";
        if (s_Info.GLSLVersion < 410) return "#version 330 core";
        return "#version 410";
    }
}
