// GL ön yüzü: GL durum makinesi, nesne adları, uniform yazımı ve 124 GL giriş noktası. Arka uçtan bağımsızdır;
// her çeviri katmanı (VkGL: Vulkan, DxGL: Direct3D 11) bunu kendi ad alanında içerir (#include) ve aynı adlı
// türleri (Texture, Buffer, Program, Context...) ve kancaları sağlar:
//   AllocateTexture, UploadTexture, ReadImage, DestroyTextureImage, GrowLevels, GenerateMipmaps, ClearTexture,
//   BlitFramebuffer, ClearCurrent, Draw, LinkProgram, DestroyProgram, FindUniform, EndRendering, FlushAndWaitGL,
//   CurrentVAO, AttachmentTexture, Get*, Live, GetLimits, ClampSamples, ReleaseBuffer, WriteTimestamp, ReadTimestamp,
//   BeginOcclusion, EndOcclusion, ReadOcclusion (QueryObject'te Occlusion / Any alanları).
// Gerekli başlıklar (glad, <algorithm>, <array>, <cstring>, <string>, <utility>, Log.h) içeren dosyadadır.

    namespace
    {
        GLuint g_ActiveOcclusion = 0;   // etkin örtüşme sorgusu (BeginQuery)

        uint32_t NewName() { return Ctx().NextName++; }

        bool IsCubeFace(GLenum target) { return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z; }

        Texture* EnsureTexture(uint32_t name)
        {
            if (!name)
                return nullptr;
            auto& c = Ctx();
            auto& slot = c.Textures[name];
            if (!slot)
                slot = std::make_unique<Texture>();
            return slot.get();
        }

        // Etkin birime bağlı doku (küp yüzü hedefleri katman verir)
        Texture* BoundTexture(GLenum target, int* layer = nullptr)
        {
            auto& c = Ctx();
            const uint32_t unit = std::min<uint32_t>(c.S.ActiveTexture, 63);
            if (layer)
                *layer = 0;
            if (IsCubeFace(target))
            {
                if (layer)
                    *layer = (int)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
                return GetTexture(c.S.TexCube[unit]);
            }
            switch (target)
            {
                case GL_TEXTURE_CUBE_MAP: return GetTexture(c.S.TexCube[unit]);
                case GL_TEXTURE_2D_MULTISAMPLE: return GetTexture(c.S.Tex2DMS[unit]);
                default: return GetTexture(c.S.Tex2D[unit]);
            }
        }

        uint32_t* BufferBindingPoint(GLenum target)
        {
            auto& c = Ctx();
            switch (target)
            {
                case GL_ARRAY_BUFFER: return &c.S.ArrayBuffer;
                case GL_ELEMENT_ARRAY_BUFFER: return &CurrentVAO()->ElementBuffer;
                case GL_UNIFORM_BUFFER: return &c.S.UniformBuffer;
                case GL_PIXEL_UNPACK_BUFFER: return &c.S.PixelUnpackBuffer;
                default: return nullptr;
            }
        }

        Buffer* BoundBuffer(GLenum target)
        {
            uint32_t* p = BufferBindingPoint(target);
            return p ? GetBuffer(*p) : nullptr;
        }

        Buffer* EnsureBuffer(uint32_t name)
        {
            if (!name)
                return nullptr;
            auto& slot = Ctx().Buffers[name];
            if (!slot)
                slot = std::make_unique<Buffer>();
            return slot.get();
        }

        void WriteBuffer(Buffer& b, size_t offset, size_t size, const void* data)
        {
            if (!data || size == 0 || offset >= b.Shadow.size())
                return;
            size = std::min(size, b.Shadow.size() - offset);
            std::memcpy(b.Shadow.data() + offset, data, size);
            b.HighWater = std::max(b.HighWater, offset + size);
            b.Dirty = true;
        }

        uint32_t* FramebufferBindingFor(GLenum target)
        {
            auto& c = Ctx();
            return target == GL_READ_FRAMEBUFFER ? &c.S.ReadFBO : &c.S.DrawFBO;
        }

        Framebuffer* TargetFramebuffer(GLenum target)
        {
            const uint32_t name = *FramebufferBindingFor(target);
            if (name == 0)
                return nullptr;   // varsayılan çerçeve arabelleğinin ekleri değiştirilemez
            return GetFramebuffer(name);
        }

        // Paketten çözme kaynağı: PIXEL_UNPACK_BUFFER bağlıysa işaretçi arabellek içi uzaklıktır
        const void* UnpackSource(const void* pixels)
        {
            auto& c = Ctx();
            if (!c.S.PixelUnpackBuffer)
                return pixels;
            Buffer* b = GetBuffer(c.S.PixelUnpackBuffer);
            const size_t off = (size_t)(uintptr_t)pixels;
            if (!b || off >= b->Shadow.size())
                return nullptr;
            return b->Shadow.data() + off;
        }

        bool IsDepthInternalFormat(GLenum f)
        {
            switch (f)
            {
                case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT16: case GL_DEPTH_COMPONENT24: case GL_DEPTH_COMPONENT32:
                case GL_DEPTH_COMPONENT32F: case GL_DEPTH_STENCIL: case GL_DEPTH24_STENCIL8: case GL_DEPTH32F_STENCIL8:
                    return true;
                default:
                    return false;
            }
        }

        void SetTexParam(Texture& t, GLenum pname, GLint i, float f)
        {
            switch (pname)
            {
                case GL_TEXTURE_MIN_FILTER: t.MinFilter = (GLenum)i; break;
                case GL_TEXTURE_MAG_FILTER: t.MagFilter = (GLenum)i; break;
                case GL_TEXTURE_WRAP_S: t.WrapS = (GLenum)i; break;
                case GL_TEXTURE_WRAP_T: t.WrapT = (GLenum)i; break;
                case GL_TEXTURE_WRAP_R: t.WrapR = (GLenum)i; break;
                case GL_TEXTURE_COMPARE_MODE: t.CompareMode = (GLenum)i; break;
                case GL_TEXTURE_COMPARE_FUNC: t.CompareFunc = (GLenum)i; break;
                case GL_TEXTURE_BASE_LEVEL: t.BaseLevel = i; break;
                case GL_TEXTURE_MAX_LEVEL: t.MaxLevel = i; break;
                case GL_TEXTURE_MAX_ANISOTROPY: t.MaxAnisotropy = f; break;
                case GL_TEXTURE_LOD_BIAS: t.LodBias = f; break;
                case GL_TEXTURE_MIN_LOD: t.MinLod = f; break;
                case GL_TEXTURE_MAX_LOD: t.MaxLod = f; break;
                default: break;
            }
        }

        // ── Değişken doku tanımı (glTexImage2D) ──
        void DefineLevel(Texture& t, GLenum texTarget, int level, GLenum internalFormat, uint32_t w, uint32_t h)
        {
            const bool fits = t.Image && t.InternalFormat == internalFormat && t.Target == texTarget && level < (int)t.Levels &&
                              std::max(1u, t.Width >> level) == w && std::max(1u, t.Height >> level) == h;
            if (fits)
                return;
            if (level == 0)
            {
                const bool mip = t.MinFilter != GL_NEAREST && t.MinFilter != GL_LINEAR;
                AllocateTexture(t, texTarget, internalFormat, w, h, mip ? 32u : 1u, 1);
                t.Immutable = false;
                return;
            }
            if (t.Image && t.InternalFormat == internalFormat && t.Target == texTarget && std::max(1u, t.Width >> level) == w &&
                std::max(1u, t.Height >> level) == h)
            {
                GrowLevels(t, (uint32_t)level + 1);
                return;
            }
            if (!t.Image)
            {
                AllocateTexture(t, texTarget, internalFormat, w << level, h << level, (uint32_t)level + 1, 1);
                t.Immutable = false;
                return;
            }
            CQ_CORE_WARN("[VkGL] Mip level {0} ({1}x{2}) does not match the base level ({3}x{4})", level, w, h, t.Width, t.Height);
        }

        // ── Uniform yazımı ──
        Program* CurrentProgram()
        {
            Program* p = GetProgram(Ctx().S.Program);
            return p && p->Linked ? p : nullptr;
        }

        void PutBytes(Program& p, uint32_t offset, const void* data, size_t bytes)
        {
            if ((size_t)offset + bytes > p.Globals.size())
                return;
            uint8_t* dst = p.Globals.data() + offset;
            if (std::memcmp(dst, data, bytes) != 0)
            {
                std::memcpy(dst, data, bytes);
                p.GlobalsDirty = true;
            }
        }

        // Skaler/vektör: dizi elemanları ArrayStride ile ilerler
        template <typename T>
        void UniformVec(GLint location, GLsizei count, int components, const T* values)
        {
            Program* p = CurrentProgram();
            if (!p || !values || count <= 0)
                return;
            UniformSlot* s = FindUniform(*p, location);
            if (!s)
                return;
            if (s->Sampler >= 0)
            {
                // Örnekleyici: değer doku birimidir
                auto& units = p->Samplers[(size_t)s->Sampler].Units;
                for (GLsizei k = 0; k < count; ++k)
                {
                    const size_t e = (size_t)s->SamplerElement + (size_t)k;
                    if (e >= units.size())
                        break;
                    units[e] = (int)values[k * components];
                }
                return;
            }
            const GLsizei n = std::min<GLsizei>(count, std::max(1, s->ArrayRemaining));
            const uint32_t stride = s->ArrayStride ? s->ArrayStride : 16;
            for (GLsizei k = 0; k < n; ++k)
                PutBytes(*p, s->Offset + (uint32_t)k * stride, values + (size_t)k * components, sizeof(T) * (size_t)components);
        }

        void UniformMatrix(GLint location, GLsizei count, GLboolean transpose, const GLfloat* v, int dim)
        {
            Program* p = CurrentProgram();
            if (!p || !v || count <= 0)
                return;
            UniformSlot* s = FindUniform(*p, location);
            if (!s || s->Sampler >= 0)
                return;
            const GLsizei n = std::min<GLsizei>(count, std::max(1, s->ArrayRemaining));
            const uint32_t elemStride = s->ArrayStride ? s->ArrayStride : s->MatrixStride * (uint32_t)dim;
            for (GLsizei k = 0; k < n; ++k)
            {
                const GLfloat* m = v + (size_t)k * dim * dim;
                for (int col = 0; col < dim; ++col)
                {
                    float column[4];
                    for (int row = 0; row < dim; ++row)
                        column[row] = transpose ? m[row * dim + col] : m[col * dim + row];
                    PutBytes(*p, s->Offset + (uint32_t)k * elemStride + (uint32_t)col * s->MatrixStride, column, sizeof(float) * (size_t)dim);
                }
            }
        }

        // ── Saplamalar: uygulanmamış işlevler adlarını bir kez bildirir ──
        constexpr size_t kMaxStubs = 2048;
        const char* g_StubNames[kMaxStubs] = {};
        bool g_StubReported[kMaxStubs] = {};
        size_t g_StubCount = 0;

        void ReportStub(size_t index)
        {
            if (index < kMaxStubs && !g_StubReported[index])
            {
                g_StubReported[index] = true;
                CQ_CORE_WARN("[VkGL] Unimplemented GL function called: {0}", g_StubNames[index] ? g_StubNames[index] : "?");
            }
        }

        template <size_t N>
        void APIENTRY StubFn() { ReportStub(N); }

        template <size_t... I>
        constexpr std::array<void (APIENTRY*)(), sizeof...(I)> MakeStubs(std::index_sequence<I...>) { return { { &StubFn<I>... } }; }

        const std::array<void (APIENTRY*)(), kMaxStubs> g_Stubs = MakeStubs(std::make_index_sequence<kMaxStubs>{});
    }

    // ════════════════════════════════════════════════════════════════════════════════════════════
    //  GL işlevleri
    // ════════════════════════════════════════════════════════════════════════════════════════════
    namespace Entry
    {
        // ── Hata / bilgi ──
        GLenum APIENTRY GetError() { return GL_NO_ERROR; }

        const GLubyte* APIENTRY GetString(GLenum name)
        {
            auto& c = Ctx();
            static std::string s_Extensions;
            switch (name)
            {
                case GL_VENDOR: return (const GLubyte*)c.Vendor.c_str();
                case GL_RENDERER: return (const GLubyte*)c.Renderer.c_str();
                case GL_VERSION: return (const GLubyte*)c.Version.c_str();
                case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte*)"4.60";
                case GL_EXTENSIONS:
                    s_Extensions.clear();
                    for (const auto& e : c.Extensions)
                        s_Extensions += e + " ";
                    return (const GLubyte*)s_Extensions.c_str();
                default: return nullptr;
            }
        }

        const GLubyte* APIENTRY GetStringi(GLenum name, GLuint index)
        {
            auto& c = Ctx();
            if (name == GL_EXTENSIONS && index < c.Extensions.size())
                return (const GLubyte*)c.Extensions[index].c_str();
            return nullptr;
        }

        void APIENTRY DebugMessageCallback(GLDEBUGPROC callback, const void* userParam)
        {
            Ctx().DebugCallback = callback;
            Ctx().DebugUserParam = userParam;
        }

        void APIENTRY DebugMessageControl(GLenum, GLenum, GLenum, GLsizei, const GLuint*, GLboolean) {}

        // ── Etkinleştirmeler ──
        bool* CapFlag(GLenum cap)
        {
            auto& s = Ctx().S;
            switch (cap)
            {
                case GL_BLEND: return &s.Blend;
                case GL_DEPTH_TEST: return &s.DepthTest;
                case GL_CULL_FACE: return &s.CullFace;
                case GL_SCISSOR_TEST: return &s.ScissorTest;
                case GL_STENCIL_TEST: return &s.StencilTest;
                case GL_POLYGON_OFFSET_FILL: return &s.PolygonOffsetFill;
                case GL_PRIMITIVE_RESTART: case GL_PRIMITIVE_RESTART_FIXED_INDEX: return &s.PrimitiveRestart;
                default: return nullptr;   // GL_MULTISAMPLE, GL_LINE_SMOOTH, GL_DEBUG_OUTPUT, küp dikişsiz (Vulkan'da hep açık)
            }
        }

        void APIENTRY Enable(GLenum cap) { if (bool* f = CapFlag(cap)) *f = true; }
        void APIENTRY Disable(GLenum cap) { if (bool* f = CapFlag(cap)) *f = false; }

        GLboolean APIENTRY IsEnabled(GLenum cap)
        {
            if (bool* f = CapFlag(cap))
                return *f ? GL_TRUE : GL_FALSE;
            return cap == GL_MULTISAMPLE || cap == GL_TEXTURE_CUBE_MAP_SEAMLESS ? GL_TRUE : GL_FALSE;
        }

        // ── Sabit işlev durumu ──
        void APIENTRY BlendFunc(GLenum s, GLenum d)
        {
            auto& st = Ctx().S;
            st.BlendSrcRGB = st.BlendSrcA = s;
            st.BlendDstRGB = st.BlendDstA = d;
        }

        void APIENTRY BlendFuncSeparate(GLenum sRGB, GLenum dRGB, GLenum sA, GLenum dA)
        {
            auto& st = Ctx().S;
            st.BlendSrcRGB = sRGB;
            st.BlendDstRGB = dRGB;
            st.BlendSrcA = sA;
            st.BlendDstA = dA;
        }

        void APIENTRY BlendEquation(GLenum mode) { Ctx().S.BlendEqRGB = Ctx().S.BlendEqA = mode; }
        void APIENTRY BlendEquationSeparate(GLenum rgb, GLenum a) { Ctx().S.BlendEqRGB = rgb; Ctx().S.BlendEqA = a; }
        void APIENTRY DepthFunc(GLenum f) { Ctx().S.DepthFunc = f; }
        void APIENTRY DepthMask(GLboolean f) { Ctx().S.DepthMask = f != GL_FALSE; }
        void APIENTRY CullFace(GLenum m) { Ctx().S.CullMode = m; }
        void APIENTRY PolygonMode(GLenum, GLenum mode) { Ctx().S.PolygonMode = mode; }
        void APIENTRY PolygonOffset(GLfloat factor, GLfloat units) { Ctx().S.PolygonOffsetFactor = factor; Ctx().S.PolygonOffsetUnits = units; }
        void APIENTRY LineWidth(GLfloat w) { Ctx().S.LineWidth = std::max(w, 0.0f); }

        void APIENTRY ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
        {
            auto& m = Ctx().S.ColorMask;
            m[0] = r != GL_FALSE;
            m[1] = g != GL_FALSE;
            m[2] = b != GL_FALSE;
            m[3] = a != GL_FALSE;
            const uint8_t bits = (uint8_t)((r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0));
            for (auto& x : Ctx().S.ColorMaskBits)
                x = bits;
        }

        void APIENTRY StencilFunc(GLenum func, GLint ref, GLuint mask)
        {
            auto& s = Ctx().S;
            s.StencilFunc = func;
            s.StencilRef = ref;
            s.StencilValueMask = mask;
        }

        void APIENTRY StencilMask(GLuint mask) { Ctx().S.StencilWriteMask = mask; }

        void APIENTRY StencilOp(GLenum fail, GLenum zfail, GLenum zpass)
        {
            auto& s = Ctx().S;
            s.StencilFail = fail;
            s.StencilZFail = zfail;
            s.StencilZPass = zpass;
        }

        void APIENTRY Viewport(GLint x, GLint y, GLsizei w, GLsizei h)
        {
            auto& v = Ctx().S.Viewport;
            v[0] = x; v[1] = y; v[2] = w; v[3] = h;
        }

        void APIENTRY Scissor(GLint x, GLint y, GLsizei w, GLsizei h)
        {
            auto& v = Ctx().S.Scissor;
            v[0] = x; v[1] = y; v[2] = w; v[3] = h;
        }

        void APIENTRY ClipControl(GLenum origin, GLenum) { Ctx().S.ClipOrigin = origin; }

        void APIENTRY PixelStorei(GLenum pname, GLint param)
        {
            auto& s = Ctx().S;
            switch (pname)
            {
                case GL_UNPACK_ALIGNMENT: s.UnpackAlignment = std::max(1, param); break;
                case GL_PACK_ALIGNMENT: s.PackAlignment = std::max(1, param); break;
                case GL_UNPACK_ROW_LENGTH: s.UnpackRowLength = std::max(0, param); break;
                default: break;
            }
        }

        // ── Temizleme ──
        void APIENTRY ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
        {
            auto& cc = Ctx().S.ClearColor;
            cc[0] = r; cc[1] = g; cc[2] = b; cc[3] = a;
        }

        void APIENTRY Clear(GLbitfield mask)
        {
            if (Live())
                ClearCurrent(mask);
        }

        // ── Dokular ──
        void APIENTRY GenTextures(GLsizei n, GLuint* out)
        {
            for (GLsizei i = 0; i < n; ++i)
            {
                out[i] = NewName();
                EnsureTexture(out[i]);
            }
        }

        void APIENTRY CreateTextures(GLenum target, GLsizei n, GLuint* out)
        {
            for (GLsizei i = 0; i < n; ++i)
            {
                out[i] = NewName();
                EnsureTexture(out[i])->Target = target;
            }
        }

        void APIENTRY DeleteTextures(GLsizei n, const GLuint* names)
        {
            auto& c = Ctx();
            for (GLsizei i = 0; i < n; ++i)
            {
                const uint32_t name = names[i];
                auto it = c.Textures.find(name);
                if (it == c.Textures.end())
                    continue;
                for (size_t u = 0; u < 64; ++u)
                {
                    if (c.S.Tex2D[u] == name) c.S.Tex2D[u] = 0;
                    if (c.S.TexCube[u] == name) c.S.TexCube[u] = 0;
                    if (c.S.Tex2DMS[u] == name) c.S.Tex2DMS[u] = 0;
                }
                if (Live())
                    DestroyTextureImage(*it->second);
                c.Textures.erase(it);
            }
        }

        void APIENTRY ActiveTexture(GLenum unit) { Ctx().S.ActiveTexture = std::min<uint32_t>(unit - GL_TEXTURE0, 63); }

        void BindToUnit(uint32_t unit, GLenum target, uint32_t name)
        {
            auto& c = Ctx();
            unit = std::min<uint32_t>(unit, 63);
            Texture* t = EnsureTexture(name);
            if (t && t->Target == 0)
                t->Target = target;
            switch (target)
            {
                case GL_TEXTURE_CUBE_MAP: c.S.TexCube[unit] = name; break;
                case GL_TEXTURE_2D_MULTISAMPLE: c.S.Tex2DMS[unit] = name; break;
                default: c.S.Tex2D[unit] = name; break;
            }
        }

        void APIENTRY BindTexture(GLenum target, GLuint name) { BindToUnit(Ctx().S.ActiveTexture, target, name); }

        void APIENTRY BindTextureUnit(GLuint unit, GLuint name)
        {
            auto& c = Ctx();
            if (!name)
            {
                const uint32_t u = std::min<uint32_t>(unit, 63);
                c.S.Tex2D[u] = c.S.TexCube[u] = c.S.Tex2DMS[u] = 0;
                return;
            }
            Texture* t = EnsureTexture(name);
            BindToUnit(unit, t->Target ? t->Target : GL_TEXTURE_2D, name);
        }

        void APIENTRY BindSampler(GLuint unit, GLuint sampler) { Ctx().S.Samplers[std::min<uint32_t>(unit, 63)] = sampler; }

        void APIENTRY TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint, GLenum format, GLenum type,
                                 const void* pixels)
        {
            if (!Live())
                return;
            int layer = 0;
            Texture* t = BoundTexture(target, &layer);
            if (!t || level < 0)
                return;
            const GLenum texTarget = IsCubeFace(target) ? GL_TEXTURE_CUBE_MAP : target;
            const uint32_t w = (uint32_t)std::max(1, width), h = (uint32_t)std::max(1, height);
            DefineLevel(*t, texTarget, level, (GLenum)internalformat, w, h);
            if (!t->Image || level >= (int)t->Levels)
                return;
            if (const void* src = UnpackSource(pixels))
                UploadTexture(*t, level, layer, 0, 0, (int)w, (int)h, format, type, src);
            t->DefinedLevels |= 1u << level;
        }

        void APIENTRY TexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean)
        {
            if (!Live())
                return;
            Texture* t = BoundTexture(target);
            if (!t)
                return;
            const uint32_t s = ClampSamples(samples, IsDepthInternalFormat(internalformat));
            AllocateTexture(*t, GL_TEXTURE_2D_MULTISAMPLE, internalformat, (uint32_t)std::max(1, width), (uint32_t)std::max(1, height), 1, s);
            t->Immutable = false;
            t->DefinedLevels = 1;
        }

        void StorageCommon(Texture* t, GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
        {
            if (!t || !Live())
                return;
            if (target == 0)
                target = t->Target ? t->Target : GL_TEXTURE_2D;
            AllocateTexture(*t, target, internalformat, (uint32_t)std::max(1, width), (uint32_t)std::max(1, height), (uint32_t)std::max(1, levels), 1);
            t->Immutable = true;
            t->DefinedLevels = t->Levels >= 32 ? ~0u : ((1u << t->Levels) - 1);
        }

        void APIENTRY TexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
        {
            StorageCommon(BoundTexture(target), target, levels, internalformat, width, height);
        }

        void APIENTRY TextureStorage2D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
        {
            Texture* t = GetTexture(texture);
            StorageCommon(t, t ? t->Target : 0, levels, internalformat, width, height);
        }

        void APIENTRY TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* pixels)
        {
            if (!Live())
                return;
            int layer = 0;
            Texture* t = BoundTexture(target, &layer);
            if (!t || !t->Image || level < 0 || level >= (int)t->Levels)
                return;
            if (const void* src = UnpackSource(pixels))
                UploadTexture(*t, level, layer, x, y, w, h, format, type, src);
        }

        void APIENTRY TextureSubImage2D(GLuint texture, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* pixels)
        {
            if (!Live())
                return;
            Texture* t = GetTexture(texture);
            if (!t || !t->Image || level < 0 || level >= (int)t->Levels)
                return;
            if (const void* src = UnpackSource(pixels))
                UploadTexture(*t, level, 0, x, y, w, h, format, type, src);
        }

        void APIENTRY TexParameteri(GLenum target, GLenum pname, GLint param)
        {
            if (Texture* t = BoundTexture(target))
                SetTexParam(*t, pname, param, (float)param);
        }

        void APIENTRY TextureParameteri(GLuint texture, GLenum pname, GLint param)
        {
            if (Texture* t = GetTexture(texture))
                SetTexParam(*t, pname, param, (float)param);
        }

        void APIENTRY TextureParameterf(GLuint texture, GLenum pname, GLfloat param)
        {
            if (Texture* t = GetTexture(texture))
                SetTexParam(*t, pname, (GLint)param, param);
        }

        void APIENTRY GenerateMipmap(GLenum target)
        {
            if (!Live())
                return;
            if (Texture* t = BoundTexture(target))
                GenerateMipmaps(*t);
        }

        void APIENTRY GenerateTextureMipmap(GLuint texture)
        {
            if (!Live())
                return;
            if (Texture* t = GetTexture(texture))
                GenerateMipmaps(*t);
        }

        void APIENTRY ClearTexImage(GLuint texture, GLint level, GLenum format, GLenum type, const void* data)
        {
            if (!Live())
                return;
            if (Texture* t = GetTexture(texture))
                ClearTexture(*t, level, format, type, data);
        }

        void APIENTRY GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void* pixels)
        {
            if (!Live())
                return;
            int layer = 0;
            Texture* t = BoundTexture(target, &layer);
            if (!t || !t->Image || level < 0 || level >= (int)t->Levels)
                return;
            const int w = (int)std::max(1u, t->Width >> level), h = (int)std::max(1u, t->Height >> level);
            ReadImage(*t, level, layer, 0, 0, w, h, format, type, pixels, Ctx().S.PackAlignment);
        }

        void APIENTRY GetTextureSubImage(GLuint texture, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type,
                                         GLsizei bufSize, void* pixels)
        {
            (void)bufSize;
            if (!Live() || !pixels || w <= 0 || h <= 0 || d <= 0)
                return;
            Texture* t = GetTexture(texture);
            if (!t || !t->Image || level < 0 || level >= (int)t->Levels)
                return;
            const int tw = (int)std::max(1u, t->Width >> level), th = (int)std::max(1u, t->Height >> level);
            if (x < 0 || y < 0 || x + w > tw || y + h > th || z < 0 || z + d > (int)t->Layers)
                return;   // GL_INVALID_VALUE
            const int channels = (format == GL_RED || format == GL_RED_INTEGER || format == GL_DEPTH_COMPONENT) ? 1 :
                                 (format == GL_RG || format == GL_RG_INTEGER) ? 2 : (format == GL_RGB || format == GL_BGR) ? 3 : 4;
            const int comp = (type == GL_UNSIGNED_BYTE || type == GL_BYTE) ? 1 : (type == GL_UNSIGNED_SHORT || type == GL_SHORT || type == GL_HALF_FLOAT) ? 2 : 4;
            const int align = std::max(1, (int)Ctx().S.PackAlignment);
            const size_t layerBytes = ((size_t)w * channels * comp + align - 1) / align * align * (size_t)h;
            for (int layer = 0; layer < d; ++layer)
                ReadImage(*t, level, z + layer, x, y, w, h, format, type, static_cast<uint8_t*>(pixels) + layerBytes * (size_t)layer, Ctx().S.PackAlignment);
        }

        void APIENTRY GetTextureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, void* pixels)
        {
            if (!Live())
                return;
            Texture* t = GetTexture(texture);
            if (!t || !t->Image || level < 0 || level >= (int)t->Levels || !pixels)
                return;
            const int w = (int)std::max(1u, t->Width >> level), h = (int)std::max(1u, t->Height >> level);
            // Bir katmanın bayt boyutu (paket hizası dahil)
            int channels = 4;
            switch (format)
            {
                case GL_RED: case GL_RED_INTEGER: case GL_DEPTH_COMPONENT: channels = 1; break;
                case GL_RG: case GL_RG_INTEGER: channels = 2; break;
                case GL_RGB: case GL_BGR: channels = 3; break;
                default: break;
            }
            const int comp = (type == GL_UNSIGNED_BYTE || type == GL_BYTE) ? 1 : (type == GL_UNSIGNED_SHORT || type == GL_SHORT || type == GL_HALF_FLOAT) ? 2 : 4;
            const int align = std::max(1, Ctx().S.PackAlignment);
            const size_t pitch = ((size_t)w * channels * comp + align - 1) / align * align;
            const size_t layerBytes = pitch * (size_t)h;
            for (uint32_t l = 0; l < t->Layers; ++l)
            {
                if ((size_t)bufSize < layerBytes * (l + 1))
                    break;
                ReadImage(*t, level, (int)l, 0, 0, w, h, format, type, static_cast<uint8_t*>(pixels) + layerBytes * l, align);
            }
        }

        // ── Renderbuffer ──
        void APIENTRY GenRenderbuffers(GLsizei n, GLuint* out)
        {
            auto& c = Ctx();
            for (GLsizei i = 0; i < n; ++i)
            {
                out[i] = NewName();
                c.Renderbuffers[out[i]] = std::make_unique<Texture>();
            }
        }

        void APIENTRY DeleteRenderbuffers(GLsizei n, const GLuint* names)
        {
            auto& c = Ctx();
            for (GLsizei i = 0; i < n; ++i)
            {
                auto it = c.Renderbuffers.find(names[i]);
                if (it == c.Renderbuffers.end())
                    continue;
                if (c.S.Renderbuffer == names[i])
                    c.S.Renderbuffer = 0;
                if (Live())
                    DestroyTextureImage(*it->second);
                c.Renderbuffers.erase(it);
            }
        }

        void APIENTRY BindRenderbuffer(GLenum, GLuint name)
        {
            auto& c = Ctx();
            c.S.Renderbuffer = name;
            if (name && !c.Renderbuffers.count(name))
                c.Renderbuffers[name] = std::make_unique<Texture>();
        }

        void APIENTRY RenderbufferStorage(GLenum, GLenum internalformat, GLsizei width, GLsizei height)
        {
            if (!Live())
                return;
            Texture* rb = GetRenderbuffer(Ctx().S.Renderbuffer);
            if (!rb)
                return;
            AllocateTexture(*rb, GL_RENDERBUFFER, internalformat, (uint32_t)std::max(1, width), (uint32_t)std::max(1, height), 1, 1);
            rb->Immutable = true;
            rb->DefinedLevels = 1;
        }

        // ── Çerçeve arabellekleri ──
        void APIENTRY GenFramebuffers(GLsizei n, GLuint* out)
        {
            auto& c = Ctx();
            for (GLsizei i = 0; i < n; ++i)
            {
                out[i] = NewName();
                c.Framebuffers[out[i]] = std::make_unique<Framebuffer>();
            }
        }

        void APIENTRY CreateFramebuffers(GLsizei n, GLuint* out) { GenFramebuffers(n, out); }

        void APIENTRY DeleteFramebuffers(GLsizei n, const GLuint* names)
        {
            auto& c = Ctx();
            for (GLsizei i = 0; i < n; ++i)
            {
                const uint32_t name = names[i];
                if (!name || !c.Framebuffers.count(name))
                    continue;
                if (c.Rendering && c.RenderingFBO == name && Live())
                    EndRendering();
                if (c.S.DrawFBO == name) c.S.DrawFBO = 0;
                if (c.S.ReadFBO == name) c.S.ReadFBO = 0;
                c.Framebuffers.erase(name);
            }
        }

        void APIENTRY BindFramebuffer(GLenum target, GLuint name)
        {
            auto& c = Ctx();
            if (name && !c.Framebuffers.count(name))
                c.Framebuffers[name] = std::make_unique<Framebuffer>();
            if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
                c.S.DrawFBO = name;
            if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
                c.S.ReadFBO = name;
        }

        Attachment* AttachmentPoint(Framebuffer& fb, GLenum attachment, bool* both)
        {
            if (both)
                *both = false;
            if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + 8)
                return &fb.Color[attachment - GL_COLOR_ATTACHMENT0];
            switch (attachment)
            {
                case GL_DEPTH_ATTACHMENT: return &fb.Depth;
                case GL_STENCIL_ATTACHMENT: return &fb.Stencil;
                case GL_DEPTH_STENCIL_ATTACHMENT:
                    if (both)
                        *both = true;
                    return &fb.Depth;
                default: return nullptr;
            }
        }

        void APIENTRY FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
        {
            Framebuffer* fb = TargetFramebuffer(target);
            if (!fb)
                return;
            bool both = false;
            Attachment* a = AttachmentPoint(*fb, attachment, &both);
            if (!a)
                return;
            Attachment v;
            v.Texture = texture;
            v.Level = level;
            v.Layer = IsCubeFace(textarget) ? (int)(textarget - GL_TEXTURE_CUBE_MAP_POSITIVE_X) : 0;
            *a = v;
            if (both)
                fb->Stencil = v;
            if (Texture* t = GetTexture(texture); t && t->Target == 0)
                t->Target = IsCubeFace(textarget) ? GL_TEXTURE_CUBE_MAP : textarget;
        }

        void APIENTRY FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum, GLuint renderbuffer)
        {
            Framebuffer* fb = TargetFramebuffer(target);
            if (!fb)
                return;
            bool both = false;
            Attachment* a = AttachmentPoint(*fb, attachment, &both);
            if (!a)
                return;
            Attachment v;
            v.Renderbuffer = renderbuffer;
            *a = v;
            if (both)
                fb->Stencil = v;
        }

        GLenum APIENTRY CheckFramebufferStatus(GLenum target)
        {
            const uint32_t name = *FramebufferBindingFor(target);
            if (name == 0)
                return GL_FRAMEBUFFER_COMPLETE;
            Framebuffer* fb = GetFramebuffer(name);
            if (!fb)
                return GL_FRAMEBUFFER_UNDEFINED;
            bool any = false;
            for (const auto& a : fb->Color)
            {
                if (!a.Valid())
                    continue;
                Texture* t = AttachmentTexture(a);
                if (!t || !t->Image)
                    return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
                any = true;
            }
            if (fb->Depth.Valid())
            {
                Texture* t = AttachmentTexture(fb->Depth);
                if (!t || !t->Image)
                    return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
                any = true;
            }
            return any ? GL_FRAMEBUFFER_COMPLETE : GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
        }

        void APIENTRY DrawBuffers(GLsizei n, const GLenum* bufs)
        {
            auto& c = Ctx();
            if (c.S.DrawFBO == 0)
                return;
            Framebuffer* fb = GetFramebuffer(c.S.DrawFBO);
            if (!fb)
                return;
            fb->NumDrawBuffers = std::clamp<int>(n, 0, 8);
            for (int i = 0; i < 8; ++i)
                fb->DrawBuffers[i] = i < fb->NumDrawBuffers ? bufs[i] : GL_NONE;
        }

        void APIENTRY DrawBuffer(GLenum buf)
        {
            const GLenum b[1] = { buf };
            DrawBuffers(1, b);
        }

        void APIENTRY ReadBuffer(GLenum src)
        {
            auto& c = Ctx();
            if (c.S.ReadFBO == 0)
                return;
            if (Framebuffer* fb = GetFramebuffer(c.S.ReadFBO))
                fb->ReadBuffer = src;
        }

        void APIENTRY BlitFramebufferEntry(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
        {
            if (Live())
                BlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
        }

        void APIENTRY ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* pixels)
        {
            if (!Live() || w <= 0 || h <= 0 || !pixels)
                return;
            auto& c = Ctx();
            Texture* src = nullptr;
            int level = 0, layer = 0;
            const bool depth = format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL;
            if (c.S.ReadFBO == 0)
                src = depth ? &c.BackDepth : &c.BackColor;
            else if (Framebuffer* fb = GetFramebuffer(c.S.ReadFBO))
            {
                const Attachment* a = nullptr;
                if (depth)
                    a = &fb->Depth;
                else if (fb->ReadBuffer >= GL_COLOR_ATTACHMENT0 && fb->ReadBuffer < GL_COLOR_ATTACHMENT0 + 8)
                    a = &fb->Color[fb->ReadBuffer - GL_COLOR_ATTACHMENT0];
                if (a && a->Valid())
                {
                    src = AttachmentTexture(*a);
                    level = a->Level;
                    layer = a->Layer;
                }
            }
            if (!src || !src->Image)
                return;
            const int tw = (int)std::max(1u, src->Width >> level), th = (int)std::max(1u, src->Height >> level);
            const int x0 = std::max(x, 0), y0 = std::max(y, 0);
            const int x1 = std::min(x + w, tw), y1 = std::min(y + h, th);
            if (x1 <= x0 || y1 <= y0)
                return;
            if (x0 == x && y0 == y && x1 == x + w && y1 == y + h)
            {
                ReadImage(*src, level, layer, x, y, w, h, format, type, pixels, c.S.PackAlignment);
                return;
            }
            // Kısmen dışarıda: içteki bölgeyi okuyup yerine yerleştir (dışarısı tanımsız kalır)
            int channels = 4;
            switch (format)
            {
                case GL_RED: case GL_RED_INTEGER: case GL_DEPTH_COMPONENT: channels = 1; break;
                case GL_RG: case GL_RG_INTEGER: channels = 2; break;
                case GL_RGB: case GL_BGR: channels = 3; break;
                default: break;
            }
            const int comp = (type == GL_UNSIGNED_BYTE || type == GL_BYTE) ? 1 : (type == GL_UNSIGNED_SHORT || type == GL_SHORT || type == GL_HALF_FLOAT) ? 2 : 4;
            const size_t texel = (size_t)channels * comp;
            const int align = std::max(1, c.S.PackAlignment);
            const size_t dstPitch = ((size_t)w * texel + align - 1) / align * align;
            const int iw = x1 - x0, ih = y1 - y0;
            std::vector<uint8_t> tmp((size_t)iw * texel * ih);
            ReadImage(*src, level, layer, x0, y0, iw, ih, format, type, tmp.data(), 1);
            for (int row = 0; row < ih; ++row)
                std::memcpy(static_cast<uint8_t*>(pixels) + dstPitch * (size_t)(row + y0 - y) + texel * (size_t)(x0 - x), tmp.data() + (size_t)row * iw * texel,
                            (size_t)iw * texel);
        }

        // ── Arabellekler ──
        void APIENTRY GenBuffers(GLsizei n, GLuint* out)
        {
            for (GLsizei i = 0; i < n; ++i)
            {
                out[i] = NewName();
                EnsureBuffer(out[i]);
            }
        }

        void APIENTRY CreateBuffers(GLsizei n, GLuint* out) { GenBuffers(n, out); }

        void APIENTRY DeleteBuffers(GLsizei n, const GLuint* names)
        {
            auto& c = Ctx();
            for (GLsizei i = 0; i < n; ++i)
            {
                const uint32_t name = names[i];
                auto it = c.Buffers.find(name);
                if (it == c.Buffers.end())
                    continue;
                if (c.S.ArrayBuffer == name) c.S.ArrayBuffer = 0;
                if (c.S.UniformBuffer == name) c.S.UniformBuffer = 0;
                if (c.S.PixelUnpackBuffer == name) c.S.PixelUnpackBuffer = 0;
                if (CurrentVAO()->ElementBuffer == name) CurrentVAO()->ElementBuffer = 0;
                for (auto& b : c.S.UBOBindings)
                    if (b == name)
                        b = 0;
                if (Live())
                    ReleaseBuffer(*it->second);
                c.Buffers.erase(it);
            }
        }

        void APIENTRY BindBuffer(GLenum target, GLuint name)
        {
            EnsureBuffer(name);
            if (uint32_t* p = BufferBindingPoint(target))
                *p = name;
        }

        void APIENTRY BindBufferBase(GLenum target, GLuint index, GLuint name)
        {
            auto& c = Ctx();
            EnsureBuffer(name);
            if (target == GL_UNIFORM_BUFFER && index < 64)
            {
                c.S.UBOBindings[index] = name;
                c.S.UBOOffsets[index] = 0;
                c.S.UBOSizes[index] = 0;
                c.S.UniformBuffer = name;
            }
        }

        void APIENTRY BufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage)
        {
            Buffer* b = BoundBuffer(target);
            if (!b)
                return;
            b->Shadow.assign((size_t)std::max<GLsizeiptr>(size, 0), 0);
            b->HighWater = 0;
            b->Usage = usage;
            if (data)
                WriteBuffer(*b, 0, (size_t)size, data);
            b->Dirty = true;
        }

        void APIENTRY BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data)
        {
            if (Buffer* b = BoundBuffer(target))
                WriteBuffer(*b, (size_t)offset, (size_t)size, data);
        }

        void APIENTRY NamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data)
        {
            if (Buffer* b = GetBuffer(buffer))
                WriteBuffer(*b, (size_t)offset, (size_t)size, data);
        }

        // ── Köşe dizileri ──
        void APIENTRY GenVertexArrays(GLsizei n, GLuint* out)
        {
            auto& c = Ctx();
            for (GLsizei i = 0; i < n; ++i)
            {
                out[i] = NewName();
                c.VertexArrays[out[i]] = std::make_unique<VertexArray>();
            }
        }

        void APIENTRY CreateVertexArrays(GLsizei n, GLuint* out) { GenVertexArrays(n, out); }

        void APIENTRY DeleteVertexArrays(GLsizei n, const GLuint* names)
        {
            auto& c = Ctx();
            for (GLsizei i = 0; i < n; ++i)
            {
                if (!names[i])
                    continue;
                if (c.S.VAO == names[i])
                    c.S.VAO = 0;
                c.VertexArrays.erase(names[i]);
            }
        }

        void APIENTRY BindVertexArray(GLuint name)
        {
            auto& c = Ctx();
            if (name && !c.VertexArrays.count(name))
                c.VertexArrays[name] = std::make_unique<VertexArray>();
            c.S.VAO = name;
        }

        void APIENTRY EnableVertexAttribArray(GLuint index) { if (index < 16) CurrentVAO()->Attribs[index].Enabled = true; }
        void APIENTRY DisableVertexAttribArray(GLuint index) { if (index < 16) CurrentVAO()->Attribs[index].Enabled = false; }

        void APIENTRY VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer)
        {
            if (index >= 16)
                return;
            Attrib& a = CurrentVAO()->Attribs[index];
            a.Size = size == GL_BGRA ? 4 : size;
            a.Type = type;
            a.Normalized = normalized != GL_FALSE;
            a.Integer = false;
            a.Stride = stride;
            a.Offset = (uint64_t)(uintptr_t)pointer;
            a.Buffer = Ctx().S.ArrayBuffer;
        }

        void APIENTRY VertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer)
        {
            if (index >= 16)
                return;
            Attrib& a = CurrentVAO()->Attribs[index];
            a.Size = size;
            a.Type = type;
            a.Normalized = false;
            a.Integer = true;
            a.Stride = stride;
            a.Offset = (uint64_t)(uintptr_t)pointer;
            a.Buffer = Ctx().S.ArrayBuffer;
        }

        void APIENTRY VertexAttribDivisor(GLuint index, GLuint divisor) { if (index < 16) CurrentVAO()->Attribs[index].Divisor = divisor; }

        void APIENTRY GetVertexAttribiv(GLuint index, GLenum pname, GLint* params)
        {
            if (!params)
                return;
            *params = 0;
            if (index >= 16)
                return;
            const Attrib& a = CurrentVAO()->Attribs[index];
            switch (pname)
            {
                case GL_VERTEX_ATTRIB_ARRAY_ENABLED: *params = a.Enabled ? 1 : 0; break;
                case GL_VERTEX_ATTRIB_ARRAY_SIZE: *params = a.Size; break;
                case GL_VERTEX_ATTRIB_ARRAY_TYPE: *params = (GLint)a.Type; break;
                case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED: *params = a.Normalized ? 1 : 0; break;
                case GL_VERTEX_ATTRIB_ARRAY_INTEGER: *params = a.Integer ? 1 : 0; break;
                case GL_VERTEX_ATTRIB_ARRAY_STRIDE: *params = a.Stride; break;
                case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: *params = (GLint)a.Buffer; break;
                case GL_VERTEX_ATTRIB_ARRAY_DIVISOR: *params = (GLint)a.Divisor; break;
                default: break;
            }
        }

        void APIENTRY GetVertexAttribPointerv(GLuint index, GLenum, void** pointer)
        {
            if (pointer)
                *pointer = index < 16 ? (void*)(uintptr_t)CurrentVAO()->Attribs[index].Offset : nullptr;
        }

        // ── Gölgelendiriciler / programlar ──
        GLuint APIENTRY CreateShader(GLenum type)
        {
            const uint32_t name = NewName();
            auto s = std::make_unique<ShaderObject>();
            s->Type = type;
            Ctx().ShaderObjects[name] = std::move(s);
            return name;
        }

        void APIENTRY ShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths)
        {
            auto it = Ctx().ShaderObjects.find(shader);
            if (it == Ctx().ShaderObjects.end())
                return;
            std::string src;
            for (GLsizei i = 0; i < count; ++i)
            {
                if (!strings[i])
                    continue;
                if (lengths && lengths[i] >= 0)
                    src.append(strings[i], (size_t)lengths[i]);
                else
                    src.append(strings[i]);
            }
            it->second->Source = std::move(src);
            it->second->Compiled = false;
        }

        // Asıl derleme bağlamada yapılır (iki aşama birlikte: ortak global uniform bloğu)
        void APIENTRY CompileShader(GLuint shader)
        {
            auto it = Ctx().ShaderObjects.find(shader);
            if (it != Ctx().ShaderObjects.end())
                it->second->Compiled = !it->second->Source.empty();
        }

        void APIENTRY GetShaderiv(GLuint shader, GLenum pname, GLint* params)
        {
            if (!params)
                return;
            *params = 0;
            auto it = Ctx().ShaderObjects.find(shader);
            if (it == Ctx().ShaderObjects.end())
                return;
            switch (pname)
            {
                case GL_COMPILE_STATUS: *params = it->second->Compiled ? GL_TRUE : GL_FALSE; break;
                case GL_INFO_LOG_LENGTH: *params = it->second->Log.empty() ? 0 : (GLint)it->second->Log.size() + 1; break;
                case GL_SHADER_TYPE: *params = (GLint)it->second->Type; break;
                case GL_DELETE_STATUS: *params = it->second->DeletePending ? GL_TRUE : GL_FALSE; break;
                case GL_SHADER_SOURCE_LENGTH: *params = (GLint)it->second->Source.size() + 1; break;
                default: break;
            }
        }

        void CopyLog(const std::string& log, GLsizei bufSize, GLsizei* length, GLchar* out)
        {
            if (!out || bufSize <= 0)
            {
                if (length)
                    *length = 0;
                return;
            }
            const GLsizei n = std::min<GLsizei>((GLsizei)log.size(), bufSize - 1);
            std::memcpy(out, log.data(), (size_t)n);
            out[n] = 0;
            if (length)
                *length = n;
        }

        void APIENTRY GetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog)
        {
            auto it = Ctx().ShaderObjects.find(shader);
            CopyLog(it == Ctx().ShaderObjects.end() ? std::string() : it->second->Log, bufSize, length, infoLog);
        }

        void APIENTRY DeleteShader(GLuint shader)
        {
            auto& c = Ctx();
            auto it = c.ShaderObjects.find(shader);
            if (it == c.ShaderObjects.end())
                return;
            if (it->second->AttachCount > 0)
                it->second->DeletePending = true;
            else
                c.ShaderObjects.erase(it);
        }

        GLuint APIENTRY CreateProgram()
        {
            const uint32_t name = NewName();
            Ctx().Programs[name] = std::make_unique<Program>();
            return name;
        }

        void APIENTRY AttachShader(GLuint program, GLuint shader)
        {
            auto& c = Ctx();
            Program* p = GetProgram(program);
            auto it = c.ShaderObjects.find(shader);
            if (!p || it == c.ShaderObjects.end())
                return;
            if (std::find(p->Shaders.begin(), p->Shaders.end(), shader) != p->Shaders.end())
                return;
            p->Shaders.push_back(shader);
            ++it->second->AttachCount;
        }

        void APIENTRY DetachShader(GLuint program, GLuint shader)
        {
            auto& c = Ctx();
            Program* p = GetProgram(program);
            if (!p)
                return;
            auto pos = std::find(p->Shaders.begin(), p->Shaders.end(), shader);
            if (pos == p->Shaders.end())
                return;
            p->Shaders.erase(pos);
            auto it = c.ShaderObjects.find(shader);
            if (it != c.ShaderObjects.end() && --it->second->AttachCount <= 0 && it->second->DeletePending)
                c.ShaderObjects.erase(it);
        }

        void APIENTRY LinkProgramEntry(GLuint program)
        {
            Program* p = GetProgram(program);
            if (!p || !Live())
                return;
            if (!LinkProgram(*p) && !p->Log.empty())
                CQ_CORE_WARN("[GLEmu] Program link failed: {0}", p->Log);
        }

        void DestroyProgramObject(uint32_t name)
        {
            auto& c = Ctx();
            auto it = c.Programs.find(name);
            if (it == c.Programs.end())
                return;
            Program& p = *it->second;
            for (uint32_t s : p.Shaders)
            {
                auto sit = c.ShaderObjects.find(s);
                if (sit != c.ShaderObjects.end() && --sit->second->AttachCount <= 0 && sit->second->DeletePending)
                    c.ShaderObjects.erase(sit);
            }
            if (Live())
                DestroyProgram(p);
            // Bu programın boru hatları anahtarda program kimliğini taşır: artık eşleşmezler
            c.Programs.erase(it);
        }

        void APIENTRY UseProgram(GLuint program)
        {
            auto& c = Ctx();
            const uint32_t prev = c.S.Program;
            c.S.Program = program;
            if (prev && prev != program)
                if (Program* p = GetProgram(prev); p && p->DeletePending)
                    DestroyProgramObject(prev);
        }

        void APIENTRY DeleteProgram(GLuint program)
        {
            auto& c = Ctx();
            if (!program)
                return;
            if (c.S.Program == program)
            {
                if (Program* p = GetProgram(program))
                    p->DeletePending = true;
                return;
            }
            DestroyProgramObject(program);
        }

        GLboolean APIENTRY IsProgram(GLuint program) { return GetProgram(program) ? GL_TRUE : GL_FALSE; }

        void APIENTRY GetProgramiv(GLuint program, GLenum pname, GLint* params)
        {
            if (!params)
                return;
            *params = 0;
            Program* p = GetProgram(program);
            if (!p)
                return;
            switch (pname)
            {
                case GL_LINK_STATUS: *params = p->Linked ? GL_TRUE : GL_FALSE; break;
                case GL_VALIDATE_STATUS: *params = p->Linked ? GL_TRUE : GL_FALSE; break;
                case GL_INFO_LOG_LENGTH: *params = p->Log.empty() ? 0 : (GLint)p->Log.size() + 1; break;
                case GL_ATTACHED_SHADERS: *params = (GLint)p->Shaders.size(); break;
                case GL_ACTIVE_UNIFORMS: *params = (GLint)p->Uniforms.size(); break;
                case GL_ACTIVE_ATTRIBUTES: *params = (GLint)p->Inputs.size(); break;
                case GL_DELETE_STATUS: *params = p->DeletePending ? GL_TRUE : GL_FALSE; break;
                default: break;
            }
        }

        void APIENTRY GetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog)
        {
            Program* p = GetProgram(program);
            CopyLog(p ? p->Log : std::string(), bufSize, length, infoLog);
        }

        GLint APIENTRY GetUniformLocation(GLuint program, const GLchar* name)
        {
            Program* p = GetProgram(program);
            if (!p || !p->Linked || !name)
                return -1;
            auto it = p->UniformByName.find(name);
            if (it != p->UniformByName.end())
                return it->second;
            // "dizi[0]" ile taban adı eşdeğer
            const size_t len = std::strlen(name);
            if (len > 3 && std::strcmp(name + len - 3, "[0]") == 0)
            {
                it = p->UniformByName.find(std::string(name, len - 3));
                if (it != p->UniformByName.end())
                    return it->second;
            }
            return -1;
        }

        GLint APIENTRY GetAttribLocation(GLuint program, const GLchar* name)
        {
            Program* p = GetProgram(program);
            if (!p || !p->Linked || !name)
                return -1;
            for (const auto& in : p->Inputs)
                if (in.Name == name)
                    return in.Location;
            return -1;
        }

        void APIENTRY Uniform1f(GLint loc, GLfloat v0) { UniformVec<GLfloat>(loc, 1, 1, &v0); }
        void APIENTRY Uniform1fv(GLint loc, GLsizei count, const GLfloat* v) { UniformVec<GLfloat>(loc, count, 1, v); }
        void APIENTRY Uniform1i(GLint loc, GLint v0) { UniformVec<GLint>(loc, 1, 1, &v0); }
        void APIENTRY Uniform1iv(GLint loc, GLsizei count, const GLint* v) { UniformVec<GLint>(loc, count, 1, v); }
        void APIENTRY Uniform2f(GLint loc, GLfloat v0, GLfloat v1) { const GLfloat v[2] = { v0, v1 }; UniformVec<GLfloat>(loc, 1, 2, v); }
        void APIENTRY Uniform3f(GLint loc, GLfloat v0, GLfloat v1, GLfloat v2) { const GLfloat v[3] = { v0, v1, v2 }; UniformVec<GLfloat>(loc, 1, 3, v); }
        void APIENTRY Uniform3fv(GLint loc, GLsizei count, const GLfloat* v) { UniformVec<GLfloat>(loc, count, 3, v); }
        void APIENTRY Uniform4f(GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { const GLfloat v[4] = { v0, v1, v2, v3 }; UniformVec<GLfloat>(loc, 1, 4, v); }
        void APIENTRY UniformMatrix3fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* v) { UniformMatrix(loc, count, transpose, v, 3); }
        void APIENTRY UniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* v) { UniformMatrix(loc, count, transpose, v, 4); }

        // ── Çizim ──
        void APIENTRY DrawArrays(GLenum mode, GLint first, GLsizei count)
        {
            if (Live())
                Draw(mode, first, count, 1, false, 0, nullptr, 0);
        }

        void APIENTRY DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
        {
            if (Live())
                Draw(mode, first, count, instances, false, 0, nullptr, 0);
        }

        void APIENTRY DrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices)
        {
            if (Live())
                Draw(mode, 0, count, 1, true, type, indices, 0);
        }

        void APIENTRY DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint basevertex)
        {
            if (Live())
                Draw(mode, 0, count, 1, true, type, indices, basevertex);
        }

        void APIENTRY DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances)
        {
            if (Live())
                Draw(mode, 0, count, instances, true, type, indices, 0);
        }

        // ── Zaman damgası sorguları ──
        void APIENTRY GenQueries(GLsizei n, GLuint* out)
        {
            auto& c = Ctx();
            for (GLsizei i = 0; i < n; ++i)
            {
                out[i] = NewName();
                c.QueryObjects[out[i]] = QueryObject{};
            }
        }

        void APIENTRY DeleteQueries(GLsizei n, const GLuint* names)
        {
            for (GLsizei i = 0; i < n; ++i)
            {
                if (g_ActiveOcclusion == names[i])
                {
                    if (auto it = Ctx().QueryObjects.find(names[i]); it != Ctx().QueryObjects.end() && Live())
                        EndOcclusion(it->second);
                    g_ActiveOcclusion = 0;
                }
                Ctx().QueryObjects.erase(names[i]);
            }
        }

        void APIENTRY QueryCounter(GLuint id, GLenum)
        {
            auto it = Ctx().QueryObjects.find(id);
            if (it != Ctx().QueryObjects.end() && Live())
                WriteTimestamp(it->second);
        }

        bool QueryResult(QueryObject& q, bool wait, uint64_t& out)
        {
            out = 0;
            if (q.Occlusion)
            {
                uint64_t samples = 0;
                if (!ReadOcclusion(q, wait, samples))
                    return false;
                out = q.Any ? (samples ? 1 : 0) : samples;
                return true;
            }
            if (!q.Written)
                return true;
            return ReadTimestamp(q, wait, out);
        }

        void APIENTRY GetQueryObjectiv(GLuint id, GLenum pname, GLint* params)
        {
            if (!params)
                return;
            *params = 0;
            auto it = Ctx().QueryObjects.find(id);
            if (it == Ctx().QueryObjects.end() || !Live())
                return;
            uint64_t v = 0;
            if (pname == GL_QUERY_RESULT_AVAILABLE)
                *params = QueryResult(it->second, false, v) ? 1 : 0;
            else if (pname == GL_QUERY_RESULT)
            {
                QueryResult(it->second, true, v);
                *params = (GLint)v;
            }
        }

        void APIENTRY GetQueryObjectui64v(GLuint id, GLenum pname, GLuint64* params)
        {
            if (!params)
                return;
            *params = 0;
            auto it = Ctx().QueryObjects.find(id);
            if (it == Ctx().QueryObjects.end() || !Live())
                return;
            uint64_t v = 0;
            if (pname == GL_QUERY_RESULT_AVAILABLE)
                *params = QueryResult(it->second, false, v) ? 1 : 0;
            else
            {
                QueryResult(it->second, true, v);
                *params = v;
            }
        }

        // ── Doğrudan durum erişimi (DSA) ve eksik GL 4.5 girişleri ──
        // Köşe biçimi / bağlama noktası modeli (glVertexArrayAttribFormat / VertexBuffer / AttribBinding): arka uçların
        // Attrib yapısı öznitelik başına arabellek tutar; bağlama noktaları burada saklanıp özniteliklere yansıtılır.
        struct VertexBindingPoint
        {
            uint32_t Buffer = 0;
            uint64_t Offset = 0;
            GLsizei Stride = 0;
            GLuint Divisor = 0;
        };
        struct VertexFormatState
        {
            VertexBindingPoint Bindings[16];
            uint32_t AttribBinding[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
            uint32_t RelativeOffset[16] = {};
        };
        std::unordered_map<uint32_t, VertexFormatState> g_VertexFormats;

        VertexArray* VertexArrayByName(GLuint vaobj)
        {
            auto& c = Ctx();
            if (vaobj == 0)
                return &c.DefaultVAO;
            auto it = c.VertexArrays.find(vaobj);
            return it == c.VertexArrays.end() ? nullptr : it->second.get();
        }

        void ApplyVertexFormat(GLuint vaobj, uint32_t attrib)
        {
            VertexArray* vao = VertexArrayByName(vaobj);
            if (!vao || attrib >= 16)
                return;
            const VertexFormatState& f = g_VertexFormats[vaobj];
            const VertexBindingPoint& b = f.Bindings[f.AttribBinding[attrib] & 15];
            Attrib& a = vao->Attribs[attrib];
            a.Buffer = b.Buffer;
            a.Offset = b.Offset + f.RelativeOffset[attrib];
            a.Stride = b.Stride;
            a.Divisor = b.Divisor;
        }

        void APIENTRY VertexArrayAttribFormat(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset)
        {
            VertexArray* vao = VertexArrayByName(vaobj);
            if (!vao || attribindex >= 16)
                return;
            Attrib& a = vao->Attribs[attribindex];
            a.Size = size == GL_BGRA ? 4 : size;
            a.Type = type;
            a.Normalized = normalized != GL_FALSE;
            a.Integer = false;
            g_VertexFormats[vaobj].RelativeOffset[attribindex] = relativeoffset;
            ApplyVertexFormat(vaobj, attribindex);
        }

        void APIENTRY VertexArrayAttribIFormat(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
        {
            VertexArray* vao = VertexArrayByName(vaobj);
            if (!vao || attribindex >= 16)
                return;
            Attrib& a = vao->Attribs[attribindex];
            a.Size = size;
            a.Type = type;
            a.Normalized = false;
            a.Integer = true;
            g_VertexFormats[vaobj].RelativeOffset[attribindex] = relativeoffset;
            ApplyVertexFormat(vaobj, attribindex);
        }

        void APIENTRY VertexArrayAttribBinding(GLuint vaobj, GLuint attribindex, GLuint bindingindex)
        {
            if (attribindex >= 16 || bindingindex >= 16)
                return;
            g_VertexFormats[vaobj].AttribBinding[attribindex] = bindingindex;
            ApplyVertexFormat(vaobj, attribindex);
        }

        void APIENTRY VertexArrayVertexBuffer(GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)
        {
            if (bindingindex >= 16)
                return;
            VertexFormatState& f = g_VertexFormats[vaobj];
            f.Bindings[bindingindex].Buffer = buffer;
            f.Bindings[bindingindex].Offset = (uint64_t)offset;
            f.Bindings[bindingindex].Stride = stride;
            for (uint32_t i = 0; i < 16; ++i)
                if (f.AttribBinding[i] == bindingindex)
                    ApplyVertexFormat(vaobj, i);
        }

        void APIENTRY VertexArrayBindingDivisor(GLuint vaobj, GLuint bindingindex, GLuint divisor)
        {
            if (bindingindex >= 16)
                return;
            VertexFormatState& f = g_VertexFormats[vaobj];
            f.Bindings[bindingindex].Divisor = divisor;
            for (uint32_t i = 0; i < 16; ++i)
                if (f.AttribBinding[i] == bindingindex)
                    ApplyVertexFormat(vaobj, i);
        }

        void APIENTRY VertexArrayElementBuffer(GLuint vaobj, GLuint buffer)
        {
            if (VertexArray* vao = VertexArrayByName(vaobj))
                vao->ElementBuffer = buffer;
        }

        void APIENTRY EnableVertexArrayAttrib(GLuint vaobj, GLuint index)
        {
            if (VertexArray* vao = VertexArrayByName(vaobj); vao && index < 16)
                vao->Attribs[index].Enabled = true;
        }

        void APIENTRY DisableVertexArrayAttrib(GLuint vaobj, GLuint index)
        {
            if (VertexArray* vao = VertexArrayByName(vaobj); vao && index < 16)
                vao->Attribs[index].Enabled = false;
        }

        void APIENTRY NamedBufferData(GLuint buffer, GLsizeiptr size, const void* data, GLenum usage)
        {
            Buffer* b = GetBuffer(buffer);
            if (!b)
                return;
            b->Shadow.assign((size_t)std::max<GLsizeiptr>(size, 0), 0);
            b->HighWater = 0;
            b->Usage = usage;
            if (data)
                WriteBuffer(*b, 0, (size_t)size, data);
            b->Dirty = true;
        }

        void APIENTRY ColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a)
        {
            auto& s = Ctx().S;
            if (index >= 8)
                return;
            s.ColorMaskBits[index] = (uint8_t)((r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0));
            if (index == 0)
            {
                s.ColorMask[0] = r != GL_FALSE;
                s.ColorMask[1] = g != GL_FALSE;
                s.ColorMask[2] = b != GL_FALSE;
                s.ColorMask[3] = a != GL_FALSE;
            }
        }

        void APIENTRY MultiDrawElements(GLenum mode, const GLsizei* count, GLenum type, const void* const* indices, GLsizei drawcount)
        {
            if (!Live() || !count || !indices)
                return;
            for (GLsizei i = 0; i < drawcount; ++i)
                Draw(mode, 0, count[i], 1, true, type, indices[i], 0);
        }

        void APIENTRY Uniform2i(GLint loc, GLint v0, GLint v1) { const GLint v[2] = { v0, v1 }; UniformVec<GLint>(loc, 1, 2, v); }
        void APIENTRY Uniform3i(GLint loc, GLint v0, GLint v1, GLint v2) { const GLint v[3] = { v0, v1, v2 }; UniformVec<GLint>(loc, 1, 3, v); }
        void APIENTRY Uniform4i(GLint loc, GLint v0, GLint v1, GLint v2, GLint v3) { const GLint v[4] = { v0, v1, v2, v3 }; UniformVec<GLint>(loc, 1, 4, v); }

        GLuint APIENTRY GetUniformBlockIndex(GLuint program, const GLchar* name)
        {
            Program* p = GetProgram(program);
            if (!p || !name)
                return GL_INVALID_INDEX;
            for (size_t i = 0; i < p->Blocks.size(); ++i)
                if (p->Blocks[i].Name == name)
                    return (GLuint)i;
            return GL_INVALID_INDEX;
        }

        void APIENTRY UniformBlockBinding(GLuint program, GLuint index, GLuint binding)
        {
            Program* p = GetProgram(program);
            if (p && index < p->Blocks.size())
                p->Blocks[index].GLBinding = binding;
        }

        // Çerçeve arabelleği DSA: bağlamayı geçici değiştirip bağlamalı sürümü çağır
        template <typename F>
        void WithFramebuffer(GLuint framebuffer, F&& fn)
        {
            auto& c = Ctx();
            if (framebuffer && !c.Framebuffers.count(framebuffer))
                c.Framebuffers[framebuffer] = std::make_unique<Framebuffer>();
            const uint32_t prevDraw = c.S.DrawFBO, prevRead = c.S.ReadFBO;
            c.S.DrawFBO = c.S.ReadFBO = framebuffer;
            fn();
            c.S.DrawFBO = prevDraw;
            c.S.ReadFBO = prevRead;
        }

        void APIENTRY NamedFramebufferTexture(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level)
        {
            Texture* t = GetTexture(texture);
            const GLenum target = t && t->Target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : GL_TEXTURE_2D;
            WithFramebuffer(framebuffer, [&] { FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, target, texture, level); });
        }

        void APIENTRY NamedFramebufferRenderbuffer(GLuint framebuffer, GLenum attachment, GLenum rbtarget, GLuint renderbuffer)
        {
            WithFramebuffer(framebuffer, [&] { FramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, attachment, rbtarget, renderbuffer); });
        }

        void APIENTRY NamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n, const GLenum* bufs)
        {
            WithFramebuffer(framebuffer, [&] { DrawBuffers(n, bufs); });
        }

        void APIENTRY NamedFramebufferDrawBuffer(GLuint framebuffer, GLenum buf)
        {
            WithFramebuffer(framebuffer, [&] { DrawBuffer(buf); });
        }

        void APIENTRY NamedFramebufferReadBuffer(GLuint framebuffer, GLenum src)
        {
            WithFramebuffer(framebuffer, [&] { ReadBuffer(src); });
        }

        void APIENTRY BlitNamedFramebuffer(GLuint readFramebuffer, GLuint drawFramebuffer, GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1,
                                           GLint dy1, GLbitfield mask, GLenum filter)
        {
            auto& c = Ctx();
            const uint32_t prevDraw = c.S.DrawFBO, prevRead = c.S.ReadFBO;
            c.S.ReadFBO = readFramebuffer;
            c.S.DrawFBO = drawFramebuffer;
            if (Live())
                BlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
            c.S.DrawFBO = prevDraw;
            c.S.ReadFBO = prevRead;
        }

        void APIENTRY GetNamedFramebufferAttachmentParameteriv(GLuint framebuffer, GLenum attachment, GLenum pname, GLint* params)
        {
            if (!params)
                return;
            *params = 0;
            Framebuffer* fb = GetFramebuffer(framebuffer);
            if (!fb)
                return;
            Attachment* a = AttachmentPoint(*fb, attachment, nullptr);
            if (!a)
                return;
            switch (pname)
            {
                case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE: *params = a->Texture ? GL_TEXTURE : a->Renderbuffer ? GL_RENDERBUFFER : GL_NONE; break;
                case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME: *params = (GLint)(a->Texture ? a->Texture : a->Renderbuffer); break;
                case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL: *params = a->Level; break;
                default: break;
            }
        }

        void APIENTRY CreateRenderbuffers(GLsizei n, GLuint* out) { GenRenderbuffers(n, out); }

        void APIENTRY NamedRenderbufferStorage(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height)
        {
            if (!Live())
                return;
            Texture* rb = GetRenderbuffer(renderbuffer);
            if (!rb)
                return;
            AllocateTexture(*rb, GL_RENDERBUFFER, internalformat, (uint32_t)std::max(1, width), (uint32_t)std::max(1, height), 1, 1);
            rb->Immutable = true;
            rb->DefinedLevels = 1;
        }

        void APIENTRY NamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)
        {
            if (!Live())
                return;
            Texture* rb = GetRenderbuffer(renderbuffer);
            if (!rb)
                return;
            const bool depth = IsDepthInternalFormat(internalformat);
            AllocateTexture(*rb, GL_RENDERBUFFER, internalformat, (uint32_t)std::max(1, width), (uint32_t)std::max(1, height), 1, ClampSamples(samples, depth));
            rb->Immutable = true;
            rb->DefinedLevels = 1;
        }

        void APIENTRY GetNamedRenderbufferParameteriv(GLuint renderbuffer, GLenum pname, GLint* params)
        {
            if (!params)
                return;
            *params = 0;
            Texture* rb = GetRenderbuffer(renderbuffer);
            if (!rb)
                return;
            switch (pname)
            {
                case GL_RENDERBUFFER_WIDTH: *params = (GLint)rb->Width; break;
                case GL_RENDERBUFFER_HEIGHT: *params = (GLint)rb->Height; break;
                case GL_RENDERBUFFER_INTERNAL_FORMAT: *params = (GLint)rb->InternalFormat; break;
                case GL_RENDERBUFFER_SAMPLES: *params = rb->Samples > 1 ? (GLint)rb->Samples : 0; break;
                default: break;
            }
        }

        void APIENTRY GetTextureLevelParameteriv(GLuint texture, GLint level, GLenum pname, GLint* params)
        {
            if (!params)
                return;
            *params = 0;
            Texture* t = GetTexture(texture);
            if (!t || !t->Image || level < 0 || level >= (int)t->Levels)
                return;
            switch (pname)
            {
                case GL_TEXTURE_WIDTH: *params = (GLint)std::max(1u, t->Width >> level); break;
                case GL_TEXTURE_HEIGHT: *params = (GLint)std::max(1u, t->Height >> level); break;
                case GL_TEXTURE_DEPTH: *params = 1; break;
                case GL_TEXTURE_INTERNAL_FORMAT: *params = (GLint)t->InternalFormat; break;
                case GL_TEXTURE_SAMPLES: *params = t->Samples > 1 ? (GLint)t->Samples : 0; break;
                default: break;
            }
        }

        // Örtüşme sorguları (GL_SAMPLES_PASSED / GL_ANY_SAMPLES_PASSED[_CONSERVATIVE]): arka uç kancaları
        // BeginOcclusion / EndOcclusion / ReadOcclusion. Aynı anda tek örtüşme sorgusu etkindir (GL kuralı).
        void APIENTRY BeginQuery(GLenum target, GLuint id)
        {
            if (target != GL_SAMPLES_PASSED && target != GL_ANY_SAMPLES_PASSED && target != GL_ANY_SAMPLES_PASSED_CONSERVATIVE)
                return;
            auto& c = Ctx();
            auto it = c.QueryObjects.find(id);
            if (it == c.QueryObjects.end() || !Live() || g_ActiveOcclusion)
                return;
            it->second.Occlusion = true;
            it->second.Any = target != GL_SAMPLES_PASSED;
            g_ActiveOcclusion = id;
            BeginOcclusion(it->second);
        }

        void APIENTRY EndQuery(GLenum target)
        {
            if (target != GL_SAMPLES_PASSED && target != GL_ANY_SAMPLES_PASSED && target != GL_ANY_SAMPLES_PASSED_CONSERVATIVE)
                return;
            auto& c = Ctx();
            const GLuint id = g_ActiveOcclusion;
            g_ActiveOcclusion = 0;
            if (auto it = c.QueryObjects.find(id); it != c.QueryObjects.end() && Live())
                EndOcclusion(it->second);
        }

        void APIENTRY GetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params)
        {
            if (!params)
                return;
            GLint v = 0;
            GetQueryObjectiv(id, pname, &v);
            *params = (GLuint)v;
        }

        // ── Durum sorguları ──
        void APIENTRY GetIntegerv(GLenum pname, GLint* data)
        {
            if (!data)
                return;
            auto& c = Ctx();
            const auto& s = c.S;
            const Limits& limits = GetLimits();
            switch (pname)
            {
                case GL_ACTIVE_TEXTURE: *data = (GLint)(GL_TEXTURE0 + s.ActiveTexture); break;
                case GL_ARRAY_BUFFER_BINDING: *data = (GLint)s.ArrayBuffer; break;
                case GL_ELEMENT_ARRAY_BUFFER_BINDING: *data = (GLint)CurrentVAO()->ElementBuffer; break;
                case GL_UNIFORM_BUFFER_BINDING: *data = (GLint)s.UniformBuffer; break;
                case GL_PIXEL_UNPACK_BUFFER_BINDING: *data = (GLint)s.PixelUnpackBuffer; break;
                case GL_PIXEL_PACK_BUFFER_BINDING: *data = 0; break;
                case GL_VERTEX_ARRAY_BINDING: *data = (GLint)s.VAO; break;
                case GL_CURRENT_PROGRAM: *data = (GLint)s.Program; break;
                case GL_FRAMEBUFFER_BINDING: *data = (GLint)s.DrawFBO; break;
                case GL_READ_FRAMEBUFFER_BINDING: *data = (GLint)s.ReadFBO; break;
                case GL_RENDERBUFFER_BINDING: *data = (GLint)s.Renderbuffer; break;
                case GL_TEXTURE_BINDING_2D: *data = (GLint)s.Tex2D[s.ActiveTexture]; break;
                case GL_TEXTURE_BINDING_CUBE_MAP: *data = (GLint)s.TexCube[s.ActiveTexture]; break;
                case GL_TEXTURE_BINDING_2D_MULTISAMPLE: *data = (GLint)s.Tex2DMS[s.ActiveTexture]; break;
                case GL_SAMPLER_BINDING: *data = (GLint)s.Samplers[s.ActiveTexture]; break;
                case GL_BLEND_SRC_RGB: *data = (GLint)s.BlendSrcRGB; break;
                case GL_BLEND_DST_RGB: *data = (GLint)s.BlendDstRGB; break;
                case GL_BLEND_SRC_ALPHA: *data = (GLint)s.BlendSrcA; break;
                case GL_BLEND_DST_ALPHA: *data = (GLint)s.BlendDstA; break;
                case GL_BLEND_EQUATION_RGB: *data = (GLint)s.BlendEqRGB; break;
                case GL_BLEND_EQUATION_ALPHA: *data = (GLint)s.BlendEqA; break;
                case GL_DEPTH_FUNC: *data = (GLint)s.DepthFunc; break;
                case GL_DEPTH_WRITEMASK: *data = s.DepthMask ? 1 : 0; break;
                case GL_CULL_FACE_MODE: *data = (GLint)s.CullMode; break;
                case GL_FRONT_FACE: *data = (GLint)s.FrontFace; break;
                case GL_POLYGON_MODE: data[0] = data[1] = (GLint)s.PolygonMode; break;
                case GL_VIEWPORT: for (int i = 0; i < 4; ++i) data[i] = s.Viewport[i]; break;
                case GL_SCISSOR_BOX: for (int i = 0; i < 4; ++i) data[i] = s.Scissor[i]; break;
                case GL_STENCIL_FUNC: *data = (GLint)s.StencilFunc; break;
                case GL_STENCIL_REF: *data = s.StencilRef; break;
                case GL_STENCIL_VALUE_MASK: *data = (GLint)s.StencilValueMask; break;
                case GL_STENCIL_WRITEMASK: *data = (GLint)s.StencilWriteMask; break;
                case GL_STENCIL_FAIL: *data = (GLint)s.StencilFail; break;
                case GL_STENCIL_PASS_DEPTH_FAIL: *data = (GLint)s.StencilZFail; break;
                case GL_STENCIL_PASS_DEPTH_PASS: *data = (GLint)s.StencilZPass; break;
                case GL_UNPACK_ALIGNMENT: *data = s.UnpackAlignment; break;
                case GL_PACK_ALIGNMENT: *data = s.PackAlignment; break;
                case GL_UNPACK_ROW_LENGTH: *data = s.UnpackRowLength; break;
                case GL_CLIP_ORIGIN: *data = (GLint)s.ClipOrigin; break;
                case GL_CLIP_DEPTH_MODE: *data = GL_NEGATIVE_ONE_TO_ONE; break;
                case GL_CONTEXT_PROFILE_MASK: *data = GL_CONTEXT_CORE_PROFILE_BIT; break;
                case GL_CONTEXT_FLAGS: *data = 0; break;
                case GL_MAJOR_VERSION: *data = 4; break;
                case GL_MINOR_VERSION: *data = 6; break;
                case GL_NUM_EXTENSIONS: *data = (GLint)c.Extensions.size(); break;
                case GL_MAX_TEXTURE_SIZE: *data = limits.MaxTextureSize; break;
                case GL_MAX_CUBE_MAP_TEXTURE_SIZE: *data = limits.MaxCubeMapSize; break;
                case GL_MAX_RENDERBUFFER_SIZE: *data = limits.MaxTextureSize; break;
                case GL_MAX_TEXTURE_IMAGE_UNITS: *data = limits.MaxTextureUnits; break;
                case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: *data = limits.MaxTextureUnits * 2; break;
                case GL_MAX_VERTEX_ATTRIBS: *data = limits.MaxVertexAttribs; break;
                case GL_MAX_UNIFORM_BUFFER_BINDINGS: *data = limits.MaxUniformBufferBindings; break;
                case GL_MAX_UNIFORM_BLOCK_SIZE: *data = limits.MaxUniformBlockSize; break;
                case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: *data = limits.UniformBufferAlignment; break;
                case GL_MAX_COLOR_ATTACHMENTS: case GL_MAX_DRAW_BUFFERS: *data = limits.MaxColorAttachments; break;
                case GL_MAX_SAMPLES: *data = (GLint)ClampSamples(64, false); break;
                case GL_MAX_VIEWPORT_DIMS: data[0] = limits.MaxViewport[0]; data[1] = limits.MaxViewport[1]; break;
                case GL_SAMPLES: *data = 1; break;
                case GL_MAX_TEXTURE_MAX_ANISOTROPY: *data = (GLint)limits.MaxAnisotropy; break;
                case 0x9047: case 0x9048: case 0x9049: case 0x87FC:
                    // NVX / ATI bellek sorguları: cihaz yerel belleği (KB)
                    *data = limits.VideoMemoryKB;
                    if (pname == 0x87FC)
                        data[1] = data[2] = data[3] = data[0];
                    break;
                default:
                    *data = 0;
                    break;
            }
        }

        void APIENTRY GetIntegeri_v(GLenum target, GLuint index, GLint* data)
        {
            if (!data)
                return;
            auto& s = Ctx().S;
            switch (target)
            {
                case GL_UNIFORM_BUFFER_BINDING: *data = index < 64 ? (GLint)s.UBOBindings[index] : 0; break;
                case GL_UNIFORM_BUFFER_START: *data = index < 64 ? (GLint)s.UBOOffsets[index] : 0; break;
                case GL_UNIFORM_BUFFER_SIZE: *data = index < 64 ? (GLint)s.UBOSizes[index] : 0; break;
                case GL_MAX_COMPUTE_WORK_GROUP_COUNT: *data = 65535; break;
                case GL_MAX_COMPUTE_WORK_GROUP_SIZE: *data = index < 2 ? 1024 : 64; break;
                default: *data = 0; break;
            }
        }

        void APIENTRY GetFloatv(GLenum pname, GLfloat* data)
        {
            if (!data)
                return;
            auto& s = Ctx().S;
            switch (pname)
            {
                case GL_MAX_TEXTURE_MAX_ANISOTROPY:
                    *data = GetLimits().MaxAnisotropy;
                    break;
                case GL_COLOR_CLEAR_VALUE: for (int i = 0; i < 4; ++i) data[i] = s.ClearColor[i]; break;
                case GL_DEPTH_CLEAR_VALUE: *data = s.ClearDepth; break;
                case GL_LINE_WIDTH: *data = s.LineWidth; break;
                case GL_POLYGON_OFFSET_FACTOR: *data = s.PolygonOffsetFactor; break;
                case GL_POLYGON_OFFSET_UNITS: *data = s.PolygonOffsetUnits; break;
                case GL_ALIASED_LINE_WIDTH_RANGE: case GL_SMOOTH_LINE_WIDTH_RANGE:
                    data[0] = GetLimits().LineWidthRange[0];
                    data[1] = GetLimits().LineWidthRange[1];
                    break;
                case GL_VIEWPORT: for (int i = 0; i < 4; ++i) data[i] = (float)s.Viewport[i]; break;
                default:
                {
                    GLint tmp[4] = {};
                    GetIntegerv(pname, tmp);
                    *data = (float)tmp[0];
                    break;
                }
            }
        }

        void APIENTRY GetBooleanv(GLenum pname, GLboolean* data)
        {
            if (!data)
                return;
            auto& s = Ctx().S;
            switch (pname)
            {
                case GL_DEPTH_WRITEMASK: *data = s.DepthMask ? GL_TRUE : GL_FALSE; break;
                case GL_COLOR_WRITEMASK: for (int i = 0; i < 4; ++i) data[i] = s.ColorMask[i] ? GL_TRUE : GL_FALSE; break;
                default:
                {
                    if (bool* f = CapFlag(pname))
                    {
                        *data = *f ? GL_TRUE : GL_FALSE;
                        break;
                    }
                    GLint tmp[4] = {};
                    GetIntegerv(pname, tmp);
                    *data = tmp[0] ? GL_TRUE : GL_FALSE;
                    break;
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════════════════════════════
    //  Yükleyici
    // ════════════════════════════════════════════════════════════════════════════════════════════
    namespace
    {
        struct NamedFn
        {
            const char* Name;
            void* Fn;
        };

#define CQ_VKGL_FN(name, fn) { "gl" #name, (void*)&Entry::fn }
        const NamedFn g_Functions[] = {
            CQ_VKGL_FN(ActiveTexture, ActiveTexture), CQ_VKGL_FN(AttachShader, AttachShader), CQ_VKGL_FN(BindBuffer, BindBuffer),
            CQ_VKGL_FN(BindBufferBase, BindBufferBase), CQ_VKGL_FN(BindFramebuffer, BindFramebuffer), CQ_VKGL_FN(BindRenderbuffer, BindRenderbuffer),
            CQ_VKGL_FN(BindSampler, BindSampler), CQ_VKGL_FN(BindTexture, BindTexture), CQ_VKGL_FN(BindTextureUnit, BindTextureUnit),
            CQ_VKGL_FN(BindVertexArray, BindVertexArray), CQ_VKGL_FN(BlendEquation, BlendEquation), CQ_VKGL_FN(BlendEquationSeparate, BlendEquationSeparate),
            CQ_VKGL_FN(BlendFunc, BlendFunc), CQ_VKGL_FN(BlendFuncSeparate, BlendFuncSeparate), CQ_VKGL_FN(BlitFramebuffer, BlitFramebufferEntry),
            CQ_VKGL_FN(BufferData, BufferData), CQ_VKGL_FN(BufferSubData, BufferSubData), CQ_VKGL_FN(CheckFramebufferStatus, CheckFramebufferStatus),
            CQ_VKGL_FN(Clear, Clear), CQ_VKGL_FN(ClearColor, ClearColor), CQ_VKGL_FN(ClearTexImage, ClearTexImage), CQ_VKGL_FN(ClipControl, ClipControl),
            CQ_VKGL_FN(ColorMask, ColorMask), CQ_VKGL_FN(CompileShader, CompileShader), CQ_VKGL_FN(CreateBuffers, CreateBuffers),
            CQ_VKGL_FN(CreateFramebuffers, CreateFramebuffers), CQ_VKGL_FN(CreateProgram, CreateProgram), CQ_VKGL_FN(CreateShader, CreateShader),
            CQ_VKGL_FN(CreateTextures, CreateTextures), CQ_VKGL_FN(CreateVertexArrays, CreateVertexArrays), CQ_VKGL_FN(CullFace, CullFace),
            CQ_VKGL_FN(DebugMessageCallback, DebugMessageCallback), CQ_VKGL_FN(DebugMessageControl, DebugMessageControl),
            CQ_VKGL_FN(DeleteBuffers, DeleteBuffers), CQ_VKGL_FN(DeleteFramebuffers, DeleteFramebuffers), CQ_VKGL_FN(DeleteProgram, DeleteProgram),
            CQ_VKGL_FN(DeleteQueries, DeleteQueries), CQ_VKGL_FN(DeleteRenderbuffers, DeleteRenderbuffers), CQ_VKGL_FN(DeleteShader, DeleteShader),
            CQ_VKGL_FN(DeleteTextures, DeleteTextures), CQ_VKGL_FN(DeleteVertexArrays, DeleteVertexArrays), CQ_VKGL_FN(DepthFunc, DepthFunc),
            CQ_VKGL_FN(DepthMask, DepthMask), CQ_VKGL_FN(DetachShader, DetachShader), CQ_VKGL_FN(Disable, Disable),
            CQ_VKGL_FN(DisableVertexAttribArray, DisableVertexAttribArray), CQ_VKGL_FN(DrawArrays, DrawArrays),
            CQ_VKGL_FN(DrawArraysInstanced, DrawArraysInstanced), CQ_VKGL_FN(DrawBuffer, DrawBuffer), CQ_VKGL_FN(DrawBuffers, DrawBuffers),
            CQ_VKGL_FN(DrawElements, DrawElements), CQ_VKGL_FN(DrawElementsBaseVertex, DrawElementsBaseVertex), CQ_VKGL_FN(DrawElementsInstanced, DrawElementsInstanced), CQ_VKGL_FN(Enable, Enable),
            CQ_VKGL_FN(EnableVertexAttribArray, EnableVertexAttribArray), CQ_VKGL_FN(FramebufferRenderbuffer, FramebufferRenderbuffer),
            CQ_VKGL_FN(FramebufferTexture2D, FramebufferTexture2D), CQ_VKGL_FN(GenBuffers, GenBuffers), CQ_VKGL_FN(GenerateMipmap, GenerateMipmap),
            CQ_VKGL_FN(GenerateTextureMipmap, GenerateTextureMipmap), CQ_VKGL_FN(GenFramebuffers, GenFramebuffers), CQ_VKGL_FN(GenQueries, GenQueries),
            CQ_VKGL_FN(GenRenderbuffers, GenRenderbuffers), CQ_VKGL_FN(GenTextures, GenTextures), CQ_VKGL_FN(GenVertexArrays, GenVertexArrays),
            CQ_VKGL_FN(GetAttribLocation, GetAttribLocation), CQ_VKGL_FN(GetBooleanv, GetBooleanv), CQ_VKGL_FN(GetError, GetError),
            CQ_VKGL_FN(GetFloatv, GetFloatv), CQ_VKGL_FN(GetIntegerv, GetIntegerv), CQ_VKGL_FN(GetIntegeri_v, GetIntegeri_v),
            CQ_VKGL_FN(GetProgramInfoLog, GetProgramInfoLog), CQ_VKGL_FN(GetProgramiv, GetProgramiv), CQ_VKGL_FN(GetQueryObjectiv, GetQueryObjectiv),
            CQ_VKGL_FN(GetQueryObjectui64v, GetQueryObjectui64v), CQ_VKGL_FN(GetShaderInfoLog, GetShaderInfoLog), CQ_VKGL_FN(GetShaderiv, GetShaderiv),
            CQ_VKGL_FN(GetString, GetString), CQ_VKGL_FN(GetStringi, GetStringi), CQ_VKGL_FN(GetTexImage, GetTexImage),
            CQ_VKGL_FN(GetTextureImage, GetTextureImage), CQ_VKGL_FN(VertexArrayAttribFormat, VertexArrayAttribFormat), CQ_VKGL_FN(VertexArrayAttribIFormat, VertexArrayAttribIFormat),
            CQ_VKGL_FN(VertexArrayAttribBinding, VertexArrayAttribBinding), CQ_VKGL_FN(VertexArrayVertexBuffer, VertexArrayVertexBuffer),
            CQ_VKGL_FN(VertexArrayBindingDivisor, VertexArrayBindingDivisor), CQ_VKGL_FN(VertexArrayElementBuffer, VertexArrayElementBuffer),
            CQ_VKGL_FN(EnableVertexArrayAttrib, EnableVertexArrayAttrib), CQ_VKGL_FN(DisableVertexArrayAttrib, DisableVertexArrayAttrib),
            CQ_VKGL_FN(NamedBufferData, NamedBufferData), CQ_VKGL_FN(ColorMaski, ColorMaski), CQ_VKGL_FN(MultiDrawElements, MultiDrawElements),
            CQ_VKGL_FN(Uniform2i, Uniform2i), CQ_VKGL_FN(Uniform3i, Uniform3i), CQ_VKGL_FN(Uniform4i, Uniform4i),
            CQ_VKGL_FN(GetUniformBlockIndex, GetUniformBlockIndex), CQ_VKGL_FN(UniformBlockBinding, UniformBlockBinding),
            CQ_VKGL_FN(NamedFramebufferTexture, NamedFramebufferTexture), CQ_VKGL_FN(NamedFramebufferRenderbuffer, NamedFramebufferRenderbuffer),
            CQ_VKGL_FN(NamedFramebufferDrawBuffers, NamedFramebufferDrawBuffers), CQ_VKGL_FN(NamedFramebufferDrawBuffer, NamedFramebufferDrawBuffer),
            CQ_VKGL_FN(NamedFramebufferReadBuffer, NamedFramebufferReadBuffer), CQ_VKGL_FN(BlitNamedFramebuffer, BlitNamedFramebuffer),
            CQ_VKGL_FN(GetNamedFramebufferAttachmentParameteriv, GetNamedFramebufferAttachmentParameteriv),
            CQ_VKGL_FN(CreateRenderbuffers, CreateRenderbuffers), CQ_VKGL_FN(NamedRenderbufferStorage, NamedRenderbufferStorage),
            CQ_VKGL_FN(NamedRenderbufferStorageMultisample, NamedRenderbufferStorageMultisample),
            CQ_VKGL_FN(GetNamedRenderbufferParameteriv, GetNamedRenderbufferParameteriv), CQ_VKGL_FN(GetTextureLevelParameteriv, GetTextureLevelParameteriv),
            CQ_VKGL_FN(BeginQuery, BeginQuery), CQ_VKGL_FN(EndQuery, EndQuery), CQ_VKGL_FN(GetQueryObjectuiv, GetQueryObjectuiv), CQ_VKGL_FN(GetTextureSubImage, GetTextureSubImage), CQ_VKGL_FN(GetUniformLocation, GetUniformLocation),
            CQ_VKGL_FN(GetVertexAttribiv, GetVertexAttribiv), CQ_VKGL_FN(GetVertexAttribPointerv, GetVertexAttribPointerv),
            CQ_VKGL_FN(IsEnabled, IsEnabled), CQ_VKGL_FN(IsProgram, IsProgram), CQ_VKGL_FN(LineWidth, LineWidth), CQ_VKGL_FN(LinkProgram, LinkProgramEntry),
            CQ_VKGL_FN(NamedBufferSubData, NamedBufferSubData), CQ_VKGL_FN(PixelStorei, PixelStorei), CQ_VKGL_FN(PolygonMode, PolygonMode),
            CQ_VKGL_FN(PolygonOffset, PolygonOffset), CQ_VKGL_FN(QueryCounter, QueryCounter), CQ_VKGL_FN(ReadBuffer, ReadBuffer),
            CQ_VKGL_FN(ReadPixels, ReadPixels), CQ_VKGL_FN(RenderbufferStorage, RenderbufferStorage), CQ_VKGL_FN(Scissor, Scissor),
            CQ_VKGL_FN(ShaderSource, ShaderSource), CQ_VKGL_FN(StencilFunc, StencilFunc), CQ_VKGL_FN(StencilMask, StencilMask),
            CQ_VKGL_FN(StencilOp, StencilOp), CQ_VKGL_FN(TexImage2D, TexImage2D), CQ_VKGL_FN(TexImage2DMultisample, TexImage2DMultisample),
            CQ_VKGL_FN(TexParameteri, TexParameteri), CQ_VKGL_FN(TexStorage2D, TexStorage2D), CQ_VKGL_FN(TexSubImage2D, TexSubImage2D),
            CQ_VKGL_FN(TextureParameterf, TextureParameterf), CQ_VKGL_FN(TextureParameteri, TextureParameteri),
            CQ_VKGL_FN(TextureStorage2D, TextureStorage2D), CQ_VKGL_FN(TextureSubImage2D, TextureSubImage2D), CQ_VKGL_FN(Uniform1f, Uniform1f),
            CQ_VKGL_FN(Uniform1fv, Uniform1fv), CQ_VKGL_FN(Uniform1i, Uniform1i), CQ_VKGL_FN(Uniform1iv, Uniform1iv), CQ_VKGL_FN(Uniform2f, Uniform2f),
            CQ_VKGL_FN(Uniform3f, Uniform3f), CQ_VKGL_FN(Uniform3fv, Uniform3fv), CQ_VKGL_FN(Uniform4f, Uniform4f),
            CQ_VKGL_FN(UniformMatrix3fv, UniformMatrix3fv), CQ_VKGL_FN(UniformMatrix4fv, UniformMatrix4fv), CQ_VKGL_FN(UseProgram, UseProgram),
            CQ_VKGL_FN(VertexAttribDivisor, VertexAttribDivisor), CQ_VKGL_FN(VertexAttribIPointer, VertexAttribIPointer),
            CQ_VKGL_FN(VertexAttribPointer, VertexAttribPointer), CQ_VKGL_FN(Viewport, Viewport),
        };
#undef CQ_VKGL_FN

        const char* VendorName(uint32_t id)
        {
            switch (id)
            {
                case 0x1002: return "AMD";
                case 0x10DE: return "NVIDIA Corporation";
                case 0x8086: return "Intel";
                case 0x13B5: return "ARM";
                case 0x5143: return "Qualcomm";
                case 0x106B: return "Apple";
                case 0x1414: return "Microsoft";
                case 0x10005: return "Mesa";
                default: return "Unknown";
            }
        }

        // Eksik doku yerine geçen 1x1 iç dokular (GL'deki gibi (0,0,0,1) döndürür)
        Texture* MakeInternalTexture(GLenum target, GLenum internalFormat, const void* pixel, GLenum format, GLenum type)
        {
            auto& c = Ctx();
            auto t = std::make_unique<Texture>();
            t->MinFilter = GL_NEAREST;
            t->MagFilter = GL_NEAREST;
            AllocateTexture(*t, target, internalFormat, 1, 1, 1, 1);
            t->Immutable = true;
            t->DefinedLevels = 1;
            for (uint32_t l = 0; l < t->Layers; ++l)
                UploadTexture(*t, 0, (int)l, 0, 0, 1, 1, format, type, pixel);
            c.InternalTextures.push_back(std::move(t));
            return c.InternalTextures.back().get();
        }

        void* GetProc(const char* name)
        {
            if (!name)
                return nullptr;
            for (const auto& f : g_Functions)
                if (std::strcmp(f.Name, name) == 0)
                    return f.Fn;
            // Uygulanmamış: adını hatırlayan saplama
            for (size_t i = 0; i < g_StubCount; ++i)
                if (std::strcmp(g_StubNames[i], name) == 0)
                    return (void*)g_Stubs[i];
            if (g_StubCount >= kMaxStubs)
                return (void*)g_Stubs[kMaxStubs - 1];
            g_StubNames[g_StubCount] = name;   // glad adları statik dizgeler
            return (void*)g_Stubs[g_StubCount++];
        }
    }
