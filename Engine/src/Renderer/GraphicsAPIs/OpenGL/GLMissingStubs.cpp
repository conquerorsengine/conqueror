// ÜRETİLDİ: Tools/Web/gen_gl_stubs.py (elle düzenlemeyin). WebGL'de olmayan GL işlevleri için imzası aynı yedekler:
// ilk çağrıda bir kez uyarı, sıfır / boş dönüş. Yalnız Web (Emscripten) derlemesinde.
#include "GLMissingStubs.h"

#if defined(__EMSCRIPTEN__)
#include "Core/Logging/Log.h"
#include <glad/glad.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

namespace Conqueror::GLProfile
{
    namespace
    {
        void Missing(const char* name) { CQ_CORE_WARN("{0} is not available in this graphics backend (WebGL 2 / WebGPU; call ignored)", name); }

        void APIENTRY S_glCullFace(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCullFace"); } }
        void APIENTRY S_glFrontFace(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFrontFace"); } }
        void APIENTRY S_glHint(GLenum target, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glHint"); } }
        void APIENTRY S_glLineWidth(GLfloat width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLineWidth"); } }
        void APIENTRY S_glPointSize(GLfloat size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointSize"); } }
        void APIENTRY S_glPolygonMode(GLenum face, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPolygonMode"); } }
        void APIENTRY S_glScissor(GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glScissor"); } }
        void APIENTRY S_glTexParameterf(GLenum target, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexParameterf"); } }
        void APIENTRY S_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexParameterfv"); } }
        void APIENTRY S_glTexParameteri(GLenum target, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexParameteri"); } }
        void APIENTRY S_glTexParameteriv(GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexParameteriv"); } }
        void APIENTRY S_glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexImage1D"); } }
        void APIENTRY S_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexImage2D"); } }
        void APIENTRY S_glDrawBuffer(GLenum buf) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawBuffer"); } }
        void APIENTRY S_glClear(GLbitfield mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClear"); } }
        void APIENTRY S_glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearColor"); } }
        void APIENTRY S_glClearStencil(GLint s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearStencil"); } }
        void APIENTRY S_glClearDepth(GLdouble depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearDepth"); } }
        void APIENTRY S_glStencilMask(GLuint mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilMask"); } }
        void APIENTRY S_glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorMask"); } }
        void APIENTRY S_glDepthMask(GLboolean flag) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthMask"); } }
        void APIENTRY S_glDisable(GLenum cap) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisable"); } }
        void APIENTRY S_glEnable(GLenum cap) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnable"); } }
        void APIENTRY S_glFinish(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFinish"); } }
        void APIENTRY S_glFlush(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFlush"); } }
        void APIENTRY S_glBlendFunc(GLenum sfactor, GLenum dfactor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendFunc"); } }
        void APIENTRY S_glLogicOp(GLenum opcode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLogicOp"); } }
        void APIENTRY S_glStencilFunc(GLenum func, GLint ref, GLuint mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilFunc"); } }
        void APIENTRY S_glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilOp"); } }
        void APIENTRY S_glDepthFunc(GLenum func) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthFunc"); } }
        void APIENTRY S_glPixelStoref(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelStoref"); } }
        void APIENTRY S_glPixelStorei(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelStorei"); } }
        void APIENTRY S_glReadBuffer(GLenum src) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReadBuffer"); } }
        void APIENTRY S_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReadPixels"); } }
        void APIENTRY S_glGetBooleanv(GLenum pname, GLboolean *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBooleanv"); } }
        void APIENTRY S_glGetDoublev(GLenum pname, GLdouble *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetDoublev"); } }
        GLenum APIENTRY S_glGetError(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetError"); } return {}; }
        void APIENTRY S_glGetFloatv(GLenum pname, GLfloat *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFloatv"); } }
        void APIENTRY S_glGetIntegerv(GLenum pname, GLint *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetIntegerv"); } }
        const GLubyte * APIENTRY S_glGetString(GLenum name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetString"); } return {}; }
        void APIENTRY S_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexImage"); } }
        void APIENTRY S_glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexParameterfv"); } }
        void APIENTRY S_glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexParameteriv"); } }
        void APIENTRY S_glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexLevelParameterfv"); } }
        void APIENTRY S_glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexLevelParameteriv"); } }
        GLboolean APIENTRY S_glIsEnabled(GLenum cap) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsEnabled"); } return {}; }
        void APIENTRY S_glDepthRange(GLdouble n, GLdouble f) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthRange"); } }
        void APIENTRY S_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glViewport"); } }
        void APIENTRY S_glDrawArrays(GLenum mode, GLint first, GLsizei count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawArrays"); } }
        void APIENTRY S_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElements"); } }
        void APIENTRY S_glPolygonOffset(GLfloat factor, GLfloat units) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPolygonOffset"); } }
        void APIENTRY S_glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTexImage1D"); } }
        void APIENTRY S_glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTexImage2D"); } }
        void APIENTRY S_glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTexSubImage1D"); } }
        void APIENTRY S_glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTexSubImage2D"); } }
        void APIENTRY S_glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexSubImage1D"); } }
        void APIENTRY S_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexSubImage2D"); } }
        void APIENTRY S_glBindTexture(GLenum target, GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindTexture"); } }
        void APIENTRY S_glDeleteTextures(GLsizei n, const GLuint *textures) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteTextures"); } }
        void APIENTRY S_glGenTextures(GLsizei n, GLuint *textures) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenTextures"); } }
        GLboolean APIENTRY S_glIsTexture(GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsTexture"); } return {}; }
        void APIENTRY S_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawRangeElements"); } }
        void APIENTRY S_glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexImage3D"); } }
        void APIENTRY S_glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexSubImage3D"); } }
        void APIENTRY S_glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTexSubImage3D"); } }
        void APIENTRY S_glActiveTexture(GLenum texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glActiveTexture"); } }
        void APIENTRY S_glSampleCoverage(GLfloat value, GLboolean invert) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSampleCoverage"); } }
        void APIENTRY S_glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexImage3D"); } }
        void APIENTRY S_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexImage2D"); } }
        void APIENTRY S_glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexImage1D"); } }
        void APIENTRY S_glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexSubImage3D"); } }
        void APIENTRY S_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexSubImage2D"); } }
        void APIENTRY S_glCompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexSubImage1D"); } }
        void APIENTRY S_glGetCompressedTexImage(GLenum target, GLint level, void *img) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCompressedTexImage"); } }
        void APIENTRY S_glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendFuncSeparate"); } }
        void APIENTRY S_glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawArrays"); } }
        void APIENTRY S_glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const void *const*indices, GLsizei drawcount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawElements"); } }
        void APIENTRY S_glPointParameterf(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterf"); } }
        void APIENTRY S_glPointParameterfv(GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterfv"); } }
        void APIENTRY S_glPointParameteri(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameteri"); } }
        void APIENTRY S_glPointParameteriv(GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameteriv"); } }
        void APIENTRY S_glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendColor"); } }
        void APIENTRY S_glBlendEquation(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendEquation"); } }
        void APIENTRY S_glGenQueries(GLsizei n, GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenQueries"); } }
        void APIENTRY S_glDeleteQueries(GLsizei n, const GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteQueries"); } }
        GLboolean APIENTRY S_glIsQuery(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsQuery"); } return {}; }
        void APIENTRY S_glBeginQuery(GLenum target, GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginQuery"); } }
        void APIENTRY S_glEndQuery(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndQuery"); } }
        void APIENTRY S_glGetQueryiv(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryiv"); } }
        void APIENTRY S_glGetQueryObjectiv(GLuint id, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryObjectiv"); } }
        void APIENTRY S_glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryObjectuiv"); } }
        void APIENTRY S_glBindBuffer(GLenum target, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBuffer"); } }
        void APIENTRY S_glDeleteBuffers(GLsizei n, const GLuint *buffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteBuffers"); } }
        void APIENTRY S_glGenBuffers(GLsizei n, GLuint *buffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenBuffers"); } }
        GLboolean APIENTRY S_glIsBuffer(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsBuffer"); } return {}; }
        void APIENTRY S_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferData"); } }
        void APIENTRY S_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferSubData"); } }
        void APIENTRY S_glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBufferSubData"); } }
        void * APIENTRY S_glMapBuffer(GLenum target, GLenum access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapBuffer"); } return {}; }
        GLboolean APIENTRY S_glUnmapBuffer(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUnmapBuffer"); } return {}; }
        void APIENTRY S_glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBufferParameteriv"); } }
        void APIENTRY S_glGetBufferPointerv(GLenum target, GLenum pname, void **params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBufferPointerv"); } }
        void APIENTRY S_glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendEquationSeparate"); } }
        void APIENTRY S_glDrawBuffers(GLsizei n, const GLenum *bufs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawBuffers"); } }
        void APIENTRY S_glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilOpSeparate"); } }
        void APIENTRY S_glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilFuncSeparate"); } }
        void APIENTRY S_glStencilMaskSeparate(GLenum face, GLuint mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilMaskSeparate"); } }
        void APIENTRY S_glAttachShader(GLuint program, GLuint shader) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAttachShader"); } }
        void APIENTRY S_glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindAttribLocation"); } }
        void APIENTRY S_glCompileShader(GLuint shader) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompileShader"); } }
        GLuint APIENTRY S_glCreateProgram(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateProgram"); } return {}; }
        GLuint APIENTRY S_glCreateShader(GLenum type) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateShader"); } return {}; }
        void APIENTRY S_glDeleteProgram(GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteProgram"); } }
        void APIENTRY S_glDeleteShader(GLuint shader) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteShader"); } }
        void APIENTRY S_glDetachShader(GLuint program, GLuint shader) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDetachShader"); } }
        void APIENTRY S_glDisableVertexAttribArray(GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisableVertexAttribArray"); } }
        void APIENTRY S_glEnableVertexAttribArray(GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnableVertexAttribArray"); } }
        void APIENTRY S_glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveAttrib"); } }
        void APIENTRY S_glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveUniform"); } }
        void APIENTRY S_glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetAttachedShaders"); } }
        GLint APIENTRY S_glGetAttribLocation(GLuint program, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetAttribLocation"); } return {}; }
        void APIENTRY S_glGetProgramiv(GLuint program, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramiv"); } }
        void APIENTRY S_glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramInfoLog"); } }
        void APIENTRY S_glGetShaderiv(GLuint shader, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetShaderiv"); } }
        void APIENTRY S_glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetShaderInfoLog"); } }
        void APIENTRY S_glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetShaderSource"); } }
        GLint APIENTRY S_glGetUniformLocation(GLuint program, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformLocation"); } return {}; }
        void APIENTRY S_glGetUniformfv(GLuint program, GLint location, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformfv"); } }
        void APIENTRY S_glGetUniformiv(GLuint program, GLint location, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformiv"); } }
        void APIENTRY S_glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribdv"); } }
        void APIENTRY S_glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribfv"); } }
        void APIENTRY S_glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribiv"); } }
        void APIENTRY S_glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribPointerv"); } }
        GLboolean APIENTRY S_glIsProgram(GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsProgram"); } return {}; }
        GLboolean APIENTRY S_glIsShader(GLuint shader) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsShader"); } return {}; }
        void APIENTRY S_glLinkProgram(GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLinkProgram"); } }
        void APIENTRY S_glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShaderSource"); } }
        void APIENTRY S_glUseProgram(GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUseProgram"); } }
        void APIENTRY S_glUniform1f(GLint location, GLfloat v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1f"); } }
        void APIENTRY S_glUniform2f(GLint location, GLfloat v0, GLfloat v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2f"); } }
        void APIENTRY S_glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3f"); } }
        void APIENTRY S_glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4f"); } }
        void APIENTRY S_glUniform1i(GLint location, GLint v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1i"); } }
        void APIENTRY S_glUniform2i(GLint location, GLint v0, GLint v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2i"); } }
        void APIENTRY S_glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3i"); } }
        void APIENTRY S_glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4i"); } }
        void APIENTRY S_glUniform1fv(GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1fv"); } }
        void APIENTRY S_glUniform2fv(GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2fv"); } }
        void APIENTRY S_glUniform3fv(GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3fv"); } }
        void APIENTRY S_glUniform4fv(GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4fv"); } }
        void APIENTRY S_glUniform1iv(GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1iv"); } }
        void APIENTRY S_glUniform2iv(GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2iv"); } }
        void APIENTRY S_glUniform3iv(GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3iv"); } }
        void APIENTRY S_glUniform4iv(GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4iv"); } }
        void APIENTRY S_glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix2fv"); } }
        void APIENTRY S_glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix3fv"); } }
        void APIENTRY S_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix4fv"); } }
        void APIENTRY S_glValidateProgram(GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glValidateProgram"); } }
        void APIENTRY S_glVertexAttrib1d(GLuint index, GLdouble x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1d"); } }
        void APIENTRY S_glVertexAttrib1dv(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1dv"); } }
        void APIENTRY S_glVertexAttrib1f(GLuint index, GLfloat x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1f"); } }
        void APIENTRY S_glVertexAttrib1fv(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1fv"); } }
        void APIENTRY S_glVertexAttrib1s(GLuint index, GLshort x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1s"); } }
        void APIENTRY S_glVertexAttrib1sv(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1sv"); } }
        void APIENTRY S_glVertexAttrib2d(GLuint index, GLdouble x, GLdouble y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2d"); } }
        void APIENTRY S_glVertexAttrib2dv(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2dv"); } }
        void APIENTRY S_glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2f"); } }
        void APIENTRY S_glVertexAttrib2fv(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2fv"); } }
        void APIENTRY S_glVertexAttrib2s(GLuint index, GLshort x, GLshort y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2s"); } }
        void APIENTRY S_glVertexAttrib2sv(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2sv"); } }
        void APIENTRY S_glVertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3d"); } }
        void APIENTRY S_glVertexAttrib3dv(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3dv"); } }
        void APIENTRY S_glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3f"); } }
        void APIENTRY S_glVertexAttrib3fv(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3fv"); } }
        void APIENTRY S_glVertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3s"); } }
        void APIENTRY S_glVertexAttrib3sv(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3sv"); } }
        void APIENTRY S_glVertexAttrib4Nbv(GLuint index, const GLbyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4Nbv"); } }
        void APIENTRY S_glVertexAttrib4Niv(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4Niv"); } }
        void APIENTRY S_glVertexAttrib4Nsv(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4Nsv"); } }
        void APIENTRY S_glVertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4Nub"); } }
        void APIENTRY S_glVertexAttrib4Nubv(GLuint index, const GLubyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4Nubv"); } }
        void APIENTRY S_glVertexAttrib4Nuiv(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4Nuiv"); } }
        void APIENTRY S_glVertexAttrib4Nusv(GLuint index, const GLushort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4Nusv"); } }
        void APIENTRY S_glVertexAttrib4bv(GLuint index, const GLbyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4bv"); } }
        void APIENTRY S_glVertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4d"); } }
        void APIENTRY S_glVertexAttrib4dv(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4dv"); } }
        void APIENTRY S_glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4f"); } }
        void APIENTRY S_glVertexAttrib4fv(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4fv"); } }
        void APIENTRY S_glVertexAttrib4iv(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4iv"); } }
        void APIENTRY S_glVertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4s"); } }
        void APIENTRY S_glVertexAttrib4sv(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4sv"); } }
        void APIENTRY S_glVertexAttrib4ubv(GLuint index, const GLubyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4ubv"); } }
        void APIENTRY S_glVertexAttrib4uiv(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4uiv"); } }
        void APIENTRY S_glVertexAttrib4usv(GLuint index, const GLushort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4usv"); } }
        void APIENTRY S_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribPointer"); } }
        void APIENTRY S_glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix2x3fv"); } }
        void APIENTRY S_glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix3x2fv"); } }
        void APIENTRY S_glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix2x4fv"); } }
        void APIENTRY S_glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix4x2fv"); } }
        void APIENTRY S_glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix3x4fv"); } }
        void APIENTRY S_glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix4x3fv"); } }
        void APIENTRY S_glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorMaski"); } }
        void APIENTRY S_glGetBooleani_v(GLenum target, GLuint index, GLboolean *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBooleani_v"); } }
        void APIENTRY S_glGetIntegeri_v(GLenum target, GLuint index, GLint *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetIntegeri_v"); } }
        void APIENTRY S_glEnablei(GLenum target, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnablei"); } }
        void APIENTRY S_glDisablei(GLenum target, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisablei"); } }
        GLboolean APIENTRY S_glIsEnabledi(GLenum target, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsEnabledi"); } return {}; }
        void APIENTRY S_glBeginTransformFeedback(GLenum primitiveMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginTransformFeedback"); } }
        void APIENTRY S_glEndTransformFeedback(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndTransformFeedback"); } }
        void APIENTRY S_glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBufferRange"); } }
        void APIENTRY S_glBindBufferBase(GLenum target, GLuint index, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBufferBase"); } }
        void APIENTRY S_glTransformFeedbackVaryings(GLuint program, GLsizei count, const GLchar *const*varyings, GLenum bufferMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTransformFeedbackVaryings"); } }
        void APIENTRY S_glGetTransformFeedbackVarying(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLsizei *size, GLenum *type, GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTransformFeedbackVarying"); } }
        void APIENTRY S_glClampColor(GLenum target, GLenum clamp) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClampColor"); } }
        void APIENTRY S_glBeginConditionalRender(GLuint id, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginConditionalRender"); } }
        void APIENTRY S_glEndConditionalRender(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndConditionalRender"); } }
        void APIENTRY S_glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribIPointer"); } }
        void APIENTRY S_glGetVertexAttribIiv(GLuint index, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribIiv"); } }
        void APIENTRY S_glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribIuiv"); } }
        void APIENTRY S_glVertexAttribI1i(GLuint index, GLint x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI1i"); } }
        void APIENTRY S_glVertexAttribI2i(GLuint index, GLint x, GLint y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI2i"); } }
        void APIENTRY S_glVertexAttribI3i(GLuint index, GLint x, GLint y, GLint z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI3i"); } }
        void APIENTRY S_glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4i"); } }
        void APIENTRY S_glVertexAttribI1ui(GLuint index, GLuint x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI1ui"); } }
        void APIENTRY S_glVertexAttribI2ui(GLuint index, GLuint x, GLuint y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI2ui"); } }
        void APIENTRY S_glVertexAttribI3ui(GLuint index, GLuint x, GLuint y, GLuint z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI3ui"); } }
        void APIENTRY S_glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4ui"); } }
        void APIENTRY S_glVertexAttribI1iv(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI1iv"); } }
        void APIENTRY S_glVertexAttribI2iv(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI2iv"); } }
        void APIENTRY S_glVertexAttribI3iv(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI3iv"); } }
        void APIENTRY S_glVertexAttribI4iv(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4iv"); } }
        void APIENTRY S_glVertexAttribI1uiv(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI1uiv"); } }
        void APIENTRY S_glVertexAttribI2uiv(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI2uiv"); } }
        void APIENTRY S_glVertexAttribI3uiv(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI3uiv"); } }
        void APIENTRY S_glVertexAttribI4uiv(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4uiv"); } }
        void APIENTRY S_glVertexAttribI4bv(GLuint index, const GLbyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4bv"); } }
        void APIENTRY S_glVertexAttribI4sv(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4sv"); } }
        void APIENTRY S_glVertexAttribI4ubv(GLuint index, const GLubyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4ubv"); } }
        void APIENTRY S_glVertexAttribI4usv(GLuint index, const GLushort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4usv"); } }
        void APIENTRY S_glGetUniformuiv(GLuint program, GLint location, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformuiv"); } }
        void APIENTRY S_glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindFragDataLocation"); } }
        GLint APIENTRY S_glGetFragDataLocation(GLuint program, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFragDataLocation"); } return {}; }
        void APIENTRY S_glUniform1ui(GLint location, GLuint v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1ui"); } }
        void APIENTRY S_glUniform2ui(GLint location, GLuint v0, GLuint v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2ui"); } }
        void APIENTRY S_glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3ui"); } }
        void APIENTRY S_glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4ui"); } }
        void APIENTRY S_glUniform1uiv(GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1uiv"); } }
        void APIENTRY S_glUniform2uiv(GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2uiv"); } }
        void APIENTRY S_glUniform3uiv(GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3uiv"); } }
        void APIENTRY S_glUniform4uiv(GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4uiv"); } }
        void APIENTRY S_glTexParameterIiv(GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexParameterIiv"); } }
        void APIENTRY S_glTexParameterIuiv(GLenum target, GLenum pname, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexParameterIuiv"); } }
        void APIENTRY S_glGetTexParameterIiv(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexParameterIiv"); } }
        void APIENTRY S_glGetTexParameterIuiv(GLenum target, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexParameterIuiv"); } }
        void APIENTRY S_glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearBufferiv"); } }
        void APIENTRY S_glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearBufferuiv"); } }
        void APIENTRY S_glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearBufferfv"); } }
        void APIENTRY S_glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearBufferfi"); } }
        const GLubyte * APIENTRY S_glGetStringi(GLenum name, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetStringi"); } return {}; }
        GLboolean APIENTRY S_glIsRenderbuffer(GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsRenderbuffer"); } return {}; }
        void APIENTRY S_glBindRenderbuffer(GLenum target, GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindRenderbuffer"); } }
        void APIENTRY S_glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteRenderbuffers"); } }
        void APIENTRY S_glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenRenderbuffers"); } }
        void APIENTRY S_glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRenderbufferStorage"); } }
        void APIENTRY S_glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetRenderbufferParameteriv"); } }
        GLboolean APIENTRY S_glIsFramebuffer(GLuint framebuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsFramebuffer"); } return {}; }
        void APIENTRY S_glBindFramebuffer(GLenum target, GLuint framebuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindFramebuffer"); } }
        void APIENTRY S_glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteFramebuffers"); } }
        void APIENTRY S_glGenFramebuffers(GLsizei n, GLuint *framebuffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenFramebuffers"); } }
        GLenum APIENTRY S_glCheckFramebufferStatus(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCheckFramebufferStatus"); } return {}; }
        void APIENTRY S_glFramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTexture1D"); } }
        void APIENTRY S_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTexture2D"); } }
        void APIENTRY S_glFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTexture3D"); } }
        void APIENTRY S_glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferRenderbuffer"); } }
        void APIENTRY S_glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFramebufferAttachmentParameteriv"); } }
        void APIENTRY S_glGenerateMipmap(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenerateMipmap"); } }
        void APIENTRY S_glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlitFramebuffer"); } }
        void APIENTRY S_glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRenderbufferStorageMultisample"); } }
        void APIENTRY S_glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTextureLayer"); } }
        void * APIENTRY S_glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapBufferRange"); } return {}; }
        void APIENTRY S_glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFlushMappedBufferRange"); } }
        void APIENTRY S_glBindVertexArray(GLuint array) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindVertexArray"); } }
        void APIENTRY S_glDeleteVertexArrays(GLsizei n, const GLuint *arrays) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteVertexArrays"); } }
        void APIENTRY S_glGenVertexArrays(GLsizei n, GLuint *arrays) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenVertexArrays"); } }
        GLboolean APIENTRY S_glIsVertexArray(GLuint array) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsVertexArray"); } return {}; }
        void APIENTRY S_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawArraysInstanced"); } }
        void APIENTRY S_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElementsInstanced"); } }
        void APIENTRY S_glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexBuffer"); } }
        void APIENTRY S_glPrimitiveRestartIndex(GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPrimitiveRestartIndex"); } }
        void APIENTRY S_glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyBufferSubData"); } }
        void APIENTRY S_glGetUniformIndices(GLuint program, GLsizei uniformCount, const GLchar *const*uniformNames, GLuint *uniformIndices) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformIndices"); } }
        void APIENTRY S_glGetActiveUniformsiv(GLuint program, GLsizei uniformCount, const GLuint *uniformIndices, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveUniformsiv"); } }
        void APIENTRY S_glGetActiveUniformName(GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformName) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveUniformName"); } }
        GLuint APIENTRY S_glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformBlockIndex"); } return {}; }
        void APIENTRY S_glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveUniformBlockiv"); } }
        void APIENTRY S_glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformBlockName) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveUniformBlockName"); } }
        void APIENTRY S_glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformBlockBinding"); } }
        void APIENTRY S_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElementsBaseVertex"); } }
        void APIENTRY S_glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint basevertex) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawRangeElementsBaseVertex"); } }
        void APIENTRY S_glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount, GLint basevertex) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElementsInstancedBaseVertex"); } }
        void APIENTRY S_glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type, const void *const*indices, GLsizei drawcount, const GLint *basevertex) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawElementsBaseVertex"); } }
        void APIENTRY S_glProvokingVertex(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProvokingVertex"); } }
        GLsync APIENTRY S_glFenceSync(GLenum condition, GLbitfield flags) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFenceSync"); } return {}; }
        GLboolean APIENTRY S_glIsSync(GLsync sync) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsSync"); } return {}; }
        void APIENTRY S_glDeleteSync(GLsync sync) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteSync"); } }
        GLenum APIENTRY S_glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClientWaitSync"); } return {}; }
        void APIENTRY S_glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWaitSync"); } }
        void APIENTRY S_glGetInteger64v(GLenum pname, GLint64 *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetInteger64v"); } }
        void APIENTRY S_glGetSynciv(GLsync sync, GLenum pname, GLsizei count, GLsizei *length, GLint *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSynciv"); } }
        void APIENTRY S_glGetInteger64i_v(GLenum target, GLuint index, GLint64 *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetInteger64i_v"); } }
        void APIENTRY S_glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBufferParameteri64v"); } }
        void APIENTRY S_glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTexture"); } }
        void APIENTRY S_glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexImage2DMultisample"); } }
        void APIENTRY S_glTexImage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexImage3DMultisample"); } }
        void APIENTRY S_glGetMultisamplefv(GLenum pname, GLuint index, GLfloat *val) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultisamplefv"); } }
        void APIENTRY S_glSampleMaski(GLuint maskNumber, GLbitfield mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSampleMaski"); } }
        void APIENTRY S_glBindFragDataLocationIndexed(GLuint program, GLuint colorNumber, GLuint index, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindFragDataLocationIndexed"); } }
        GLint APIENTRY S_glGetFragDataIndex(GLuint program, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFragDataIndex"); } return {}; }
        void APIENTRY S_glGenSamplers(GLsizei count, GLuint *samplers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenSamplers"); } }
        void APIENTRY S_glDeleteSamplers(GLsizei count, const GLuint *samplers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteSamplers"); } }
        GLboolean APIENTRY S_glIsSampler(GLuint sampler) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsSampler"); } return {}; }
        void APIENTRY S_glBindSampler(GLuint unit, GLuint sampler) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindSampler"); } }
        void APIENTRY S_glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSamplerParameteri"); } }
        void APIENTRY S_glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSamplerParameteriv"); } }
        void APIENTRY S_glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSamplerParameterf"); } }
        void APIENTRY S_glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSamplerParameterfv"); } }
        void APIENTRY S_glSamplerParameterIiv(GLuint sampler, GLenum pname, const GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSamplerParameterIiv"); } }
        void APIENTRY S_glSamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSamplerParameterIuiv"); } }
        void APIENTRY S_glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSamplerParameteriv"); } }
        void APIENTRY S_glGetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSamplerParameterIiv"); } }
        void APIENTRY S_glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSamplerParameterfv"); } }
        void APIENTRY S_glGetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSamplerParameterIuiv"); } }
        void APIENTRY S_glQueryCounter(GLuint id, GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glQueryCounter"); } }
        void APIENTRY S_glGetQueryObjecti64v(GLuint id, GLenum pname, GLint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryObjecti64v"); } }
        void APIENTRY S_glGetQueryObjectui64v(GLuint id, GLenum pname, GLuint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryObjectui64v"); } }
        void APIENTRY S_glVertexAttribDivisor(GLuint index, GLuint divisor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribDivisor"); } }
        void APIENTRY S_glVertexAttribP1ui(GLuint index, GLenum type, GLboolean normalized, GLuint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribP1ui"); } }
        void APIENTRY S_glVertexAttribP1uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribP1uiv"); } }
        void APIENTRY S_glVertexAttribP2ui(GLuint index, GLenum type, GLboolean normalized, GLuint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribP2ui"); } }
        void APIENTRY S_glVertexAttribP2uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribP2uiv"); } }
        void APIENTRY S_glVertexAttribP3ui(GLuint index, GLenum type, GLboolean normalized, GLuint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribP3ui"); } }
        void APIENTRY S_glVertexAttribP3uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribP3uiv"); } }
        void APIENTRY S_glVertexAttribP4ui(GLuint index, GLenum type, GLboolean normalized, GLuint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribP4ui"); } }
        void APIENTRY S_glVertexAttribP4uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribP4uiv"); } }
        void APIENTRY S_glVertexP2ui(GLenum type, GLuint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexP2ui"); } }
        void APIENTRY S_glVertexP2uiv(GLenum type, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexP2uiv"); } }
        void APIENTRY S_glVertexP3ui(GLenum type, GLuint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexP3ui"); } }
        void APIENTRY S_glVertexP3uiv(GLenum type, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexP3uiv"); } }
        void APIENTRY S_glVertexP4ui(GLenum type, GLuint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexP4ui"); } }
        void APIENTRY S_glVertexP4uiv(GLenum type, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexP4uiv"); } }
        void APIENTRY S_glTexCoordP1ui(GLenum type, GLuint coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordP1ui"); } }
        void APIENTRY S_glTexCoordP1uiv(GLenum type, const GLuint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordP1uiv"); } }
        void APIENTRY S_glTexCoordP2ui(GLenum type, GLuint coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordP2ui"); } }
        void APIENTRY S_glTexCoordP2uiv(GLenum type, const GLuint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordP2uiv"); } }
        void APIENTRY S_glTexCoordP3ui(GLenum type, GLuint coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordP3ui"); } }
        void APIENTRY S_glTexCoordP3uiv(GLenum type, const GLuint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordP3uiv"); } }
        void APIENTRY S_glTexCoordP4ui(GLenum type, GLuint coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordP4ui"); } }
        void APIENTRY S_glTexCoordP4uiv(GLenum type, const GLuint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordP4uiv"); } }
        void APIENTRY S_glMultiTexCoordP1ui(GLenum texture, GLenum type, GLuint coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoordP1ui"); } }
        void APIENTRY S_glMultiTexCoordP1uiv(GLenum texture, GLenum type, const GLuint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoordP1uiv"); } }
        void APIENTRY S_glMultiTexCoordP2ui(GLenum texture, GLenum type, GLuint coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoordP2ui"); } }
        void APIENTRY S_glMultiTexCoordP2uiv(GLenum texture, GLenum type, const GLuint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoordP2uiv"); } }
        void APIENTRY S_glMultiTexCoordP3ui(GLenum texture, GLenum type, GLuint coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoordP3ui"); } }
        void APIENTRY S_glMultiTexCoordP3uiv(GLenum texture, GLenum type, const GLuint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoordP3uiv"); } }
        void APIENTRY S_glMultiTexCoordP4ui(GLenum texture, GLenum type, GLuint coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoordP4ui"); } }
        void APIENTRY S_glMultiTexCoordP4uiv(GLenum texture, GLenum type, const GLuint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoordP4uiv"); } }
        void APIENTRY S_glNormalP3ui(GLenum type, GLuint coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalP3ui"); } }
        void APIENTRY S_glNormalP3uiv(GLenum type, const GLuint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalP3uiv"); } }
        void APIENTRY S_glColorP3ui(GLenum type, GLuint color) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorP3ui"); } }
        void APIENTRY S_glColorP3uiv(GLenum type, const GLuint *color) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorP3uiv"); } }
        void APIENTRY S_glColorP4ui(GLenum type, GLuint color) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorP4ui"); } }
        void APIENTRY S_glColorP4uiv(GLenum type, const GLuint *color) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorP4uiv"); } }
        void APIENTRY S_glSecondaryColorP3ui(GLenum type, GLuint color) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColorP3ui"); } }
        void APIENTRY S_glSecondaryColorP3uiv(GLenum type, const GLuint *color) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColorP3uiv"); } }
        void APIENTRY S_glMinSampleShading(GLfloat value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMinSampleShading"); } }
        void APIENTRY S_glBlendEquationi(GLuint buf, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendEquationi"); } }
        void APIENTRY S_glBlendEquationSeparatei(GLuint buf, GLenum modeRGB, GLenum modeAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendEquationSeparatei"); } }
        void APIENTRY S_glBlendFunci(GLuint buf, GLenum src, GLenum dst) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendFunci"); } }
        void APIENTRY S_glBlendFuncSeparatei(GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendFuncSeparatei"); } }
        void APIENTRY S_glDrawArraysIndirect(GLenum mode, const void *indirect) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawArraysIndirect"); } }
        void APIENTRY S_glDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElementsIndirect"); } }
        void APIENTRY S_glUniform1d(GLint location, GLdouble x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1d"); } }
        void APIENTRY S_glUniform2d(GLint location, GLdouble x, GLdouble y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2d"); } }
        void APIENTRY S_glUniform3d(GLint location, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3d"); } }
        void APIENTRY S_glUniform4d(GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4d"); } }
        void APIENTRY S_glUniform1dv(GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1dv"); } }
        void APIENTRY S_glUniform2dv(GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2dv"); } }
        void APIENTRY S_glUniform3dv(GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3dv"); } }
        void APIENTRY S_glUniform4dv(GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4dv"); } }
        void APIENTRY S_glUniformMatrix2dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix2dv"); } }
        void APIENTRY S_glUniformMatrix3dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix3dv"); } }
        void APIENTRY S_glUniformMatrix4dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix4dv"); } }
        void APIENTRY S_glUniformMatrix2x3dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix2x3dv"); } }
        void APIENTRY S_glUniformMatrix2x4dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix2x4dv"); } }
        void APIENTRY S_glUniformMatrix3x2dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix3x2dv"); } }
        void APIENTRY S_glUniformMatrix3x4dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix3x4dv"); } }
        void APIENTRY S_glUniformMatrix4x2dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix4x2dv"); } }
        void APIENTRY S_glUniformMatrix4x3dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix4x3dv"); } }
        void APIENTRY S_glGetUniformdv(GLuint program, GLint location, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformdv"); } }
        GLint APIENTRY S_glGetSubroutineUniformLocation(GLuint program, GLenum shadertype, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSubroutineUniformLocation"); } return {}; }
        GLuint APIENTRY S_glGetSubroutineIndex(GLuint program, GLenum shadertype, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSubroutineIndex"); } return {}; }
        void APIENTRY S_glGetActiveSubroutineUniformiv(GLuint program, GLenum shadertype, GLuint index, GLenum pname, GLint *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveSubroutineUniformiv"); } }
        void APIENTRY S_glGetActiveSubroutineUniformName(GLuint program, GLenum shadertype, GLuint index, GLsizei bufSize, GLsizei *length, GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveSubroutineUniformName"); } }
        void APIENTRY S_glGetActiveSubroutineName(GLuint program, GLenum shadertype, GLuint index, GLsizei bufSize, GLsizei *length, GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveSubroutineName"); } }
        void APIENTRY S_glUniformSubroutinesuiv(GLenum shadertype, GLsizei count, const GLuint *indices) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformSubroutinesuiv"); } }
        void APIENTRY S_glGetUniformSubroutineuiv(GLenum shadertype, GLint location, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformSubroutineuiv"); } }
        void APIENTRY S_glGetProgramStageiv(GLuint program, GLenum shadertype, GLenum pname, GLint *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramStageiv"); } }
        void APIENTRY S_glPatchParameteri(GLenum pname, GLint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPatchParameteri"); } }
        void APIENTRY S_glPatchParameterfv(GLenum pname, const GLfloat *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPatchParameterfv"); } }
        void APIENTRY S_glBindTransformFeedback(GLenum target, GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindTransformFeedback"); } }
        void APIENTRY S_glDeleteTransformFeedbacks(GLsizei n, const GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteTransformFeedbacks"); } }
        void APIENTRY S_glGenTransformFeedbacks(GLsizei n, GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenTransformFeedbacks"); } }
        GLboolean APIENTRY S_glIsTransformFeedback(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsTransformFeedback"); } return {}; }
        void APIENTRY S_glPauseTransformFeedback(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPauseTransformFeedback"); } }
        void APIENTRY S_glResumeTransformFeedback(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glResumeTransformFeedback"); } }
        void APIENTRY S_glDrawTransformFeedback(GLenum mode, GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawTransformFeedback"); } }
        void APIENTRY S_glDrawTransformFeedbackStream(GLenum mode, GLuint id, GLuint stream) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawTransformFeedbackStream"); } }
        void APIENTRY S_glBeginQueryIndexed(GLenum target, GLuint index, GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginQueryIndexed"); } }
        void APIENTRY S_glEndQueryIndexed(GLenum target, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndQueryIndexed"); } }
        void APIENTRY S_glGetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryIndexediv"); } }
        void APIENTRY S_glReleaseShaderCompiler(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReleaseShaderCompiler"); } }
        void APIENTRY S_glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat, const void *binary, GLsizei length) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShaderBinary"); } }
        void APIENTRY S_glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetShaderPrecisionFormat"); } }
        void APIENTRY S_glDepthRangef(GLfloat n, GLfloat f) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthRangef"); } }
        void APIENTRY S_glClearDepthf(GLfloat d) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearDepthf"); } }
        void APIENTRY S_glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length, GLenum *binaryFormat, void *binary) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramBinary"); } }
        void APIENTRY S_glProgramBinary(GLuint program, GLenum binaryFormat, const void *binary, GLsizei length) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramBinary"); } }
        void APIENTRY S_glProgramParameteri(GLuint program, GLenum pname, GLint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramParameteri"); } }
        void APIENTRY S_glUseProgramStages(GLuint pipeline, GLbitfield stages, GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUseProgramStages"); } }
        void APIENTRY S_glActiveShaderProgram(GLuint pipeline, GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glActiveShaderProgram"); } }
        GLuint APIENTRY S_glCreateShaderProgramv(GLenum type, GLsizei count, const GLchar *const*strings) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateShaderProgramv"); } return {}; }
        void APIENTRY S_glBindProgramPipeline(GLuint pipeline) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindProgramPipeline"); } }
        void APIENTRY S_glDeleteProgramPipelines(GLsizei n, const GLuint *pipelines) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteProgramPipelines"); } }
        void APIENTRY S_glGenProgramPipelines(GLsizei n, GLuint *pipelines) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenProgramPipelines"); } }
        GLboolean APIENTRY S_glIsProgramPipeline(GLuint pipeline) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsProgramPipeline"); } return {}; }
        void APIENTRY S_glGetProgramPipelineiv(GLuint pipeline, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramPipelineiv"); } }
        void APIENTRY S_glProgramUniform1i(GLuint program, GLint location, GLint v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1i"); } }
        void APIENTRY S_glProgramUniform1iv(GLuint program, GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1iv"); } }
        void APIENTRY S_glProgramUniform1f(GLuint program, GLint location, GLfloat v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1f"); } }
        void APIENTRY S_glProgramUniform1fv(GLuint program, GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1fv"); } }
        void APIENTRY S_glProgramUniform1d(GLuint program, GLint location, GLdouble v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1d"); } }
        void APIENTRY S_glProgramUniform1dv(GLuint program, GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1dv"); } }
        void APIENTRY S_glProgramUniform1ui(GLuint program, GLint location, GLuint v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1ui"); } }
        void APIENTRY S_glProgramUniform1uiv(GLuint program, GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1uiv"); } }
        void APIENTRY S_glProgramUniform2i(GLuint program, GLint location, GLint v0, GLint v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2i"); } }
        void APIENTRY S_glProgramUniform2iv(GLuint program, GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2iv"); } }
        void APIENTRY S_glProgramUniform2f(GLuint program, GLint location, GLfloat v0, GLfloat v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2f"); } }
        void APIENTRY S_glProgramUniform2fv(GLuint program, GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2fv"); } }
        void APIENTRY S_glProgramUniform2d(GLuint program, GLint location, GLdouble v0, GLdouble v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2d"); } }
        void APIENTRY S_glProgramUniform2dv(GLuint program, GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2dv"); } }
        void APIENTRY S_glProgramUniform2ui(GLuint program, GLint location, GLuint v0, GLuint v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2ui"); } }
        void APIENTRY S_glProgramUniform2uiv(GLuint program, GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2uiv"); } }
        void APIENTRY S_glProgramUniform3i(GLuint program, GLint location, GLint v0, GLint v1, GLint v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3i"); } }
        void APIENTRY S_glProgramUniform3iv(GLuint program, GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3iv"); } }
        void APIENTRY S_glProgramUniform3f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3f"); } }
        void APIENTRY S_glProgramUniform3fv(GLuint program, GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3fv"); } }
        void APIENTRY S_glProgramUniform3d(GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3d"); } }
        void APIENTRY S_glProgramUniform3dv(GLuint program, GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3dv"); } }
        void APIENTRY S_glProgramUniform3ui(GLuint program, GLint location, GLuint v0, GLuint v1, GLuint v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3ui"); } }
        void APIENTRY S_glProgramUniform3uiv(GLuint program, GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3uiv"); } }
        void APIENTRY S_glProgramUniform4i(GLuint program, GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4i"); } }
        void APIENTRY S_glProgramUniform4iv(GLuint program, GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4iv"); } }
        void APIENTRY S_glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4f"); } }
        void APIENTRY S_glProgramUniform4fv(GLuint program, GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4fv"); } }
        void APIENTRY S_glProgramUniform4d(GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2, GLdouble v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4d"); } }
        void APIENTRY S_glProgramUniform4dv(GLuint program, GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4dv"); } }
        void APIENTRY S_glProgramUniform4ui(GLuint program, GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4ui"); } }
        void APIENTRY S_glProgramUniform4uiv(GLuint program, GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4uiv"); } }
        void APIENTRY S_glProgramUniformMatrix2fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2fv"); } }
        void APIENTRY S_glProgramUniformMatrix3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3fv"); } }
        void APIENTRY S_glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4fv"); } }
        void APIENTRY S_glProgramUniformMatrix2dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2dv"); } }
        void APIENTRY S_glProgramUniformMatrix3dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3dv"); } }
        void APIENTRY S_glProgramUniformMatrix4dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4dv"); } }
        void APIENTRY S_glProgramUniformMatrix2x3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2x3fv"); } }
        void APIENTRY S_glProgramUniformMatrix3x2fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3x2fv"); } }
        void APIENTRY S_glProgramUniformMatrix2x4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2x4fv"); } }
        void APIENTRY S_glProgramUniformMatrix4x2fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4x2fv"); } }
        void APIENTRY S_glProgramUniformMatrix3x4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3x4fv"); } }
        void APIENTRY S_glProgramUniformMatrix4x3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4x3fv"); } }
        void APIENTRY S_glProgramUniformMatrix2x3dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2x3dv"); } }
        void APIENTRY S_glProgramUniformMatrix3x2dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3x2dv"); } }
        void APIENTRY S_glProgramUniformMatrix2x4dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2x4dv"); } }
        void APIENTRY S_glProgramUniformMatrix4x2dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4x2dv"); } }
        void APIENTRY S_glProgramUniformMatrix3x4dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3x4dv"); } }
        void APIENTRY S_glProgramUniformMatrix4x3dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4x3dv"); } }
        void APIENTRY S_glValidateProgramPipeline(GLuint pipeline) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glValidateProgramPipeline"); } }
        void APIENTRY S_glGetProgramPipelineInfoLog(GLuint pipeline, GLsizei bufSize, GLsizei *length, GLchar *infoLog) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramPipelineInfoLog"); } }
        void APIENTRY S_glVertexAttribL1d(GLuint index, GLdouble x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL1d"); } }
        void APIENTRY S_glVertexAttribL2d(GLuint index, GLdouble x, GLdouble y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL2d"); } }
        void APIENTRY S_glVertexAttribL3d(GLuint index, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL3d"); } }
        void APIENTRY S_glVertexAttribL4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL4d"); } }
        void APIENTRY S_glVertexAttribL1dv(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL1dv"); } }
        void APIENTRY S_glVertexAttribL2dv(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL2dv"); } }
        void APIENTRY S_glVertexAttribL3dv(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL3dv"); } }
        void APIENTRY S_glVertexAttribL4dv(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL4dv"); } }
        void APIENTRY S_glVertexAttribLPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribLPointer"); } }
        void APIENTRY S_glGetVertexAttribLdv(GLuint index, GLenum pname, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribLdv"); } }
        void APIENTRY S_glViewportArrayv(GLuint first, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glViewportArrayv"); } }
        void APIENTRY S_glViewportIndexedf(GLuint index, GLfloat x, GLfloat y, GLfloat w, GLfloat h) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glViewportIndexedf"); } }
        void APIENTRY S_glViewportIndexedfv(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glViewportIndexedfv"); } }
        void APIENTRY S_glScissorArrayv(GLuint first, GLsizei count, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glScissorArrayv"); } }
        void APIENTRY S_glScissorIndexed(GLuint index, GLint left, GLint bottom, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glScissorIndexed"); } }
        void APIENTRY S_glScissorIndexedv(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glScissorIndexedv"); } }
        void APIENTRY S_glDepthRangeArrayv(GLuint first, GLsizei count, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthRangeArrayv"); } }
        void APIENTRY S_glDepthRangeIndexed(GLuint index, GLdouble n, GLdouble f) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthRangeIndexed"); } }
        void APIENTRY S_glGetFloati_v(GLenum target, GLuint index, GLfloat *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFloati_v"); } }
        void APIENTRY S_glGetDoublei_v(GLenum target, GLuint index, GLdouble *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetDoublei_v"); } }
        void APIENTRY S_glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei instancecount, GLuint baseinstance) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawArraysInstancedBaseInstance"); } }
        void APIENTRY S_glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount, GLuint baseinstance) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElementsInstancedBaseInstance"); } }
        void APIENTRY S_glDrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount, GLint basevertex, GLuint baseinstance) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElementsInstancedBaseVertexBaseInstance"); } }
        void APIENTRY S_glGetInternalformativ(GLenum target, GLenum internalformat, GLenum pname, GLsizei count, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetInternalformativ"); } }
        void APIENTRY S_glGetActiveAtomicCounterBufferiv(GLuint program, GLuint bufferIndex, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveAtomicCounterBufferiv"); } }
        void APIENTRY S_glBindImageTexture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindImageTexture"); } }
        void APIENTRY S_glMemoryBarrier(GLbitfield barriers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMemoryBarrier"); } }
        void APIENTRY S_glTexStorage1D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorage1D"); } }
        void APIENTRY S_glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorage2D"); } }
        void APIENTRY S_glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorage3D"); } }
        void APIENTRY S_glDrawTransformFeedbackInstanced(GLenum mode, GLuint id, GLsizei instancecount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawTransformFeedbackInstanced"); } }
        void APIENTRY S_glDrawTransformFeedbackStreamInstanced(GLenum mode, GLuint id, GLuint stream, GLsizei instancecount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawTransformFeedbackStreamInstanced"); } }
        void APIENTRY S_glClearBufferData(GLenum target, GLenum internalformat, GLenum format, GLenum type, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearBufferData"); } }
        void APIENTRY S_glClearBufferSubData(GLenum target, GLenum internalformat, GLintptr offset, GLsizeiptr size, GLenum format, GLenum type, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearBufferSubData"); } }
        void APIENTRY S_glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDispatchCompute"); } }
        void APIENTRY S_glDispatchComputeIndirect(GLintptr indirect) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDispatchComputeIndirect"); } }
        void APIENTRY S_glCopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyImageSubData"); } }
        void APIENTRY S_glFramebufferParameteri(GLenum target, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferParameteri"); } }
        void APIENTRY S_glGetFramebufferParameteriv(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFramebufferParameteriv"); } }
        void APIENTRY S_glGetInternalformati64v(GLenum target, GLenum internalformat, GLenum pname, GLsizei count, GLint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetInternalformati64v"); } }
        void APIENTRY S_glInvalidateTexSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInvalidateTexSubImage"); } }
        void APIENTRY S_glInvalidateTexImage(GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInvalidateTexImage"); } }
        void APIENTRY S_glInvalidateBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr length) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInvalidateBufferSubData"); } }
        void APIENTRY S_glInvalidateBufferData(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInvalidateBufferData"); } }
        void APIENTRY S_glInvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInvalidateFramebuffer"); } }
        void APIENTRY S_glInvalidateSubFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInvalidateSubFramebuffer"); } }
        void APIENTRY S_glMultiDrawArraysIndirect(GLenum mode, const void *indirect, GLsizei drawcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawArraysIndirect"); } }
        void APIENTRY S_glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect, GLsizei drawcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawElementsIndirect"); } }
        void APIENTRY S_glGetProgramInterfaceiv(GLuint program, GLenum programInterface, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramInterfaceiv"); } }
        GLuint APIENTRY S_glGetProgramResourceIndex(GLuint program, GLenum programInterface, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramResourceIndex"); } return {}; }
        void APIENTRY S_glGetProgramResourceName(GLuint program, GLenum programInterface, GLuint index, GLsizei bufSize, GLsizei *length, GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramResourceName"); } }
        void APIENTRY S_glGetProgramResourceiv(GLuint program, GLenum programInterface, GLuint index, GLsizei propCount, const GLenum *props, GLsizei count, GLsizei *length, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramResourceiv"); } }
        GLint APIENTRY S_glGetProgramResourceLocation(GLuint program, GLenum programInterface, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramResourceLocation"); } return {}; }
        GLint APIENTRY S_glGetProgramResourceLocationIndex(GLuint program, GLenum programInterface, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramResourceLocationIndex"); } return {}; }
        void APIENTRY S_glShaderStorageBlockBinding(GLuint program, GLuint storageBlockIndex, GLuint storageBlockBinding) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShaderStorageBlockBinding"); } }
        void APIENTRY S_glTexBufferRange(GLenum target, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexBufferRange"); } }
        void APIENTRY S_glTexStorage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorage2DMultisample"); } }
        void APIENTRY S_glTexStorage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorage3DMultisample"); } }
        void APIENTRY S_glTextureView(GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel, GLuint numlevels, GLuint minlayer, GLuint numlayers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureView"); } }
        void APIENTRY S_glBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindVertexBuffer"); } }
        void APIENTRY S_glVertexAttribFormat(GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribFormat"); } }
        void APIENTRY S_glVertexAttribIFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribIFormat"); } }
        void APIENTRY S_glVertexAttribLFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribLFormat"); } }
        void APIENTRY S_glVertexAttribBinding(GLuint attribindex, GLuint bindingindex) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribBinding"); } }
        void APIENTRY S_glVertexBindingDivisor(GLuint bindingindex, GLuint divisor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexBindingDivisor"); } }
        void APIENTRY S_glDebugMessageControl(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageControl"); } }
        void APIENTRY S_glDebugMessageInsert(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *buf) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageInsert"); } }
        void APIENTRY S_glDebugMessageCallback(GLDEBUGPROC callback, const void *userParam) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageCallback"); } }
        GLuint APIENTRY S_glGetDebugMessageLog(GLuint count, GLsizei bufSize, GLenum *sources, GLenum *types, GLuint *ids, GLenum *severities, GLsizei *lengths, GLchar *messageLog) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetDebugMessageLog"); } return {}; }
        void APIENTRY S_glPushDebugGroup(GLenum source, GLuint id, GLsizei length, const GLchar *message) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPushDebugGroup"); } }
        void APIENTRY S_glPopDebugGroup(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPopDebugGroup"); } }
        void APIENTRY S_glObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar *label) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glObjectLabel"); } }
        void APIENTRY S_glGetObjectLabel(GLenum identifier, GLuint name, GLsizei bufSize, GLsizei *length, GLchar *label) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetObjectLabel"); } }
        void APIENTRY S_glObjectPtrLabel(const void *ptr, GLsizei length, const GLchar *label) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glObjectPtrLabel"); } }
        void APIENTRY S_glGetObjectPtrLabel(const void *ptr, GLsizei bufSize, GLsizei *length, GLchar *label) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetObjectPtrLabel"); } }
        void APIENTRY S_glGetPointerv(GLenum pname, void **params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPointerv"); } }
        void APIENTRY S_glBufferStorage(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferStorage"); } }
        void APIENTRY S_glClearTexImage(GLuint texture, GLint level, GLenum format, GLenum type, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearTexImage"); } }
        void APIENTRY S_glClearTexSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearTexSubImage"); } }
        void APIENTRY S_glBindBuffersBase(GLenum target, GLuint first, GLsizei count, const GLuint *buffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBuffersBase"); } }
        void APIENTRY S_glBindBuffersRange(GLenum target, GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets, const GLsizeiptr *sizes) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBuffersRange"); } }
        void APIENTRY S_glBindTextures(GLuint first, GLsizei count, const GLuint *textures) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindTextures"); } }
        void APIENTRY S_glBindSamplers(GLuint first, GLsizei count, const GLuint *samplers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindSamplers"); } }
        void APIENTRY S_glBindImageTextures(GLuint first, GLsizei count, const GLuint *textures) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindImageTextures"); } }
        void APIENTRY S_glBindVertexBuffers(GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets, const GLsizei *strides) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindVertexBuffers"); } }
        void APIENTRY S_glClipControl(GLenum origin, GLenum depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClipControl"); } }
        void APIENTRY S_glCreateTransformFeedbacks(GLsizei n, GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateTransformFeedbacks"); } }
        void APIENTRY S_glTransformFeedbackBufferBase(GLuint xfb, GLuint index, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTransformFeedbackBufferBase"); } }
        void APIENTRY S_glTransformFeedbackBufferRange(GLuint xfb, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTransformFeedbackBufferRange"); } }
        void APIENTRY S_glGetTransformFeedbackiv(GLuint xfb, GLenum pname, GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTransformFeedbackiv"); } }
        void APIENTRY S_glGetTransformFeedbacki_v(GLuint xfb, GLenum pname, GLuint index, GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTransformFeedbacki_v"); } }
        void APIENTRY S_glGetTransformFeedbacki64_v(GLuint xfb, GLenum pname, GLuint index, GLint64 *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTransformFeedbacki64_v"); } }
        void APIENTRY S_glCreateBuffers(GLsizei n, GLuint *buffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateBuffers"); } }
        void APIENTRY S_glNamedBufferStorage(GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferStorage"); } }
        void APIENTRY S_glNamedBufferData(GLuint buffer, GLsizeiptr size, const void *data, GLenum usage) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferData"); } }
        void APIENTRY S_glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferSubData"); } }
        void APIENTRY S_glCopyNamedBufferSubData(GLuint readBuffer, GLuint writeBuffer, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyNamedBufferSubData"); } }
        void APIENTRY S_glClearNamedBufferData(GLuint buffer, GLenum internalformat, GLenum format, GLenum type, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearNamedBufferData"); } }
        void APIENTRY S_glClearNamedBufferSubData(GLuint buffer, GLenum internalformat, GLintptr offset, GLsizeiptr size, GLenum format, GLenum type, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearNamedBufferSubData"); } }
        void * APIENTRY S_glMapNamedBuffer(GLuint buffer, GLenum access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapNamedBuffer"); } return {}; }
        void * APIENTRY S_glMapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapNamedBufferRange"); } return {}; }
        GLboolean APIENTRY S_glUnmapNamedBuffer(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUnmapNamedBuffer"); } return {}; }
        void APIENTRY S_glFlushMappedNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFlushMappedNamedBufferRange"); } }
        void APIENTRY S_glGetNamedBufferParameteriv(GLuint buffer, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedBufferParameteriv"); } }
        void APIENTRY S_glGetNamedBufferParameteri64v(GLuint buffer, GLenum pname, GLint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedBufferParameteri64v"); } }
        void APIENTRY S_glGetNamedBufferPointerv(GLuint buffer, GLenum pname, void **params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedBufferPointerv"); } }
        void APIENTRY S_glGetNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedBufferSubData"); } }
        void APIENTRY S_glCreateFramebuffers(GLsizei n, GLuint *framebuffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateFramebuffers"); } }
        void APIENTRY S_glNamedFramebufferRenderbuffer(GLuint framebuffer, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferRenderbuffer"); } }
        void APIENTRY S_glNamedFramebufferParameteri(GLuint framebuffer, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferParameteri"); } }
        void APIENTRY S_glNamedFramebufferTexture(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferTexture"); } }
        void APIENTRY S_glNamedFramebufferTextureLayer(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint layer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferTextureLayer"); } }
        void APIENTRY S_glNamedFramebufferDrawBuffer(GLuint framebuffer, GLenum buf) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferDrawBuffer"); } }
        void APIENTRY S_glNamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n, const GLenum *bufs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferDrawBuffers"); } }
        void APIENTRY S_glNamedFramebufferReadBuffer(GLuint framebuffer, GLenum src) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferReadBuffer"); } }
        void APIENTRY S_glInvalidateNamedFramebufferData(GLuint framebuffer, GLsizei numAttachments, const GLenum *attachments) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInvalidateNamedFramebufferData"); } }
        void APIENTRY S_glInvalidateNamedFramebufferSubData(GLuint framebuffer, GLsizei numAttachments, const GLenum *attachments, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInvalidateNamedFramebufferSubData"); } }
        void APIENTRY S_glClearNamedFramebufferiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearNamedFramebufferiv"); } }
        void APIENTRY S_glClearNamedFramebufferuiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearNamedFramebufferuiv"); } }
        void APIENTRY S_glClearNamedFramebufferfv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearNamedFramebufferfv"); } }
        void APIENTRY S_glClearNamedFramebufferfi(GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearNamedFramebufferfi"); } }
        void APIENTRY S_glBlitNamedFramebuffer(GLuint readFramebuffer, GLuint drawFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlitNamedFramebuffer"); } }
        GLenum APIENTRY S_glCheckNamedFramebufferStatus(GLuint framebuffer, GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCheckNamedFramebufferStatus"); } return {}; }
        void APIENTRY S_glGetNamedFramebufferParameteriv(GLuint framebuffer, GLenum pname, GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedFramebufferParameteriv"); } }
        void APIENTRY S_glGetNamedFramebufferAttachmentParameteriv(GLuint framebuffer, GLenum attachment, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedFramebufferAttachmentParameteriv"); } }
        void APIENTRY S_glCreateRenderbuffers(GLsizei n, GLuint *renderbuffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateRenderbuffers"); } }
        void APIENTRY S_glNamedRenderbufferStorage(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedRenderbufferStorage"); } }
        void APIENTRY S_glNamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedRenderbufferStorageMultisample"); } }
        void APIENTRY S_glGetNamedRenderbufferParameteriv(GLuint renderbuffer, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedRenderbufferParameteriv"); } }
        void APIENTRY S_glCreateTextures(GLenum target, GLsizei n, GLuint *textures) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateTextures"); } }
        void APIENTRY S_glTextureBuffer(GLuint texture, GLenum internalformat, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureBuffer"); } }
        void APIENTRY S_glTextureBufferRange(GLuint texture, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureBufferRange"); } }
        void APIENTRY S_glTextureStorage1D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorage1D"); } }
        void APIENTRY S_glTextureStorage2D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorage2D"); } }
        void APIENTRY S_glTextureStorage3D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorage3D"); } }
        void APIENTRY S_glTextureStorage2DMultisample(GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorage2DMultisample"); } }
        void APIENTRY S_glTextureStorage3DMultisample(GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorage3DMultisample"); } }
        void APIENTRY S_glTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureSubImage1D"); } }
        void APIENTRY S_glTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureSubImage2D"); } }
        void APIENTRY S_glTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureSubImage3D"); } }
        void APIENTRY S_glCompressedTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTextureSubImage1D"); } }
        void APIENTRY S_glCompressedTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTextureSubImage2D"); } }
        void APIENTRY S_glCompressedTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTextureSubImage3D"); } }
        void APIENTRY S_glCopyTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTextureSubImage1D"); } }
        void APIENTRY S_glCopyTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTextureSubImage2D"); } }
        void APIENTRY S_glCopyTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTextureSubImage3D"); } }
        void APIENTRY S_glTextureParameterf(GLuint texture, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameterf"); } }
        void APIENTRY S_glTextureParameterfv(GLuint texture, GLenum pname, const GLfloat *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameterfv"); } }
        void APIENTRY S_glTextureParameteri(GLuint texture, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameteri"); } }
        void APIENTRY S_glTextureParameterIiv(GLuint texture, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameterIiv"); } }
        void APIENTRY S_glTextureParameterIuiv(GLuint texture, GLenum pname, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameterIuiv"); } }
        void APIENTRY S_glTextureParameteriv(GLuint texture, GLenum pname, const GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameteriv"); } }
        void APIENTRY S_glGenerateTextureMipmap(GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenerateTextureMipmap"); } }
        void APIENTRY S_glBindTextureUnit(GLuint unit, GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindTextureUnit"); } }
        void APIENTRY S_glGetTextureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureImage"); } }
        void APIENTRY S_glGetCompressedTextureImage(GLuint texture, GLint level, GLsizei bufSize, void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCompressedTextureImage"); } }
        void APIENTRY S_glGetTextureLevelParameterfv(GLuint texture, GLint level, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureLevelParameterfv"); } }
        void APIENTRY S_glGetTextureLevelParameteriv(GLuint texture, GLint level, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureLevelParameteriv"); } }
        void APIENTRY S_glGetTextureParameterfv(GLuint texture, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureParameterfv"); } }
        void APIENTRY S_glGetTextureParameterIiv(GLuint texture, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureParameterIiv"); } }
        void APIENTRY S_glGetTextureParameterIuiv(GLuint texture, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureParameterIuiv"); } }
        void APIENTRY S_glGetTextureParameteriv(GLuint texture, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureParameteriv"); } }
        void APIENTRY S_glCreateVertexArrays(GLsizei n, GLuint *arrays) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateVertexArrays"); } }
        void APIENTRY S_glDisableVertexArrayAttrib(GLuint vaobj, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisableVertexArrayAttrib"); } }
        void APIENTRY S_glEnableVertexArrayAttrib(GLuint vaobj, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnableVertexArrayAttrib"); } }
        void APIENTRY S_glVertexArrayElementBuffer(GLuint vaobj, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayElementBuffer"); } }
        void APIENTRY S_glVertexArrayVertexBuffer(GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexBuffer"); } }
        void APIENTRY S_glVertexArrayVertexBuffers(GLuint vaobj, GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets, const GLsizei *strides) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexBuffers"); } }
        void APIENTRY S_glVertexArrayAttribBinding(GLuint vaobj, GLuint attribindex, GLuint bindingindex) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayAttribBinding"); } }
        void APIENTRY S_glVertexArrayAttribFormat(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayAttribFormat"); } }
        void APIENTRY S_glVertexArrayAttribIFormat(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayAttribIFormat"); } }
        void APIENTRY S_glVertexArrayAttribLFormat(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayAttribLFormat"); } }
        void APIENTRY S_glVertexArrayBindingDivisor(GLuint vaobj, GLuint bindingindex, GLuint divisor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayBindingDivisor"); } }
        void APIENTRY S_glGetVertexArrayiv(GLuint vaobj, GLenum pname, GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexArrayiv"); } }
        void APIENTRY S_glGetVertexArrayIndexediv(GLuint vaobj, GLuint index, GLenum pname, GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexArrayIndexediv"); } }
        void APIENTRY S_glGetVertexArrayIndexed64iv(GLuint vaobj, GLuint index, GLenum pname, GLint64 *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexArrayIndexed64iv"); } }
        void APIENTRY S_glCreateSamplers(GLsizei n, GLuint *samplers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateSamplers"); } }
        void APIENTRY S_glCreateProgramPipelines(GLsizei n, GLuint *pipelines) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateProgramPipelines"); } }
        void APIENTRY S_glCreateQueries(GLenum target, GLsizei n, GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateQueries"); } }
        void APIENTRY S_glGetQueryBufferObjecti64v(GLuint id, GLuint buffer, GLenum pname, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryBufferObjecti64v"); } }
        void APIENTRY S_glGetQueryBufferObjectiv(GLuint id, GLuint buffer, GLenum pname, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryBufferObjectiv"); } }
        void APIENTRY S_glGetQueryBufferObjectui64v(GLuint id, GLuint buffer, GLenum pname, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryBufferObjectui64v"); } }
        void APIENTRY S_glGetQueryBufferObjectuiv(GLuint id, GLuint buffer, GLenum pname, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryBufferObjectuiv"); } }
        void APIENTRY S_glMemoryBarrierByRegion(GLbitfield barriers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMemoryBarrierByRegion"); } }
        void APIENTRY S_glGetTextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, GLsizei bufSize, void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureSubImage"); } }
        void APIENTRY S_glGetCompressedTextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLsizei bufSize, void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCompressedTextureSubImage"); } }
        GLenum APIENTRY S_glGetGraphicsResetStatus(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetGraphicsResetStatus"); } return {}; }
        void APIENTRY S_glGetnCompressedTexImage(GLenum target, GLint lod, GLsizei bufSize, void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnCompressedTexImage"); } }
        void APIENTRY S_glGetnTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLsizei bufSize, void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnTexImage"); } }
        void APIENTRY S_glGetnUniformdv(GLuint program, GLint location, GLsizei bufSize, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformdv"); } }
        void APIENTRY S_glGetnUniformfv(GLuint program, GLint location, GLsizei bufSize, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformfv"); } }
        void APIENTRY S_glGetnUniformiv(GLuint program, GLint location, GLsizei bufSize, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformiv"); } }
        void APIENTRY S_glGetnUniformuiv(GLuint program, GLint location, GLsizei bufSize, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformuiv"); } }
        void APIENTRY S_glReadnPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLsizei bufSize, void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReadnPixels"); } }
        void APIENTRY S_glGetnMapdv(GLenum target, GLenum query, GLsizei bufSize, GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnMapdv"); } }
        void APIENTRY S_glGetnMapfv(GLenum target, GLenum query, GLsizei bufSize, GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnMapfv"); } }
        void APIENTRY S_glGetnMapiv(GLenum target, GLenum query, GLsizei bufSize, GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnMapiv"); } }
        void APIENTRY S_glGetnPixelMapfv(GLenum map, GLsizei bufSize, GLfloat *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnPixelMapfv"); } }
        void APIENTRY S_glGetnPixelMapuiv(GLenum map, GLsizei bufSize, GLuint *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnPixelMapuiv"); } }
        void APIENTRY S_glGetnPixelMapusv(GLenum map, GLsizei bufSize, GLushort *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnPixelMapusv"); } }
        void APIENTRY S_glGetnPolygonStipple(GLsizei bufSize, GLubyte *pattern) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnPolygonStipple"); } }
        void APIENTRY S_glGetnColorTable(GLenum target, GLenum format, GLenum type, GLsizei bufSize, void *table) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnColorTable"); } }
        void APIENTRY S_glGetnConvolutionFilter(GLenum target, GLenum format, GLenum type, GLsizei bufSize, void *image) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnConvolutionFilter"); } }
        void APIENTRY S_glGetnSeparableFilter(GLenum target, GLenum format, GLenum type, GLsizei rowBufSize, void *row, GLsizei columnBufSize, void *column, void *span) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnSeparableFilter"); } }
        void APIENTRY S_glGetnHistogram(GLenum target, GLboolean reset, GLenum format, GLenum type, GLsizei bufSize, void *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnHistogram"); } }
        void APIENTRY S_glGetnMinmax(GLenum target, GLboolean reset, GLenum format, GLenum type, GLsizei bufSize, void *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnMinmax"); } }
        void APIENTRY S_glTextureBarrier(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureBarrier"); } }
        void APIENTRY S_glSpecializeShader(GLuint shader, const GLchar *pEntryPoint, GLuint numSpecializationConstants, const GLuint *pConstantIndex, const GLuint *pConstantValue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSpecializeShader"); } }
        void APIENTRY S_glMultiDrawArraysIndirectCount(GLenum mode, const void *indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawArraysIndirectCount"); } }
        void APIENTRY S_glMultiDrawElementsIndirectCount(GLenum mode, GLenum type, const void *indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawElementsIndirectCount"); } }
        void APIENTRY S_glPolygonOffsetClamp(GLfloat factor, GLfloat units, GLfloat clamp) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPolygonOffsetClamp"); } }
        void APIENTRY S_glTbufferMask3DFX(GLuint mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTbufferMask3DFX"); } }
        void APIENTRY S_glDebugMessageEnableAMD(GLenum category, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageEnableAMD"); } }
        void APIENTRY S_glDebugMessageInsertAMD(GLenum category, GLenum severity, GLuint id, GLsizei length, const GLchar *buf) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageInsertAMD"); } }
        void APIENTRY S_glDebugMessageCallbackAMD(GLDEBUGPROCAMD callback, void *userParam) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageCallbackAMD"); } }
        GLuint APIENTRY S_glGetDebugMessageLogAMD(GLuint count, GLsizei bufSize, GLenum *categories, GLenum *severities, GLuint *ids, GLsizei *lengths, GLchar *message) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetDebugMessageLogAMD"); } return {}; }
        void APIENTRY S_glBlendFuncIndexedAMD(GLuint buf, GLenum src, GLenum dst) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendFuncIndexedAMD"); } }
        void APIENTRY S_glBlendFuncSeparateIndexedAMD(GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendFuncSeparateIndexedAMD"); } }
        void APIENTRY S_glBlendEquationIndexedAMD(GLuint buf, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendEquationIndexedAMD"); } }
        void APIENTRY S_glBlendEquationSeparateIndexedAMD(GLuint buf, GLenum modeRGB, GLenum modeAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendEquationSeparateIndexedAMD"); } }
        void APIENTRY S_glRenderbufferStorageMultisampleAdvancedAMD(GLenum target, GLsizei samples, GLsizei storageSamples, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRenderbufferStorageMultisampleAdvancedAMD"); } }
        void APIENTRY S_glNamedRenderbufferStorageMultisampleAdvancedAMD(GLuint renderbuffer, GLsizei samples, GLsizei storageSamples, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedRenderbufferStorageMultisampleAdvancedAMD"); } }
        void APIENTRY S_glFramebufferSamplePositionsfvAMD(GLenum target, GLuint numsamples, GLuint pixelindex, const GLfloat *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferSamplePositionsfvAMD"); } }
        void APIENTRY S_glNamedFramebufferSamplePositionsfvAMD(GLuint framebuffer, GLuint numsamples, GLuint pixelindex, const GLfloat *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferSamplePositionsfvAMD"); } }
        void APIENTRY S_glGetFramebufferParameterfvAMD(GLenum target, GLenum pname, GLuint numsamples, GLuint pixelindex, GLsizei size, GLfloat *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFramebufferParameterfvAMD"); } }
        void APIENTRY S_glGetNamedFramebufferParameterfvAMD(GLuint framebuffer, GLenum pname, GLuint numsamples, GLuint pixelindex, GLsizei size, GLfloat *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedFramebufferParameterfvAMD"); } }
        void APIENTRY S_glUniform1i64NV(GLint location, GLint64EXT x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1i64NV"); } }
        void APIENTRY S_glUniform2i64NV(GLint location, GLint64EXT x, GLint64EXT y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2i64NV"); } }
        void APIENTRY S_glUniform3i64NV(GLint location, GLint64EXT x, GLint64EXT y, GLint64EXT z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3i64NV"); } }
        void APIENTRY S_glUniform4i64NV(GLint location, GLint64EXT x, GLint64EXT y, GLint64EXT z, GLint64EXT w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4i64NV"); } }
        void APIENTRY S_glUniform1i64vNV(GLint location, GLsizei count, const GLint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1i64vNV"); } }
        void APIENTRY S_glUniform2i64vNV(GLint location, GLsizei count, const GLint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2i64vNV"); } }
        void APIENTRY S_glUniform3i64vNV(GLint location, GLsizei count, const GLint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3i64vNV"); } }
        void APIENTRY S_glUniform4i64vNV(GLint location, GLsizei count, const GLint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4i64vNV"); } }
        void APIENTRY S_glUniform1ui64NV(GLint location, GLuint64EXT x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1ui64NV"); } }
        void APIENTRY S_glUniform2ui64NV(GLint location, GLuint64EXT x, GLuint64EXT y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2ui64NV"); } }
        void APIENTRY S_glUniform3ui64NV(GLint location, GLuint64EXT x, GLuint64EXT y, GLuint64EXT z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3ui64NV"); } }
        void APIENTRY S_glUniform4ui64NV(GLint location, GLuint64EXT x, GLuint64EXT y, GLuint64EXT z, GLuint64EXT w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4ui64NV"); } }
        void APIENTRY S_glUniform1ui64vNV(GLint location, GLsizei count, const GLuint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1ui64vNV"); } }
        void APIENTRY S_glUniform2ui64vNV(GLint location, GLsizei count, const GLuint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2ui64vNV"); } }
        void APIENTRY S_glUniform3ui64vNV(GLint location, GLsizei count, const GLuint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3ui64vNV"); } }
        void APIENTRY S_glUniform4ui64vNV(GLint location, GLsizei count, const GLuint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4ui64vNV"); } }
        void APIENTRY S_glGetUniformi64vNV(GLuint program, GLint location, GLint64EXT *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformi64vNV"); } }
        void APIENTRY S_glGetUniformui64vNV(GLuint program, GLint location, GLuint64EXT *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformui64vNV"); } }
        void APIENTRY S_glProgramUniform1i64NV(GLuint program, GLint location, GLint64EXT x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1i64NV"); } }
        void APIENTRY S_glProgramUniform2i64NV(GLuint program, GLint location, GLint64EXT x, GLint64EXT y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2i64NV"); } }
        void APIENTRY S_glProgramUniform3i64NV(GLuint program, GLint location, GLint64EXT x, GLint64EXT y, GLint64EXT z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3i64NV"); } }
        void APIENTRY S_glProgramUniform4i64NV(GLuint program, GLint location, GLint64EXT x, GLint64EXT y, GLint64EXT z, GLint64EXT w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4i64NV"); } }
        void APIENTRY S_glProgramUniform1i64vNV(GLuint program, GLint location, GLsizei count, const GLint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1i64vNV"); } }
        void APIENTRY S_glProgramUniform2i64vNV(GLuint program, GLint location, GLsizei count, const GLint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2i64vNV"); } }
        void APIENTRY S_glProgramUniform3i64vNV(GLuint program, GLint location, GLsizei count, const GLint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3i64vNV"); } }
        void APIENTRY S_glProgramUniform4i64vNV(GLuint program, GLint location, GLsizei count, const GLint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4i64vNV"); } }
        void APIENTRY S_glProgramUniform1ui64NV(GLuint program, GLint location, GLuint64EXT x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1ui64NV"); } }
        void APIENTRY S_glProgramUniform2ui64NV(GLuint program, GLint location, GLuint64EXT x, GLuint64EXT y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2ui64NV"); } }
        void APIENTRY S_glProgramUniform3ui64NV(GLuint program, GLint location, GLuint64EXT x, GLuint64EXT y, GLuint64EXT z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3ui64NV"); } }
        void APIENTRY S_glProgramUniform4ui64NV(GLuint program, GLint location, GLuint64EXT x, GLuint64EXT y, GLuint64EXT z, GLuint64EXT w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4ui64NV"); } }
        void APIENTRY S_glProgramUniform1ui64vNV(GLuint program, GLint location, GLsizei count, const GLuint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1ui64vNV"); } }
        void APIENTRY S_glProgramUniform2ui64vNV(GLuint program, GLint location, GLsizei count, const GLuint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2ui64vNV"); } }
        void APIENTRY S_glProgramUniform3ui64vNV(GLuint program, GLint location, GLsizei count, const GLuint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3ui64vNV"); } }
        void APIENTRY S_glProgramUniform4ui64vNV(GLuint program, GLint location, GLsizei count, const GLuint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4ui64vNV"); } }
        void APIENTRY S_glVertexAttribParameteriAMD(GLuint index, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribParameteriAMD"); } }
        void APIENTRY S_glMultiDrawArraysIndirectAMD(GLenum mode, const void *indirect, GLsizei primcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawArraysIndirectAMD"); } }
        void APIENTRY S_glMultiDrawElementsIndirectAMD(GLenum mode, GLenum type, const void *indirect, GLsizei primcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawElementsIndirectAMD"); } }
        void APIENTRY S_glGenNamesAMD(GLenum identifier, GLuint num, GLuint *names) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenNamesAMD"); } }
        void APIENTRY S_glDeleteNamesAMD(GLenum identifier, GLuint num, const GLuint *names) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteNamesAMD"); } }
        GLboolean APIENTRY S_glIsNameAMD(GLenum identifier, GLuint name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsNameAMD"); } return {}; }
        void APIENTRY S_glQueryObjectParameteruiAMD(GLenum target, GLuint id, GLenum pname, GLuint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glQueryObjectParameteruiAMD"); } }
        void APIENTRY S_glGetPerfMonitorGroupsAMD(GLint *numGroups, GLsizei groupsSize, GLuint *groups) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPerfMonitorGroupsAMD"); } }
        void APIENTRY S_glGetPerfMonitorCountersAMD(GLuint group, GLint *numCounters, GLint *maxActiveCounters, GLsizei counterSize, GLuint *counters) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPerfMonitorCountersAMD"); } }
        void APIENTRY S_glGetPerfMonitorGroupStringAMD(GLuint group, GLsizei bufSize, GLsizei *length, GLchar *groupString) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPerfMonitorGroupStringAMD"); } }
        void APIENTRY S_glGetPerfMonitorCounterStringAMD(GLuint group, GLuint counter, GLsizei bufSize, GLsizei *length, GLchar *counterString) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPerfMonitorCounterStringAMD"); } }
        void APIENTRY S_glGetPerfMonitorCounterInfoAMD(GLuint group, GLuint counter, GLenum pname, void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPerfMonitorCounterInfoAMD"); } }
        void APIENTRY S_glGenPerfMonitorsAMD(GLsizei n, GLuint *monitors) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenPerfMonitorsAMD"); } }
        void APIENTRY S_glDeletePerfMonitorsAMD(GLsizei n, GLuint *monitors) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeletePerfMonitorsAMD"); } }
        void APIENTRY S_glSelectPerfMonitorCountersAMD(GLuint monitor, GLboolean enable, GLuint group, GLint numCounters, GLuint *counterList) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSelectPerfMonitorCountersAMD"); } }
        void APIENTRY S_glBeginPerfMonitorAMD(GLuint monitor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginPerfMonitorAMD"); } }
        void APIENTRY S_glEndPerfMonitorAMD(GLuint monitor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndPerfMonitorAMD"); } }
        void APIENTRY S_glGetPerfMonitorCounterDataAMD(GLuint monitor, GLenum pname, GLsizei dataSize, GLuint *data, GLint *bytesWritten) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPerfMonitorCounterDataAMD"); } }
        void APIENTRY S_glSetMultisamplefvAMD(GLenum pname, GLuint index, const GLfloat *val) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSetMultisamplefvAMD"); } }
        void APIENTRY S_glTexStorageSparseAMD(GLenum target, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLsizei layers, GLbitfield flags) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorageSparseAMD"); } }
        void APIENTRY S_glTextureStorageSparseAMD(GLuint texture, GLenum target, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLsizei layers, GLbitfield flags) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorageSparseAMD"); } }
        void APIENTRY S_glStencilOpValueAMD(GLenum face, GLuint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilOpValueAMD"); } }
        void APIENTRY S_glTessellationFactorAMD(GLfloat factor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTessellationFactorAMD"); } }
        void APIENTRY S_glTessellationModeAMD(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTessellationModeAMD"); } }
        void APIENTRY S_glElementPointerAPPLE(GLenum type, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glElementPointerAPPLE"); } }
        void APIENTRY S_glDrawElementArrayAPPLE(GLenum mode, GLint first, GLsizei count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElementArrayAPPLE"); } }
        void APIENTRY S_glDrawRangeElementArrayAPPLE(GLenum mode, GLuint start, GLuint end, GLint first, GLsizei count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawRangeElementArrayAPPLE"); } }
        void APIENTRY S_glMultiDrawElementArrayAPPLE(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawElementArrayAPPLE"); } }
        void APIENTRY S_glMultiDrawRangeElementArrayAPPLE(GLenum mode, GLuint start, GLuint end, const GLint *first, const GLsizei *count, GLsizei primcount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawRangeElementArrayAPPLE"); } }
        void APIENTRY S_glGenFencesAPPLE(GLsizei n, GLuint *fences) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenFencesAPPLE"); } }
        void APIENTRY S_glDeleteFencesAPPLE(GLsizei n, const GLuint *fences) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteFencesAPPLE"); } }
        void APIENTRY S_glSetFenceAPPLE(GLuint fence) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSetFenceAPPLE"); } }
        GLboolean APIENTRY S_glIsFenceAPPLE(GLuint fence) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsFenceAPPLE"); } return {}; }
        GLboolean APIENTRY S_glTestFenceAPPLE(GLuint fence) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTestFenceAPPLE"); } return {}; }
        void APIENTRY S_glFinishFenceAPPLE(GLuint fence) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFinishFenceAPPLE"); } }
        GLboolean APIENTRY S_glTestObjectAPPLE(GLenum object, GLuint name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTestObjectAPPLE"); } return {}; }
        void APIENTRY S_glFinishObjectAPPLE(GLenum object, GLint name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFinishObjectAPPLE"); } }
        void APIENTRY S_glBufferParameteriAPPLE(GLenum target, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferParameteriAPPLE"); } }
        void APIENTRY S_glFlushMappedBufferRangeAPPLE(GLenum target, GLintptr offset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFlushMappedBufferRangeAPPLE"); } }
        GLenum APIENTRY S_glObjectPurgeableAPPLE(GLenum objectType, GLuint name, GLenum option) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glObjectPurgeableAPPLE"); } return {}; }
        GLenum APIENTRY S_glObjectUnpurgeableAPPLE(GLenum objectType, GLuint name, GLenum option) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glObjectUnpurgeableAPPLE"); } return {}; }
        void APIENTRY S_glGetObjectParameterivAPPLE(GLenum objectType, GLuint name, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetObjectParameterivAPPLE"); } }
        void APIENTRY S_glTextureRangeAPPLE(GLenum target, GLsizei length, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureRangeAPPLE"); } }
        void APIENTRY S_glGetTexParameterPointervAPPLE(GLenum target, GLenum pname, void **params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexParameterPointervAPPLE"); } }
        void APIENTRY S_glBindVertexArrayAPPLE(GLuint array) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindVertexArrayAPPLE"); } }
        void APIENTRY S_glDeleteVertexArraysAPPLE(GLsizei n, const GLuint *arrays) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteVertexArraysAPPLE"); } }
        void APIENTRY S_glGenVertexArraysAPPLE(GLsizei n, GLuint *arrays) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenVertexArraysAPPLE"); } }
        GLboolean APIENTRY S_glIsVertexArrayAPPLE(GLuint array) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsVertexArrayAPPLE"); } return {}; }
        void APIENTRY S_glVertexArrayRangeAPPLE(GLsizei length, void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayRangeAPPLE"); } }
        void APIENTRY S_glFlushVertexArrayRangeAPPLE(GLsizei length, void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFlushVertexArrayRangeAPPLE"); } }
        void APIENTRY S_glVertexArrayParameteriAPPLE(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayParameteriAPPLE"); } }
        void APIENTRY S_glEnableVertexAttribAPPLE(GLuint index, GLenum pname) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnableVertexAttribAPPLE"); } }
        void APIENTRY S_glDisableVertexAttribAPPLE(GLuint index, GLenum pname) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisableVertexAttribAPPLE"); } }
        GLboolean APIENTRY S_glIsVertexAttribEnabledAPPLE(GLuint index, GLenum pname) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsVertexAttribEnabledAPPLE"); } return {}; }
        void APIENTRY S_glMapVertexAttrib1dAPPLE(GLuint index, GLuint size, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapVertexAttrib1dAPPLE"); } }
        void APIENTRY S_glMapVertexAttrib1fAPPLE(GLuint index, GLuint size, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapVertexAttrib1fAPPLE"); } }
        void APIENTRY S_glMapVertexAttrib2dAPPLE(GLuint index, GLuint size, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapVertexAttrib2dAPPLE"); } }
        void APIENTRY S_glMapVertexAttrib2fAPPLE(GLuint index, GLuint size, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapVertexAttrib2fAPPLE"); } }
        void APIENTRY S_glPrimitiveBoundingBoxARB(GLfloat minX, GLfloat minY, GLfloat minZ, GLfloat minW, GLfloat maxX, GLfloat maxY, GLfloat maxZ, GLfloat maxW) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPrimitiveBoundingBoxARB"); } }
        GLuint64 APIENTRY S_glGetTextureHandleARB(GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureHandleARB"); } return {}; }
        GLuint64 APIENTRY S_glGetTextureSamplerHandleARB(GLuint texture, GLuint sampler) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureSamplerHandleARB"); } return {}; }
        void APIENTRY S_glMakeTextureHandleResidentARB(GLuint64 handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeTextureHandleResidentARB"); } }
        void APIENTRY S_glMakeTextureHandleNonResidentARB(GLuint64 handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeTextureHandleNonResidentARB"); } }
        GLuint64 APIENTRY S_glGetImageHandleARB(GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum format) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetImageHandleARB"); } return {}; }
        void APIENTRY S_glMakeImageHandleResidentARB(GLuint64 handle, GLenum access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeImageHandleResidentARB"); } }
        void APIENTRY S_glMakeImageHandleNonResidentARB(GLuint64 handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeImageHandleNonResidentARB"); } }
        void APIENTRY S_glUniformHandleui64ARB(GLint location, GLuint64 value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformHandleui64ARB"); } }
        void APIENTRY S_glUniformHandleui64vARB(GLint location, GLsizei count, const GLuint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformHandleui64vARB"); } }
        void APIENTRY S_glProgramUniformHandleui64ARB(GLuint program, GLint location, GLuint64 value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformHandleui64ARB"); } }
        void APIENTRY S_glProgramUniformHandleui64vARB(GLuint program, GLint location, GLsizei count, const GLuint64 *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformHandleui64vARB"); } }
        GLboolean APIENTRY S_glIsTextureHandleResidentARB(GLuint64 handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsTextureHandleResidentARB"); } return {}; }
        GLboolean APIENTRY S_glIsImageHandleResidentARB(GLuint64 handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsImageHandleResidentARB"); } return {}; }
        void APIENTRY S_glVertexAttribL1ui64ARB(GLuint index, GLuint64EXT x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL1ui64ARB"); } }
        void APIENTRY S_glVertexAttribL1ui64vARB(GLuint index, const GLuint64EXT *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL1ui64vARB"); } }
        void APIENTRY S_glGetVertexAttribLui64vARB(GLuint index, GLenum pname, GLuint64EXT *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribLui64vARB"); } }
        GLsync APIENTRY S_glCreateSyncFromCLeventARB(struct _cl_context *context, struct _cl_event *event, GLbitfield flags) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateSyncFromCLeventARB"); } return {}; }
        void APIENTRY S_glClampColorARB(GLenum target, GLenum clamp) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClampColorARB"); } }
        void APIENTRY S_glDispatchComputeGroupSizeARB(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z, GLuint group_size_x, GLuint group_size_y, GLuint group_size_z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDispatchComputeGroupSizeARB"); } }
        void APIENTRY S_glDebugMessageControlARB(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageControlARB"); } }
        void APIENTRY S_glDebugMessageInsertARB(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *buf) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageInsertARB"); } }
        void APIENTRY S_glDebugMessageCallbackARB(GLDEBUGPROCARB callback, const void *userParam) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageCallbackARB"); } }
        GLuint APIENTRY S_glGetDebugMessageLogARB(GLuint count, GLsizei bufSize, GLenum *sources, GLenum *types, GLuint *ids, GLenum *severities, GLsizei *lengths, GLchar *messageLog) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetDebugMessageLogARB"); } return {}; }
        void APIENTRY S_glDrawBuffersARB(GLsizei n, const GLenum *bufs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawBuffersARB"); } }
        void APIENTRY S_glBlendEquationiARB(GLuint buf, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendEquationiARB"); } }
        void APIENTRY S_glBlendEquationSeparateiARB(GLuint buf, GLenum modeRGB, GLenum modeAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendEquationSeparateiARB"); } }
        void APIENTRY S_glBlendFunciARB(GLuint buf, GLenum src, GLenum dst) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendFunciARB"); } }
        void APIENTRY S_glBlendFuncSeparateiARB(GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendFuncSeparateiARB"); } }
        void APIENTRY S_glDrawArraysInstancedARB(GLenum mode, GLint first, GLsizei count, GLsizei primcount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawArraysInstancedARB"); } }
        void APIENTRY S_glDrawElementsInstancedARB(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElementsInstancedARB"); } }
        void APIENTRY S_glProgramStringARB(GLenum target, GLenum format, GLsizei len, const void *string) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramStringARB"); } }
        void APIENTRY S_glBindProgramARB(GLenum target, GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindProgramARB"); } }
        void APIENTRY S_glDeleteProgramsARB(GLsizei n, const GLuint *programs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteProgramsARB"); } }
        void APIENTRY S_glGenProgramsARB(GLsizei n, GLuint *programs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenProgramsARB"); } }
        void APIENTRY S_glProgramEnvParameter4dARB(GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParameter4dARB"); } }
        void APIENTRY S_glProgramEnvParameter4dvARB(GLenum target, GLuint index, const GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParameter4dvARB"); } }
        void APIENTRY S_glProgramEnvParameter4fARB(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParameter4fARB"); } }
        void APIENTRY S_glProgramEnvParameter4fvARB(GLenum target, GLuint index, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParameter4fvARB"); } }
        void APIENTRY S_glProgramLocalParameter4dARB(GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParameter4dARB"); } }
        void APIENTRY S_glProgramLocalParameter4dvARB(GLenum target, GLuint index, const GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParameter4dvARB"); } }
        void APIENTRY S_glProgramLocalParameter4fARB(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParameter4fARB"); } }
        void APIENTRY S_glProgramLocalParameter4fvARB(GLenum target, GLuint index, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParameter4fvARB"); } }
        void APIENTRY S_glGetProgramEnvParameterdvARB(GLenum target, GLuint index, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramEnvParameterdvARB"); } }
        void APIENTRY S_glGetProgramEnvParameterfvARB(GLenum target, GLuint index, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramEnvParameterfvARB"); } }
        void APIENTRY S_glGetProgramLocalParameterdvARB(GLenum target, GLuint index, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramLocalParameterdvARB"); } }
        void APIENTRY S_glGetProgramLocalParameterfvARB(GLenum target, GLuint index, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramLocalParameterfvARB"); } }
        void APIENTRY S_glGetProgramivARB(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramivARB"); } }
        void APIENTRY S_glGetProgramStringARB(GLenum target, GLenum pname, void *string) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramStringARB"); } }
        GLboolean APIENTRY S_glIsProgramARB(GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsProgramARB"); } return {}; }
        void APIENTRY S_glProgramParameteriARB(GLuint program, GLenum pname, GLint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramParameteriARB"); } }
        void APIENTRY S_glFramebufferTextureARB(GLenum target, GLenum attachment, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTextureARB"); } }
        void APIENTRY S_glFramebufferTextureLayerARB(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTextureLayerARB"); } }
        void APIENTRY S_glFramebufferTextureFaceARB(GLenum target, GLenum attachment, GLuint texture, GLint level, GLenum face) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTextureFaceARB"); } }
        void APIENTRY S_glSpecializeShaderARB(GLuint shader, const GLchar *pEntryPoint, GLuint numSpecializationConstants, const GLuint *pConstantIndex, const GLuint *pConstantValue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSpecializeShaderARB"); } }
        void APIENTRY S_glUniform1i64ARB(GLint location, GLint64 x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1i64ARB"); } }
        void APIENTRY S_glUniform2i64ARB(GLint location, GLint64 x, GLint64 y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2i64ARB"); } }
        void APIENTRY S_glUniform3i64ARB(GLint location, GLint64 x, GLint64 y, GLint64 z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3i64ARB"); } }
        void APIENTRY S_glUniform4i64ARB(GLint location, GLint64 x, GLint64 y, GLint64 z, GLint64 w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4i64ARB"); } }
        void APIENTRY S_glUniform1i64vARB(GLint location, GLsizei count, const GLint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1i64vARB"); } }
        void APIENTRY S_glUniform2i64vARB(GLint location, GLsizei count, const GLint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2i64vARB"); } }
        void APIENTRY S_glUniform3i64vARB(GLint location, GLsizei count, const GLint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3i64vARB"); } }
        void APIENTRY S_glUniform4i64vARB(GLint location, GLsizei count, const GLint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4i64vARB"); } }
        void APIENTRY S_glUniform1ui64ARB(GLint location, GLuint64 x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1ui64ARB"); } }
        void APIENTRY S_glUniform2ui64ARB(GLint location, GLuint64 x, GLuint64 y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2ui64ARB"); } }
        void APIENTRY S_glUniform3ui64ARB(GLint location, GLuint64 x, GLuint64 y, GLuint64 z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3ui64ARB"); } }
        void APIENTRY S_glUniform4ui64ARB(GLint location, GLuint64 x, GLuint64 y, GLuint64 z, GLuint64 w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4ui64ARB"); } }
        void APIENTRY S_glUniform1ui64vARB(GLint location, GLsizei count, const GLuint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1ui64vARB"); } }
        void APIENTRY S_glUniform2ui64vARB(GLint location, GLsizei count, const GLuint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2ui64vARB"); } }
        void APIENTRY S_glUniform3ui64vARB(GLint location, GLsizei count, const GLuint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3ui64vARB"); } }
        void APIENTRY S_glUniform4ui64vARB(GLint location, GLsizei count, const GLuint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4ui64vARB"); } }
        void APIENTRY S_glGetUniformi64vARB(GLuint program, GLint location, GLint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformi64vARB"); } }
        void APIENTRY S_glGetUniformui64vARB(GLuint program, GLint location, GLuint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformui64vARB"); } }
        void APIENTRY S_glGetnUniformi64vARB(GLuint program, GLint location, GLsizei bufSize, GLint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformi64vARB"); } }
        void APIENTRY S_glGetnUniformui64vARB(GLuint program, GLint location, GLsizei bufSize, GLuint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformui64vARB"); } }
        void APIENTRY S_glProgramUniform1i64ARB(GLuint program, GLint location, GLint64 x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1i64ARB"); } }
        void APIENTRY S_glProgramUniform2i64ARB(GLuint program, GLint location, GLint64 x, GLint64 y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2i64ARB"); } }
        void APIENTRY S_glProgramUniform3i64ARB(GLuint program, GLint location, GLint64 x, GLint64 y, GLint64 z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3i64ARB"); } }
        void APIENTRY S_glProgramUniform4i64ARB(GLuint program, GLint location, GLint64 x, GLint64 y, GLint64 z, GLint64 w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4i64ARB"); } }
        void APIENTRY S_glProgramUniform1i64vARB(GLuint program, GLint location, GLsizei count, const GLint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1i64vARB"); } }
        void APIENTRY S_glProgramUniform2i64vARB(GLuint program, GLint location, GLsizei count, const GLint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2i64vARB"); } }
        void APIENTRY S_glProgramUniform3i64vARB(GLuint program, GLint location, GLsizei count, const GLint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3i64vARB"); } }
        void APIENTRY S_glProgramUniform4i64vARB(GLuint program, GLint location, GLsizei count, const GLint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4i64vARB"); } }
        void APIENTRY S_glProgramUniform1ui64ARB(GLuint program, GLint location, GLuint64 x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1ui64ARB"); } }
        void APIENTRY S_glProgramUniform2ui64ARB(GLuint program, GLint location, GLuint64 x, GLuint64 y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2ui64ARB"); } }
        void APIENTRY S_glProgramUniform3ui64ARB(GLuint program, GLint location, GLuint64 x, GLuint64 y, GLuint64 z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3ui64ARB"); } }
        void APIENTRY S_glProgramUniform4ui64ARB(GLuint program, GLint location, GLuint64 x, GLuint64 y, GLuint64 z, GLuint64 w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4ui64ARB"); } }
        void APIENTRY S_glProgramUniform1ui64vARB(GLuint program, GLint location, GLsizei count, const GLuint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1ui64vARB"); } }
        void APIENTRY S_glProgramUniform2ui64vARB(GLuint program, GLint location, GLsizei count, const GLuint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2ui64vARB"); } }
        void APIENTRY S_glProgramUniform3ui64vARB(GLuint program, GLint location, GLsizei count, const GLuint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3ui64vARB"); } }
        void APIENTRY S_glProgramUniform4ui64vARB(GLuint program, GLint location, GLsizei count, const GLuint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4ui64vARB"); } }
        void APIENTRY S_glColorTable(GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const void *table) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorTable"); } }
        void APIENTRY S_glColorTableParameterfv(GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorTableParameterfv"); } }
        void APIENTRY S_glColorTableParameteriv(GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorTableParameteriv"); } }
        void APIENTRY S_glCopyColorTable(GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyColorTable"); } }
        void APIENTRY S_glGetColorTable(GLenum target, GLenum format, GLenum type, void *table) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetColorTable"); } }
        void APIENTRY S_glGetColorTableParameterfv(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetColorTableParameterfv"); } }
        void APIENTRY S_glGetColorTableParameteriv(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetColorTableParameteriv"); } }
        void APIENTRY S_glColorSubTable(GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorSubTable"); } }
        void APIENTRY S_glCopyColorSubTable(GLenum target, GLsizei start, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyColorSubTable"); } }
        void APIENTRY S_glConvolutionFilter1D(GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const void *image) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionFilter1D"); } }
        void APIENTRY S_glConvolutionFilter2D(GLenum target, GLenum internalformat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *image) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionFilter2D"); } }
        void APIENTRY S_glConvolutionParameterf(GLenum target, GLenum pname, GLfloat params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionParameterf"); } }
        void APIENTRY S_glConvolutionParameterfv(GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionParameterfv"); } }
        void APIENTRY S_glConvolutionParameteri(GLenum target, GLenum pname, GLint params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionParameteri"); } }
        void APIENTRY S_glConvolutionParameteriv(GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionParameteriv"); } }
        void APIENTRY S_glCopyConvolutionFilter1D(GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyConvolutionFilter1D"); } }
        void APIENTRY S_glCopyConvolutionFilter2D(GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyConvolutionFilter2D"); } }
        void APIENTRY S_glGetConvolutionFilter(GLenum target, GLenum format, GLenum type, void *image) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetConvolutionFilter"); } }
        void APIENTRY S_glGetConvolutionParameterfv(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetConvolutionParameterfv"); } }
        void APIENTRY S_glGetConvolutionParameteriv(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetConvolutionParameteriv"); } }
        void APIENTRY S_glGetSeparableFilter(GLenum target, GLenum format, GLenum type, void *row, void *column, void *span) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSeparableFilter"); } }
        void APIENTRY S_glSeparableFilter2D(GLenum target, GLenum internalformat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *row, const void *column) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSeparableFilter2D"); } }
        void APIENTRY S_glGetHistogram(GLenum target, GLboolean reset, GLenum format, GLenum type, void *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetHistogram"); } }
        void APIENTRY S_glGetHistogramParameterfv(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetHistogramParameterfv"); } }
        void APIENTRY S_glGetHistogramParameteriv(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetHistogramParameteriv"); } }
        void APIENTRY S_glGetMinmax(GLenum target, GLboolean reset, GLenum format, GLenum type, void *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMinmax"); } }
        void APIENTRY S_glGetMinmaxParameterfv(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMinmaxParameterfv"); } }
        void APIENTRY S_glGetMinmaxParameteriv(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMinmaxParameteriv"); } }
        void APIENTRY S_glHistogram(GLenum target, GLsizei width, GLenum internalformat, GLboolean sink) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glHistogram"); } }
        void APIENTRY S_glMinmax(GLenum target, GLenum internalformat, GLboolean sink) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMinmax"); } }
        void APIENTRY S_glResetHistogram(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glResetHistogram"); } }
        void APIENTRY S_glResetMinmax(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glResetMinmax"); } }
        void APIENTRY S_glMultiDrawArraysIndirectCountARB(GLenum mode, const void *indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawArraysIndirectCountARB"); } }
        void APIENTRY S_glMultiDrawElementsIndirectCountARB(GLenum mode, GLenum type, const void *indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawElementsIndirectCountARB"); } }
        void APIENTRY S_glVertexAttribDivisorARB(GLuint index, GLuint divisor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribDivisorARB"); } }
        void APIENTRY S_glCurrentPaletteMatrixARB(GLint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCurrentPaletteMatrixARB"); } }
        void APIENTRY S_glMatrixIndexubvARB(GLint size, const GLubyte *indices) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixIndexubvARB"); } }
        void APIENTRY S_glMatrixIndexusvARB(GLint size, const GLushort *indices) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixIndexusvARB"); } }
        void APIENTRY S_glMatrixIndexuivARB(GLint size, const GLuint *indices) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixIndexuivARB"); } }
        void APIENTRY S_glMatrixIndexPointerARB(GLint size, GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixIndexPointerARB"); } }
        void APIENTRY S_glSampleCoverageARB(GLfloat value, GLboolean invert) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSampleCoverageARB"); } }
        void APIENTRY S_glActiveTextureARB(GLenum texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glActiveTextureARB"); } }
        void APIENTRY S_glClientActiveTextureARB(GLenum texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClientActiveTextureARB"); } }
        void APIENTRY S_glMultiTexCoord1dARB(GLenum target, GLdouble s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1dARB"); } }
        void APIENTRY S_glMultiTexCoord1dvARB(GLenum target, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1dvARB"); } }
        void APIENTRY S_glMultiTexCoord1fARB(GLenum target, GLfloat s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1fARB"); } }
        void APIENTRY S_glMultiTexCoord1fvARB(GLenum target, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1fvARB"); } }
        void APIENTRY S_glMultiTexCoord1iARB(GLenum target, GLint s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1iARB"); } }
        void APIENTRY S_glMultiTexCoord1ivARB(GLenum target, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1ivARB"); } }
        void APIENTRY S_glMultiTexCoord1sARB(GLenum target, GLshort s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1sARB"); } }
        void APIENTRY S_glMultiTexCoord1svARB(GLenum target, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1svARB"); } }
        void APIENTRY S_glMultiTexCoord2dARB(GLenum target, GLdouble s, GLdouble t) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2dARB"); } }
        void APIENTRY S_glMultiTexCoord2dvARB(GLenum target, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2dvARB"); } }
        void APIENTRY S_glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2fARB"); } }
        void APIENTRY S_glMultiTexCoord2fvARB(GLenum target, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2fvARB"); } }
        void APIENTRY S_glMultiTexCoord2iARB(GLenum target, GLint s, GLint t) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2iARB"); } }
        void APIENTRY S_glMultiTexCoord2ivARB(GLenum target, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2ivARB"); } }
        void APIENTRY S_glMultiTexCoord2sARB(GLenum target, GLshort s, GLshort t) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2sARB"); } }
        void APIENTRY S_glMultiTexCoord2svARB(GLenum target, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2svARB"); } }
        void APIENTRY S_glMultiTexCoord3dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3dARB"); } }
        void APIENTRY S_glMultiTexCoord3dvARB(GLenum target, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3dvARB"); } }
        void APIENTRY S_glMultiTexCoord3fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3fARB"); } }
        void APIENTRY S_glMultiTexCoord3fvARB(GLenum target, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3fvARB"); } }
        void APIENTRY S_glMultiTexCoord3iARB(GLenum target, GLint s, GLint t, GLint r) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3iARB"); } }
        void APIENTRY S_glMultiTexCoord3ivARB(GLenum target, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3ivARB"); } }
        void APIENTRY S_glMultiTexCoord3sARB(GLenum target, GLshort s, GLshort t, GLshort r) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3sARB"); } }
        void APIENTRY S_glMultiTexCoord3svARB(GLenum target, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3svARB"); } }
        void APIENTRY S_glMultiTexCoord4dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4dARB"); } }
        void APIENTRY S_glMultiTexCoord4dvARB(GLenum target, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4dvARB"); } }
        void APIENTRY S_glMultiTexCoord4fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4fARB"); } }
        void APIENTRY S_glMultiTexCoord4fvARB(GLenum target, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4fvARB"); } }
        void APIENTRY S_glMultiTexCoord4iARB(GLenum target, GLint s, GLint t, GLint r, GLint q) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4iARB"); } }
        void APIENTRY S_glMultiTexCoord4ivARB(GLenum target, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4ivARB"); } }
        void APIENTRY S_glMultiTexCoord4sARB(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4sARB"); } }
        void APIENTRY S_glMultiTexCoord4svARB(GLenum target, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4svARB"); } }
        void APIENTRY S_glGenQueriesARB(GLsizei n, GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenQueriesARB"); } }
        void APIENTRY S_glDeleteQueriesARB(GLsizei n, const GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteQueriesARB"); } }
        GLboolean APIENTRY S_glIsQueryARB(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsQueryARB"); } return {}; }
        void APIENTRY S_glBeginQueryARB(GLenum target, GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginQueryARB"); } }
        void APIENTRY S_glEndQueryARB(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndQueryARB"); } }
        void APIENTRY S_glGetQueryivARB(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryivARB"); } }
        void APIENTRY S_glGetQueryObjectivARB(GLuint id, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryObjectivARB"); } }
        void APIENTRY S_glGetQueryObjectuivARB(GLuint id, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryObjectuivARB"); } }
        void APIENTRY S_glMaxShaderCompilerThreadsARB(GLuint count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMaxShaderCompilerThreadsARB"); } }
        void APIENTRY S_glPointParameterfARB(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterfARB"); } }
        void APIENTRY S_glPointParameterfvARB(GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterfvARB"); } }
        GLenum APIENTRY S_glGetGraphicsResetStatusARB(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetGraphicsResetStatusARB"); } return {}; }
        void APIENTRY S_glGetnTexImageARB(GLenum target, GLint level, GLenum format, GLenum type, GLsizei bufSize, void *img) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnTexImageARB"); } }
        void APIENTRY S_glReadnPixelsARB(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLsizei bufSize, void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReadnPixelsARB"); } }
        void APIENTRY S_glGetnCompressedTexImageARB(GLenum target, GLint lod, GLsizei bufSize, void *img) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnCompressedTexImageARB"); } }
        void APIENTRY S_glGetnUniformfvARB(GLuint program, GLint location, GLsizei bufSize, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformfvARB"); } }
        void APIENTRY S_glGetnUniformivARB(GLuint program, GLint location, GLsizei bufSize, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformivARB"); } }
        void APIENTRY S_glGetnUniformuivARB(GLuint program, GLint location, GLsizei bufSize, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformuivARB"); } }
        void APIENTRY S_glGetnUniformdvARB(GLuint program, GLint location, GLsizei bufSize, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformdvARB"); } }
        void APIENTRY S_glGetnMapdvARB(GLenum target, GLenum query, GLsizei bufSize, GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnMapdvARB"); } }
        void APIENTRY S_glGetnMapfvARB(GLenum target, GLenum query, GLsizei bufSize, GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnMapfvARB"); } }
        void APIENTRY S_glGetnMapivARB(GLenum target, GLenum query, GLsizei bufSize, GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnMapivARB"); } }
        void APIENTRY S_glGetnPixelMapfvARB(GLenum map, GLsizei bufSize, GLfloat *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnPixelMapfvARB"); } }
        void APIENTRY S_glGetnPixelMapuivARB(GLenum map, GLsizei bufSize, GLuint *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnPixelMapuivARB"); } }
        void APIENTRY S_glGetnPixelMapusvARB(GLenum map, GLsizei bufSize, GLushort *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnPixelMapusvARB"); } }
        void APIENTRY S_glGetnPolygonStippleARB(GLsizei bufSize, GLubyte *pattern) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnPolygonStippleARB"); } }
        void APIENTRY S_glGetnColorTableARB(GLenum target, GLenum format, GLenum type, GLsizei bufSize, void *table) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnColorTableARB"); } }
        void APIENTRY S_glGetnConvolutionFilterARB(GLenum target, GLenum format, GLenum type, GLsizei bufSize, void *image) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnConvolutionFilterARB"); } }
        void APIENTRY S_glGetnSeparableFilterARB(GLenum target, GLenum format, GLenum type, GLsizei rowBufSize, void *row, GLsizei columnBufSize, void *column, void *span) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnSeparableFilterARB"); } }
        void APIENTRY S_glGetnHistogramARB(GLenum target, GLboolean reset, GLenum format, GLenum type, GLsizei bufSize, void *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnHistogramARB"); } }
        void APIENTRY S_glGetnMinmaxARB(GLenum target, GLboolean reset, GLenum format, GLenum type, GLsizei bufSize, void *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnMinmaxARB"); } }
        void APIENTRY S_glFramebufferSampleLocationsfvARB(GLenum target, GLuint start, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferSampleLocationsfvARB"); } }
        void APIENTRY S_glNamedFramebufferSampleLocationsfvARB(GLuint framebuffer, GLuint start, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferSampleLocationsfvARB"); } }
        void APIENTRY S_glEvaluateDepthValuesARB(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEvaluateDepthValuesARB"); } }
        void APIENTRY S_glMinSampleShadingARB(GLfloat value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMinSampleShadingARB"); } }
        void APIENTRY S_glDeleteObjectARB(GLhandleARB obj) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteObjectARB"); } }
        GLhandleARB APIENTRY S_glGetHandleARB(GLenum pname) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetHandleARB"); } return {}; }
        void APIENTRY S_glDetachObjectARB(GLhandleARB containerObj, GLhandleARB attachedObj) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDetachObjectARB"); } }
        GLhandleARB APIENTRY S_glCreateShaderObjectARB(GLenum shaderType) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateShaderObjectARB"); } return {}; }
        void APIENTRY S_glShaderSourceARB(GLhandleARB shaderObj, GLsizei count, const GLcharARB **string, const GLint *length) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShaderSourceARB"); } }
        void APIENTRY S_glCompileShaderARB(GLhandleARB shaderObj) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompileShaderARB"); } }
        GLhandleARB APIENTRY S_glCreateProgramObjectARB(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateProgramObjectARB"); } return {}; }
        void APIENTRY S_glAttachObjectARB(GLhandleARB containerObj, GLhandleARB obj) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAttachObjectARB"); } }
        void APIENTRY S_glLinkProgramARB(GLhandleARB programObj) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLinkProgramARB"); } }
        void APIENTRY S_glUseProgramObjectARB(GLhandleARB programObj) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUseProgramObjectARB"); } }
        void APIENTRY S_glValidateProgramARB(GLhandleARB programObj) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glValidateProgramARB"); } }
        void APIENTRY S_glUniform1fARB(GLint location, GLfloat v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1fARB"); } }
        void APIENTRY S_glUniform2fARB(GLint location, GLfloat v0, GLfloat v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2fARB"); } }
        void APIENTRY S_glUniform3fARB(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3fARB"); } }
        void APIENTRY S_glUniform4fARB(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4fARB"); } }
        void APIENTRY S_glUniform1iARB(GLint location, GLint v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1iARB"); } }
        void APIENTRY S_glUniform2iARB(GLint location, GLint v0, GLint v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2iARB"); } }
        void APIENTRY S_glUniform3iARB(GLint location, GLint v0, GLint v1, GLint v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3iARB"); } }
        void APIENTRY S_glUniform4iARB(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4iARB"); } }
        void APIENTRY S_glUniform1fvARB(GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1fvARB"); } }
        void APIENTRY S_glUniform2fvARB(GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2fvARB"); } }
        void APIENTRY S_glUniform3fvARB(GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3fvARB"); } }
        void APIENTRY S_glUniform4fvARB(GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4fvARB"); } }
        void APIENTRY S_glUniform1ivARB(GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1ivARB"); } }
        void APIENTRY S_glUniform2ivARB(GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2ivARB"); } }
        void APIENTRY S_glUniform3ivARB(GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3ivARB"); } }
        void APIENTRY S_glUniform4ivARB(GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4ivARB"); } }
        void APIENTRY S_glUniformMatrix2fvARB(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix2fvARB"); } }
        void APIENTRY S_glUniformMatrix3fvARB(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix3fvARB"); } }
        void APIENTRY S_glUniformMatrix4fvARB(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformMatrix4fvARB"); } }
        void APIENTRY S_glGetObjectParameterfvARB(GLhandleARB obj, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetObjectParameterfvARB"); } }
        void APIENTRY S_glGetObjectParameterivARB(GLhandleARB obj, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetObjectParameterivARB"); } }
        void APIENTRY S_glGetInfoLogARB(GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *infoLog) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetInfoLogARB"); } }
        void APIENTRY S_glGetAttachedObjectsARB(GLhandleARB containerObj, GLsizei maxCount, GLsizei *count, GLhandleARB *obj) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetAttachedObjectsARB"); } }
        GLint APIENTRY S_glGetUniformLocationARB(GLhandleARB programObj, const GLcharARB *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformLocationARB"); } return {}; }
        void APIENTRY S_glGetActiveUniformARB(GLhandleARB programObj, GLuint index, GLsizei maxLength, GLsizei *length, GLint *size, GLenum *type, GLcharARB *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveUniformARB"); } }
        void APIENTRY S_glGetUniformfvARB(GLhandleARB programObj, GLint location, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformfvARB"); } }
        void APIENTRY S_glGetUniformivARB(GLhandleARB programObj, GLint location, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformivARB"); } }
        void APIENTRY S_glGetShaderSourceARB(GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *source) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetShaderSourceARB"); } }
        void APIENTRY S_glNamedStringARB(GLenum type, GLint namelen, const GLchar *name, GLint stringlen, const GLchar *string) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedStringARB"); } }
        void APIENTRY S_glDeleteNamedStringARB(GLint namelen, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteNamedStringARB"); } }
        void APIENTRY S_glCompileShaderIncludeARB(GLuint shader, GLsizei count, const GLchar *const*path, const GLint *length) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompileShaderIncludeARB"); } }
        GLboolean APIENTRY S_glIsNamedStringARB(GLint namelen, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsNamedStringARB"); } return {}; }
        void APIENTRY S_glGetNamedStringARB(GLint namelen, const GLchar *name, GLsizei bufSize, GLint *stringlen, GLchar *string) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedStringARB"); } }
        void APIENTRY S_glGetNamedStringivARB(GLint namelen, const GLchar *name, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedStringivARB"); } }
        void APIENTRY S_glBufferPageCommitmentARB(GLenum target, GLintptr offset, GLsizeiptr size, GLboolean commit) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferPageCommitmentARB"); } }
        void APIENTRY S_glNamedBufferPageCommitmentEXT(GLuint buffer, GLintptr offset, GLsizeiptr size, GLboolean commit) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferPageCommitmentEXT"); } }
        void APIENTRY S_glNamedBufferPageCommitmentARB(GLuint buffer, GLintptr offset, GLsizeiptr size, GLboolean commit) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferPageCommitmentARB"); } }
        void APIENTRY S_glTexPageCommitmentARB(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLboolean commit) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexPageCommitmentARB"); } }
        void APIENTRY S_glTexBufferARB(GLenum target, GLenum internalformat, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexBufferARB"); } }
        void APIENTRY S_glCompressedTexImage3DARB(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexImage3DARB"); } }
        void APIENTRY S_glCompressedTexImage2DARB(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexImage2DARB"); } }
        void APIENTRY S_glCompressedTexImage1DARB(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexImage1DARB"); } }
        void APIENTRY S_glCompressedTexSubImage3DARB(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexSubImage3DARB"); } }
        void APIENTRY S_glCompressedTexSubImage2DARB(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexSubImage2DARB"); } }
        void APIENTRY S_glCompressedTexSubImage1DARB(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTexSubImage1DARB"); } }
        void APIENTRY S_glGetCompressedTexImageARB(GLenum target, GLint level, void *img) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCompressedTexImageARB"); } }
        void APIENTRY S_glLoadTransposeMatrixfARB(const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLoadTransposeMatrixfARB"); } }
        void APIENTRY S_glLoadTransposeMatrixdARB(const GLdouble *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLoadTransposeMatrixdARB"); } }
        void APIENTRY S_glMultTransposeMatrixfARB(const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultTransposeMatrixfARB"); } }
        void APIENTRY S_glMultTransposeMatrixdARB(const GLdouble *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultTransposeMatrixdARB"); } }
        void APIENTRY S_glWeightbvARB(GLint size, const GLbyte *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWeightbvARB"); } }
        void APIENTRY S_glWeightsvARB(GLint size, const GLshort *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWeightsvARB"); } }
        void APIENTRY S_glWeightivARB(GLint size, const GLint *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWeightivARB"); } }
        void APIENTRY S_glWeightfvARB(GLint size, const GLfloat *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWeightfvARB"); } }
        void APIENTRY S_glWeightdvARB(GLint size, const GLdouble *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWeightdvARB"); } }
        void APIENTRY S_glWeightubvARB(GLint size, const GLubyte *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWeightubvARB"); } }
        void APIENTRY S_glWeightusvARB(GLint size, const GLushort *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWeightusvARB"); } }
        void APIENTRY S_glWeightuivARB(GLint size, const GLuint *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWeightuivARB"); } }
        void APIENTRY S_glWeightPointerARB(GLint size, GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWeightPointerARB"); } }
        void APIENTRY S_glVertexBlendARB(GLint count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexBlendARB"); } }
        void APIENTRY S_glBindBufferARB(GLenum target, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBufferARB"); } }
        void APIENTRY S_glDeleteBuffersARB(GLsizei n, const GLuint *buffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteBuffersARB"); } }
        void APIENTRY S_glGenBuffersARB(GLsizei n, GLuint *buffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenBuffersARB"); } }
        GLboolean APIENTRY S_glIsBufferARB(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsBufferARB"); } return {}; }
        void APIENTRY S_glBufferDataARB(GLenum target, GLsizeiptrARB size, const void *data, GLenum usage) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferDataARB"); } }
        void APIENTRY S_glBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferSubDataARB"); } }
        void APIENTRY S_glGetBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBufferSubDataARB"); } }
        void * APIENTRY S_glMapBufferARB(GLenum target, GLenum access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapBufferARB"); } return {}; }
        GLboolean APIENTRY S_glUnmapBufferARB(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUnmapBufferARB"); } return {}; }
        void APIENTRY S_glGetBufferParameterivARB(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBufferParameterivARB"); } }
        void APIENTRY S_glGetBufferPointervARB(GLenum target, GLenum pname, void **params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBufferPointervARB"); } }
        void APIENTRY S_glVertexAttrib1dARB(GLuint index, GLdouble x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1dARB"); } }
        void APIENTRY S_glVertexAttrib1dvARB(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1dvARB"); } }
        void APIENTRY S_glVertexAttrib1fARB(GLuint index, GLfloat x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1fARB"); } }
        void APIENTRY S_glVertexAttrib1fvARB(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1fvARB"); } }
        void APIENTRY S_glVertexAttrib1sARB(GLuint index, GLshort x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1sARB"); } }
        void APIENTRY S_glVertexAttrib1svARB(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1svARB"); } }
        void APIENTRY S_glVertexAttrib2dARB(GLuint index, GLdouble x, GLdouble y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2dARB"); } }
        void APIENTRY S_glVertexAttrib2dvARB(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2dvARB"); } }
        void APIENTRY S_glVertexAttrib2fARB(GLuint index, GLfloat x, GLfloat y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2fARB"); } }
        void APIENTRY S_glVertexAttrib2fvARB(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2fvARB"); } }
        void APIENTRY S_glVertexAttrib2sARB(GLuint index, GLshort x, GLshort y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2sARB"); } }
        void APIENTRY S_glVertexAttrib2svARB(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2svARB"); } }
        void APIENTRY S_glVertexAttrib3dARB(GLuint index, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3dARB"); } }
        void APIENTRY S_glVertexAttrib3dvARB(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3dvARB"); } }
        void APIENTRY S_glVertexAttrib3fARB(GLuint index, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3fARB"); } }
        void APIENTRY S_glVertexAttrib3fvARB(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3fvARB"); } }
        void APIENTRY S_glVertexAttrib3sARB(GLuint index, GLshort x, GLshort y, GLshort z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3sARB"); } }
        void APIENTRY S_glVertexAttrib3svARB(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3svARB"); } }
        void APIENTRY S_glVertexAttrib4NbvARB(GLuint index, const GLbyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4NbvARB"); } }
        void APIENTRY S_glVertexAttrib4NivARB(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4NivARB"); } }
        void APIENTRY S_glVertexAttrib4NsvARB(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4NsvARB"); } }
        void APIENTRY S_glVertexAttrib4NubARB(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4NubARB"); } }
        void APIENTRY S_glVertexAttrib4NubvARB(GLuint index, const GLubyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4NubvARB"); } }
        void APIENTRY S_glVertexAttrib4NuivARB(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4NuivARB"); } }
        void APIENTRY S_glVertexAttrib4NusvARB(GLuint index, const GLushort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4NusvARB"); } }
        void APIENTRY S_glVertexAttrib4bvARB(GLuint index, const GLbyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4bvARB"); } }
        void APIENTRY S_glVertexAttrib4dARB(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4dARB"); } }
        void APIENTRY S_glVertexAttrib4dvARB(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4dvARB"); } }
        void APIENTRY S_glVertexAttrib4fARB(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4fARB"); } }
        void APIENTRY S_glVertexAttrib4fvARB(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4fvARB"); } }
        void APIENTRY S_glVertexAttrib4ivARB(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4ivARB"); } }
        void APIENTRY S_glVertexAttrib4sARB(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4sARB"); } }
        void APIENTRY S_glVertexAttrib4svARB(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4svARB"); } }
        void APIENTRY S_glVertexAttrib4ubvARB(GLuint index, const GLubyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4ubvARB"); } }
        void APIENTRY S_glVertexAttrib4uivARB(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4uivARB"); } }
        void APIENTRY S_glVertexAttrib4usvARB(GLuint index, const GLushort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4usvARB"); } }
        void APIENTRY S_glVertexAttribPointerARB(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribPointerARB"); } }
        void APIENTRY S_glEnableVertexAttribArrayARB(GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnableVertexAttribArrayARB"); } }
        void APIENTRY S_glDisableVertexAttribArrayARB(GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisableVertexAttribArrayARB"); } }
        void APIENTRY S_glGetVertexAttribdvARB(GLuint index, GLenum pname, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribdvARB"); } }
        void APIENTRY S_glGetVertexAttribfvARB(GLuint index, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribfvARB"); } }
        void APIENTRY S_glGetVertexAttribivARB(GLuint index, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribivARB"); } }
        void APIENTRY S_glGetVertexAttribPointervARB(GLuint index, GLenum pname, void **pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribPointervARB"); } }
        void APIENTRY S_glBindAttribLocationARB(GLhandleARB programObj, GLuint index, const GLcharARB *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindAttribLocationARB"); } }
        void APIENTRY S_glGetActiveAttribARB(GLhandleARB programObj, GLuint index, GLsizei maxLength, GLsizei *length, GLint *size, GLenum *type, GLcharARB *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveAttribARB"); } }
        GLint APIENTRY S_glGetAttribLocationARB(GLhandleARB programObj, const GLcharARB *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetAttribLocationARB"); } return {}; }
        void APIENTRY S_glDepthRangeArraydvNV(GLuint first, GLsizei count, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthRangeArraydvNV"); } }
        void APIENTRY S_glDepthRangeIndexeddNV(GLuint index, GLdouble n, GLdouble f) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthRangeIndexeddNV"); } }
        void APIENTRY S_glWindowPos2dARB(GLdouble x, GLdouble y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2dARB"); } }
        void APIENTRY S_glWindowPos2dvARB(const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2dvARB"); } }
        void APIENTRY S_glWindowPos2fARB(GLfloat x, GLfloat y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2fARB"); } }
        void APIENTRY S_glWindowPos2fvARB(const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2fvARB"); } }
        void APIENTRY S_glWindowPos2iARB(GLint x, GLint y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2iARB"); } }
        void APIENTRY S_glWindowPos2ivARB(const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2ivARB"); } }
        void APIENTRY S_glWindowPos2sARB(GLshort x, GLshort y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2sARB"); } }
        void APIENTRY S_glWindowPos2svARB(const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2svARB"); } }
        void APIENTRY S_glWindowPos3dARB(GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3dARB"); } }
        void APIENTRY S_glWindowPos3dvARB(const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3dvARB"); } }
        void APIENTRY S_glWindowPos3fARB(GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3fARB"); } }
        void APIENTRY S_glWindowPos3fvARB(const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3fvARB"); } }
        void APIENTRY S_glWindowPos3iARB(GLint x, GLint y, GLint z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3iARB"); } }
        void APIENTRY S_glWindowPos3ivARB(const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3ivARB"); } }
        void APIENTRY S_glWindowPos3sARB(GLshort x, GLshort y, GLshort z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3sARB"); } }
        void APIENTRY S_glWindowPos3svARB(const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3svARB"); } }
        void APIENTRY S_glDrawBuffersATI(GLsizei n, const GLenum *bufs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawBuffersATI"); } }
        void APIENTRY S_glElementPointerATI(GLenum type, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glElementPointerATI"); } }
        void APIENTRY S_glDrawElementArrayATI(GLenum mode, GLsizei count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElementArrayATI"); } }
        void APIENTRY S_glDrawRangeElementArrayATI(GLenum mode, GLuint start, GLuint end, GLsizei count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawRangeElementArrayATI"); } }
        void APIENTRY S_glTexBumpParameterivATI(GLenum pname, const GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexBumpParameterivATI"); } }
        void APIENTRY S_glTexBumpParameterfvATI(GLenum pname, const GLfloat *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexBumpParameterfvATI"); } }
        void APIENTRY S_glGetTexBumpParameterivATI(GLenum pname, GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexBumpParameterivATI"); } }
        void APIENTRY S_glGetTexBumpParameterfvATI(GLenum pname, GLfloat *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexBumpParameterfvATI"); } }
        GLuint APIENTRY S_glGenFragmentShadersATI(GLuint range) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenFragmentShadersATI"); } return {}; }
        void APIENTRY S_glBindFragmentShaderATI(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindFragmentShaderATI"); } }
        void APIENTRY S_glDeleteFragmentShaderATI(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteFragmentShaderATI"); } }
        void APIENTRY S_glBeginFragmentShaderATI(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginFragmentShaderATI"); } }
        void APIENTRY S_glEndFragmentShaderATI(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndFragmentShaderATI"); } }
        void APIENTRY S_glPassTexCoordATI(GLuint dst, GLuint coord, GLenum swizzle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPassTexCoordATI"); } }
        void APIENTRY S_glSampleMapATI(GLuint dst, GLuint interp, GLenum swizzle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSampleMapATI"); } }
        void APIENTRY S_glColorFragmentOp1ATI(GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorFragmentOp1ATI"); } }
        void APIENTRY S_glColorFragmentOp2ATI(GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorFragmentOp2ATI"); } }
        void APIENTRY S_glColorFragmentOp3ATI(GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod, GLuint arg3, GLuint arg3Rep, GLuint arg3Mod) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorFragmentOp3ATI"); } }
        void APIENTRY S_glAlphaFragmentOp1ATI(GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAlphaFragmentOp1ATI"); } }
        void APIENTRY S_glAlphaFragmentOp2ATI(GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAlphaFragmentOp2ATI"); } }
        void APIENTRY S_glAlphaFragmentOp3ATI(GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod, GLuint arg3, GLuint arg3Rep, GLuint arg3Mod) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAlphaFragmentOp3ATI"); } }
        void APIENTRY S_glSetFragmentShaderConstantATI(GLuint dst, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSetFragmentShaderConstantATI"); } }
        void * APIENTRY S_glMapObjectBufferATI(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapObjectBufferATI"); } return {}; }
        void APIENTRY S_glUnmapObjectBufferATI(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUnmapObjectBufferATI"); } }
        void APIENTRY S_glPNTrianglesiATI(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPNTrianglesiATI"); } }
        void APIENTRY S_glPNTrianglesfATI(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPNTrianglesfATI"); } }
        void APIENTRY S_glStencilOpSeparateATI(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilOpSeparateATI"); } }
        void APIENTRY S_glStencilFuncSeparateATI(GLenum frontfunc, GLenum backfunc, GLint ref, GLuint mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilFuncSeparateATI"); } }
        GLuint APIENTRY S_glNewObjectBufferATI(GLsizei size, const void *pointer, GLenum usage) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNewObjectBufferATI"); } return {}; }
        GLboolean APIENTRY S_glIsObjectBufferATI(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsObjectBufferATI"); } return {}; }
        void APIENTRY S_glUpdateObjectBufferATI(GLuint buffer, GLuint offset, GLsizei size, const void *pointer, GLenum preserve) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUpdateObjectBufferATI"); } }
        void APIENTRY S_glGetObjectBufferfvATI(GLuint buffer, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetObjectBufferfvATI"); } }
        void APIENTRY S_glGetObjectBufferivATI(GLuint buffer, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetObjectBufferivATI"); } }
        void APIENTRY S_glFreeObjectBufferATI(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFreeObjectBufferATI"); } }
        void APIENTRY S_glArrayObjectATI(GLenum array, GLint size, GLenum type, GLsizei stride, GLuint buffer, GLuint offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glArrayObjectATI"); } }
        void APIENTRY S_glGetArrayObjectfvATI(GLenum array, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetArrayObjectfvATI"); } }
        void APIENTRY S_glGetArrayObjectivATI(GLenum array, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetArrayObjectivATI"); } }
        void APIENTRY S_glVariantArrayObjectATI(GLuint id, GLenum type, GLsizei stride, GLuint buffer, GLuint offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVariantArrayObjectATI"); } }
        void APIENTRY S_glGetVariantArrayObjectfvATI(GLuint id, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVariantArrayObjectfvATI"); } }
        void APIENTRY S_glGetVariantArrayObjectivATI(GLuint id, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVariantArrayObjectivATI"); } }
        void APIENTRY S_glVertexAttribArrayObjectATI(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLuint buffer, GLuint offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribArrayObjectATI"); } }
        void APIENTRY S_glGetVertexAttribArrayObjectfvATI(GLuint index, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribArrayObjectfvATI"); } }
        void APIENTRY S_glGetVertexAttribArrayObjectivATI(GLuint index, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribArrayObjectivATI"); } }
        void APIENTRY S_glVertexStream1sATI(GLenum stream, GLshort x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream1sATI"); } }
        void APIENTRY S_glVertexStream1svATI(GLenum stream, const GLshort *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream1svATI"); } }
        void APIENTRY S_glVertexStream1iATI(GLenum stream, GLint x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream1iATI"); } }
        void APIENTRY S_glVertexStream1ivATI(GLenum stream, const GLint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream1ivATI"); } }
        void APIENTRY S_glVertexStream1fATI(GLenum stream, GLfloat x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream1fATI"); } }
        void APIENTRY S_glVertexStream1fvATI(GLenum stream, const GLfloat *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream1fvATI"); } }
        void APIENTRY S_glVertexStream1dATI(GLenum stream, GLdouble x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream1dATI"); } }
        void APIENTRY S_glVertexStream1dvATI(GLenum stream, const GLdouble *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream1dvATI"); } }
        void APIENTRY S_glVertexStream2sATI(GLenum stream, GLshort x, GLshort y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream2sATI"); } }
        void APIENTRY S_glVertexStream2svATI(GLenum stream, const GLshort *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream2svATI"); } }
        void APIENTRY S_glVertexStream2iATI(GLenum stream, GLint x, GLint y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream2iATI"); } }
        void APIENTRY S_glVertexStream2ivATI(GLenum stream, const GLint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream2ivATI"); } }
        void APIENTRY S_glVertexStream2fATI(GLenum stream, GLfloat x, GLfloat y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream2fATI"); } }
        void APIENTRY S_glVertexStream2fvATI(GLenum stream, const GLfloat *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream2fvATI"); } }
        void APIENTRY S_glVertexStream2dATI(GLenum stream, GLdouble x, GLdouble y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream2dATI"); } }
        void APIENTRY S_glVertexStream2dvATI(GLenum stream, const GLdouble *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream2dvATI"); } }
        void APIENTRY S_glVertexStream3sATI(GLenum stream, GLshort x, GLshort y, GLshort z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream3sATI"); } }
        void APIENTRY S_glVertexStream3svATI(GLenum stream, const GLshort *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream3svATI"); } }
        void APIENTRY S_glVertexStream3iATI(GLenum stream, GLint x, GLint y, GLint z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream3iATI"); } }
        void APIENTRY S_glVertexStream3ivATI(GLenum stream, const GLint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream3ivATI"); } }
        void APIENTRY S_glVertexStream3fATI(GLenum stream, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream3fATI"); } }
        void APIENTRY S_glVertexStream3fvATI(GLenum stream, const GLfloat *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream3fvATI"); } }
        void APIENTRY S_glVertexStream3dATI(GLenum stream, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream3dATI"); } }
        void APIENTRY S_glVertexStream3dvATI(GLenum stream, const GLdouble *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream3dvATI"); } }
        void APIENTRY S_glVertexStream4sATI(GLenum stream, GLshort x, GLshort y, GLshort z, GLshort w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream4sATI"); } }
        void APIENTRY S_glVertexStream4svATI(GLenum stream, const GLshort *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream4svATI"); } }
        void APIENTRY S_glVertexStream4iATI(GLenum stream, GLint x, GLint y, GLint z, GLint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream4iATI"); } }
        void APIENTRY S_glVertexStream4ivATI(GLenum stream, const GLint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream4ivATI"); } }
        void APIENTRY S_glVertexStream4fATI(GLenum stream, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream4fATI"); } }
        void APIENTRY S_glVertexStream4fvATI(GLenum stream, const GLfloat *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream4fvATI"); } }
        void APIENTRY S_glVertexStream4dATI(GLenum stream, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream4dATI"); } }
        void APIENTRY S_glVertexStream4dvATI(GLenum stream, const GLdouble *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexStream4dvATI"); } }
        void APIENTRY S_glNormalStream3bATI(GLenum stream, GLbyte nx, GLbyte ny, GLbyte nz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalStream3bATI"); } }
        void APIENTRY S_glNormalStream3bvATI(GLenum stream, const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalStream3bvATI"); } }
        void APIENTRY S_glNormalStream3sATI(GLenum stream, GLshort nx, GLshort ny, GLshort nz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalStream3sATI"); } }
        void APIENTRY S_glNormalStream3svATI(GLenum stream, const GLshort *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalStream3svATI"); } }
        void APIENTRY S_glNormalStream3iATI(GLenum stream, GLint nx, GLint ny, GLint nz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalStream3iATI"); } }
        void APIENTRY S_glNormalStream3ivATI(GLenum stream, const GLint *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalStream3ivATI"); } }
        void APIENTRY S_glNormalStream3fATI(GLenum stream, GLfloat nx, GLfloat ny, GLfloat nz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalStream3fATI"); } }
        void APIENTRY S_glNormalStream3fvATI(GLenum stream, const GLfloat *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalStream3fvATI"); } }
        void APIENTRY S_glNormalStream3dATI(GLenum stream, GLdouble nx, GLdouble ny, GLdouble nz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalStream3dATI"); } }
        void APIENTRY S_glNormalStream3dvATI(GLenum stream, const GLdouble *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalStream3dvATI"); } }
        void APIENTRY S_glClientActiveVertexStreamATI(GLenum stream) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClientActiveVertexStreamATI"); } }
        void APIENTRY S_glVertexBlendEnviATI(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexBlendEnviATI"); } }
        void APIENTRY S_glVertexBlendEnvfATI(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexBlendEnvfATI"); } }
        void APIENTRY S_glEGLImageTargetTexStorageEXT(GLenum target, GLeglImageOES image, const GLint* attrib_list) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEGLImageTargetTexStorageEXT"); } }
        void APIENTRY S_glEGLImageTargetTextureStorageEXT(GLuint texture, GLeglImageOES image, const GLint* attrib_list) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEGLImageTargetTextureStorageEXT"); } }
        void APIENTRY S_glUniformBufferEXT(GLuint program, GLint location, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformBufferEXT"); } }
        GLint APIENTRY S_glGetUniformBufferSizeEXT(GLuint program, GLint location) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformBufferSizeEXT"); } return {}; }
        GLintptr APIENTRY S_glGetUniformOffsetEXT(GLuint program, GLint location) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformOffsetEXT"); } return {}; }
        void APIENTRY S_glBlendColorEXT(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendColorEXT"); } }
        void APIENTRY S_glBlendEquationSeparateEXT(GLenum modeRGB, GLenum modeAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendEquationSeparateEXT"); } }
        void APIENTRY S_glBlendFuncSeparateEXT(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendFuncSeparateEXT"); } }
        void APIENTRY S_glBlendEquationEXT(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendEquationEXT"); } }
        void APIENTRY S_glColorSubTableEXT(GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorSubTableEXT"); } }
        void APIENTRY S_glCopyColorSubTableEXT(GLenum target, GLsizei start, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyColorSubTableEXT"); } }
        void APIENTRY S_glLockArraysEXT(GLint first, GLsizei count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLockArraysEXT"); } }
        void APIENTRY S_glUnlockArraysEXT(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUnlockArraysEXT"); } }
        void APIENTRY S_glConvolutionFilter1DEXT(GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const void *image) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionFilter1DEXT"); } }
        void APIENTRY S_glConvolutionFilter2DEXT(GLenum target, GLenum internalformat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *image) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionFilter2DEXT"); } }
        void APIENTRY S_glConvolutionParameterfEXT(GLenum target, GLenum pname, GLfloat params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionParameterfEXT"); } }
        void APIENTRY S_glConvolutionParameterfvEXT(GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionParameterfvEXT"); } }
        void APIENTRY S_glConvolutionParameteriEXT(GLenum target, GLenum pname, GLint params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionParameteriEXT"); } }
        void APIENTRY S_glConvolutionParameterivEXT(GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionParameterivEXT"); } }
        void APIENTRY S_glCopyConvolutionFilter1DEXT(GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyConvolutionFilter1DEXT"); } }
        void APIENTRY S_glCopyConvolutionFilter2DEXT(GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyConvolutionFilter2DEXT"); } }
        void APIENTRY S_glGetConvolutionFilterEXT(GLenum target, GLenum format, GLenum type, void *image) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetConvolutionFilterEXT"); } }
        void APIENTRY S_glGetConvolutionParameterfvEXT(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetConvolutionParameterfvEXT"); } }
        void APIENTRY S_glGetConvolutionParameterivEXT(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetConvolutionParameterivEXT"); } }
        void APIENTRY S_glGetSeparableFilterEXT(GLenum target, GLenum format, GLenum type, void *row, void *column, void *span) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSeparableFilterEXT"); } }
        void APIENTRY S_glSeparableFilter2DEXT(GLenum target, GLenum internalformat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *row, const void *column) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSeparableFilter2DEXT"); } }
        void APIENTRY S_glTangent3bEXT(GLbyte tx, GLbyte ty, GLbyte tz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangent3bEXT"); } }
        void APIENTRY S_glTangent3bvEXT(const GLbyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangent3bvEXT"); } }
        void APIENTRY S_glTangent3dEXT(GLdouble tx, GLdouble ty, GLdouble tz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangent3dEXT"); } }
        void APIENTRY S_glTangent3dvEXT(const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangent3dvEXT"); } }
        void APIENTRY S_glTangent3fEXT(GLfloat tx, GLfloat ty, GLfloat tz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangent3fEXT"); } }
        void APIENTRY S_glTangent3fvEXT(const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangent3fvEXT"); } }
        void APIENTRY S_glTangent3iEXT(GLint tx, GLint ty, GLint tz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangent3iEXT"); } }
        void APIENTRY S_glTangent3ivEXT(const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangent3ivEXT"); } }
        void APIENTRY S_glTangent3sEXT(GLshort tx, GLshort ty, GLshort tz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangent3sEXT"); } }
        void APIENTRY S_glTangent3svEXT(const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangent3svEXT"); } }
        void APIENTRY S_glBinormal3bEXT(GLbyte bx, GLbyte by, GLbyte bz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormal3bEXT"); } }
        void APIENTRY S_glBinormal3bvEXT(const GLbyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormal3bvEXT"); } }
        void APIENTRY S_glBinormal3dEXT(GLdouble bx, GLdouble by, GLdouble bz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormal3dEXT"); } }
        void APIENTRY S_glBinormal3dvEXT(const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormal3dvEXT"); } }
        void APIENTRY S_glBinormal3fEXT(GLfloat bx, GLfloat by, GLfloat bz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormal3fEXT"); } }
        void APIENTRY S_glBinormal3fvEXT(const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormal3fvEXT"); } }
        void APIENTRY S_glBinormal3iEXT(GLint bx, GLint by, GLint bz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormal3iEXT"); } }
        void APIENTRY S_glBinormal3ivEXT(const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormal3ivEXT"); } }
        void APIENTRY S_glBinormal3sEXT(GLshort bx, GLshort by, GLshort bz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormal3sEXT"); } }
        void APIENTRY S_glBinormal3svEXT(const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormal3svEXT"); } }
        void APIENTRY S_glTangentPointerEXT(GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTangentPointerEXT"); } }
        void APIENTRY S_glBinormalPointerEXT(GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBinormalPointerEXT"); } }
        void APIENTRY S_glCopyTexImage1DEXT(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTexImage1DEXT"); } }
        void APIENTRY S_glCopyTexImage2DEXT(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTexImage2DEXT"); } }
        void APIENTRY S_glCopyTexSubImage1DEXT(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTexSubImage1DEXT"); } }
        void APIENTRY S_glCopyTexSubImage2DEXT(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTexSubImage2DEXT"); } }
        void APIENTRY S_glCopyTexSubImage3DEXT(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTexSubImage3DEXT"); } }
        void APIENTRY S_glCullParameterdvEXT(GLenum pname, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCullParameterdvEXT"); } }
        void APIENTRY S_glCullParameterfvEXT(GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCullParameterfvEXT"); } }
        void APIENTRY S_glLabelObjectEXT(GLenum type, GLuint object, GLsizei length, const GLchar *label) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLabelObjectEXT"); } }
        void APIENTRY S_glGetObjectLabelEXT(GLenum type, GLuint object, GLsizei bufSize, GLsizei *length, GLchar *label) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetObjectLabelEXT"); } }
        void APIENTRY S_glInsertEventMarkerEXT(GLsizei length, const GLchar *marker) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInsertEventMarkerEXT"); } }
        void APIENTRY S_glPushGroupMarkerEXT(GLsizei length, const GLchar *marker) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPushGroupMarkerEXT"); } }
        void APIENTRY S_glPopGroupMarkerEXT(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPopGroupMarkerEXT"); } }
        void APIENTRY S_glDepthBoundsEXT(GLclampd zmin, GLclampd zmax) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthBoundsEXT"); } }
        void APIENTRY S_glMatrixLoadfEXT(GLenum mode, const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixLoadfEXT"); } }
        void APIENTRY S_glMatrixLoaddEXT(GLenum mode, const GLdouble *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixLoaddEXT"); } }
        void APIENTRY S_glMatrixMultfEXT(GLenum mode, const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixMultfEXT"); } }
        void APIENTRY S_glMatrixMultdEXT(GLenum mode, const GLdouble *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixMultdEXT"); } }
        void APIENTRY S_glMatrixLoadIdentityEXT(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixLoadIdentityEXT"); } }
        void APIENTRY S_glMatrixRotatefEXT(GLenum mode, GLfloat angle, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixRotatefEXT"); } }
        void APIENTRY S_glMatrixRotatedEXT(GLenum mode, GLdouble angle, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixRotatedEXT"); } }
        void APIENTRY S_glMatrixScalefEXT(GLenum mode, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixScalefEXT"); } }
        void APIENTRY S_glMatrixScaledEXT(GLenum mode, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixScaledEXT"); } }
        void APIENTRY S_glMatrixTranslatefEXT(GLenum mode, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixTranslatefEXT"); } }
        void APIENTRY S_glMatrixTranslatedEXT(GLenum mode, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixTranslatedEXT"); } }
        void APIENTRY S_glMatrixFrustumEXT(GLenum mode, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixFrustumEXT"); } }
        void APIENTRY S_glMatrixOrthoEXT(GLenum mode, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixOrthoEXT"); } }
        void APIENTRY S_glMatrixPopEXT(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixPopEXT"); } }
        void APIENTRY S_glMatrixPushEXT(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixPushEXT"); } }
        void APIENTRY S_glClientAttribDefaultEXT(GLbitfield mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClientAttribDefaultEXT"); } }
        void APIENTRY S_glPushClientAttribDefaultEXT(GLbitfield mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPushClientAttribDefaultEXT"); } }
        void APIENTRY S_glTextureParameterfEXT(GLuint texture, GLenum target, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameterfEXT"); } }
        void APIENTRY S_glTextureParameterfvEXT(GLuint texture, GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameterfvEXT"); } }
        void APIENTRY S_glTextureParameteriEXT(GLuint texture, GLenum target, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameteriEXT"); } }
        void APIENTRY S_glTextureParameterivEXT(GLuint texture, GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameterivEXT"); } }
        void APIENTRY S_glTextureImage1DEXT(GLuint texture, GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureImage1DEXT"); } }
        void APIENTRY S_glTextureImage2DEXT(GLuint texture, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureImage2DEXT"); } }
        void APIENTRY S_glTextureSubImage1DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureSubImage1DEXT"); } }
        void APIENTRY S_glTextureSubImage2DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureSubImage2DEXT"); } }
        void APIENTRY S_glCopyTextureImage1DEXT(GLuint texture, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTextureImage1DEXT"); } }
        void APIENTRY S_glCopyTextureImage2DEXT(GLuint texture, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTextureImage2DEXT"); } }
        void APIENTRY S_glCopyTextureSubImage1DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTextureSubImage1DEXT"); } }
        void APIENTRY S_glCopyTextureSubImage2DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTextureSubImage2DEXT"); } }
        void APIENTRY S_glGetTextureImageEXT(GLuint texture, GLenum target, GLint level, GLenum format, GLenum type, void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureImageEXT"); } }
        void APIENTRY S_glGetTextureParameterfvEXT(GLuint texture, GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureParameterfvEXT"); } }
        void APIENTRY S_glGetTextureParameterivEXT(GLuint texture, GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureParameterivEXT"); } }
        void APIENTRY S_glGetTextureLevelParameterfvEXT(GLuint texture, GLenum target, GLint level, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureLevelParameterfvEXT"); } }
        void APIENTRY S_glGetTextureLevelParameterivEXT(GLuint texture, GLenum target, GLint level, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureLevelParameterivEXT"); } }
        void APIENTRY S_glTextureImage3DEXT(GLuint texture, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureImage3DEXT"); } }
        void APIENTRY S_glTextureSubImage3DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureSubImage3DEXT"); } }
        void APIENTRY S_glCopyTextureSubImage3DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyTextureSubImage3DEXT"); } }
        void APIENTRY S_glBindMultiTextureEXT(GLenum texunit, GLenum target, GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindMultiTextureEXT"); } }
        void APIENTRY S_glMultiTexCoordPointerEXT(GLenum texunit, GLint size, GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoordPointerEXT"); } }
        void APIENTRY S_glMultiTexEnvfEXT(GLenum texunit, GLenum target, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexEnvfEXT"); } }
        void APIENTRY S_glMultiTexEnvfvEXT(GLenum texunit, GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexEnvfvEXT"); } }
        void APIENTRY S_glMultiTexEnviEXT(GLenum texunit, GLenum target, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexEnviEXT"); } }
        void APIENTRY S_glMultiTexEnvivEXT(GLenum texunit, GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexEnvivEXT"); } }
        void APIENTRY S_glMultiTexGendEXT(GLenum texunit, GLenum coord, GLenum pname, GLdouble param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexGendEXT"); } }
        void APIENTRY S_glMultiTexGendvEXT(GLenum texunit, GLenum coord, GLenum pname, const GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexGendvEXT"); } }
        void APIENTRY S_glMultiTexGenfEXT(GLenum texunit, GLenum coord, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexGenfEXT"); } }
        void APIENTRY S_glMultiTexGenfvEXT(GLenum texunit, GLenum coord, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexGenfvEXT"); } }
        void APIENTRY S_glMultiTexGeniEXT(GLenum texunit, GLenum coord, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexGeniEXT"); } }
        void APIENTRY S_glMultiTexGenivEXT(GLenum texunit, GLenum coord, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexGenivEXT"); } }
        void APIENTRY S_glGetMultiTexEnvfvEXT(GLenum texunit, GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexEnvfvEXT"); } }
        void APIENTRY S_glGetMultiTexEnvivEXT(GLenum texunit, GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexEnvivEXT"); } }
        void APIENTRY S_glGetMultiTexGendvEXT(GLenum texunit, GLenum coord, GLenum pname, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexGendvEXT"); } }
        void APIENTRY S_glGetMultiTexGenfvEXT(GLenum texunit, GLenum coord, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexGenfvEXT"); } }
        void APIENTRY S_glGetMultiTexGenivEXT(GLenum texunit, GLenum coord, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexGenivEXT"); } }
        void APIENTRY S_glMultiTexParameteriEXT(GLenum texunit, GLenum target, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexParameteriEXT"); } }
        void APIENTRY S_glMultiTexParameterivEXT(GLenum texunit, GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexParameterivEXT"); } }
        void APIENTRY S_glMultiTexParameterfEXT(GLenum texunit, GLenum target, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexParameterfEXT"); } }
        void APIENTRY S_glMultiTexParameterfvEXT(GLenum texunit, GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexParameterfvEXT"); } }
        void APIENTRY S_glMultiTexImage1DEXT(GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexImage1DEXT"); } }
        void APIENTRY S_glMultiTexImage2DEXT(GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexImage2DEXT"); } }
        void APIENTRY S_glMultiTexSubImage1DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexSubImage1DEXT"); } }
        void APIENTRY S_glMultiTexSubImage2DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexSubImage2DEXT"); } }
        void APIENTRY S_glCopyMultiTexImage1DEXT(GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyMultiTexImage1DEXT"); } }
        void APIENTRY S_glCopyMultiTexImage2DEXT(GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyMultiTexImage2DEXT"); } }
        void APIENTRY S_glCopyMultiTexSubImage1DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyMultiTexSubImage1DEXT"); } }
        void APIENTRY S_glCopyMultiTexSubImage2DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyMultiTexSubImage2DEXT"); } }
        void APIENTRY S_glGetMultiTexImageEXT(GLenum texunit, GLenum target, GLint level, GLenum format, GLenum type, void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexImageEXT"); } }
        void APIENTRY S_glGetMultiTexParameterfvEXT(GLenum texunit, GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexParameterfvEXT"); } }
        void APIENTRY S_glGetMultiTexParameterivEXT(GLenum texunit, GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexParameterivEXT"); } }
        void APIENTRY S_glGetMultiTexLevelParameterfvEXT(GLenum texunit, GLenum target, GLint level, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexLevelParameterfvEXT"); } }
        void APIENTRY S_glGetMultiTexLevelParameterivEXT(GLenum texunit, GLenum target, GLint level, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexLevelParameterivEXT"); } }
        void APIENTRY S_glMultiTexImage3DEXT(GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexImage3DEXT"); } }
        void APIENTRY S_glMultiTexSubImage3DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexSubImage3DEXT"); } }
        void APIENTRY S_glCopyMultiTexSubImage3DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyMultiTexSubImage3DEXT"); } }
        void APIENTRY S_glEnableClientStateIndexedEXT(GLenum array, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnableClientStateIndexedEXT"); } }
        void APIENTRY S_glDisableClientStateIndexedEXT(GLenum array, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisableClientStateIndexedEXT"); } }
        void APIENTRY S_glGetFloatIndexedvEXT(GLenum target, GLuint index, GLfloat *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFloatIndexedvEXT"); } }
        void APIENTRY S_glGetDoubleIndexedvEXT(GLenum target, GLuint index, GLdouble *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetDoubleIndexedvEXT"); } }
        void APIENTRY S_glGetPointerIndexedvEXT(GLenum target, GLuint index, void **data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPointerIndexedvEXT"); } }
        void APIENTRY S_glEnableIndexedEXT(GLenum target, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnableIndexedEXT"); } }
        void APIENTRY S_glDisableIndexedEXT(GLenum target, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisableIndexedEXT"); } }
        GLboolean APIENTRY S_glIsEnabledIndexedEXT(GLenum target, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsEnabledIndexedEXT"); } return {}; }
        void APIENTRY S_glGetIntegerIndexedvEXT(GLenum target, GLuint index, GLint *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetIntegerIndexedvEXT"); } }
        void APIENTRY S_glGetBooleanIndexedvEXT(GLenum target, GLuint index, GLboolean *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBooleanIndexedvEXT"); } }
        void APIENTRY S_glCompressedTextureImage3DEXT(GLuint texture, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTextureImage3DEXT"); } }
        void APIENTRY S_glCompressedTextureImage2DEXT(GLuint texture, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTextureImage2DEXT"); } }
        void APIENTRY S_glCompressedTextureImage1DEXT(GLuint texture, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTextureImage1DEXT"); } }
        void APIENTRY S_glCompressedTextureSubImage3DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTextureSubImage3DEXT"); } }
        void APIENTRY S_glCompressedTextureSubImage2DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTextureSubImage2DEXT"); } }
        void APIENTRY S_glCompressedTextureSubImage1DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedTextureSubImage1DEXT"); } }
        void APIENTRY S_glGetCompressedTextureImageEXT(GLuint texture, GLenum target, GLint lod, void *img) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCompressedTextureImageEXT"); } }
        void APIENTRY S_glCompressedMultiTexImage3DEXT(GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedMultiTexImage3DEXT"); } }
        void APIENTRY S_glCompressedMultiTexImage2DEXT(GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedMultiTexImage2DEXT"); } }
        void APIENTRY S_glCompressedMultiTexImage1DEXT(GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedMultiTexImage1DEXT"); } }
        void APIENTRY S_glCompressedMultiTexSubImage3DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedMultiTexSubImage3DEXT"); } }
        void APIENTRY S_glCompressedMultiTexSubImage2DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedMultiTexSubImage2DEXT"); } }
        void APIENTRY S_glCompressedMultiTexSubImage1DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *bits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompressedMultiTexSubImage1DEXT"); } }
        void APIENTRY S_glGetCompressedMultiTexImageEXT(GLenum texunit, GLenum target, GLint lod, void *img) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCompressedMultiTexImageEXT"); } }
        void APIENTRY S_glMatrixLoadTransposefEXT(GLenum mode, const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixLoadTransposefEXT"); } }
        void APIENTRY S_glMatrixLoadTransposedEXT(GLenum mode, const GLdouble *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixLoadTransposedEXT"); } }
        void APIENTRY S_glMatrixMultTransposefEXT(GLenum mode, const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixMultTransposefEXT"); } }
        void APIENTRY S_glMatrixMultTransposedEXT(GLenum mode, const GLdouble *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixMultTransposedEXT"); } }
        void APIENTRY S_glNamedBufferDataEXT(GLuint buffer, GLsizeiptr size, const void *data, GLenum usage) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferDataEXT"); } }
        void APIENTRY S_glNamedBufferSubDataEXT(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferSubDataEXT"); } }
        void * APIENTRY S_glMapNamedBufferEXT(GLuint buffer, GLenum access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapNamedBufferEXT"); } return {}; }
        GLboolean APIENTRY S_glUnmapNamedBufferEXT(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUnmapNamedBufferEXT"); } return {}; }
        void APIENTRY S_glGetNamedBufferParameterivEXT(GLuint buffer, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedBufferParameterivEXT"); } }
        void APIENTRY S_glGetNamedBufferPointervEXT(GLuint buffer, GLenum pname, void **params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedBufferPointervEXT"); } }
        void APIENTRY S_glGetNamedBufferSubDataEXT(GLuint buffer, GLintptr offset, GLsizeiptr size, void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedBufferSubDataEXT"); } }
        void APIENTRY S_glProgramUniform1fEXT(GLuint program, GLint location, GLfloat v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1fEXT"); } }
        void APIENTRY S_glProgramUniform2fEXT(GLuint program, GLint location, GLfloat v0, GLfloat v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2fEXT"); } }
        void APIENTRY S_glProgramUniform3fEXT(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3fEXT"); } }
        void APIENTRY S_glProgramUniform4fEXT(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4fEXT"); } }
        void APIENTRY S_glProgramUniform1iEXT(GLuint program, GLint location, GLint v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1iEXT"); } }
        void APIENTRY S_glProgramUniform2iEXT(GLuint program, GLint location, GLint v0, GLint v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2iEXT"); } }
        void APIENTRY S_glProgramUniform3iEXT(GLuint program, GLint location, GLint v0, GLint v1, GLint v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3iEXT"); } }
        void APIENTRY S_glProgramUniform4iEXT(GLuint program, GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4iEXT"); } }
        void APIENTRY S_glProgramUniform1fvEXT(GLuint program, GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1fvEXT"); } }
        void APIENTRY S_glProgramUniform2fvEXT(GLuint program, GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2fvEXT"); } }
        void APIENTRY S_glProgramUniform3fvEXT(GLuint program, GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3fvEXT"); } }
        void APIENTRY S_glProgramUniform4fvEXT(GLuint program, GLint location, GLsizei count, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4fvEXT"); } }
        void APIENTRY S_glProgramUniform1ivEXT(GLuint program, GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1ivEXT"); } }
        void APIENTRY S_glProgramUniform2ivEXT(GLuint program, GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2ivEXT"); } }
        void APIENTRY S_glProgramUniform3ivEXT(GLuint program, GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3ivEXT"); } }
        void APIENTRY S_glProgramUniform4ivEXT(GLuint program, GLint location, GLsizei count, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4ivEXT"); } }
        void APIENTRY S_glProgramUniformMatrix2fvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2fvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix3fvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3fvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix4fvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4fvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix2x3fvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2x3fvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix3x2fvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3x2fvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix2x4fvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2x4fvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix4x2fvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4x2fvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix3x4fvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3x4fvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix4x3fvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4x3fvEXT"); } }
        void APIENTRY S_glTextureBufferEXT(GLuint texture, GLenum target, GLenum internalformat, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureBufferEXT"); } }
        void APIENTRY S_glMultiTexBufferEXT(GLenum texunit, GLenum target, GLenum internalformat, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexBufferEXT"); } }
        void APIENTRY S_glTextureParameterIivEXT(GLuint texture, GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameterIivEXT"); } }
        void APIENTRY S_glTextureParameterIuivEXT(GLuint texture, GLenum target, GLenum pname, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureParameterIuivEXT"); } }
        void APIENTRY S_glGetTextureParameterIivEXT(GLuint texture, GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureParameterIivEXT"); } }
        void APIENTRY S_glGetTextureParameterIuivEXT(GLuint texture, GLenum target, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureParameterIuivEXT"); } }
        void APIENTRY S_glMultiTexParameterIivEXT(GLenum texunit, GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexParameterIivEXT"); } }
        void APIENTRY S_glMultiTexParameterIuivEXT(GLenum texunit, GLenum target, GLenum pname, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexParameterIuivEXT"); } }
        void APIENTRY S_glGetMultiTexParameterIivEXT(GLenum texunit, GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexParameterIivEXT"); } }
        void APIENTRY S_glGetMultiTexParameterIuivEXT(GLenum texunit, GLenum target, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultiTexParameterIuivEXT"); } }
        void APIENTRY S_glProgramUniform1uiEXT(GLuint program, GLint location, GLuint v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1uiEXT"); } }
        void APIENTRY S_glProgramUniform2uiEXT(GLuint program, GLint location, GLuint v0, GLuint v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2uiEXT"); } }
        void APIENTRY S_glProgramUniform3uiEXT(GLuint program, GLint location, GLuint v0, GLuint v1, GLuint v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3uiEXT"); } }
        void APIENTRY S_glProgramUniform4uiEXT(GLuint program, GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4uiEXT"); } }
        void APIENTRY S_glProgramUniform1uivEXT(GLuint program, GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1uivEXT"); } }
        void APIENTRY S_glProgramUniform2uivEXT(GLuint program, GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2uivEXT"); } }
        void APIENTRY S_glProgramUniform3uivEXT(GLuint program, GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3uivEXT"); } }
        void APIENTRY S_glProgramUniform4uivEXT(GLuint program, GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4uivEXT"); } }
        void APIENTRY S_glNamedProgramLocalParameters4fvEXT(GLuint program, GLenum target, GLuint index, GLsizei count, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParameters4fvEXT"); } }
        void APIENTRY S_glNamedProgramLocalParameterI4iEXT(GLuint program, GLenum target, GLuint index, GLint x, GLint y, GLint z, GLint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParameterI4iEXT"); } }
        void APIENTRY S_glNamedProgramLocalParameterI4ivEXT(GLuint program, GLenum target, GLuint index, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParameterI4ivEXT"); } }
        void APIENTRY S_glNamedProgramLocalParametersI4ivEXT(GLuint program, GLenum target, GLuint index, GLsizei count, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParametersI4ivEXT"); } }
        void APIENTRY S_glNamedProgramLocalParameterI4uiEXT(GLuint program, GLenum target, GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParameterI4uiEXT"); } }
        void APIENTRY S_glNamedProgramLocalParameterI4uivEXT(GLuint program, GLenum target, GLuint index, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParameterI4uivEXT"); } }
        void APIENTRY S_glNamedProgramLocalParametersI4uivEXT(GLuint program, GLenum target, GLuint index, GLsizei count, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParametersI4uivEXT"); } }
        void APIENTRY S_glGetNamedProgramLocalParameterIivEXT(GLuint program, GLenum target, GLuint index, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedProgramLocalParameterIivEXT"); } }
        void APIENTRY S_glGetNamedProgramLocalParameterIuivEXT(GLuint program, GLenum target, GLuint index, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedProgramLocalParameterIuivEXT"); } }
        void APIENTRY S_glEnableClientStateiEXT(GLenum array, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnableClientStateiEXT"); } }
        void APIENTRY S_glDisableClientStateiEXT(GLenum array, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisableClientStateiEXT"); } }
        void APIENTRY S_glGetFloati_vEXT(GLenum pname, GLuint index, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFloati_vEXT"); } }
        void APIENTRY S_glGetDoublei_vEXT(GLenum pname, GLuint index, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetDoublei_vEXT"); } }
        void APIENTRY S_glGetPointeri_vEXT(GLenum pname, GLuint index, void **params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPointeri_vEXT"); } }
        void APIENTRY S_glNamedProgramStringEXT(GLuint program, GLenum target, GLenum format, GLsizei len, const void *string) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramStringEXT"); } }
        void APIENTRY S_glNamedProgramLocalParameter4dEXT(GLuint program, GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParameter4dEXT"); } }
        void APIENTRY S_glNamedProgramLocalParameter4dvEXT(GLuint program, GLenum target, GLuint index, const GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParameter4dvEXT"); } }
        void APIENTRY S_glNamedProgramLocalParameter4fEXT(GLuint program, GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParameter4fEXT"); } }
        void APIENTRY S_glNamedProgramLocalParameter4fvEXT(GLuint program, GLenum target, GLuint index, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedProgramLocalParameter4fvEXT"); } }
        void APIENTRY S_glGetNamedProgramLocalParameterdvEXT(GLuint program, GLenum target, GLuint index, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedProgramLocalParameterdvEXT"); } }
        void APIENTRY S_glGetNamedProgramLocalParameterfvEXT(GLuint program, GLenum target, GLuint index, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedProgramLocalParameterfvEXT"); } }
        void APIENTRY S_glGetNamedProgramivEXT(GLuint program, GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedProgramivEXT"); } }
        void APIENTRY S_glGetNamedProgramStringEXT(GLuint program, GLenum target, GLenum pname, void *string) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedProgramStringEXT"); } }
        void APIENTRY S_glNamedRenderbufferStorageEXT(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedRenderbufferStorageEXT"); } }
        void APIENTRY S_glGetNamedRenderbufferParameterivEXT(GLuint renderbuffer, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedRenderbufferParameterivEXT"); } }
        void APIENTRY S_glNamedRenderbufferStorageMultisampleEXT(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedRenderbufferStorageMultisampleEXT"); } }
        void APIENTRY S_glNamedRenderbufferStorageMultisampleCoverageEXT(GLuint renderbuffer, GLsizei coverageSamples, GLsizei colorSamples, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedRenderbufferStorageMultisampleCoverageEXT"); } }
        GLenum APIENTRY S_glCheckNamedFramebufferStatusEXT(GLuint framebuffer, GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCheckNamedFramebufferStatusEXT"); } return {}; }
        void APIENTRY S_glNamedFramebufferTexture1DEXT(GLuint framebuffer, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferTexture1DEXT"); } }
        void APIENTRY S_glNamedFramebufferTexture2DEXT(GLuint framebuffer, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferTexture2DEXT"); } }
        void APIENTRY S_glNamedFramebufferTexture3DEXT(GLuint framebuffer, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferTexture3DEXT"); } }
        void APIENTRY S_glNamedFramebufferRenderbufferEXT(GLuint framebuffer, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferRenderbufferEXT"); } }
        void APIENTRY S_glGetNamedFramebufferAttachmentParameterivEXT(GLuint framebuffer, GLenum attachment, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedFramebufferAttachmentParameterivEXT"); } }
        void APIENTRY S_glGenerateTextureMipmapEXT(GLuint texture, GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenerateTextureMipmapEXT"); } }
        void APIENTRY S_glGenerateMultiTexMipmapEXT(GLenum texunit, GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenerateMultiTexMipmapEXT"); } }
        void APIENTRY S_glFramebufferDrawBufferEXT(GLuint framebuffer, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferDrawBufferEXT"); } }
        void APIENTRY S_glFramebufferDrawBuffersEXT(GLuint framebuffer, GLsizei n, const GLenum *bufs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferDrawBuffersEXT"); } }
        void APIENTRY S_glFramebufferReadBufferEXT(GLuint framebuffer, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferReadBufferEXT"); } }
        void APIENTRY S_glGetFramebufferParameterivEXT(GLuint framebuffer, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFramebufferParameterivEXT"); } }
        void APIENTRY S_glNamedCopyBufferSubDataEXT(GLuint readBuffer, GLuint writeBuffer, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedCopyBufferSubDataEXT"); } }
        void APIENTRY S_glNamedFramebufferTextureEXT(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferTextureEXT"); } }
        void APIENTRY S_glNamedFramebufferTextureLayerEXT(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint layer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferTextureLayerEXT"); } }
        void APIENTRY S_glNamedFramebufferTextureFaceEXT(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLenum face) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferTextureFaceEXT"); } }
        void APIENTRY S_glTextureRenderbufferEXT(GLuint texture, GLenum target, GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureRenderbufferEXT"); } }
        void APIENTRY S_glMultiTexRenderbufferEXT(GLenum texunit, GLenum target, GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexRenderbufferEXT"); } }
        void APIENTRY S_glVertexArrayVertexOffsetEXT(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexOffsetEXT"); } }
        void APIENTRY S_glVertexArrayColorOffsetEXT(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayColorOffsetEXT"); } }
        void APIENTRY S_glVertexArrayEdgeFlagOffsetEXT(GLuint vaobj, GLuint buffer, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayEdgeFlagOffsetEXT"); } }
        void APIENTRY S_glVertexArrayIndexOffsetEXT(GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayIndexOffsetEXT"); } }
        void APIENTRY S_glVertexArrayNormalOffsetEXT(GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayNormalOffsetEXT"); } }
        void APIENTRY S_glVertexArrayTexCoordOffsetEXT(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayTexCoordOffsetEXT"); } }
        void APIENTRY S_glVertexArrayMultiTexCoordOffsetEXT(GLuint vaobj, GLuint buffer, GLenum texunit, GLint size, GLenum type, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayMultiTexCoordOffsetEXT"); } }
        void APIENTRY S_glVertexArrayFogCoordOffsetEXT(GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayFogCoordOffsetEXT"); } }
        void APIENTRY S_glVertexArraySecondaryColorOffsetEXT(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArraySecondaryColorOffsetEXT"); } }
        void APIENTRY S_glVertexArrayVertexAttribOffsetEXT(GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexAttribOffsetEXT"); } }
        void APIENTRY S_glVertexArrayVertexAttribIOffsetEXT(GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexAttribIOffsetEXT"); } }
        void APIENTRY S_glEnableVertexArrayEXT(GLuint vaobj, GLenum array) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnableVertexArrayEXT"); } }
        void APIENTRY S_glDisableVertexArrayEXT(GLuint vaobj, GLenum array) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisableVertexArrayEXT"); } }
        void APIENTRY S_glEnableVertexArrayAttribEXT(GLuint vaobj, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnableVertexArrayAttribEXT"); } }
        void APIENTRY S_glDisableVertexArrayAttribEXT(GLuint vaobj, GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisableVertexArrayAttribEXT"); } }
        void APIENTRY S_glGetVertexArrayIntegervEXT(GLuint vaobj, GLenum pname, GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexArrayIntegervEXT"); } }
        void APIENTRY S_glGetVertexArrayPointervEXT(GLuint vaobj, GLenum pname, void **param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexArrayPointervEXT"); } }
        void APIENTRY S_glGetVertexArrayIntegeri_vEXT(GLuint vaobj, GLuint index, GLenum pname, GLint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexArrayIntegeri_vEXT"); } }
        void APIENTRY S_glGetVertexArrayPointeri_vEXT(GLuint vaobj, GLuint index, GLenum pname, void **param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexArrayPointeri_vEXT"); } }
        void * APIENTRY S_glMapNamedBufferRangeEXT(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapNamedBufferRangeEXT"); } return {}; }
        void APIENTRY S_glFlushMappedNamedBufferRangeEXT(GLuint buffer, GLintptr offset, GLsizeiptr length) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFlushMappedNamedBufferRangeEXT"); } }
        void APIENTRY S_glNamedBufferStorageEXT(GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferStorageEXT"); } }
        void APIENTRY S_glClearNamedBufferDataEXT(GLuint buffer, GLenum internalformat, GLenum format, GLenum type, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearNamedBufferDataEXT"); } }
        void APIENTRY S_glClearNamedBufferSubDataEXT(GLuint buffer, GLenum internalformat, GLsizeiptr offset, GLsizeiptr size, GLenum format, GLenum type, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearNamedBufferSubDataEXT"); } }
        void APIENTRY S_glNamedFramebufferParameteriEXT(GLuint framebuffer, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferParameteriEXT"); } }
        void APIENTRY S_glGetNamedFramebufferParameterivEXT(GLuint framebuffer, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedFramebufferParameterivEXT"); } }
        void APIENTRY S_glProgramUniform1dEXT(GLuint program, GLint location, GLdouble x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1dEXT"); } }
        void APIENTRY S_glProgramUniform2dEXT(GLuint program, GLint location, GLdouble x, GLdouble y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2dEXT"); } }
        void APIENTRY S_glProgramUniform3dEXT(GLuint program, GLint location, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3dEXT"); } }
        void APIENTRY S_glProgramUniform4dEXT(GLuint program, GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4dEXT"); } }
        void APIENTRY S_glProgramUniform1dvEXT(GLuint program, GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform1dvEXT"); } }
        void APIENTRY S_glProgramUniform2dvEXT(GLuint program, GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform2dvEXT"); } }
        void APIENTRY S_glProgramUniform3dvEXT(GLuint program, GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform3dvEXT"); } }
        void APIENTRY S_glProgramUniform4dvEXT(GLuint program, GLint location, GLsizei count, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniform4dvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix2dvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2dvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix3dvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3dvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix4dvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4dvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix2x3dvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2x3dvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix2x4dvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix2x4dvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix3x2dvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3x2dvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix3x4dvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix3x4dvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix4x2dvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4x2dvEXT"); } }
        void APIENTRY S_glProgramUniformMatrix4x3dvEXT(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformMatrix4x3dvEXT"); } }
        void APIENTRY S_glTextureBufferRangeEXT(GLuint texture, GLenum target, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureBufferRangeEXT"); } }
        void APIENTRY S_glTextureStorage1DEXT(GLuint texture, GLenum target, GLsizei levels, GLenum internalformat, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorage1DEXT"); } }
        void APIENTRY S_glTextureStorage2DEXT(GLuint texture, GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorage2DEXT"); } }
        void APIENTRY S_glTextureStorage3DEXT(GLuint texture, GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorage3DEXT"); } }
        void APIENTRY S_glTextureStorage2DMultisampleEXT(GLuint texture, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorage2DMultisampleEXT"); } }
        void APIENTRY S_glTextureStorage3DMultisampleEXT(GLuint texture, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorage3DMultisampleEXT"); } }
        void APIENTRY S_glVertexArrayBindVertexBufferEXT(GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayBindVertexBufferEXT"); } }
        void APIENTRY S_glVertexArrayVertexAttribFormatEXT(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexAttribFormatEXT"); } }
        void APIENTRY S_glVertexArrayVertexAttribIFormatEXT(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexAttribIFormatEXT"); } }
        void APIENTRY S_glVertexArrayVertexAttribLFormatEXT(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexAttribLFormatEXT"); } }
        void APIENTRY S_glVertexArrayVertexAttribBindingEXT(GLuint vaobj, GLuint attribindex, GLuint bindingindex) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexAttribBindingEXT"); } }
        void APIENTRY S_glVertexArrayVertexBindingDivisorEXT(GLuint vaobj, GLuint bindingindex, GLuint divisor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexBindingDivisorEXT"); } }
        void APIENTRY S_glVertexArrayVertexAttribLOffsetEXT(GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLsizei stride, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexAttribLOffsetEXT"); } }
        void APIENTRY S_glTexturePageCommitmentEXT(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLboolean commit) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexturePageCommitmentEXT"); } }
        void APIENTRY S_glVertexArrayVertexAttribDivisorEXT(GLuint vaobj, GLuint index, GLuint divisor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayVertexAttribDivisorEXT"); } }
        void APIENTRY S_glColorMaskIndexedEXT(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorMaskIndexedEXT"); } }
        void APIENTRY S_glDrawArraysInstancedEXT(GLenum mode, GLint start, GLsizei count, GLsizei primcount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawArraysInstancedEXT"); } }
        void APIENTRY S_glDrawElementsInstancedEXT(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawElementsInstancedEXT"); } }
        void APIENTRY S_glDrawRangeElementsEXT(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawRangeElementsEXT"); } }
        void APIENTRY S_glBufferStorageExternalEXT(GLenum target, GLintptr offset, GLsizeiptr size, GLeglClientBufferEXT clientBuffer, GLbitfield flags) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferStorageExternalEXT"); } }
        void APIENTRY S_glNamedBufferStorageExternalEXT(GLuint buffer, GLintptr offset, GLsizeiptr size, GLeglClientBufferEXT clientBuffer, GLbitfield flags) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferStorageExternalEXT"); } }
        void APIENTRY S_glFogCoordfEXT(GLfloat coord) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogCoordfEXT"); } }
        void APIENTRY S_glFogCoordfvEXT(const GLfloat *coord) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogCoordfvEXT"); } }
        void APIENTRY S_glFogCoorddEXT(GLdouble coord) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogCoorddEXT"); } }
        void APIENTRY S_glFogCoorddvEXT(const GLdouble *coord) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogCoorddvEXT"); } }
        void APIENTRY S_glFogCoordPointerEXT(GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogCoordPointerEXT"); } }
        void APIENTRY S_glGetFragmentShadingRatesEXT(GLsizei samples, GLsizei maxCount, GLsizei *count, GLenum *shadingRates) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFragmentShadingRatesEXT"); } }
        void APIENTRY S_glShadingRateEXT(GLenum rate) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShadingRateEXT"); } }
        void APIENTRY S_glShadingRateCombinerOpsEXT(GLenum combinerOp0, GLenum combinerOp1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShadingRateCombinerOpsEXT"); } }
        void APIENTRY S_glFramebufferShadingRateEXT(GLenum target, GLenum attachment, GLuint texture, GLint baseLayer, GLsizei numLayers, GLsizei texelWidth, GLsizei texelHeight) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferShadingRateEXT"); } }
        void APIENTRY S_glBlitFramebufferEXT(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlitFramebufferEXT"); } }
        void APIENTRY S_glBlitFramebufferLayersEXT(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlitFramebufferLayersEXT"); } }
        void APIENTRY S_glBlitFramebufferLayerEXT(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint srcLayer, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLint dstLayer, GLbitfield mask, GLenum filter) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlitFramebufferLayerEXT"); } }
        void APIENTRY S_glRenderbufferStorageMultisampleEXT(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRenderbufferStorageMultisampleEXT"); } }
        GLboolean APIENTRY S_glIsRenderbufferEXT(GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsRenderbufferEXT"); } return {}; }
        void APIENTRY S_glBindRenderbufferEXT(GLenum target, GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindRenderbufferEXT"); } }
        void APIENTRY S_glDeleteRenderbuffersEXT(GLsizei n, const GLuint *renderbuffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteRenderbuffersEXT"); } }
        void APIENTRY S_glGenRenderbuffersEXT(GLsizei n, GLuint *renderbuffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenRenderbuffersEXT"); } }
        void APIENTRY S_glRenderbufferStorageEXT(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRenderbufferStorageEXT"); } }
        void APIENTRY S_glGetRenderbufferParameterivEXT(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetRenderbufferParameterivEXT"); } }
        GLboolean APIENTRY S_glIsFramebufferEXT(GLuint framebuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsFramebufferEXT"); } return {}; }
        void APIENTRY S_glBindFramebufferEXT(GLenum target, GLuint framebuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindFramebufferEXT"); } }
        void APIENTRY S_glDeleteFramebuffersEXT(GLsizei n, const GLuint *framebuffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteFramebuffersEXT"); } }
        void APIENTRY S_glGenFramebuffersEXT(GLsizei n, GLuint *framebuffers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenFramebuffersEXT"); } }
        GLenum APIENTRY S_glCheckFramebufferStatusEXT(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCheckFramebufferStatusEXT"); } return {}; }
        void APIENTRY S_glFramebufferTexture1DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTexture1DEXT"); } }
        void APIENTRY S_glFramebufferTexture2DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTexture2DEXT"); } }
        void APIENTRY S_glFramebufferTexture3DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTexture3DEXT"); } }
        void APIENTRY S_glFramebufferRenderbufferEXT(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferRenderbufferEXT"); } }
        void APIENTRY S_glGetFramebufferAttachmentParameterivEXT(GLenum target, GLenum attachment, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFramebufferAttachmentParameterivEXT"); } }
        void APIENTRY S_glGenerateMipmapEXT(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenerateMipmapEXT"); } }
        void APIENTRY S_glProgramParameteriEXT(GLuint program, GLenum pname, GLint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramParameteriEXT"); } }
        void APIENTRY S_glProgramEnvParameters4fvEXT(GLenum target, GLuint index, GLsizei count, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParameters4fvEXT"); } }
        void APIENTRY S_glProgramLocalParameters4fvEXT(GLenum target, GLuint index, GLsizei count, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParameters4fvEXT"); } }
        void APIENTRY S_glGetUniformuivEXT(GLuint program, GLint location, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUniformuivEXT"); } }
        void APIENTRY S_glBindFragDataLocationEXT(GLuint program, GLuint color, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindFragDataLocationEXT"); } }
        GLint APIENTRY S_glGetFragDataLocationEXT(GLuint program, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFragDataLocationEXT"); } return {}; }
        void APIENTRY S_glUniform1uiEXT(GLint location, GLuint v0) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1uiEXT"); } }
        void APIENTRY S_glUniform2uiEXT(GLint location, GLuint v0, GLuint v1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2uiEXT"); } }
        void APIENTRY S_glUniform3uiEXT(GLint location, GLuint v0, GLuint v1, GLuint v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3uiEXT"); } }
        void APIENTRY S_glUniform4uiEXT(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4uiEXT"); } }
        void APIENTRY S_glUniform1uivEXT(GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform1uivEXT"); } }
        void APIENTRY S_glUniform2uivEXT(GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform2uivEXT"); } }
        void APIENTRY S_glUniform3uivEXT(GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform3uivEXT"); } }
        void APIENTRY S_glUniform4uivEXT(GLint location, GLsizei count, const GLuint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniform4uivEXT"); } }
        void APIENTRY S_glVertexAttribI1iEXT(GLuint index, GLint x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI1iEXT"); } }
        void APIENTRY S_glVertexAttribI2iEXT(GLuint index, GLint x, GLint y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI2iEXT"); } }
        void APIENTRY S_glVertexAttribI3iEXT(GLuint index, GLint x, GLint y, GLint z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI3iEXT"); } }
        void APIENTRY S_glVertexAttribI4iEXT(GLuint index, GLint x, GLint y, GLint z, GLint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4iEXT"); } }
        void APIENTRY S_glVertexAttribI1uiEXT(GLuint index, GLuint x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI1uiEXT"); } }
        void APIENTRY S_glVertexAttribI2uiEXT(GLuint index, GLuint x, GLuint y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI2uiEXT"); } }
        void APIENTRY S_glVertexAttribI3uiEXT(GLuint index, GLuint x, GLuint y, GLuint z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI3uiEXT"); } }
        void APIENTRY S_glVertexAttribI4uiEXT(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4uiEXT"); } }
        void APIENTRY S_glVertexAttribI1ivEXT(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI1ivEXT"); } }
        void APIENTRY S_glVertexAttribI2ivEXT(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI2ivEXT"); } }
        void APIENTRY S_glVertexAttribI3ivEXT(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI3ivEXT"); } }
        void APIENTRY S_glVertexAttribI4ivEXT(GLuint index, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4ivEXT"); } }
        void APIENTRY S_glVertexAttribI1uivEXT(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI1uivEXT"); } }
        void APIENTRY S_glVertexAttribI2uivEXT(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI2uivEXT"); } }
        void APIENTRY S_glVertexAttribI3uivEXT(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI3uivEXT"); } }
        void APIENTRY S_glVertexAttribI4uivEXT(GLuint index, const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4uivEXT"); } }
        void APIENTRY S_glVertexAttribI4bvEXT(GLuint index, const GLbyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4bvEXT"); } }
        void APIENTRY S_glVertexAttribI4svEXT(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4svEXT"); } }
        void APIENTRY S_glVertexAttribI4ubvEXT(GLuint index, const GLubyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4ubvEXT"); } }
        void APIENTRY S_glVertexAttribI4usvEXT(GLuint index, const GLushort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribI4usvEXT"); } }
        void APIENTRY S_glVertexAttribIPointerEXT(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribIPointerEXT"); } }
        void APIENTRY S_glGetVertexAttribIivEXT(GLuint index, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribIivEXT"); } }
        void APIENTRY S_glGetVertexAttribIuivEXT(GLuint index, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribIuivEXT"); } }
        void APIENTRY S_glGetHistogramEXT(GLenum target, GLboolean reset, GLenum format, GLenum type, void *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetHistogramEXT"); } }
        void APIENTRY S_glGetHistogramParameterfvEXT(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetHistogramParameterfvEXT"); } }
        void APIENTRY S_glGetHistogramParameterivEXT(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetHistogramParameterivEXT"); } }
        void APIENTRY S_glGetMinmaxEXT(GLenum target, GLboolean reset, GLenum format, GLenum type, void *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMinmaxEXT"); } }
        void APIENTRY S_glGetMinmaxParameterfvEXT(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMinmaxParameterfvEXT"); } }
        void APIENTRY S_glGetMinmaxParameterivEXT(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMinmaxParameterivEXT"); } }
        void APIENTRY S_glHistogramEXT(GLenum target, GLsizei width, GLenum internalformat, GLboolean sink) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glHistogramEXT"); } }
        void APIENTRY S_glMinmaxEXT(GLenum target, GLenum internalformat, GLboolean sink) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMinmaxEXT"); } }
        void APIENTRY S_glResetHistogramEXT(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glResetHistogramEXT"); } }
        void APIENTRY S_glResetMinmaxEXT(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glResetMinmaxEXT"); } }
        void APIENTRY S_glIndexFuncEXT(GLenum func, GLclampf ref) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIndexFuncEXT"); } }
        void APIENTRY S_glIndexMaterialEXT(GLenum face, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIndexMaterialEXT"); } }
        void APIENTRY S_glApplyTextureEXT(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glApplyTextureEXT"); } }
        void APIENTRY S_glTextureLightEXT(GLenum pname) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureLightEXT"); } }
        void APIENTRY S_glTextureMaterialEXT(GLenum face, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureMaterialEXT"); } }
        void APIENTRY S_glGetUnsignedBytevEXT(GLenum pname, GLubyte *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUnsignedBytevEXT"); } }
        void APIENTRY S_glGetUnsignedBytei_vEXT(GLenum target, GLuint index, GLubyte *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetUnsignedBytei_vEXT"); } }
        void APIENTRY S_glDeleteMemoryObjectsEXT(GLsizei n, const GLuint *memoryObjects) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteMemoryObjectsEXT"); } }
        GLboolean APIENTRY S_glIsMemoryObjectEXT(GLuint memoryObject) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsMemoryObjectEXT"); } return {}; }
        void APIENTRY S_glCreateMemoryObjectsEXT(GLsizei n, GLuint *memoryObjects) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateMemoryObjectsEXT"); } }
        void APIENTRY S_glMemoryObjectParameterivEXT(GLuint memoryObject, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMemoryObjectParameterivEXT"); } }
        void APIENTRY S_glGetMemoryObjectParameterivEXT(GLuint memoryObject, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMemoryObjectParameterivEXT"); } }
        void APIENTRY S_glTexStorageMem2DEXT(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorageMem2DEXT"); } }
        void APIENTRY S_glTexStorageMem2DMultisampleEXT(GLenum target, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorageMem2DMultisampleEXT"); } }
        void APIENTRY S_glTexStorageMem3DEXT(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorageMem3DEXT"); } }
        void APIENTRY S_glTexStorageMem3DMultisampleEXT(GLenum target, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorageMem3DMultisampleEXT"); } }
        void APIENTRY S_glBufferStorageMemEXT(GLenum target, GLsizeiptr size, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferStorageMemEXT"); } }
        void APIENTRY S_glTextureStorageMem2DEXT(GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorageMem2DEXT"); } }
        void APIENTRY S_glTextureStorageMem2DMultisampleEXT(GLuint texture, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorageMem2DMultisampleEXT"); } }
        void APIENTRY S_glTextureStorageMem3DEXT(GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorageMem3DEXT"); } }
        void APIENTRY S_glTextureStorageMem3DMultisampleEXT(GLuint texture, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorageMem3DMultisampleEXT"); } }
        void APIENTRY S_glNamedBufferStorageMemEXT(GLuint buffer, GLsizeiptr size, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferStorageMemEXT"); } }
        void APIENTRY S_glTexStorageMem1DEXT(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorageMem1DEXT"); } }
        void APIENTRY S_glTextureStorageMem1DEXT(GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureStorageMem1DEXT"); } }
        void APIENTRY S_glImportMemoryFdEXT(GLuint memory, GLuint64 size, GLenum handleType, GLint fd) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImportMemoryFdEXT"); } }
        void APIENTRY S_glImportMemoryWin32HandleEXT(GLuint memory, GLuint64 size, GLenum handleType, void *handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImportMemoryWin32HandleEXT"); } }
        void APIENTRY S_glImportMemoryWin32NameEXT(GLuint memory, GLuint64 size, GLenum handleType, const void *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImportMemoryWin32NameEXT"); } }
        void APIENTRY S_glDrawMeshTasksEXT(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawMeshTasksEXT"); } }
        void APIENTRY S_glDrawMeshTasksIndirectEXT(GLintptr indirect) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawMeshTasksIndirectEXT"); } }
        void APIENTRY S_glMultiDrawMeshTasksIndirectEXT(GLintptr indirect, GLsizei drawcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawMeshTasksIndirectEXT"); } }
        void APIENTRY S_glMultiDrawMeshTasksIndirectCountEXT(GLintptr indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawMeshTasksIndirectCountEXT"); } }
        void APIENTRY S_glMultiDrawArraysEXT(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawArraysEXT"); } }
        void APIENTRY S_glMultiDrawElementsEXT(GLenum mode, const GLsizei *count, GLenum type, const void *const*indices, GLsizei primcount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawElementsEXT"); } }
        void APIENTRY S_glSampleMaskEXT(GLclampf value, GLboolean invert) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSampleMaskEXT"); } }
        void APIENTRY S_glSamplePatternEXT(GLenum pattern) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSamplePatternEXT"); } }
        void APIENTRY S_glColorTableEXT(GLenum target, GLenum internalFormat, GLsizei width, GLenum format, GLenum type, const void *table) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorTableEXT"); } }
        void APIENTRY S_glGetColorTableEXT(GLenum target, GLenum format, GLenum type, void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetColorTableEXT"); } }
        void APIENTRY S_glGetColorTableParameterivEXT(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetColorTableParameterivEXT"); } }
        void APIENTRY S_glGetColorTableParameterfvEXT(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetColorTableParameterfvEXT"); } }
        void APIENTRY S_glPixelTransformParameteriEXT(GLenum target, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelTransformParameteriEXT"); } }
        void APIENTRY S_glPixelTransformParameterfEXT(GLenum target, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelTransformParameterfEXT"); } }
        void APIENTRY S_glPixelTransformParameterivEXT(GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelTransformParameterivEXT"); } }
        void APIENTRY S_glPixelTransformParameterfvEXT(GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelTransformParameterfvEXT"); } }
        void APIENTRY S_glGetPixelTransformParameterivEXT(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPixelTransformParameterivEXT"); } }
        void APIENTRY S_glGetPixelTransformParameterfvEXT(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPixelTransformParameterfvEXT"); } }
        void APIENTRY S_glPointParameterfEXT(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterfEXT"); } }
        void APIENTRY S_glPointParameterfvEXT(GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterfvEXT"); } }
        void APIENTRY S_glPolygonOffsetEXT(GLfloat factor, GLfloat bias) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPolygonOffsetEXT"); } }
        void APIENTRY S_glPolygonOffsetClampEXT(GLfloat factor, GLfloat units, GLfloat clamp) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPolygonOffsetClampEXT"); } }
        void APIENTRY S_glProvokingVertexEXT(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProvokingVertexEXT"); } }
        void APIENTRY S_glRasterSamplesEXT(GLuint samples, GLboolean fixedsamplelocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRasterSamplesEXT"); } }
        void APIENTRY S_glSecondaryColor3bEXT(GLbyte red, GLbyte green, GLbyte blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3bEXT"); } }
        void APIENTRY S_glSecondaryColor3bvEXT(const GLbyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3bvEXT"); } }
        void APIENTRY S_glSecondaryColor3dEXT(GLdouble red, GLdouble green, GLdouble blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3dEXT"); } }
        void APIENTRY S_glSecondaryColor3dvEXT(const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3dvEXT"); } }
        void APIENTRY S_glSecondaryColor3fEXT(GLfloat red, GLfloat green, GLfloat blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3fEXT"); } }
        void APIENTRY S_glSecondaryColor3fvEXT(const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3fvEXT"); } }
        void APIENTRY S_glSecondaryColor3iEXT(GLint red, GLint green, GLint blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3iEXT"); } }
        void APIENTRY S_glSecondaryColor3ivEXT(const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3ivEXT"); } }
        void APIENTRY S_glSecondaryColor3sEXT(GLshort red, GLshort green, GLshort blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3sEXT"); } }
        void APIENTRY S_glSecondaryColor3svEXT(const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3svEXT"); } }
        void APIENTRY S_glSecondaryColor3ubEXT(GLubyte red, GLubyte green, GLubyte blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3ubEXT"); } }
        void APIENTRY S_glSecondaryColor3ubvEXT(const GLubyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3ubvEXT"); } }
        void APIENTRY S_glSecondaryColor3uiEXT(GLuint red, GLuint green, GLuint blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3uiEXT"); } }
        void APIENTRY S_glSecondaryColor3uivEXT(const GLuint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3uivEXT"); } }
        void APIENTRY S_glSecondaryColor3usEXT(GLushort red, GLushort green, GLushort blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3usEXT"); } }
        void APIENTRY S_glSecondaryColor3usvEXT(const GLushort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3usvEXT"); } }
        void APIENTRY S_glSecondaryColorPointerEXT(GLint size, GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColorPointerEXT"); } }
        void APIENTRY S_glGenSemaphoresEXT(GLsizei n, GLuint *semaphores) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenSemaphoresEXT"); } }
        void APIENTRY S_glDeleteSemaphoresEXT(GLsizei n, const GLuint *semaphores) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteSemaphoresEXT"); } }
        GLboolean APIENTRY S_glIsSemaphoreEXT(GLuint semaphore) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsSemaphoreEXT"); } return {}; }
        void APIENTRY S_glSemaphoreParameterui64vEXT(GLuint semaphore, GLenum pname, const GLuint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSemaphoreParameterui64vEXT"); } }
        void APIENTRY S_glGetSemaphoreParameterui64vEXT(GLuint semaphore, GLenum pname, GLuint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSemaphoreParameterui64vEXT"); } }
        void APIENTRY S_glWaitSemaphoreEXT(GLuint semaphore, GLuint numBufferBarriers, const GLuint *buffers, GLuint numTextureBarriers, const GLuint *textures, const GLenum *srcLayouts) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWaitSemaphoreEXT"); } }
        void APIENTRY S_glSignalSemaphoreEXT(GLuint semaphore, GLuint numBufferBarriers, const GLuint *buffers, GLuint numTextureBarriers, const GLuint *textures, const GLenum *dstLayouts) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSignalSemaphoreEXT"); } }
        void APIENTRY S_glImportSemaphoreFdEXT(GLuint semaphore, GLenum handleType, GLint fd) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImportSemaphoreFdEXT"); } }
        void APIENTRY S_glImportSemaphoreWin32HandleEXT(GLuint semaphore, GLenum handleType, void *handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImportSemaphoreWin32HandleEXT"); } }
        void APIENTRY S_glImportSemaphoreWin32NameEXT(GLuint semaphore, GLenum handleType, const void *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImportSemaphoreWin32NameEXT"); } }
        void APIENTRY S_glUseShaderProgramEXT(GLenum type, GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUseShaderProgramEXT"); } }
        void APIENTRY S_glActiveProgramEXT(GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glActiveProgramEXT"); } }
        GLuint APIENTRY S_glCreateShaderProgramEXT(GLenum type, const GLchar *string) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateShaderProgramEXT"); } return {}; }
        void APIENTRY S_glActiveShaderProgramEXT(GLuint pipeline, GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glActiveShaderProgramEXT"); } }
        void APIENTRY S_glBindProgramPipelineEXT(GLuint pipeline) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindProgramPipelineEXT"); } }
        GLuint APIENTRY S_glCreateShaderProgramvEXT(GLenum type, GLsizei count, const GLchar *const*strings) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateShaderProgramvEXT"); } return {}; }
        void APIENTRY S_glDeleteProgramPipelinesEXT(GLsizei n, const GLuint *pipelines) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteProgramPipelinesEXT"); } }
        void APIENTRY S_glGenProgramPipelinesEXT(GLsizei n, GLuint *pipelines) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenProgramPipelinesEXT"); } }
        void APIENTRY S_glGetProgramPipelineInfoLogEXT(GLuint pipeline, GLsizei bufSize, GLsizei *length, GLchar *infoLog) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramPipelineInfoLogEXT"); } }
        void APIENTRY S_glGetProgramPipelineivEXT(GLuint pipeline, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramPipelineivEXT"); } }
        GLboolean APIENTRY S_glIsProgramPipelineEXT(GLuint pipeline) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsProgramPipelineEXT"); } return {}; }
        void APIENTRY S_glUseProgramStagesEXT(GLuint pipeline, GLbitfield stages, GLuint program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUseProgramStagesEXT"); } }
        void APIENTRY S_glValidateProgramPipelineEXT(GLuint pipeline) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glValidateProgramPipelineEXT"); } }
        void APIENTRY S_glFramebufferFetchBarrierEXT(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferFetchBarrierEXT"); } }
        void APIENTRY S_glBindImageTextureEXT(GLuint index, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLint format) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindImageTextureEXT"); } }
        void APIENTRY S_glMemoryBarrierEXT(GLbitfield barriers) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMemoryBarrierEXT"); } }
        void APIENTRY S_glStencilClearTagEXT(GLsizei stencilTagBits, GLuint stencilClearTag) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilClearTagEXT"); } }
        void APIENTRY S_glActiveStencilFaceEXT(GLenum face) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glActiveStencilFaceEXT"); } }
        void APIENTRY S_glTexSubImage1DEXT(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexSubImage1DEXT"); } }
        void APIENTRY S_glTexSubImage2DEXT(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexSubImage2DEXT"); } }
        void APIENTRY S_glTexImage3DEXT(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexImage3DEXT"); } }
        void APIENTRY S_glTexSubImage3DEXT(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexSubImage3DEXT"); } }
        void APIENTRY S_glFramebufferTextureLayerEXT(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTextureLayerEXT"); } }
        void APIENTRY S_glTexBufferEXT(GLenum target, GLenum internalformat, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexBufferEXT"); } }
        void APIENTRY S_glTexParameterIivEXT(GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexParameterIivEXT"); } }
        void APIENTRY S_glTexParameterIuivEXT(GLenum target, GLenum pname, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexParameterIuivEXT"); } }
        void APIENTRY S_glGetTexParameterIivEXT(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexParameterIivEXT"); } }
        void APIENTRY S_glGetTexParameterIuivEXT(GLenum target, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexParameterIuivEXT"); } }
        void APIENTRY S_glClearColorIiEXT(GLint red, GLint green, GLint blue, GLint alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearColorIiEXT"); } }
        void APIENTRY S_glClearColorIuiEXT(GLuint red, GLuint green, GLuint blue, GLuint alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearColorIuiEXT"); } }
        GLboolean APIENTRY S_glAreTexturesResidentEXT(GLsizei n, const GLuint *textures, GLboolean *residences) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAreTexturesResidentEXT"); } return {}; }
        void APIENTRY S_glBindTextureEXT(GLenum target, GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindTextureEXT"); } }
        void APIENTRY S_glDeleteTexturesEXT(GLsizei n, const GLuint *textures) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteTexturesEXT"); } }
        void APIENTRY S_glGenTexturesEXT(GLsizei n, GLuint *textures) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenTexturesEXT"); } }
        GLboolean APIENTRY S_glIsTextureEXT(GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsTextureEXT"); } return {}; }
        void APIENTRY S_glPrioritizeTexturesEXT(GLsizei n, const GLuint *textures, const GLclampf *priorities) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPrioritizeTexturesEXT"); } }
        void APIENTRY S_glTextureNormalEXT(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureNormalEXT"); } }
        void APIENTRY S_glTexStorage1DEXT(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorage1DEXT"); } }
        void APIENTRY S_glTexStorage2DEXT(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorage2DEXT"); } }
        void APIENTRY S_glTexStorage3DEXT(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexStorage3DEXT"); } }
        void APIENTRY S_glGetQueryObjecti64vEXT(GLuint id, GLenum pname, GLint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryObjecti64vEXT"); } }
        void APIENTRY S_glGetQueryObjectui64vEXT(GLuint id, GLenum pname, GLuint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetQueryObjectui64vEXT"); } }
        void APIENTRY S_glBeginTransformFeedbackEXT(GLenum primitiveMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginTransformFeedbackEXT"); } }
        void APIENTRY S_glEndTransformFeedbackEXT(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndTransformFeedbackEXT"); } }
        void APIENTRY S_glBindBufferRangeEXT(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBufferRangeEXT"); } }
        void APIENTRY S_glBindBufferOffsetEXT(GLenum target, GLuint index, GLuint buffer, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBufferOffsetEXT"); } }
        void APIENTRY S_glBindBufferBaseEXT(GLenum target, GLuint index, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBufferBaseEXT"); } }
        void APIENTRY S_glTransformFeedbackVaryingsEXT(GLuint program, GLsizei count, const GLchar *const*varyings, GLenum bufferMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTransformFeedbackVaryingsEXT"); } }
        void APIENTRY S_glGetTransformFeedbackVaryingEXT(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLsizei *size, GLenum *type, GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTransformFeedbackVaryingEXT"); } }
        void APIENTRY S_glArrayElementEXT(GLint i) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glArrayElementEXT"); } }
        void APIENTRY S_glColorPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorPointerEXT"); } }
        void APIENTRY S_glDrawArraysEXT(GLenum mode, GLint first, GLsizei count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawArraysEXT"); } }
        void APIENTRY S_glEdgeFlagPointerEXT(GLsizei stride, GLsizei count, const GLboolean *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEdgeFlagPointerEXT"); } }
        void APIENTRY S_glGetPointervEXT(GLenum pname, void **params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPointervEXT"); } }
        void APIENTRY S_glIndexPointerEXT(GLenum type, GLsizei stride, GLsizei count, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIndexPointerEXT"); } }
        void APIENTRY S_glNormalPointerEXT(GLenum type, GLsizei stride, GLsizei count, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalPointerEXT"); } }
        void APIENTRY S_glTexCoordPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordPointerEXT"); } }
        void APIENTRY S_glVertexPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexPointerEXT"); } }
        void APIENTRY S_glVertexAttribL1dEXT(GLuint index, GLdouble x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL1dEXT"); } }
        void APIENTRY S_glVertexAttribL2dEXT(GLuint index, GLdouble x, GLdouble y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL2dEXT"); } }
        void APIENTRY S_glVertexAttribL3dEXT(GLuint index, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL3dEXT"); } }
        void APIENTRY S_glVertexAttribL4dEXT(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL4dEXT"); } }
        void APIENTRY S_glVertexAttribL1dvEXT(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL1dvEXT"); } }
        void APIENTRY S_glVertexAttribL2dvEXT(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL2dvEXT"); } }
        void APIENTRY S_glVertexAttribL3dvEXT(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL3dvEXT"); } }
        void APIENTRY S_glVertexAttribL4dvEXT(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL4dvEXT"); } }
        void APIENTRY S_glVertexAttribLPointerEXT(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribLPointerEXT"); } }
        void APIENTRY S_glGetVertexAttribLdvEXT(GLuint index, GLenum pname, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribLdvEXT"); } }
        void APIENTRY S_glBeginVertexShaderEXT(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginVertexShaderEXT"); } }
        void APIENTRY S_glEndVertexShaderEXT(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndVertexShaderEXT"); } }
        void APIENTRY S_glBindVertexShaderEXT(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindVertexShaderEXT"); } }
        GLuint APIENTRY S_glGenVertexShadersEXT(GLuint range) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenVertexShadersEXT"); } return {}; }
        void APIENTRY S_glDeleteVertexShaderEXT(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteVertexShaderEXT"); } }
        void APIENTRY S_glShaderOp1EXT(GLenum op, GLuint res, GLuint arg1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShaderOp1EXT"); } }
        void APIENTRY S_glShaderOp2EXT(GLenum op, GLuint res, GLuint arg1, GLuint arg2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShaderOp2EXT"); } }
        void APIENTRY S_glShaderOp3EXT(GLenum op, GLuint res, GLuint arg1, GLuint arg2, GLuint arg3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShaderOp3EXT"); } }
        void APIENTRY S_glSwizzleEXT(GLuint res, GLuint in, GLenum outX, GLenum outY, GLenum outZ, GLenum outW) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSwizzleEXT"); } }
        void APIENTRY S_glWriteMaskEXT(GLuint res, GLuint in, GLenum outX, GLenum outY, GLenum outZ, GLenum outW) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWriteMaskEXT"); } }
        void APIENTRY S_glInsertComponentEXT(GLuint res, GLuint src, GLuint num) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInsertComponentEXT"); } }
        void APIENTRY S_glExtractComponentEXT(GLuint res, GLuint src, GLuint num) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glExtractComponentEXT"); } }
        GLuint APIENTRY S_glGenSymbolsEXT(GLenum datatype, GLenum storagetype, GLenum range, GLuint components) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenSymbolsEXT"); } return {}; }
        void APIENTRY S_glSetInvariantEXT(GLuint id, GLenum type, const void *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSetInvariantEXT"); } }
        void APIENTRY S_glSetLocalConstantEXT(GLuint id, GLenum type, const void *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSetLocalConstantEXT"); } }
        void APIENTRY S_glVariantbvEXT(GLuint id, const GLbyte *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVariantbvEXT"); } }
        void APIENTRY S_glVariantsvEXT(GLuint id, const GLshort *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVariantsvEXT"); } }
        void APIENTRY S_glVariantivEXT(GLuint id, const GLint *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVariantivEXT"); } }
        void APIENTRY S_glVariantfvEXT(GLuint id, const GLfloat *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVariantfvEXT"); } }
        void APIENTRY S_glVariantdvEXT(GLuint id, const GLdouble *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVariantdvEXT"); } }
        void APIENTRY S_glVariantubvEXT(GLuint id, const GLubyte *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVariantubvEXT"); } }
        void APIENTRY S_glVariantusvEXT(GLuint id, const GLushort *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVariantusvEXT"); } }
        void APIENTRY S_glVariantuivEXT(GLuint id, const GLuint *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVariantuivEXT"); } }
        void APIENTRY S_glVariantPointerEXT(GLuint id, GLenum type, GLuint stride, const void *addr) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVariantPointerEXT"); } }
        void APIENTRY S_glEnableVariantClientStateEXT(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEnableVariantClientStateEXT"); } }
        void APIENTRY S_glDisableVariantClientStateEXT(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDisableVariantClientStateEXT"); } }
        GLuint APIENTRY S_glBindLightParameterEXT(GLenum light, GLenum value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindLightParameterEXT"); } return {}; }
        GLuint APIENTRY S_glBindMaterialParameterEXT(GLenum face, GLenum value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindMaterialParameterEXT"); } return {}; }
        GLuint APIENTRY S_glBindTexGenParameterEXT(GLenum unit, GLenum coord, GLenum value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindTexGenParameterEXT"); } return {}; }
        GLuint APIENTRY S_glBindTextureUnitParameterEXT(GLenum unit, GLenum value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindTextureUnitParameterEXT"); } return {}; }
        GLuint APIENTRY S_glBindParameterEXT(GLenum value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindParameterEXT"); } return {}; }
        GLboolean APIENTRY S_glIsVariantEnabledEXT(GLuint id, GLenum cap) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsVariantEnabledEXT"); } return {}; }
        void APIENTRY S_glGetVariantBooleanvEXT(GLuint id, GLenum value, GLboolean *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVariantBooleanvEXT"); } }
        void APIENTRY S_glGetVariantIntegervEXT(GLuint id, GLenum value, GLint *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVariantIntegervEXT"); } }
        void APIENTRY S_glGetVariantFloatvEXT(GLuint id, GLenum value, GLfloat *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVariantFloatvEXT"); } }
        void APIENTRY S_glGetVariantPointervEXT(GLuint id, GLenum value, void **data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVariantPointervEXT"); } }
        void APIENTRY S_glGetInvariantBooleanvEXT(GLuint id, GLenum value, GLboolean *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetInvariantBooleanvEXT"); } }
        void APIENTRY S_glGetInvariantIntegervEXT(GLuint id, GLenum value, GLint *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetInvariantIntegervEXT"); } }
        void APIENTRY S_glGetInvariantFloatvEXT(GLuint id, GLenum value, GLfloat *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetInvariantFloatvEXT"); } }
        void APIENTRY S_glGetLocalConstantBooleanvEXT(GLuint id, GLenum value, GLboolean *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetLocalConstantBooleanvEXT"); } }
        void APIENTRY S_glGetLocalConstantIntegervEXT(GLuint id, GLenum value, GLint *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetLocalConstantIntegervEXT"); } }
        void APIENTRY S_glGetLocalConstantFloatvEXT(GLuint id, GLenum value, GLfloat *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetLocalConstantFloatvEXT"); } }
        void APIENTRY S_glVertexWeightfEXT(GLfloat weight) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexWeightfEXT"); } }
        void APIENTRY S_glVertexWeightfvEXT(const GLfloat *weight) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexWeightfvEXT"); } }
        void APIENTRY S_glVertexWeightPointerEXT(GLint size, GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexWeightPointerEXT"); } }
        GLboolean APIENTRY S_glAcquireKeyedMutexWin32EXT(GLuint memory, GLuint64 key, GLuint timeout) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAcquireKeyedMutexWin32EXT"); } return {}; }
        GLboolean APIENTRY S_glReleaseKeyedMutexWin32EXT(GLuint memory, GLuint64 key) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReleaseKeyedMutexWin32EXT"); } return {}; }
        void APIENTRY S_glWindowRectanglesEXT(GLenum mode, GLsizei count, const GLint *box) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowRectanglesEXT"); } }
        GLsync APIENTRY S_glImportSyncEXT(GLenum external_sync_type, GLintptr external_sync, GLbitfield flags) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImportSyncEXT"); } return {}; }
        void APIENTRY S_glFrameTerminatorGREMEDY(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFrameTerminatorGREMEDY"); } }
        void APIENTRY S_glStringMarkerGREMEDY(GLsizei len, const void *string) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStringMarkerGREMEDY"); } }
        void APIENTRY S_glImageTransformParameteriHP(GLenum target, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImageTransformParameteriHP"); } }
        void APIENTRY S_glImageTransformParameterfHP(GLenum target, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImageTransformParameterfHP"); } }
        void APIENTRY S_glImageTransformParameterivHP(GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImageTransformParameterivHP"); } }
        void APIENTRY S_glImageTransformParameterfvHP(GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glImageTransformParameterfvHP"); } }
        void APIENTRY S_glGetImageTransformParameterivHP(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetImageTransformParameterivHP"); } }
        void APIENTRY S_glGetImageTransformParameterfvHP(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetImageTransformParameterfvHP"); } }
        void APIENTRY S_glMultiModeDrawArraysIBM(const GLenum *mode, const GLint *first, const GLsizei *count, GLsizei primcount, GLint modestride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiModeDrawArraysIBM"); } }
        void APIENTRY S_glMultiModeDrawElementsIBM(const GLenum *mode, const GLsizei *count, GLenum type, const void *const*indices, GLsizei primcount, GLint modestride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiModeDrawElementsIBM"); } }
        void APIENTRY S_glFlushStaticDataIBM(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFlushStaticDataIBM"); } }
        void APIENTRY S_glColorPointerListIBM(GLint size, GLenum type, GLint stride, const void **pointer, GLint ptrstride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorPointerListIBM"); } }
        void APIENTRY S_glSecondaryColorPointerListIBM(GLint size, GLenum type, GLint stride, const void **pointer, GLint ptrstride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColorPointerListIBM"); } }
        void APIENTRY S_glEdgeFlagPointerListIBM(GLint stride, const GLboolean **pointer, GLint ptrstride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEdgeFlagPointerListIBM"); } }
        void APIENTRY S_glFogCoordPointerListIBM(GLenum type, GLint stride, const void **pointer, GLint ptrstride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogCoordPointerListIBM"); } }
        void APIENTRY S_glIndexPointerListIBM(GLenum type, GLint stride, const void **pointer, GLint ptrstride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIndexPointerListIBM"); } }
        void APIENTRY S_glNormalPointerListIBM(GLenum type, GLint stride, const void **pointer, GLint ptrstride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalPointerListIBM"); } }
        void APIENTRY S_glTexCoordPointerListIBM(GLint size, GLenum type, GLint stride, const void **pointer, GLint ptrstride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordPointerListIBM"); } }
        void APIENTRY S_glVertexPointerListIBM(GLint size, GLenum type, GLint stride, const void **pointer, GLint ptrstride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexPointerListIBM"); } }
        void APIENTRY S_glBlendFuncSeparateINGR(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendFuncSeparateINGR"); } }
        void APIENTRY S_glApplyFramebufferAttachmentCMAAINTEL(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glApplyFramebufferAttachmentCMAAINTEL"); } }
        void APIENTRY S_glSyncTextureINTEL(GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSyncTextureINTEL"); } }
        void APIENTRY S_glUnmapTexture2DINTEL(GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUnmapTexture2DINTEL"); } }
        void * APIENTRY S_glMapTexture2DINTEL(GLuint texture, GLint level, GLbitfield access, GLint *stride, GLenum *layout) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapTexture2DINTEL"); } return {}; }
        void APIENTRY S_glVertexPointervINTEL(GLint size, GLenum type, const void **pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexPointervINTEL"); } }
        void APIENTRY S_glNormalPointervINTEL(GLenum type, const void **pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalPointervINTEL"); } }
        void APIENTRY S_glColorPointervINTEL(GLint size, GLenum type, const void **pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorPointervINTEL"); } }
        void APIENTRY S_glTexCoordPointervINTEL(GLint size, GLenum type, const void **pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordPointervINTEL"); } }
        void APIENTRY S_glBeginPerfQueryINTEL(GLuint queryHandle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginPerfQueryINTEL"); } }
        void APIENTRY S_glCreatePerfQueryINTEL(GLuint queryId, GLuint *queryHandle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreatePerfQueryINTEL"); } }
        void APIENTRY S_glDeletePerfQueryINTEL(GLuint queryHandle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeletePerfQueryINTEL"); } }
        void APIENTRY S_glEndPerfQueryINTEL(GLuint queryHandle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndPerfQueryINTEL"); } }
        void APIENTRY S_glGetFirstPerfQueryIdINTEL(GLuint *queryId) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFirstPerfQueryIdINTEL"); } }
        void APIENTRY S_glGetNextPerfQueryIdINTEL(GLuint queryId, GLuint *nextQueryId) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNextPerfQueryIdINTEL"); } }
        void APIENTRY S_glGetPerfCounterInfoINTEL(GLuint queryId, GLuint counterId, GLuint counterNameLength, GLchar *counterName, GLuint counterDescLength, GLchar *counterDesc, GLuint *counterOffset, GLuint *counterDataSize, GLuint *counterTypeEnum, GLuint *counterDataTypeEnum, GLuint64 *rawCounterMaxValue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPerfCounterInfoINTEL"); } }
        void APIENTRY S_glGetPerfQueryDataINTEL(GLuint queryHandle, GLuint flags, GLsizei dataSize, void *data, GLuint *bytesWritten) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPerfQueryDataINTEL"); } }
        void APIENTRY S_glGetPerfQueryIdByNameINTEL(GLchar *queryName, GLuint *queryId) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPerfQueryIdByNameINTEL"); } }
        void APIENTRY S_glGetPerfQueryInfoINTEL(GLuint queryId, GLuint queryNameLength, GLchar *queryName, GLuint *dataSize, GLuint *noCounters, GLuint *noInstances, GLuint *capsMask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPerfQueryInfoINTEL"); } }
        void APIENTRY S_glBlendBarrierKHR(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendBarrierKHR"); } }
        void APIENTRY S_glDebugMessageControlKHR(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageControlKHR"); } }
        void APIENTRY S_glDebugMessageInsertKHR(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *buf) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageInsertKHR"); } }
        void APIENTRY S_glDebugMessageCallbackKHR(GLDEBUGPROCKHR callback, const void *userParam) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDebugMessageCallbackKHR"); } }
        GLuint APIENTRY S_glGetDebugMessageLogKHR(GLuint count, GLsizei bufSize, GLenum *sources, GLenum *types, GLuint *ids, GLenum *severities, GLsizei *lengths, GLchar *messageLog) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetDebugMessageLogKHR"); } return {}; }
        void APIENTRY S_glPushDebugGroupKHR(GLenum source, GLuint id, GLsizei length, const GLchar *message) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPushDebugGroupKHR"); } }
        void APIENTRY S_glPopDebugGroupKHR(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPopDebugGroupKHR"); } }
        void APIENTRY S_glObjectLabelKHR(GLenum identifier, GLuint name, GLsizei length, const GLchar *label) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glObjectLabelKHR"); } }
        void APIENTRY S_glGetObjectLabelKHR(GLenum identifier, GLuint name, GLsizei bufSize, GLsizei *length, GLchar *label) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetObjectLabelKHR"); } }
        void APIENTRY S_glObjectPtrLabelKHR(const void *ptr, GLsizei length, const GLchar *label) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glObjectPtrLabelKHR"); } }
        void APIENTRY S_glGetObjectPtrLabelKHR(const void *ptr, GLsizei bufSize, GLsizei *length, GLchar *label) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetObjectPtrLabelKHR"); } }
        void APIENTRY S_glGetPointervKHR(GLenum pname, void **params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPointervKHR"); } }
        void APIENTRY S_glMaxShaderCompilerThreadsKHR(GLuint count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMaxShaderCompilerThreadsKHR"); } }
        GLenum APIENTRY S_glGetGraphicsResetStatusKHR(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetGraphicsResetStatusKHR"); } return {}; }
        void APIENTRY S_glReadnPixelsKHR(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLsizei bufSize, void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReadnPixelsKHR"); } }
        void APIENTRY S_glGetnUniformfvKHR(GLuint program, GLint location, GLsizei bufSize, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformfvKHR"); } }
        void APIENTRY S_glGetnUniformivKHR(GLuint program, GLint location, GLsizei bufSize, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformivKHR"); } }
        void APIENTRY S_glGetnUniformuivKHR(GLuint program, GLint location, GLsizei bufSize, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetnUniformuivKHR"); } }
        void APIENTRY S_glFramebufferParameteriMESA(GLenum target, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferParameteriMESA"); } }
        void APIENTRY S_glGetFramebufferParameterivMESA(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFramebufferParameterivMESA"); } }
        void APIENTRY S_glResizeBuffersMESA(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glResizeBuffersMESA"); } }
        void APIENTRY S_glWindowPos2dMESA(GLdouble x, GLdouble y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2dMESA"); } }
        void APIENTRY S_glWindowPos2dvMESA(const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2dvMESA"); } }
        void APIENTRY S_glWindowPos2fMESA(GLfloat x, GLfloat y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2fMESA"); } }
        void APIENTRY S_glWindowPos2fvMESA(const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2fvMESA"); } }
        void APIENTRY S_glWindowPos2iMESA(GLint x, GLint y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2iMESA"); } }
        void APIENTRY S_glWindowPos2ivMESA(const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2ivMESA"); } }
        void APIENTRY S_glWindowPos2sMESA(GLshort x, GLshort y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2sMESA"); } }
        void APIENTRY S_glWindowPos2svMESA(const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos2svMESA"); } }
        void APIENTRY S_glWindowPos3dMESA(GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3dMESA"); } }
        void APIENTRY S_glWindowPos3dvMESA(const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3dvMESA"); } }
        void APIENTRY S_glWindowPos3fMESA(GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3fMESA"); } }
        void APIENTRY S_glWindowPos3fvMESA(const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3fvMESA"); } }
        void APIENTRY S_glWindowPos3iMESA(GLint x, GLint y, GLint z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3iMESA"); } }
        void APIENTRY S_glWindowPos3ivMESA(const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3ivMESA"); } }
        void APIENTRY S_glWindowPos3sMESA(GLshort x, GLshort y, GLshort z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3sMESA"); } }
        void APIENTRY S_glWindowPos3svMESA(const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos3svMESA"); } }
        void APIENTRY S_glWindowPos4dMESA(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos4dMESA"); } }
        void APIENTRY S_glWindowPos4dvMESA(const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos4dvMESA"); } }
        void APIENTRY S_glWindowPos4fMESA(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos4fMESA"); } }
        void APIENTRY S_glWindowPos4fvMESA(const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos4fvMESA"); } }
        void APIENTRY S_glWindowPos4iMESA(GLint x, GLint y, GLint z, GLint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos4iMESA"); } }
        void APIENTRY S_glWindowPos4ivMESA(const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos4ivMESA"); } }
        void APIENTRY S_glWindowPos4sMESA(GLshort x, GLshort y, GLshort z, GLshort w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos4sMESA"); } }
        void APIENTRY S_glWindowPos4svMESA(const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWindowPos4svMESA"); } }
        void APIENTRY S_glBeginConditionalRenderNVX(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginConditionalRenderNVX"); } }
        void APIENTRY S_glEndConditionalRenderNVX(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndConditionalRenderNVX"); } }
        void APIENTRY S_glUploadGpuMaskNVX(GLbitfield mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUploadGpuMaskNVX"); } }
        void APIENTRY S_glMulticastViewportArrayvNVX(GLuint gpu, GLuint first, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastViewportArrayvNVX"); } }
        void APIENTRY S_glMulticastViewportPositionWScaleNVX(GLuint gpu, GLuint index, GLfloat xcoeff, GLfloat ycoeff) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastViewportPositionWScaleNVX"); } }
        void APIENTRY S_glMulticastScissorArrayvNVX(GLuint gpu, GLuint first, GLsizei count, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastScissorArrayvNVX"); } }
        GLuint APIENTRY S_glAsyncCopyBufferSubDataNVX(GLsizei waitSemaphoreCount, const GLuint *waitSemaphoreArray, const GLuint64 *fenceValueArray, GLuint readGpu, GLbitfield writeGpuMask, GLuint readBuffer, GLuint writeBuffer, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size, GLsizei signalSemaphoreCount, const GLuint *signalSemaphoreArray, const GLuint64 *signalValueArray) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAsyncCopyBufferSubDataNVX"); } return {}; }
        GLuint APIENTRY S_glAsyncCopyImageSubDataNVX(GLsizei waitSemaphoreCount, const GLuint *waitSemaphoreArray, const GLuint64 *waitValueArray, GLuint srcGpu, GLbitfield dstGpuMask, GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth, GLsizei signalSemaphoreCount, const GLuint *signalSemaphoreArray, const GLuint64 *signalValueArray) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAsyncCopyImageSubDataNVX"); } return {}; }
        void APIENTRY S_glLGPUNamedBufferSubDataNVX(GLbitfield gpuMask, GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLGPUNamedBufferSubDataNVX"); } }
        void APIENTRY S_glLGPUCopyImageSubDataNVX(GLuint sourceGpu, GLbitfield destinationGpuMask, GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srxY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei width, GLsizei height, GLsizei depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLGPUCopyImageSubDataNVX"); } }
        void APIENTRY S_glLGPUInterlockNVX(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLGPUInterlockNVX"); } }
        GLuint APIENTRY S_glCreateProgressFenceNVX(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateProgressFenceNVX"); } return {}; }
        void APIENTRY S_glSignalSemaphoreui64NVX(GLuint signalGpu, GLsizei fenceObjectCount, const GLuint *semaphoreArray, const GLuint64 *fenceValueArray) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSignalSemaphoreui64NVX"); } }
        void APIENTRY S_glWaitSemaphoreui64NVX(GLuint waitGpu, GLsizei fenceObjectCount, const GLuint *semaphoreArray, const GLuint64 *fenceValueArray) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWaitSemaphoreui64NVX"); } }
        void APIENTRY S_glClientWaitSemaphoreui64NVX(GLsizei fenceObjectCount, const GLuint *semaphoreArray, const GLuint64 *fenceValueArray) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClientWaitSemaphoreui64NVX"); } }
        void APIENTRY S_glAlphaToCoverageDitherControlNV(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAlphaToCoverageDitherControlNV"); } }
        void APIENTRY S_glMultiDrawArraysIndirectBindlessNV(GLenum mode, const void *indirect, GLsizei drawCount, GLsizei stride, GLint vertexBufferCount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawArraysIndirectBindlessNV"); } }
        void APIENTRY S_glMultiDrawElementsIndirectBindlessNV(GLenum mode, GLenum type, const void *indirect, GLsizei drawCount, GLsizei stride, GLint vertexBufferCount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawElementsIndirectBindlessNV"); } }
        void APIENTRY S_glMultiDrawArraysIndirectBindlessCountNV(GLenum mode, const void *indirect, GLsizei drawCount, GLsizei maxDrawCount, GLsizei stride, GLint vertexBufferCount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawArraysIndirectBindlessCountNV"); } }
        void APIENTRY S_glMultiDrawElementsIndirectBindlessCountNV(GLenum mode, GLenum type, const void *indirect, GLsizei drawCount, GLsizei maxDrawCount, GLsizei stride, GLint vertexBufferCount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawElementsIndirectBindlessCountNV"); } }
        GLuint64 APIENTRY S_glGetTextureHandleNV(GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureHandleNV"); } return {}; }
        GLuint64 APIENTRY S_glGetTextureSamplerHandleNV(GLuint texture, GLuint sampler) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTextureSamplerHandleNV"); } return {}; }
        void APIENTRY S_glMakeTextureHandleResidentNV(GLuint64 handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeTextureHandleResidentNV"); } }
        void APIENTRY S_glMakeTextureHandleNonResidentNV(GLuint64 handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeTextureHandleNonResidentNV"); } }
        GLuint64 APIENTRY S_glGetImageHandleNV(GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum format) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetImageHandleNV"); } return {}; }
        void APIENTRY S_glMakeImageHandleResidentNV(GLuint64 handle, GLenum access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeImageHandleResidentNV"); } }
        void APIENTRY S_glMakeImageHandleNonResidentNV(GLuint64 handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeImageHandleNonResidentNV"); } }
        void APIENTRY S_glUniformHandleui64NV(GLint location, GLuint64 value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformHandleui64NV"); } }
        void APIENTRY S_glUniformHandleui64vNV(GLint location, GLsizei count, const GLuint64 *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformHandleui64vNV"); } }
        void APIENTRY S_glProgramUniformHandleui64NV(GLuint program, GLint location, GLuint64 value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformHandleui64NV"); } }
        void APIENTRY S_glProgramUniformHandleui64vNV(GLuint program, GLint location, GLsizei count, const GLuint64 *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformHandleui64vNV"); } }
        GLboolean APIENTRY S_glIsTextureHandleResidentNV(GLuint64 handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsTextureHandleResidentNV"); } return {}; }
        GLboolean APIENTRY S_glIsImageHandleResidentNV(GLuint64 handle) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsImageHandleResidentNV"); } return {}; }
        void APIENTRY S_glBlendParameteriNV(GLenum pname, GLint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendParameteriNV"); } }
        void APIENTRY S_glBlendBarrierNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendBarrierNV"); } }
        void APIENTRY S_glViewportPositionWScaleNV(GLuint index, GLfloat xcoeff, GLfloat ycoeff) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glViewportPositionWScaleNV"); } }
        void APIENTRY S_glCreateStatesNV(GLsizei n, GLuint *states) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateStatesNV"); } }
        void APIENTRY S_glDeleteStatesNV(GLsizei n, const GLuint *states) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteStatesNV"); } }
        GLboolean APIENTRY S_glIsStateNV(GLuint state) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsStateNV"); } return {}; }
        void APIENTRY S_glStateCaptureNV(GLuint state, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStateCaptureNV"); } }
        GLuint APIENTRY S_glGetCommandHeaderNV(GLenum tokenID, GLuint size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCommandHeaderNV"); } return {}; }
        GLushort APIENTRY S_glGetStageIndexNV(GLenum shadertype) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetStageIndexNV"); } return {}; }
        void APIENTRY S_glDrawCommandsNV(GLenum primitiveMode, GLuint buffer, const GLintptr *indirects, const GLsizei *sizes, GLuint count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawCommandsNV"); } }
        void APIENTRY S_glDrawCommandsAddressNV(GLenum primitiveMode, const GLuint64 *indirects, const GLsizei *sizes, GLuint count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawCommandsAddressNV"); } }
        void APIENTRY S_glDrawCommandsStatesNV(GLuint buffer, const GLintptr *indirects, const GLsizei *sizes, const GLuint *states, const GLuint *fbos, GLuint count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawCommandsStatesNV"); } }
        void APIENTRY S_glDrawCommandsStatesAddressNV(const GLuint64 *indirects, const GLsizei *sizes, const GLuint *states, const GLuint *fbos, GLuint count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawCommandsStatesAddressNV"); } }
        void APIENTRY S_glCreateCommandListsNV(GLsizei n, GLuint *lists) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateCommandListsNV"); } }
        void APIENTRY S_glDeleteCommandListsNV(GLsizei n, const GLuint *lists) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteCommandListsNV"); } }
        GLboolean APIENTRY S_glIsCommandListNV(GLuint list) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsCommandListNV"); } return {}; }
        void APIENTRY S_glListDrawCommandsStatesClientNV(GLuint list, GLuint segment, const void **indirects, const GLsizei *sizes, const GLuint *states, const GLuint *fbos, GLuint count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glListDrawCommandsStatesClientNV"); } }
        void APIENTRY S_glCommandListSegmentsNV(GLuint list, GLuint segments) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCommandListSegmentsNV"); } }
        void APIENTRY S_glCompileCommandListNV(GLuint list) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCompileCommandListNV"); } }
        void APIENTRY S_glCallCommandListNV(GLuint list) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCallCommandListNV"); } }
        void APIENTRY S_glBeginConditionalRenderNV(GLuint id, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginConditionalRenderNV"); } }
        void APIENTRY S_glEndConditionalRenderNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndConditionalRenderNV"); } }
        void APIENTRY S_glSubpixelPrecisionBiasNV(GLuint xbits, GLuint ybits) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSubpixelPrecisionBiasNV"); } }
        void APIENTRY S_glConservativeRasterParameterfNV(GLenum pname, GLfloat value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConservativeRasterParameterfNV"); } }
        void APIENTRY S_glConservativeRasterParameteriNV(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConservativeRasterParameteriNV"); } }
        void APIENTRY S_glCopyImageSubDataNV(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei width, GLsizei height, GLsizei depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyImageSubDataNV"); } }
        void APIENTRY S_glDepthRangedNV(GLdouble zNear, GLdouble zFar) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthRangedNV"); } }
        void APIENTRY S_glClearDepthdNV(GLdouble depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearDepthdNV"); } }
        void APIENTRY S_glDepthBoundsdNV(GLdouble zmin, GLdouble zmax) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthBoundsdNV"); } }
        void APIENTRY S_glDrawTextureNV(GLuint texture, GLuint sampler, GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1, GLfloat z, GLfloat s0, GLfloat t0, GLfloat s1, GLfloat t1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawTextureNV"); } }
        void APIENTRY S_glDrawVkImageNV(GLuint64 vkImage, GLuint sampler, GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1, GLfloat z, GLfloat s0, GLfloat t0, GLfloat s1, GLfloat t1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawVkImageNV"); } }
        GLVULKANPROCNV APIENTRY S_glGetVkProcAddrNV(const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVkProcAddrNV"); } return {}; }
        void APIENTRY S_glWaitVkSemaphoreNV(GLuint64 vkSemaphore) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWaitVkSemaphoreNV"); } }
        void APIENTRY S_glSignalVkSemaphoreNV(GLuint64 vkSemaphore) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSignalVkSemaphoreNV"); } }
        void APIENTRY S_glSignalVkFenceNV(GLuint64 vkFence) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSignalVkFenceNV"); } }
        void APIENTRY S_glMapControlPointsNV(GLenum target, GLuint index, GLenum type, GLsizei ustride, GLsizei vstride, GLint uorder, GLint vorder, GLboolean packed, const void *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapControlPointsNV"); } }
        void APIENTRY S_glMapParameterivNV(GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapParameterivNV"); } }
        void APIENTRY S_glMapParameterfvNV(GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapParameterfvNV"); } }
        void APIENTRY S_glGetMapControlPointsNV(GLenum target, GLuint index, GLenum type, GLsizei ustride, GLsizei vstride, GLboolean packed, void *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMapControlPointsNV"); } }
        void APIENTRY S_glGetMapParameterivNV(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMapParameterivNV"); } }
        void APIENTRY S_glGetMapParameterfvNV(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMapParameterfvNV"); } }
        void APIENTRY S_glGetMapAttribParameterivNV(GLenum target, GLuint index, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMapAttribParameterivNV"); } }
        void APIENTRY S_glGetMapAttribParameterfvNV(GLenum target, GLuint index, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMapAttribParameterfvNV"); } }
        void APIENTRY S_glEvalMapsNV(GLenum target, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEvalMapsNV"); } }
        void APIENTRY S_glGetMultisamplefvNV(GLenum pname, GLuint index, GLfloat *val) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMultisamplefvNV"); } }
        void APIENTRY S_glSampleMaskIndexedNV(GLuint index, GLbitfield mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSampleMaskIndexedNV"); } }
        void APIENTRY S_glTexRenderbufferNV(GLenum target, GLuint renderbuffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexRenderbufferNV"); } }
        void APIENTRY S_glDeleteFencesNV(GLsizei n, const GLuint *fences) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteFencesNV"); } }
        void APIENTRY S_glGenFencesNV(GLsizei n, GLuint *fences) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenFencesNV"); } }
        GLboolean APIENTRY S_glIsFenceNV(GLuint fence) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsFenceNV"); } return {}; }
        GLboolean APIENTRY S_glTestFenceNV(GLuint fence) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTestFenceNV"); } return {}; }
        void APIENTRY S_glGetFenceivNV(GLuint fence, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFenceivNV"); } }
        void APIENTRY S_glFinishFenceNV(GLuint fence) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFinishFenceNV"); } }
        void APIENTRY S_glSetFenceNV(GLuint fence, GLenum condition) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSetFenceNV"); } }
        void APIENTRY S_glFragmentCoverageColorNV(GLuint color) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentCoverageColorNV"); } }
        void APIENTRY S_glProgramNamedParameter4fNV(GLuint id, GLsizei len, const GLubyte *name, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramNamedParameter4fNV"); } }
        void APIENTRY S_glProgramNamedParameter4fvNV(GLuint id, GLsizei len, const GLubyte *name, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramNamedParameter4fvNV"); } }
        void APIENTRY S_glProgramNamedParameter4dNV(GLuint id, GLsizei len, const GLubyte *name, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramNamedParameter4dNV"); } }
        void APIENTRY S_glProgramNamedParameter4dvNV(GLuint id, GLsizei len, const GLubyte *name, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramNamedParameter4dvNV"); } }
        void APIENTRY S_glGetProgramNamedParameterfvNV(GLuint id, GLsizei len, const GLubyte *name, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramNamedParameterfvNV"); } }
        void APIENTRY S_glGetProgramNamedParameterdvNV(GLuint id, GLsizei len, const GLubyte *name, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramNamedParameterdvNV"); } }
        void APIENTRY S_glCoverageModulationTableNV(GLsizei n, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCoverageModulationTableNV"); } }
        void APIENTRY S_glGetCoverageModulationTableNV(GLsizei bufSize, GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCoverageModulationTableNV"); } }
        void APIENTRY S_glCoverageModulationNV(GLenum components) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCoverageModulationNV"); } }
        void APIENTRY S_glRenderbufferStorageMultisampleCoverageNV(GLenum target, GLsizei coverageSamples, GLsizei colorSamples, GLenum internalformat, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRenderbufferStorageMultisampleCoverageNV"); } }
        void APIENTRY S_glProgramVertexLimitNV(GLenum target, GLint limit) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramVertexLimitNV"); } }
        void APIENTRY S_glFramebufferTextureEXT(GLenum target, GLenum attachment, GLuint texture, GLint level) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTextureEXT"); } }
        void APIENTRY S_glFramebufferTextureFaceEXT(GLenum target, GLenum attachment, GLuint texture, GLint level, GLenum face) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTextureFaceEXT"); } }
        void APIENTRY S_glRenderGpuMaskNV(GLbitfield mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRenderGpuMaskNV"); } }
        void APIENTRY S_glMulticastBufferSubDataNV(GLbitfield gpuMask, GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastBufferSubDataNV"); } }
        void APIENTRY S_glMulticastCopyBufferSubDataNV(GLuint readGpu, GLbitfield writeGpuMask, GLuint readBuffer, GLuint writeBuffer, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastCopyBufferSubDataNV"); } }
        void APIENTRY S_glMulticastCopyImageSubDataNV(GLuint srcGpu, GLbitfield dstGpuMask, GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastCopyImageSubDataNV"); } }
        void APIENTRY S_glMulticastBlitFramebufferNV(GLuint srcGpu, GLuint dstGpu, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastBlitFramebufferNV"); } }
        void APIENTRY S_glMulticastFramebufferSampleLocationsfvNV(GLuint gpu, GLuint framebuffer, GLuint start, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastFramebufferSampleLocationsfvNV"); } }
        void APIENTRY S_glMulticastBarrierNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastBarrierNV"); } }
        void APIENTRY S_glMulticastWaitSyncNV(GLuint signalGpu, GLbitfield waitGpuMask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastWaitSyncNV"); } }
        void APIENTRY S_glMulticastGetQueryObjectivNV(GLuint gpu, GLuint id, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastGetQueryObjectivNV"); } }
        void APIENTRY S_glMulticastGetQueryObjectuivNV(GLuint gpu, GLuint id, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastGetQueryObjectuivNV"); } }
        void APIENTRY S_glMulticastGetQueryObjecti64vNV(GLuint gpu, GLuint id, GLenum pname, GLint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastGetQueryObjecti64vNV"); } }
        void APIENTRY S_glMulticastGetQueryObjectui64vNV(GLuint gpu, GLuint id, GLenum pname, GLuint64 *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMulticastGetQueryObjectui64vNV"); } }
        void APIENTRY S_glProgramLocalParameterI4iNV(GLenum target, GLuint index, GLint x, GLint y, GLint z, GLint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParameterI4iNV"); } }
        void APIENTRY S_glProgramLocalParameterI4ivNV(GLenum target, GLuint index, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParameterI4ivNV"); } }
        void APIENTRY S_glProgramLocalParametersI4ivNV(GLenum target, GLuint index, GLsizei count, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParametersI4ivNV"); } }
        void APIENTRY S_glProgramLocalParameterI4uiNV(GLenum target, GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParameterI4uiNV"); } }
        void APIENTRY S_glProgramLocalParameterI4uivNV(GLenum target, GLuint index, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParameterI4uivNV"); } }
        void APIENTRY S_glProgramLocalParametersI4uivNV(GLenum target, GLuint index, GLsizei count, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramLocalParametersI4uivNV"); } }
        void APIENTRY S_glProgramEnvParameterI4iNV(GLenum target, GLuint index, GLint x, GLint y, GLint z, GLint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParameterI4iNV"); } }
        void APIENTRY S_glProgramEnvParameterI4ivNV(GLenum target, GLuint index, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParameterI4ivNV"); } }
        void APIENTRY S_glProgramEnvParametersI4ivNV(GLenum target, GLuint index, GLsizei count, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParametersI4ivNV"); } }
        void APIENTRY S_glProgramEnvParameterI4uiNV(GLenum target, GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParameterI4uiNV"); } }
        void APIENTRY S_glProgramEnvParameterI4uivNV(GLenum target, GLuint index, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParameterI4uivNV"); } }
        void APIENTRY S_glProgramEnvParametersI4uivNV(GLenum target, GLuint index, GLsizei count, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramEnvParametersI4uivNV"); } }
        void APIENTRY S_glGetProgramLocalParameterIivNV(GLenum target, GLuint index, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramLocalParameterIivNV"); } }
        void APIENTRY S_glGetProgramLocalParameterIuivNV(GLenum target, GLuint index, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramLocalParameterIuivNV"); } }
        void APIENTRY S_glGetProgramEnvParameterIivNV(GLenum target, GLuint index, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramEnvParameterIivNV"); } }
        void APIENTRY S_glGetProgramEnvParameterIuivNV(GLenum target, GLuint index, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramEnvParameterIuivNV"); } }
        void APIENTRY S_glProgramSubroutineParametersuivNV(GLenum target, GLsizei count, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramSubroutineParametersuivNV"); } }
        void APIENTRY S_glGetProgramSubroutineParameteruivNV(GLenum target, GLuint index, GLuint *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramSubroutineParameteruivNV"); } }
        void APIENTRY S_glVertex2hNV(GLhalfNV x, GLhalfNV y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex2hNV"); } }
        void APIENTRY S_glVertex2hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex2hvNV"); } }
        void APIENTRY S_glVertex3hNV(GLhalfNV x, GLhalfNV y, GLhalfNV z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex3hNV"); } }
        void APIENTRY S_glVertex3hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex3hvNV"); } }
        void APIENTRY S_glVertex4hNV(GLhalfNV x, GLhalfNV y, GLhalfNV z, GLhalfNV w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex4hNV"); } }
        void APIENTRY S_glVertex4hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex4hvNV"); } }
        void APIENTRY S_glNormal3hNV(GLhalfNV nx, GLhalfNV ny, GLhalfNV nz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormal3hNV"); } }
        void APIENTRY S_glNormal3hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormal3hvNV"); } }
        void APIENTRY S_glColor3hNV(GLhalfNV red, GLhalfNV green, GLhalfNV blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor3hNV"); } }
        void APIENTRY S_glColor3hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor3hvNV"); } }
        void APIENTRY S_glColor4hNV(GLhalfNV red, GLhalfNV green, GLhalfNV blue, GLhalfNV alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor4hNV"); } }
        void APIENTRY S_glColor4hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor4hvNV"); } }
        void APIENTRY S_glTexCoord1hNV(GLhalfNV s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord1hNV"); } }
        void APIENTRY S_glTexCoord1hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord1hvNV"); } }
        void APIENTRY S_glTexCoord2hNV(GLhalfNV s, GLhalfNV t) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2hNV"); } }
        void APIENTRY S_glTexCoord2hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2hvNV"); } }
        void APIENTRY S_glTexCoord3hNV(GLhalfNV s, GLhalfNV t, GLhalfNV r) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord3hNV"); } }
        void APIENTRY S_glTexCoord3hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord3hvNV"); } }
        void APIENTRY S_glTexCoord4hNV(GLhalfNV s, GLhalfNV t, GLhalfNV r, GLhalfNV q) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord4hNV"); } }
        void APIENTRY S_glTexCoord4hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord4hvNV"); } }
        void APIENTRY S_glMultiTexCoord1hNV(GLenum target, GLhalfNV s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1hNV"); } }
        void APIENTRY S_glMultiTexCoord1hvNV(GLenum target, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1hvNV"); } }
        void APIENTRY S_glMultiTexCoord2hNV(GLenum target, GLhalfNV s, GLhalfNV t) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2hNV"); } }
        void APIENTRY S_glMultiTexCoord2hvNV(GLenum target, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2hvNV"); } }
        void APIENTRY S_glMultiTexCoord3hNV(GLenum target, GLhalfNV s, GLhalfNV t, GLhalfNV r) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3hNV"); } }
        void APIENTRY S_glMultiTexCoord3hvNV(GLenum target, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3hvNV"); } }
        void APIENTRY S_glMultiTexCoord4hNV(GLenum target, GLhalfNV s, GLhalfNV t, GLhalfNV r, GLhalfNV q) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4hNV"); } }
        void APIENTRY S_glMultiTexCoord4hvNV(GLenum target, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4hvNV"); } }
        void APIENTRY S_glVertexAttrib1hNV(GLuint index, GLhalfNV x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1hNV"); } }
        void APIENTRY S_glVertexAttrib1hvNV(GLuint index, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1hvNV"); } }
        void APIENTRY S_glVertexAttrib2hNV(GLuint index, GLhalfNV x, GLhalfNV y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2hNV"); } }
        void APIENTRY S_glVertexAttrib2hvNV(GLuint index, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2hvNV"); } }
        void APIENTRY S_glVertexAttrib3hNV(GLuint index, GLhalfNV x, GLhalfNV y, GLhalfNV z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3hNV"); } }
        void APIENTRY S_glVertexAttrib3hvNV(GLuint index, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3hvNV"); } }
        void APIENTRY S_glVertexAttrib4hNV(GLuint index, GLhalfNV x, GLhalfNV y, GLhalfNV z, GLhalfNV w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4hNV"); } }
        void APIENTRY S_glVertexAttrib4hvNV(GLuint index, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4hvNV"); } }
        void APIENTRY S_glVertexAttribs1hvNV(GLuint index, GLsizei n, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs1hvNV"); } }
        void APIENTRY S_glVertexAttribs2hvNV(GLuint index, GLsizei n, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs2hvNV"); } }
        void APIENTRY S_glVertexAttribs3hvNV(GLuint index, GLsizei n, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs3hvNV"); } }
        void APIENTRY S_glVertexAttribs4hvNV(GLuint index, GLsizei n, const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs4hvNV"); } }
        void APIENTRY S_glFogCoordhNV(GLhalfNV fog) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogCoordhNV"); } }
        void APIENTRY S_glFogCoordhvNV(const GLhalfNV *fog) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogCoordhvNV"); } }
        void APIENTRY S_glSecondaryColor3hNV(GLhalfNV red, GLhalfNV green, GLhalfNV blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3hNV"); } }
        void APIENTRY S_glSecondaryColor3hvNV(const GLhalfNV *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColor3hvNV"); } }
        void APIENTRY S_glVertexWeighthNV(GLhalfNV weight) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexWeighthNV"); } }
        void APIENTRY S_glVertexWeighthvNV(const GLhalfNV *weight) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexWeighthvNV"); } }
        void APIENTRY S_glGetInternalformatSampleivNV(GLenum target, GLenum internalformat, GLsizei samples, GLenum pname, GLsizei count, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetInternalformatSampleivNV"); } }
        void APIENTRY S_glGetMemoryObjectDetachedResourcesuivNV(GLuint memory, GLenum pname, GLint first, GLsizei count, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMemoryObjectDetachedResourcesuivNV"); } }
        void APIENTRY S_glResetMemoryObjectParameterNV(GLuint memory, GLenum pname) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glResetMemoryObjectParameterNV"); } }
        void APIENTRY S_glTexAttachMemoryNV(GLenum target, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexAttachMemoryNV"); } }
        void APIENTRY S_glBufferAttachMemoryNV(GLenum target, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferAttachMemoryNV"); } }
        void APIENTRY S_glTextureAttachMemoryNV(GLuint texture, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureAttachMemoryNV"); } }
        void APIENTRY S_glNamedBufferAttachMemoryNV(GLuint buffer, GLuint memory, GLuint64 offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferAttachMemoryNV"); } }
        void APIENTRY S_glBufferPageCommitmentMemNV(GLenum target, GLintptr offset, GLsizeiptr size, GLuint memory, GLuint64 memOffset, GLboolean commit) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferPageCommitmentMemNV"); } }
        void APIENTRY S_glTexPageCommitmentMemNV(GLenum target, GLint layer, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset, GLboolean commit) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexPageCommitmentMemNV"); } }
        void APIENTRY S_glNamedBufferPageCommitmentMemNV(GLuint buffer, GLintptr offset, GLsizeiptr size, GLuint memory, GLuint64 memOffset, GLboolean commit) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedBufferPageCommitmentMemNV"); } }
        void APIENTRY S_glTexturePageCommitmentMemNV(GLuint texture, GLint layer, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset, GLboolean commit) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexturePageCommitmentMemNV"); } }
        void APIENTRY S_glDrawMeshTasksNV(GLuint first, GLuint count) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawMeshTasksNV"); } }
        void APIENTRY S_glDrawMeshTasksIndirectNV(GLintptr indirect) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawMeshTasksIndirectNV"); } }
        void APIENTRY S_glMultiDrawMeshTasksIndirectNV(GLintptr indirect, GLsizei drawcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawMeshTasksIndirectNV"); } }
        void APIENTRY S_glMultiDrawMeshTasksIndirectCountNV(GLintptr indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiDrawMeshTasksIndirectCountNV"); } }
        void APIENTRY S_glGenOcclusionQueriesNV(GLsizei n, GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenOcclusionQueriesNV"); } }
        void APIENTRY S_glDeleteOcclusionQueriesNV(GLsizei n, const GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteOcclusionQueriesNV"); } }
        GLboolean APIENTRY S_glIsOcclusionQueryNV(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsOcclusionQueryNV"); } return {}; }
        void APIENTRY S_glBeginOcclusionQueryNV(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginOcclusionQueryNV"); } }
        void APIENTRY S_glEndOcclusionQueryNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndOcclusionQueryNV"); } }
        void APIENTRY S_glGetOcclusionQueryivNV(GLuint id, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetOcclusionQueryivNV"); } }
        void APIENTRY S_glGetOcclusionQueryuivNV(GLuint id, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetOcclusionQueryuivNV"); } }
        void APIENTRY S_glProgramBufferParametersfvNV(GLenum target, GLuint bindingIndex, GLuint wordIndex, GLsizei count, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramBufferParametersfvNV"); } }
        void APIENTRY S_glProgramBufferParametersIivNV(GLenum target, GLuint bindingIndex, GLuint wordIndex, GLsizei count, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramBufferParametersIivNV"); } }
        void APIENTRY S_glProgramBufferParametersIuivNV(GLenum target, GLuint bindingIndex, GLuint wordIndex, GLsizei count, const GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramBufferParametersIuivNV"); } }
        GLuint APIENTRY S_glGenPathsNV(GLsizei range) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenPathsNV"); } return {}; }
        void APIENTRY S_glDeletePathsNV(GLuint path, GLsizei range) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeletePathsNV"); } }
        GLboolean APIENTRY S_glIsPathNV(GLuint path) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsPathNV"); } return {}; }
        void APIENTRY S_glPathCommandsNV(GLuint path, GLsizei numCommands, const GLubyte *commands, GLsizei numCoords, GLenum coordType, const void *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathCommandsNV"); } }
        void APIENTRY S_glPathCoordsNV(GLuint path, GLsizei numCoords, GLenum coordType, const void *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathCoordsNV"); } }
        void APIENTRY S_glPathSubCommandsNV(GLuint path, GLsizei commandStart, GLsizei commandsToDelete, GLsizei numCommands, const GLubyte *commands, GLsizei numCoords, GLenum coordType, const void *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathSubCommandsNV"); } }
        void APIENTRY S_glPathSubCoordsNV(GLuint path, GLsizei coordStart, GLsizei numCoords, GLenum coordType, const void *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathSubCoordsNV"); } }
        void APIENTRY S_glPathStringNV(GLuint path, GLenum format, GLsizei length, const void *pathString) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathStringNV"); } }
        void APIENTRY S_glPathGlyphsNV(GLuint firstPathName, GLenum fontTarget, const void *fontName, GLbitfield fontStyle, GLsizei numGlyphs, GLenum type, const void *charcodes, GLenum handleMissingGlyphs, GLuint pathParameterTemplate, GLfloat emScale) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathGlyphsNV"); } }
        void APIENTRY S_glPathGlyphRangeNV(GLuint firstPathName, GLenum fontTarget, const void *fontName, GLbitfield fontStyle, GLuint firstGlyph, GLsizei numGlyphs, GLenum handleMissingGlyphs, GLuint pathParameterTemplate, GLfloat emScale) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathGlyphRangeNV"); } }
        void APIENTRY S_glWeightPathsNV(GLuint resultPath, GLsizei numPaths, const GLuint *paths, const GLfloat *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glWeightPathsNV"); } }
        void APIENTRY S_glCopyPathNV(GLuint resultPath, GLuint srcPath) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyPathNV"); } }
        void APIENTRY S_glInterpolatePathsNV(GLuint resultPath, GLuint pathA, GLuint pathB, GLfloat weight) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInterpolatePathsNV"); } }
        void APIENTRY S_glTransformPathNV(GLuint resultPath, GLuint srcPath, GLenum transformType, const GLfloat *transformValues) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTransformPathNV"); } }
        void APIENTRY S_glPathParameterivNV(GLuint path, GLenum pname, const GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathParameterivNV"); } }
        void APIENTRY S_glPathParameteriNV(GLuint path, GLenum pname, GLint value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathParameteriNV"); } }
        void APIENTRY S_glPathParameterfvNV(GLuint path, GLenum pname, const GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathParameterfvNV"); } }
        void APIENTRY S_glPathParameterfNV(GLuint path, GLenum pname, GLfloat value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathParameterfNV"); } }
        void APIENTRY S_glPathDashArrayNV(GLuint path, GLsizei dashCount, const GLfloat *dashArray) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathDashArrayNV"); } }
        void APIENTRY S_glPathStencilFuncNV(GLenum func, GLint ref, GLuint mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathStencilFuncNV"); } }
        void APIENTRY S_glPathStencilDepthOffsetNV(GLfloat factor, GLfloat units) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathStencilDepthOffsetNV"); } }
        void APIENTRY S_glStencilFillPathNV(GLuint path, GLenum fillMode, GLuint mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilFillPathNV"); } }
        void APIENTRY S_glStencilStrokePathNV(GLuint path, GLint reference, GLuint mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilStrokePathNV"); } }
        void APIENTRY S_glStencilFillPathInstancedNV(GLsizei numPaths, GLenum pathNameType, const void *paths, GLuint pathBase, GLenum fillMode, GLuint mask, GLenum transformType, const GLfloat *transformValues) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilFillPathInstancedNV"); } }
        void APIENTRY S_glStencilStrokePathInstancedNV(GLsizei numPaths, GLenum pathNameType, const void *paths, GLuint pathBase, GLint reference, GLuint mask, GLenum transformType, const GLfloat *transformValues) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilStrokePathInstancedNV"); } }
        void APIENTRY S_glPathCoverDepthFuncNV(GLenum func) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathCoverDepthFuncNV"); } }
        void APIENTRY S_glCoverFillPathNV(GLuint path, GLenum coverMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCoverFillPathNV"); } }
        void APIENTRY S_glCoverStrokePathNV(GLuint path, GLenum coverMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCoverStrokePathNV"); } }
        void APIENTRY S_glCoverFillPathInstancedNV(GLsizei numPaths, GLenum pathNameType, const void *paths, GLuint pathBase, GLenum coverMode, GLenum transformType, const GLfloat *transformValues) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCoverFillPathInstancedNV"); } }
        void APIENTRY S_glCoverStrokePathInstancedNV(GLsizei numPaths, GLenum pathNameType, const void *paths, GLuint pathBase, GLenum coverMode, GLenum transformType, const GLfloat *transformValues) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCoverStrokePathInstancedNV"); } }
        void APIENTRY S_glGetPathParameterivNV(GLuint path, GLenum pname, GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathParameterivNV"); } }
        void APIENTRY S_glGetPathParameterfvNV(GLuint path, GLenum pname, GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathParameterfvNV"); } }
        void APIENTRY S_glGetPathCommandsNV(GLuint path, GLubyte *commands) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathCommandsNV"); } }
        void APIENTRY S_glGetPathCoordsNV(GLuint path, GLfloat *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathCoordsNV"); } }
        void APIENTRY S_glGetPathDashArrayNV(GLuint path, GLfloat *dashArray) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathDashArrayNV"); } }
        void APIENTRY S_glGetPathMetricsNV(GLbitfield metricQueryMask, GLsizei numPaths, GLenum pathNameType, const void *paths, GLuint pathBase, GLsizei stride, GLfloat *metrics) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathMetricsNV"); } }
        void APIENTRY S_glGetPathMetricRangeNV(GLbitfield metricQueryMask, GLuint firstPathName, GLsizei numPaths, GLsizei stride, GLfloat *metrics) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathMetricRangeNV"); } }
        void APIENTRY S_glGetPathSpacingNV(GLenum pathListMode, GLsizei numPaths, GLenum pathNameType, const void *paths, GLuint pathBase, GLfloat advanceScale, GLfloat kerningScale, GLenum transformType, GLfloat *returnedSpacing) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathSpacingNV"); } }
        GLboolean APIENTRY S_glIsPointInFillPathNV(GLuint path, GLuint mask, GLfloat x, GLfloat y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsPointInFillPathNV"); } return {}; }
        GLboolean APIENTRY S_glIsPointInStrokePathNV(GLuint path, GLfloat x, GLfloat y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsPointInStrokePathNV"); } return {}; }
        GLfloat APIENTRY S_glGetPathLengthNV(GLuint path, GLsizei startSegment, GLsizei numSegments) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathLengthNV"); } return {}; }
        GLboolean APIENTRY S_glPointAlongPathNV(GLuint path, GLsizei startSegment, GLsizei numSegments, GLfloat distance, GLfloat *x, GLfloat *y, GLfloat *tangentX, GLfloat *tangentY) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointAlongPathNV"); } return {}; }
        void APIENTRY S_glMatrixLoad3x2fNV(GLenum matrixMode, const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixLoad3x2fNV"); } }
        void APIENTRY S_glMatrixLoad3x3fNV(GLenum matrixMode, const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixLoad3x3fNV"); } }
        void APIENTRY S_glMatrixLoadTranspose3x3fNV(GLenum matrixMode, const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixLoadTranspose3x3fNV"); } }
        void APIENTRY S_glMatrixMult3x2fNV(GLenum matrixMode, const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixMult3x2fNV"); } }
        void APIENTRY S_glMatrixMult3x3fNV(GLenum matrixMode, const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixMult3x3fNV"); } }
        void APIENTRY S_glMatrixMultTranspose3x3fNV(GLenum matrixMode, const GLfloat *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMatrixMultTranspose3x3fNV"); } }
        void APIENTRY S_glStencilThenCoverFillPathNV(GLuint path, GLenum fillMode, GLuint mask, GLenum coverMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilThenCoverFillPathNV"); } }
        void APIENTRY S_glStencilThenCoverStrokePathNV(GLuint path, GLint reference, GLuint mask, GLenum coverMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilThenCoverStrokePathNV"); } }
        void APIENTRY S_glStencilThenCoverFillPathInstancedNV(GLsizei numPaths, GLenum pathNameType, const void *paths, GLuint pathBase, GLenum fillMode, GLuint mask, GLenum coverMode, GLenum transformType, const GLfloat *transformValues) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilThenCoverFillPathInstancedNV"); } }
        void APIENTRY S_glStencilThenCoverStrokePathInstancedNV(GLsizei numPaths, GLenum pathNameType, const void *paths, GLuint pathBase, GLint reference, GLuint mask, GLenum coverMode, GLenum transformType, const GLfloat *transformValues) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStencilThenCoverStrokePathInstancedNV"); } }
        GLenum APIENTRY S_glPathGlyphIndexRangeNV(GLenum fontTarget, const void *fontName, GLbitfield fontStyle, GLuint pathParameterTemplate, GLfloat emScale, GLuint *baseAndCount) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathGlyphIndexRangeNV"); } return {}; }
        GLenum APIENTRY S_glPathGlyphIndexArrayNV(GLuint firstPathName, GLenum fontTarget, const void *fontName, GLbitfield fontStyle, GLuint firstGlyphIndex, GLsizei numGlyphs, GLuint pathParameterTemplate, GLfloat emScale) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathGlyphIndexArrayNV"); } return {}; }
        GLenum APIENTRY S_glPathMemoryGlyphIndexArrayNV(GLuint firstPathName, GLenum fontTarget, GLsizeiptr fontSize, const void *fontData, GLsizei faceIndex, GLuint firstGlyphIndex, GLsizei numGlyphs, GLuint pathParameterTemplate, GLfloat emScale) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathMemoryGlyphIndexArrayNV"); } return {}; }
        void APIENTRY S_glProgramPathFragmentInputGenNV(GLuint program, GLint location, GLenum genMode, GLint components, const GLfloat *coeffs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramPathFragmentInputGenNV"); } }
        void APIENTRY S_glGetProgramResourcefvNV(GLuint program, GLenum programInterface, GLuint index, GLsizei propCount, const GLenum *props, GLsizei count, GLsizei *length, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramResourcefvNV"); } }
        void APIENTRY S_glPathColorGenNV(GLenum color, GLenum genMode, GLenum colorFormat, const GLfloat *coeffs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathColorGenNV"); } }
        void APIENTRY S_glPathTexGenNV(GLenum texCoordSet, GLenum genMode, GLint components, const GLfloat *coeffs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathTexGenNV"); } }
        void APIENTRY S_glPathFogGenNV(GLenum genMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPathFogGenNV"); } }
        void APIENTRY S_glGetPathColorGenivNV(GLenum color, GLenum pname, GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathColorGenivNV"); } }
        void APIENTRY S_glGetPathColorGenfvNV(GLenum color, GLenum pname, GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathColorGenfvNV"); } }
        void APIENTRY S_glGetPathTexGenivNV(GLenum texCoordSet, GLenum pname, GLint *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathTexGenivNV"); } }
        void APIENTRY S_glGetPathTexGenfvNV(GLenum texCoordSet, GLenum pname, GLfloat *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPathTexGenfvNV"); } }
        void APIENTRY S_glPixelDataRangeNV(GLenum target, GLsizei length, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelDataRangeNV"); } }
        void APIENTRY S_glFlushPixelDataRangeNV(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFlushPixelDataRangeNV"); } }
        void APIENTRY S_glPointParameteriNV(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameteriNV"); } }
        void APIENTRY S_glPointParameterivNV(GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterivNV"); } }
        void APIENTRY S_glPresentFrameKeyedNV(GLuint video_slot, GLuint64EXT minPresentTime, GLuint beginPresentTimeId, GLuint presentDurationId, GLenum type, GLenum target0, GLuint fill0, GLuint key0, GLenum target1, GLuint fill1, GLuint key1) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPresentFrameKeyedNV"); } }
        void APIENTRY S_glPresentFrameDualFillNV(GLuint video_slot, GLuint64EXT minPresentTime, GLuint beginPresentTimeId, GLuint presentDurationId, GLenum type, GLenum target0, GLuint fill0, GLenum target1, GLuint fill1, GLenum target2, GLuint fill2, GLenum target3, GLuint fill3) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPresentFrameDualFillNV"); } }
        void APIENTRY S_glGetVideoivNV(GLuint video_slot, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVideoivNV"); } }
        void APIENTRY S_glGetVideouivNV(GLuint video_slot, GLenum pname, GLuint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVideouivNV"); } }
        void APIENTRY S_glGetVideoi64vNV(GLuint video_slot, GLenum pname, GLint64EXT *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVideoi64vNV"); } }
        void APIENTRY S_glGetVideoui64vNV(GLuint video_slot, GLenum pname, GLuint64EXT *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVideoui64vNV"); } }
        void APIENTRY S_glPrimitiveRestartNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPrimitiveRestartNV"); } }
        void APIENTRY S_glPrimitiveRestartIndexNV(GLuint index) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPrimitiveRestartIndexNV"); } }
        GLint APIENTRY S_glQueryResourceNV(GLenum queryType, GLint tagId, GLuint count, GLint *buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glQueryResourceNV"); } return {}; }
        void APIENTRY S_glGenQueryResourceTagNV(GLsizei n, GLint *tagIds) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenQueryResourceTagNV"); } }
        void APIENTRY S_glDeleteQueryResourceTagNV(GLsizei n, const GLint *tagIds) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteQueryResourceTagNV"); } }
        void APIENTRY S_glQueryResourceTagNV(GLint tagId, const GLchar *tagString) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glQueryResourceTagNV"); } }
        void APIENTRY S_glCombinerParameterfvNV(GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCombinerParameterfvNV"); } }
        void APIENTRY S_glCombinerParameterfNV(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCombinerParameterfNV"); } }
        void APIENTRY S_glCombinerParameterivNV(GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCombinerParameterivNV"); } }
        void APIENTRY S_glCombinerParameteriNV(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCombinerParameteriNV"); } }
        void APIENTRY S_glCombinerInputNV(GLenum stage, GLenum portion, GLenum variable, GLenum input, GLenum mapping, GLenum componentUsage) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCombinerInputNV"); } }
        void APIENTRY S_glCombinerOutputNV(GLenum stage, GLenum portion, GLenum abOutput, GLenum cdOutput, GLenum sumOutput, GLenum scale, GLenum bias, GLboolean abDotProduct, GLboolean cdDotProduct, GLboolean muxSum) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCombinerOutputNV"); } }
        void APIENTRY S_glFinalCombinerInputNV(GLenum variable, GLenum input, GLenum mapping, GLenum componentUsage) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFinalCombinerInputNV"); } }
        void APIENTRY S_glGetCombinerInputParameterfvNV(GLenum stage, GLenum portion, GLenum variable, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCombinerInputParameterfvNV"); } }
        void APIENTRY S_glGetCombinerInputParameterivNV(GLenum stage, GLenum portion, GLenum variable, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCombinerInputParameterivNV"); } }
        void APIENTRY S_glGetCombinerOutputParameterfvNV(GLenum stage, GLenum portion, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCombinerOutputParameterfvNV"); } }
        void APIENTRY S_glGetCombinerOutputParameterivNV(GLenum stage, GLenum portion, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCombinerOutputParameterivNV"); } }
        void APIENTRY S_glGetFinalCombinerInputParameterfvNV(GLenum variable, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFinalCombinerInputParameterfvNV"); } }
        void APIENTRY S_glGetFinalCombinerInputParameterivNV(GLenum variable, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFinalCombinerInputParameterivNV"); } }
        void APIENTRY S_glCombinerStageParameterfvNV(GLenum stage, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCombinerStageParameterfvNV"); } }
        void APIENTRY S_glGetCombinerStageParameterfvNV(GLenum stage, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetCombinerStageParameterfvNV"); } }
        void APIENTRY S_glFramebufferSampleLocationsfvNV(GLenum target, GLuint start, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferSampleLocationsfvNV"); } }
        void APIENTRY S_glNamedFramebufferSampleLocationsfvNV(GLuint framebuffer, GLuint start, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferSampleLocationsfvNV"); } }
        void APIENTRY S_glResolveDepthValuesNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glResolveDepthValuesNV"); } }
        void APIENTRY S_glScissorExclusiveNV(GLint x, GLint y, GLsizei width, GLsizei height) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glScissorExclusiveNV"); } }
        void APIENTRY S_glScissorExclusiveArrayvNV(GLuint first, GLsizei count, const GLint *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glScissorExclusiveArrayvNV"); } }
        void APIENTRY S_glMakeBufferResidentNV(GLenum target, GLenum access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeBufferResidentNV"); } }
        void APIENTRY S_glMakeBufferNonResidentNV(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeBufferNonResidentNV"); } }
        GLboolean APIENTRY S_glIsBufferResidentNV(GLenum target) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsBufferResidentNV"); } return {}; }
        void APIENTRY S_glMakeNamedBufferResidentNV(GLuint buffer, GLenum access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeNamedBufferResidentNV"); } }
        void APIENTRY S_glMakeNamedBufferNonResidentNV(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMakeNamedBufferNonResidentNV"); } }
        GLboolean APIENTRY S_glIsNamedBufferResidentNV(GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsNamedBufferResidentNV"); } return {}; }
        void APIENTRY S_glGetBufferParameterui64vNV(GLenum target, GLenum pname, GLuint64EXT *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetBufferParameterui64vNV"); } }
        void APIENTRY S_glGetNamedBufferParameterui64vNV(GLuint buffer, GLenum pname, GLuint64EXT *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetNamedBufferParameterui64vNV"); } }
        void APIENTRY S_glGetIntegerui64vNV(GLenum value, GLuint64EXT *result) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetIntegerui64vNV"); } }
        void APIENTRY S_glUniformui64NV(GLint location, GLuint64EXT value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformui64NV"); } }
        void APIENTRY S_glUniformui64vNV(GLint location, GLsizei count, const GLuint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glUniformui64vNV"); } }
        void APIENTRY S_glProgramUniformui64NV(GLuint program, GLint location, GLuint64EXT value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformui64NV"); } }
        void APIENTRY S_glProgramUniformui64vNV(GLuint program, GLint location, GLsizei count, const GLuint64EXT *value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramUniformui64vNV"); } }
        void APIENTRY S_glBindShadingRateImageNV(GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindShadingRateImageNV"); } }
        void APIENTRY S_glGetShadingRateImagePaletteNV(GLuint viewport, GLuint entry, GLenum *rate) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetShadingRateImagePaletteNV"); } }
        void APIENTRY S_glGetShadingRateSampleLocationivNV(GLenum rate, GLuint samples, GLuint index, GLint *location) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetShadingRateSampleLocationivNV"); } }
        void APIENTRY S_glShadingRateImageBarrierNV(GLboolean synchronize) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShadingRateImageBarrierNV"); } }
        void APIENTRY S_glShadingRateImagePaletteNV(GLuint viewport, GLuint first, GLsizei count, const GLenum *rates) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShadingRateImagePaletteNV"); } }
        void APIENTRY S_glShadingRateSampleOrderNV(GLenum order) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShadingRateSampleOrderNV"); } }
        void APIENTRY S_glShadingRateSampleOrderCustomNV(GLenum rate, GLuint samples, const GLint *locations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glShadingRateSampleOrderCustomNV"); } }
        void APIENTRY S_glTextureBarrierNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureBarrierNV"); } }
        void APIENTRY S_glTexImage2DMultisampleCoverageNV(GLenum target, GLsizei coverageSamples, GLsizei colorSamples, GLint internalFormat, GLsizei width, GLsizei height, GLboolean fixedSampleLocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexImage2DMultisampleCoverageNV"); } }
        void APIENTRY S_glTexImage3DMultisampleCoverageNV(GLenum target, GLsizei coverageSamples, GLsizei colorSamples, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedSampleLocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexImage3DMultisampleCoverageNV"); } }
        void APIENTRY S_glTextureImage2DMultisampleNV(GLuint texture, GLenum target, GLsizei samples, GLint internalFormat, GLsizei width, GLsizei height, GLboolean fixedSampleLocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureImage2DMultisampleNV"); } }
        void APIENTRY S_glTextureImage3DMultisampleNV(GLuint texture, GLenum target, GLsizei samples, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedSampleLocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureImage3DMultisampleNV"); } }
        void APIENTRY S_glTextureImage2DMultisampleCoverageNV(GLuint texture, GLenum target, GLsizei coverageSamples, GLsizei colorSamples, GLint internalFormat, GLsizei width, GLsizei height, GLboolean fixedSampleLocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureImage2DMultisampleCoverageNV"); } }
        void APIENTRY S_glTextureImage3DMultisampleCoverageNV(GLuint texture, GLenum target, GLsizei coverageSamples, GLsizei colorSamples, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedSampleLocations) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureImage3DMultisampleCoverageNV"); } }
        void APIENTRY S_glCreateSemaphoresNV(GLsizei n, GLuint *semaphores) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCreateSemaphoresNV"); } }
        void APIENTRY S_glSemaphoreParameterivNV(GLuint semaphore, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSemaphoreParameterivNV"); } }
        void APIENTRY S_glGetSemaphoreParameterivNV(GLuint semaphore, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSemaphoreParameterivNV"); } }
        void APIENTRY S_glBeginTransformFeedbackNV(GLenum primitiveMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginTransformFeedbackNV"); } }
        void APIENTRY S_glEndTransformFeedbackNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndTransformFeedbackNV"); } }
        void APIENTRY S_glTransformFeedbackAttribsNV(GLsizei count, const GLint *attribs, GLenum bufferMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTransformFeedbackAttribsNV"); } }
        void APIENTRY S_glBindBufferRangeNV(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBufferRangeNV"); } }
        void APIENTRY S_glBindBufferOffsetNV(GLenum target, GLuint index, GLuint buffer, GLintptr offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBufferOffsetNV"); } }
        void APIENTRY S_glBindBufferBaseNV(GLenum target, GLuint index, GLuint buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindBufferBaseNV"); } }
        void APIENTRY S_glTransformFeedbackVaryingsNV(GLuint program, GLsizei count, const GLint *locations, GLenum bufferMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTransformFeedbackVaryingsNV"); } }
        void APIENTRY S_glActiveVaryingNV(GLuint program, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glActiveVaryingNV"); } }
        GLint APIENTRY S_glGetVaryingLocationNV(GLuint program, const GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVaryingLocationNV"); } return {}; }
        void APIENTRY S_glGetActiveVaryingNV(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLsizei *size, GLenum *type, GLchar *name) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetActiveVaryingNV"); } }
        void APIENTRY S_glGetTransformFeedbackVaryingNV(GLuint program, GLuint index, GLint *location) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTransformFeedbackVaryingNV"); } }
        void APIENTRY S_glTransformFeedbackStreamAttribsNV(GLsizei count, const GLint *attribs, GLsizei nbuffers, const GLint *bufstreams, GLenum bufferMode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTransformFeedbackStreamAttribsNV"); } }
        void APIENTRY S_glBindTransformFeedbackNV(GLenum target, GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindTransformFeedbackNV"); } }
        void APIENTRY S_glDeleteTransformFeedbacksNV(GLsizei n, const GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteTransformFeedbacksNV"); } }
        void APIENTRY S_glGenTransformFeedbacksNV(GLsizei n, GLuint *ids) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenTransformFeedbacksNV"); } }
        GLboolean APIENTRY S_glIsTransformFeedbackNV(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsTransformFeedbackNV"); } return {}; }
        void APIENTRY S_glPauseTransformFeedbackNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPauseTransformFeedbackNV"); } }
        void APIENTRY S_glResumeTransformFeedbackNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glResumeTransformFeedbackNV"); } }
        void APIENTRY S_glDrawTransformFeedbackNV(GLenum mode, GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawTransformFeedbackNV"); } }
        void APIENTRY S_glVDPAUInitNV(const void *vdpDevice, const void *getProcAddress) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAUInitNV"); } }
        void APIENTRY S_glVDPAUFiniNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAUFiniNV"); } }
        GLvdpauSurfaceNV APIENTRY S_glVDPAURegisterVideoSurfaceNV(const void *vdpSurface, GLenum target, GLsizei numTextureNames, const GLuint *textureNames) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAURegisterVideoSurfaceNV"); } return {}; }
        GLvdpauSurfaceNV APIENTRY S_glVDPAURegisterOutputSurfaceNV(const void *vdpSurface, GLenum target, GLsizei numTextureNames, const GLuint *textureNames) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAURegisterOutputSurfaceNV"); } return {}; }
        GLboolean APIENTRY S_glVDPAUIsSurfaceNV(GLvdpauSurfaceNV surface) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAUIsSurfaceNV"); } return {}; }
        void APIENTRY S_glVDPAUUnregisterSurfaceNV(GLvdpauSurfaceNV surface) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAUUnregisterSurfaceNV"); } }
        void APIENTRY S_glVDPAUGetSurfaceivNV(GLvdpauSurfaceNV surface, GLenum pname, GLsizei count, GLsizei *length, GLint *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAUGetSurfaceivNV"); } }
        void APIENTRY S_glVDPAUSurfaceAccessNV(GLvdpauSurfaceNV surface, GLenum access) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAUSurfaceAccessNV"); } }
        void APIENTRY S_glVDPAUMapSurfacesNV(GLsizei numSurfaces, const GLvdpauSurfaceNV *surfaces) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAUMapSurfacesNV"); } }
        void APIENTRY S_glVDPAUUnmapSurfacesNV(GLsizei numSurface, const GLvdpauSurfaceNV *surfaces) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAUUnmapSurfacesNV"); } }
        GLvdpauSurfaceNV APIENTRY S_glVDPAURegisterVideoSurfaceWithPictureStructureNV(const void *vdpSurface, GLenum target, GLsizei numTextureNames, const GLuint *textureNames, GLboolean isFrameStructure) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVDPAURegisterVideoSurfaceWithPictureStructureNV"); } return {}; }
        void APIENTRY S_glFlushVertexArrayRangeNV(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFlushVertexArrayRangeNV"); } }
        void APIENTRY S_glVertexArrayRangeNV(GLsizei length, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexArrayRangeNV"); } }
        void APIENTRY S_glVertexAttribL1i64NV(GLuint index, GLint64EXT x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL1i64NV"); } }
        void APIENTRY S_glVertexAttribL2i64NV(GLuint index, GLint64EXT x, GLint64EXT y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL2i64NV"); } }
        void APIENTRY S_glVertexAttribL3i64NV(GLuint index, GLint64EXT x, GLint64EXT y, GLint64EXT z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL3i64NV"); } }
        void APIENTRY S_glVertexAttribL4i64NV(GLuint index, GLint64EXT x, GLint64EXT y, GLint64EXT z, GLint64EXT w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL4i64NV"); } }
        void APIENTRY S_glVertexAttribL1i64vNV(GLuint index, const GLint64EXT *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL1i64vNV"); } }
        void APIENTRY S_glVertexAttribL2i64vNV(GLuint index, const GLint64EXT *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL2i64vNV"); } }
        void APIENTRY S_glVertexAttribL3i64vNV(GLuint index, const GLint64EXT *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL3i64vNV"); } }
        void APIENTRY S_glVertexAttribL4i64vNV(GLuint index, const GLint64EXT *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL4i64vNV"); } }
        void APIENTRY S_glVertexAttribL1ui64NV(GLuint index, GLuint64EXT x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL1ui64NV"); } }
        void APIENTRY S_glVertexAttribL2ui64NV(GLuint index, GLuint64EXT x, GLuint64EXT y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL2ui64NV"); } }
        void APIENTRY S_glVertexAttribL3ui64NV(GLuint index, GLuint64EXT x, GLuint64EXT y, GLuint64EXT z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL3ui64NV"); } }
        void APIENTRY S_glVertexAttribL4ui64NV(GLuint index, GLuint64EXT x, GLuint64EXT y, GLuint64EXT z, GLuint64EXT w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL4ui64NV"); } }
        void APIENTRY S_glVertexAttribL1ui64vNV(GLuint index, const GLuint64EXT *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL1ui64vNV"); } }
        void APIENTRY S_glVertexAttribL2ui64vNV(GLuint index, const GLuint64EXT *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL2ui64vNV"); } }
        void APIENTRY S_glVertexAttribL3ui64vNV(GLuint index, const GLuint64EXT *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL3ui64vNV"); } }
        void APIENTRY S_glVertexAttribL4ui64vNV(GLuint index, const GLuint64EXT *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribL4ui64vNV"); } }
        void APIENTRY S_glGetVertexAttribLi64vNV(GLuint index, GLenum pname, GLint64EXT *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribLi64vNV"); } }
        void APIENTRY S_glGetVertexAttribLui64vNV(GLuint index, GLenum pname, GLuint64EXT *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribLui64vNV"); } }
        void APIENTRY S_glVertexAttribLFormatNV(GLuint index, GLint size, GLenum type, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribLFormatNV"); } }
        void APIENTRY S_glBufferAddressRangeNV(GLenum pname, GLuint index, GLuint64EXT address, GLsizeiptr length) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBufferAddressRangeNV"); } }
        void APIENTRY S_glVertexFormatNV(GLint size, GLenum type, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexFormatNV"); } }
        void APIENTRY S_glNormalFormatNV(GLenum type, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormalFormatNV"); } }
        void APIENTRY S_glColorFormatNV(GLint size, GLenum type, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorFormatNV"); } }
        void APIENTRY S_glIndexFormatNV(GLenum type, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIndexFormatNV"); } }
        void APIENTRY S_glTexCoordFormatNV(GLint size, GLenum type, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoordFormatNV"); } }
        void APIENTRY S_glEdgeFlagFormatNV(GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEdgeFlagFormatNV"); } }
        void APIENTRY S_glSecondaryColorFormatNV(GLint size, GLenum type, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSecondaryColorFormatNV"); } }
        void APIENTRY S_glFogCoordFormatNV(GLenum type, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogCoordFormatNV"); } }
        void APIENTRY S_glVertexAttribFormatNV(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribFormatNV"); } }
        void APIENTRY S_glVertexAttribIFormatNV(GLuint index, GLint size, GLenum type, GLsizei stride) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribIFormatNV"); } }
        void APIENTRY S_glGetIntegerui64i_vNV(GLenum value, GLuint index, GLuint64EXT *result) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetIntegerui64i_vNV"); } }
        GLboolean APIENTRY S_glAreProgramsResidentNV(GLsizei n, const GLuint *programs, GLboolean *residences) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAreProgramsResidentNV"); } return {}; }
        void APIENTRY S_glBindProgramNV(GLenum target, GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindProgramNV"); } }
        void APIENTRY S_glDeleteProgramsNV(GLsizei n, const GLuint *programs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteProgramsNV"); } }
        void APIENTRY S_glExecuteProgramNV(GLenum target, GLuint id, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glExecuteProgramNV"); } }
        void APIENTRY S_glGenProgramsNV(GLsizei n, GLuint *programs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenProgramsNV"); } }
        void APIENTRY S_glGetProgramParameterdvNV(GLenum target, GLuint index, GLenum pname, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramParameterdvNV"); } }
        void APIENTRY S_glGetProgramParameterfvNV(GLenum target, GLuint index, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramParameterfvNV"); } }
        void APIENTRY S_glGetProgramivNV(GLuint id, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramivNV"); } }
        void APIENTRY S_glGetProgramStringNV(GLuint id, GLenum pname, GLubyte *program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetProgramStringNV"); } }
        void APIENTRY S_glGetTrackMatrixivNV(GLenum target, GLuint address, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTrackMatrixivNV"); } }
        void APIENTRY S_glGetVertexAttribdvNV(GLuint index, GLenum pname, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribdvNV"); } }
        void APIENTRY S_glGetVertexAttribfvNV(GLuint index, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribfvNV"); } }
        void APIENTRY S_glGetVertexAttribivNV(GLuint index, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribivNV"); } }
        void APIENTRY S_glGetVertexAttribPointervNV(GLuint index, GLenum pname, void **pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVertexAttribPointervNV"); } }
        GLboolean APIENTRY S_glIsProgramNV(GLuint id) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsProgramNV"); } return {}; }
        void APIENTRY S_glLoadProgramNV(GLenum target, GLuint id, GLsizei len, const GLubyte *program) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLoadProgramNV"); } }
        void APIENTRY S_glProgramParameter4dNV(GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramParameter4dNV"); } }
        void APIENTRY S_glProgramParameter4dvNV(GLenum target, GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramParameter4dvNV"); } }
        void APIENTRY S_glProgramParameter4fNV(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramParameter4fNV"); } }
        void APIENTRY S_glProgramParameter4fvNV(GLenum target, GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramParameter4fvNV"); } }
        void APIENTRY S_glProgramParameters4dvNV(GLenum target, GLuint index, GLsizei count, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramParameters4dvNV"); } }
        void APIENTRY S_glProgramParameters4fvNV(GLenum target, GLuint index, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glProgramParameters4fvNV"); } }
        void APIENTRY S_glRequestResidentProgramsNV(GLsizei n, const GLuint *programs) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRequestResidentProgramsNV"); } }
        void APIENTRY S_glTrackMatrixNV(GLenum target, GLuint address, GLenum matrix, GLenum transform) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTrackMatrixNV"); } }
        void APIENTRY S_glVertexAttribPointerNV(GLuint index, GLint fsize, GLenum type, GLsizei stride, const void *pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribPointerNV"); } }
        void APIENTRY S_glVertexAttrib1dNV(GLuint index, GLdouble x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1dNV"); } }
        void APIENTRY S_glVertexAttrib1dvNV(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1dvNV"); } }
        void APIENTRY S_glVertexAttrib1fNV(GLuint index, GLfloat x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1fNV"); } }
        void APIENTRY S_glVertexAttrib1fvNV(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1fvNV"); } }
        void APIENTRY S_glVertexAttrib1sNV(GLuint index, GLshort x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1sNV"); } }
        void APIENTRY S_glVertexAttrib1svNV(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib1svNV"); } }
        void APIENTRY S_glVertexAttrib2dNV(GLuint index, GLdouble x, GLdouble y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2dNV"); } }
        void APIENTRY S_glVertexAttrib2dvNV(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2dvNV"); } }
        void APIENTRY S_glVertexAttrib2fNV(GLuint index, GLfloat x, GLfloat y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2fNV"); } }
        void APIENTRY S_glVertexAttrib2fvNV(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2fvNV"); } }
        void APIENTRY S_glVertexAttrib2sNV(GLuint index, GLshort x, GLshort y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2sNV"); } }
        void APIENTRY S_glVertexAttrib2svNV(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib2svNV"); } }
        void APIENTRY S_glVertexAttrib3dNV(GLuint index, GLdouble x, GLdouble y, GLdouble z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3dNV"); } }
        void APIENTRY S_glVertexAttrib3dvNV(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3dvNV"); } }
        void APIENTRY S_glVertexAttrib3fNV(GLuint index, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3fNV"); } }
        void APIENTRY S_glVertexAttrib3fvNV(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3fvNV"); } }
        void APIENTRY S_glVertexAttrib3sNV(GLuint index, GLshort x, GLshort y, GLshort z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3sNV"); } }
        void APIENTRY S_glVertexAttrib3svNV(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib3svNV"); } }
        void APIENTRY S_glVertexAttrib4dNV(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4dNV"); } }
        void APIENTRY S_glVertexAttrib4dvNV(GLuint index, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4dvNV"); } }
        void APIENTRY S_glVertexAttrib4fNV(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4fNV"); } }
        void APIENTRY S_glVertexAttrib4fvNV(GLuint index, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4fvNV"); } }
        void APIENTRY S_glVertexAttrib4sNV(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4sNV"); } }
        void APIENTRY S_glVertexAttrib4svNV(GLuint index, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4svNV"); } }
        void APIENTRY S_glVertexAttrib4ubNV(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4ubNV"); } }
        void APIENTRY S_glVertexAttrib4ubvNV(GLuint index, const GLubyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttrib4ubvNV"); } }
        void APIENTRY S_glVertexAttribs1dvNV(GLuint index, GLsizei count, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs1dvNV"); } }
        void APIENTRY S_glVertexAttribs1fvNV(GLuint index, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs1fvNV"); } }
        void APIENTRY S_glVertexAttribs1svNV(GLuint index, GLsizei count, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs1svNV"); } }
        void APIENTRY S_glVertexAttribs2dvNV(GLuint index, GLsizei count, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs2dvNV"); } }
        void APIENTRY S_glVertexAttribs2fvNV(GLuint index, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs2fvNV"); } }
        void APIENTRY S_glVertexAttribs2svNV(GLuint index, GLsizei count, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs2svNV"); } }
        void APIENTRY S_glVertexAttribs3dvNV(GLuint index, GLsizei count, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs3dvNV"); } }
        void APIENTRY S_glVertexAttribs3fvNV(GLuint index, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs3fvNV"); } }
        void APIENTRY S_glVertexAttribs3svNV(GLuint index, GLsizei count, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs3svNV"); } }
        void APIENTRY S_glVertexAttribs4dvNV(GLuint index, GLsizei count, const GLdouble *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs4dvNV"); } }
        void APIENTRY S_glVertexAttribs4fvNV(GLuint index, GLsizei count, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs4fvNV"); } }
        void APIENTRY S_glVertexAttribs4svNV(GLuint index, GLsizei count, const GLshort *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs4svNV"); } }
        void APIENTRY S_glVertexAttribs4ubvNV(GLuint index, GLsizei count, const GLubyte *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertexAttribs4ubvNV"); } }
        void APIENTRY S_glBeginVideoCaptureNV(GLuint video_capture_slot) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBeginVideoCaptureNV"); } }
        void APIENTRY S_glBindVideoCaptureStreamBufferNV(GLuint video_capture_slot, GLuint stream, GLenum frame_region, GLintptrARB offset) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindVideoCaptureStreamBufferNV"); } }
        void APIENTRY S_glBindVideoCaptureStreamTextureNV(GLuint video_capture_slot, GLuint stream, GLenum frame_region, GLenum target, GLuint texture) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBindVideoCaptureStreamTextureNV"); } }
        void APIENTRY S_glEndVideoCaptureNV(GLuint video_capture_slot) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEndVideoCaptureNV"); } }
        void APIENTRY S_glGetVideoCaptureivNV(GLuint video_capture_slot, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVideoCaptureivNV"); } }
        void APIENTRY S_glGetVideoCaptureStreamivNV(GLuint video_capture_slot, GLuint stream, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVideoCaptureStreamivNV"); } }
        void APIENTRY S_glGetVideoCaptureStreamfvNV(GLuint video_capture_slot, GLuint stream, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVideoCaptureStreamfvNV"); } }
        void APIENTRY S_glGetVideoCaptureStreamdvNV(GLuint video_capture_slot, GLuint stream, GLenum pname, GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetVideoCaptureStreamdvNV"); } }
        GLenum APIENTRY S_glVideoCaptureNV(GLuint video_capture_slot, GLuint *sequence_num, GLuint64EXT *capture_time) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVideoCaptureNV"); } return {}; }
        void APIENTRY S_glVideoCaptureStreamParameterivNV(GLuint video_capture_slot, GLuint stream, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVideoCaptureStreamParameterivNV"); } }
        void APIENTRY S_glVideoCaptureStreamParameterfvNV(GLuint video_capture_slot, GLuint stream, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVideoCaptureStreamParameterfvNV"); } }
        void APIENTRY S_glVideoCaptureStreamParameterdvNV(GLuint video_capture_slot, GLuint stream, GLenum pname, const GLdouble *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVideoCaptureStreamParameterdvNV"); } }
        void APIENTRY S_glViewportSwizzleNV(GLuint index, GLenum swizzlex, GLenum swizzley, GLenum swizzlez, GLenum swizzlew) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glViewportSwizzleNV"); } }
        void APIENTRY S_glMultiTexCoord1bOES(GLenum texture, GLbyte s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1bOES"); } }
        void APIENTRY S_glMultiTexCoord1bvOES(GLenum texture, const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1bvOES"); } }
        void APIENTRY S_glMultiTexCoord2bOES(GLenum texture, GLbyte s, GLbyte t) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2bOES"); } }
        void APIENTRY S_glMultiTexCoord2bvOES(GLenum texture, const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2bvOES"); } }
        void APIENTRY S_glMultiTexCoord3bOES(GLenum texture, GLbyte s, GLbyte t, GLbyte r) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3bOES"); } }
        void APIENTRY S_glMultiTexCoord3bvOES(GLenum texture, const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3bvOES"); } }
        void APIENTRY S_glMultiTexCoord4bOES(GLenum texture, GLbyte s, GLbyte t, GLbyte r, GLbyte q) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4bOES"); } }
        void APIENTRY S_glMultiTexCoord4bvOES(GLenum texture, const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4bvOES"); } }
        void APIENTRY S_glTexCoord1bOES(GLbyte s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord1bOES"); } }
        void APIENTRY S_glTexCoord1bvOES(const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord1bvOES"); } }
        void APIENTRY S_glTexCoord2bOES(GLbyte s, GLbyte t) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2bOES"); } }
        void APIENTRY S_glTexCoord2bvOES(const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2bvOES"); } }
        void APIENTRY S_glTexCoord3bOES(GLbyte s, GLbyte t, GLbyte r) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord3bOES"); } }
        void APIENTRY S_glTexCoord3bvOES(const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord3bvOES"); } }
        void APIENTRY S_glTexCoord4bOES(GLbyte s, GLbyte t, GLbyte r, GLbyte q) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord4bOES"); } }
        void APIENTRY S_glTexCoord4bvOES(const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord4bvOES"); } }
        void APIENTRY S_glVertex2bOES(GLbyte x, GLbyte y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex2bOES"); } }
        void APIENTRY S_glVertex2bvOES(const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex2bvOES"); } }
        void APIENTRY S_glVertex3bOES(GLbyte x, GLbyte y, GLbyte z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex3bOES"); } }
        void APIENTRY S_glVertex3bvOES(const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex3bvOES"); } }
        void APIENTRY S_glVertex4bOES(GLbyte x, GLbyte y, GLbyte z, GLbyte w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex4bOES"); } }
        void APIENTRY S_glVertex4bvOES(const GLbyte *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex4bvOES"); } }
        void APIENTRY S_glAlphaFuncxOES(GLenum func, GLfixed ref) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAlphaFuncxOES"); } }
        void APIENTRY S_glClearColorxOES(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearColorxOES"); } }
        void APIENTRY S_glClearDepthxOES(GLfixed depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearDepthxOES"); } }
        void APIENTRY S_glClipPlanexOES(GLenum plane, const GLfixed *equation) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClipPlanexOES"); } }
        void APIENTRY S_glColor4xOES(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor4xOES"); } }
        void APIENTRY S_glDepthRangexOES(GLfixed n, GLfixed f) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthRangexOES"); } }
        void APIENTRY S_glFogxOES(GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogxOES"); } }
        void APIENTRY S_glFogxvOES(GLenum pname, const GLfixed *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogxvOES"); } }
        void APIENTRY S_glFrustumxOES(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFrustumxOES"); } }
        void APIENTRY S_glGetClipPlanexOES(GLenum plane, GLfixed *equation) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetClipPlanexOES"); } }
        void APIENTRY S_glGetFixedvOES(GLenum pname, GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFixedvOES"); } }
        void APIENTRY S_glGetTexEnvxvOES(GLenum target, GLenum pname, GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexEnvxvOES"); } }
        void APIENTRY S_glGetTexParameterxvOES(GLenum target, GLenum pname, GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexParameterxvOES"); } }
        void APIENTRY S_glLightModelxOES(GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLightModelxOES"); } }
        void APIENTRY S_glLightModelxvOES(GLenum pname, const GLfixed *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLightModelxvOES"); } }
        void APIENTRY S_glLightxOES(GLenum light, GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLightxOES"); } }
        void APIENTRY S_glLightxvOES(GLenum light, GLenum pname, const GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLightxvOES"); } }
        void APIENTRY S_glLineWidthxOES(GLfixed width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLineWidthxOES"); } }
        void APIENTRY S_glLoadMatrixxOES(const GLfixed *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLoadMatrixxOES"); } }
        void APIENTRY S_glMaterialxOES(GLenum face, GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMaterialxOES"); } }
        void APIENTRY S_glMaterialxvOES(GLenum face, GLenum pname, const GLfixed *param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMaterialxvOES"); } }
        void APIENTRY S_glMultMatrixxOES(const GLfixed *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultMatrixxOES"); } }
        void APIENTRY S_glMultiTexCoord4xOES(GLenum texture, GLfixed s, GLfixed t, GLfixed r, GLfixed q) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4xOES"); } }
        void APIENTRY S_glNormal3xOES(GLfixed nx, GLfixed ny, GLfixed nz) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormal3xOES"); } }
        void APIENTRY S_glOrthoxOES(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glOrthoxOES"); } }
        void APIENTRY S_glPointParameterxvOES(GLenum pname, const GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterxvOES"); } }
        void APIENTRY S_glPointSizexOES(GLfixed size) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointSizexOES"); } }
        void APIENTRY S_glPolygonOffsetxOES(GLfixed factor, GLfixed units) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPolygonOffsetxOES"); } }
        void APIENTRY S_glRotatexOES(GLfixed angle, GLfixed x, GLfixed y, GLfixed z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRotatexOES"); } }
        void APIENTRY S_glScalexOES(GLfixed x, GLfixed y, GLfixed z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glScalexOES"); } }
        void APIENTRY S_glTexEnvxOES(GLenum target, GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexEnvxOES"); } }
        void APIENTRY S_glTexEnvxvOES(GLenum target, GLenum pname, const GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexEnvxvOES"); } }
        void APIENTRY S_glTexParameterxOES(GLenum target, GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexParameterxOES"); } }
        void APIENTRY S_glTexParameterxvOES(GLenum target, GLenum pname, const GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexParameterxvOES"); } }
        void APIENTRY S_glTranslatexOES(GLfixed x, GLfixed y, GLfixed z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTranslatexOES"); } }
        void APIENTRY S_glGetLightxvOES(GLenum light, GLenum pname, GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetLightxvOES"); } }
        void APIENTRY S_glGetMaterialxvOES(GLenum face, GLenum pname, GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMaterialxvOES"); } }
        void APIENTRY S_glPointParameterxOES(GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterxOES"); } }
        void APIENTRY S_glSampleCoveragexOES(GLclampx value, GLboolean invert) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSampleCoveragexOES"); } }
        void APIENTRY S_glAccumxOES(GLenum op, GLfixed value) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAccumxOES"); } }
        void APIENTRY S_glBitmapxOES(GLsizei width, GLsizei height, GLfixed xorig, GLfixed yorig, GLfixed xmove, GLfixed ymove, const GLubyte *bitmap) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBitmapxOES"); } }
        void APIENTRY S_glBlendColorxOES(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glBlendColorxOES"); } }
        void APIENTRY S_glClearAccumxOES(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearAccumxOES"); } }
        void APIENTRY S_glColor3xOES(GLfixed red, GLfixed green, GLfixed blue) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor3xOES"); } }
        void APIENTRY S_glColor3xvOES(const GLfixed *components) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor3xvOES"); } }
        void APIENTRY S_glColor4xvOES(const GLfixed *components) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor4xvOES"); } }
        void APIENTRY S_glConvolutionParameterxOES(GLenum target, GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionParameterxOES"); } }
        void APIENTRY S_glConvolutionParameterxvOES(GLenum target, GLenum pname, const GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glConvolutionParameterxvOES"); } }
        void APIENTRY S_glEvalCoord1xOES(GLfixed u) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEvalCoord1xOES"); } }
        void APIENTRY S_glEvalCoord1xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEvalCoord1xvOES"); } }
        void APIENTRY S_glEvalCoord2xOES(GLfixed u, GLfixed v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEvalCoord2xOES"); } }
        void APIENTRY S_glEvalCoord2xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glEvalCoord2xvOES"); } }
        void APIENTRY S_glFeedbackBufferxOES(GLsizei n, GLenum type, const GLfixed *buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFeedbackBufferxOES"); } }
        void APIENTRY S_glGetConvolutionParameterxvOES(GLenum target, GLenum pname, GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetConvolutionParameterxvOES"); } }
        void APIENTRY S_glGetHistogramParameterxvOES(GLenum target, GLenum pname, GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetHistogramParameterxvOES"); } }
        void APIENTRY S_glGetLightxOES(GLenum light, GLenum pname, GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetLightxOES"); } }
        void APIENTRY S_glGetMapxvOES(GLenum target, GLenum query, GLfixed *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMapxvOES"); } }
        void APIENTRY S_glGetMaterialxOES(GLenum face, GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetMaterialxOES"); } }
        void APIENTRY S_glGetPixelMapxv(GLenum map, GLint size, GLfixed *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPixelMapxv"); } }
        void APIENTRY S_glGetTexGenxvOES(GLenum coord, GLenum pname, GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexGenxvOES"); } }
        void APIENTRY S_glGetTexLevelParameterxvOES(GLenum target, GLint level, GLenum pname, GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexLevelParameterxvOES"); } }
        void APIENTRY S_glIndexxOES(GLfixed component) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIndexxOES"); } }
        void APIENTRY S_glIndexxvOES(const GLfixed *component) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIndexxvOES"); } }
        void APIENTRY S_glLoadTransposeMatrixxOES(const GLfixed *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLoadTransposeMatrixxOES"); } }
        void APIENTRY S_glMap1xOES(GLenum target, GLfixed u1, GLfixed u2, GLint stride, GLint order, GLfixed points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMap1xOES"); } }
        void APIENTRY S_glMap2xOES(GLenum target, GLfixed u1, GLfixed u2, GLint ustride, GLint uorder, GLfixed v1, GLfixed v2, GLint vstride, GLint vorder, GLfixed points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMap2xOES"); } }
        void APIENTRY S_glMapGrid1xOES(GLint n, GLfixed u1, GLfixed u2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapGrid1xOES"); } }
        void APIENTRY S_glMapGrid2xOES(GLint n, GLfixed u1, GLfixed u2, GLfixed v1, GLfixed v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMapGrid2xOES"); } }
        void APIENTRY S_glMultTransposeMatrixxOES(const GLfixed *m) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultTransposeMatrixxOES"); } }
        void APIENTRY S_glMultiTexCoord1xOES(GLenum texture, GLfixed s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1xOES"); } }
        void APIENTRY S_glMultiTexCoord1xvOES(GLenum texture, const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord1xvOES"); } }
        void APIENTRY S_glMultiTexCoord2xOES(GLenum texture, GLfixed s, GLfixed t) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2xOES"); } }
        void APIENTRY S_glMultiTexCoord2xvOES(GLenum texture, const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord2xvOES"); } }
        void APIENTRY S_glMultiTexCoord3xOES(GLenum texture, GLfixed s, GLfixed t, GLfixed r) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3xOES"); } }
        void APIENTRY S_glMultiTexCoord3xvOES(GLenum texture, const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord3xvOES"); } }
        void APIENTRY S_glMultiTexCoord4xvOES(GLenum texture, const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glMultiTexCoord4xvOES"); } }
        void APIENTRY S_glNormal3xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormal3xvOES"); } }
        void APIENTRY S_glPassThroughxOES(GLfixed token) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPassThroughxOES"); } }
        void APIENTRY S_glPixelMapx(GLenum map, GLint size, const GLfixed *values) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelMapx"); } }
        void APIENTRY S_glPixelStorex(GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelStorex"); } }
        void APIENTRY S_glPixelTransferxOES(GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelTransferxOES"); } }
        void APIENTRY S_glPixelZoomxOES(GLfixed xfactor, GLfixed yfactor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelZoomxOES"); } }
        void APIENTRY S_glPrioritizeTexturesxOES(GLsizei n, const GLuint *textures, const GLfixed *priorities) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPrioritizeTexturesxOES"); } }
        void APIENTRY S_glRasterPos2xOES(GLfixed x, GLfixed y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRasterPos2xOES"); } }
        void APIENTRY S_glRasterPos2xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRasterPos2xvOES"); } }
        void APIENTRY S_glRasterPos3xOES(GLfixed x, GLfixed y, GLfixed z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRasterPos3xOES"); } }
        void APIENTRY S_glRasterPos3xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRasterPos3xvOES"); } }
        void APIENTRY S_glRasterPos4xOES(GLfixed x, GLfixed y, GLfixed z, GLfixed w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRasterPos4xOES"); } }
        void APIENTRY S_glRasterPos4xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRasterPos4xvOES"); } }
        void APIENTRY S_glRectxOES(GLfixed x1, GLfixed y1, GLfixed x2, GLfixed y2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRectxOES"); } }
        void APIENTRY S_glRectxvOES(const GLfixed *v1, const GLfixed *v2) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glRectxvOES"); } }
        void APIENTRY S_glTexCoord1xOES(GLfixed s) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord1xOES"); } }
        void APIENTRY S_glTexCoord1xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord1xvOES"); } }
        void APIENTRY S_glTexCoord2xOES(GLfixed s, GLfixed t) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2xOES"); } }
        void APIENTRY S_glTexCoord2xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2xvOES"); } }
        void APIENTRY S_glTexCoord3xOES(GLfixed s, GLfixed t, GLfixed r) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord3xOES"); } }
        void APIENTRY S_glTexCoord3xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord3xvOES"); } }
        void APIENTRY S_glTexCoord4xOES(GLfixed s, GLfixed t, GLfixed r, GLfixed q) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord4xOES"); } }
        void APIENTRY S_glTexCoord4xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord4xvOES"); } }
        void APIENTRY S_glTexGenxOES(GLenum coord, GLenum pname, GLfixed param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexGenxOES"); } }
        void APIENTRY S_glTexGenxvOES(GLenum coord, GLenum pname, const GLfixed *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexGenxvOES"); } }
        void APIENTRY S_glVertex2xOES(GLfixed x) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex2xOES"); } }
        void APIENTRY S_glVertex2xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex2xvOES"); } }
        void APIENTRY S_glVertex3xOES(GLfixed x, GLfixed y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex3xOES"); } }
        void APIENTRY S_glVertex3xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex3xvOES"); } }
        void APIENTRY S_glVertex4xOES(GLfixed x, GLfixed y, GLfixed z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex4xOES"); } }
        void APIENTRY S_glVertex4xvOES(const GLfixed *coords) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glVertex4xvOES"); } }
        GLbitfield APIENTRY S_glQueryMatrixxOES(GLfixed *mantissa, GLint *exponent) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glQueryMatrixxOES"); } return {}; }
        void APIENTRY S_glClearDepthfOES(GLclampf depth) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClearDepthfOES"); } }
        void APIENTRY S_glClipPlanefOES(GLenum plane, const GLfloat *equation) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glClipPlanefOES"); } }
        void APIENTRY S_glDepthRangefOES(GLclampf n, GLclampf f) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDepthRangefOES"); } }
        void APIENTRY S_glFrustumfOES(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFrustumfOES"); } }
        void APIENTRY S_glGetClipPlanefOES(GLenum plane, GLfloat *equation) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetClipPlanefOES"); } }
        void APIENTRY S_glOrthofOES(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glOrthofOES"); } }
        void APIENTRY S_glFramebufferTextureMultiviewOVR(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint baseViewIndex, GLsizei numViews) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFramebufferTextureMultiviewOVR"); } }
        void APIENTRY S_glNamedFramebufferTextureMultiviewOVR(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint baseViewIndex, GLsizei numViews) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNamedFramebufferTextureMultiviewOVR"); } }
        void APIENTRY S_glHintPGI(GLenum target, GLint mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glHintPGI"); } }
        void APIENTRY S_glDetailTexFuncSGIS(GLenum target, GLsizei n, const GLfloat *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDetailTexFuncSGIS"); } }
        void APIENTRY S_glGetDetailTexFuncSGIS(GLenum target, GLfloat *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetDetailTexFuncSGIS"); } }
        void APIENTRY S_glFogFuncSGIS(GLsizei n, const GLfloat *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFogFuncSGIS"); } }
        void APIENTRY S_glGetFogFuncSGIS(GLfloat *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFogFuncSGIS"); } }
        void APIENTRY S_glSampleMaskSGIS(GLclampf value, GLboolean invert) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSampleMaskSGIS"); } }
        void APIENTRY S_glSamplePatternSGIS(GLenum pattern) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSamplePatternSGIS"); } }
        void APIENTRY S_glPixelTexGenParameteriSGIS(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelTexGenParameteriSGIS"); } }
        void APIENTRY S_glPixelTexGenParameterivSGIS(GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelTexGenParameterivSGIS"); } }
        void APIENTRY S_glPixelTexGenParameterfSGIS(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelTexGenParameterfSGIS"); } }
        void APIENTRY S_glPixelTexGenParameterfvSGIS(GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelTexGenParameterfvSGIS"); } }
        void APIENTRY S_glGetPixelTexGenParameterivSGIS(GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPixelTexGenParameterivSGIS"); } }
        void APIENTRY S_glGetPixelTexGenParameterfvSGIS(GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetPixelTexGenParameterfvSGIS"); } }
        void APIENTRY S_glPointParameterfSGIS(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterfSGIS"); } }
        void APIENTRY S_glPointParameterfvSGIS(GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPointParameterfvSGIS"); } }
        void APIENTRY S_glSharpenTexFuncSGIS(GLenum target, GLsizei n, const GLfloat *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSharpenTexFuncSGIS"); } }
        void APIENTRY S_glGetSharpenTexFuncSGIS(GLenum target, GLfloat *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetSharpenTexFuncSGIS"); } }
        void APIENTRY S_glTexImage4DSGIS(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLsizei size4d, GLint border, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexImage4DSGIS"); } }
        void APIENTRY S_glTexSubImage4DSGIS(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint woffset, GLsizei width, GLsizei height, GLsizei depth, GLsizei size4d, GLenum format, GLenum type, const void *pixels) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexSubImage4DSGIS"); } }
        void APIENTRY S_glTextureColorMaskSGIS(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTextureColorMaskSGIS"); } }
        void APIENTRY S_glGetTexFilterFuncSGIS(GLenum target, GLenum filter, GLfloat *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetTexFilterFuncSGIS"); } }
        void APIENTRY S_glTexFilterFuncSGIS(GLenum target, GLenum filter, GLsizei n, const GLfloat *weights) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexFilterFuncSGIS"); } }
        void APIENTRY S_glAsyncMarkerSGIX(GLuint marker) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glAsyncMarkerSGIX"); } }
        GLint APIENTRY S_glFinishAsyncSGIX(GLuint *markerp) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFinishAsyncSGIX"); } return {}; }
        GLint APIENTRY S_glPollAsyncSGIX(GLuint *markerp) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPollAsyncSGIX"); } return {}; }
        GLuint APIENTRY S_glGenAsyncMarkersSGIX(GLsizei range) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGenAsyncMarkersSGIX"); } return {}; }
        void APIENTRY S_glDeleteAsyncMarkersSGIX(GLuint marker, GLsizei range) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeleteAsyncMarkersSGIX"); } }
        GLboolean APIENTRY S_glIsAsyncMarkerSGIX(GLuint marker) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIsAsyncMarkerSGIX"); } return {}; }
        void APIENTRY S_glFlushRasterSGIX(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFlushRasterSGIX"); } }
        void APIENTRY S_glFragmentColorMaterialSGIX(GLenum face, GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentColorMaterialSGIX"); } }
        void APIENTRY S_glFragmentLightfSGIX(GLenum light, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentLightfSGIX"); } }
        void APIENTRY S_glFragmentLightfvSGIX(GLenum light, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentLightfvSGIX"); } }
        void APIENTRY S_glFragmentLightiSGIX(GLenum light, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentLightiSGIX"); } }
        void APIENTRY S_glFragmentLightivSGIX(GLenum light, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentLightivSGIX"); } }
        void APIENTRY S_glFragmentLightModelfSGIX(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentLightModelfSGIX"); } }
        void APIENTRY S_glFragmentLightModelfvSGIX(GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentLightModelfvSGIX"); } }
        void APIENTRY S_glFragmentLightModeliSGIX(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentLightModeliSGIX"); } }
        void APIENTRY S_glFragmentLightModelivSGIX(GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentLightModelivSGIX"); } }
        void APIENTRY S_glFragmentMaterialfSGIX(GLenum face, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentMaterialfSGIX"); } }
        void APIENTRY S_glFragmentMaterialfvSGIX(GLenum face, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentMaterialfvSGIX"); } }
        void APIENTRY S_glFragmentMaterialiSGIX(GLenum face, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentMaterialiSGIX"); } }
        void APIENTRY S_glFragmentMaterialivSGIX(GLenum face, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFragmentMaterialivSGIX"); } }
        void APIENTRY S_glGetFragmentLightfvSGIX(GLenum light, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFragmentLightfvSGIX"); } }
        void APIENTRY S_glGetFragmentLightivSGIX(GLenum light, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFragmentLightivSGIX"); } }
        void APIENTRY S_glGetFragmentMaterialfvSGIX(GLenum face, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFragmentMaterialfvSGIX"); } }
        void APIENTRY S_glGetFragmentMaterialivSGIX(GLenum face, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetFragmentMaterialivSGIX"); } }
        void APIENTRY S_glLightEnviSGIX(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLightEnviSGIX"); } }
        void APIENTRY S_glFrameZoomSGIX(GLint factor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFrameZoomSGIX"); } }
        void APIENTRY S_glIglooInterfaceSGIX(GLenum pname, const void *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glIglooInterfaceSGIX"); } }
        GLint APIENTRY S_glGetInstrumentsSGIX(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetInstrumentsSGIX"); } return {}; }
        void APIENTRY S_glInstrumentsBufferSGIX(GLsizei size, GLint *buffer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glInstrumentsBufferSGIX"); } }
        GLint APIENTRY S_glPollInstrumentsSGIX(GLint *marker_p) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPollInstrumentsSGIX"); } return {}; }
        void APIENTRY S_glReadInstrumentsSGIX(GLint marker) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReadInstrumentsSGIX"); } }
        void APIENTRY S_glStartInstrumentsSGIX(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStartInstrumentsSGIX"); } }
        void APIENTRY S_glStopInstrumentsSGIX(GLint marker) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glStopInstrumentsSGIX"); } }
        void APIENTRY S_glGetListParameterfvSGIX(GLuint list, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetListParameterfvSGIX"); } }
        void APIENTRY S_glGetListParameterivSGIX(GLuint list, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetListParameterivSGIX"); } }
        void APIENTRY S_glListParameterfSGIX(GLuint list, GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glListParameterfSGIX"); } }
        void APIENTRY S_glListParameterfvSGIX(GLuint list, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glListParameterfvSGIX"); } }
        void APIENTRY S_glListParameteriSGIX(GLuint list, GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glListParameteriSGIX"); } }
        void APIENTRY S_glListParameterivSGIX(GLuint list, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glListParameterivSGIX"); } }
        void APIENTRY S_glPixelTexGenSGIX(GLenum mode) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glPixelTexGenSGIX"); } }
        void APIENTRY S_glDeformationMap3dSGIX(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, GLdouble w1, GLdouble w2, GLint wstride, GLint worder, const GLdouble *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeformationMap3dSGIX"); } }
        void APIENTRY S_glDeformationMap3fSGIX(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, GLfloat w1, GLfloat w2, GLint wstride, GLint worder, const GLfloat *points) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeformationMap3fSGIX"); } }
        void APIENTRY S_glDeformSGIX(GLbitfield mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDeformSGIX"); } }
        void APIENTRY S_glLoadIdentityDeformationMapSGIX(GLbitfield mask) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glLoadIdentityDeformationMapSGIX"); } }
        void APIENTRY S_glReferencePlaneSGIX(const GLdouble *equation) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReferencePlaneSGIX"); } }
        void APIENTRY S_glSpriteParameterfSGIX(GLenum pname, GLfloat param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSpriteParameterfSGIX"); } }
        void APIENTRY S_glSpriteParameterfvSGIX(GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSpriteParameterfvSGIX"); } }
        void APIENTRY S_glSpriteParameteriSGIX(GLenum pname, GLint param) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSpriteParameteriSGIX"); } }
        void APIENTRY S_glSpriteParameterivSGIX(GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glSpriteParameterivSGIX"); } }
        void APIENTRY S_glTagSampleBufferSGIX(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTagSampleBufferSGIX"); } }
        void APIENTRY S_glColorTableSGI(GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const void *table) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorTableSGI"); } }
        void APIENTRY S_glColorTableParameterfvSGI(GLenum target, GLenum pname, const GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorTableParameterfvSGI"); } }
        void APIENTRY S_glColorTableParameterivSGI(GLenum target, GLenum pname, const GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColorTableParameterivSGI"); } }
        void APIENTRY S_glCopyColorTableSGI(GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glCopyColorTableSGI"); } }
        void APIENTRY S_glGetColorTableSGI(GLenum target, GLenum format, GLenum type, void *table) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetColorTableSGI"); } }
        void APIENTRY S_glGetColorTableParameterfvSGI(GLenum target, GLenum pname, GLfloat *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetColorTableParameterfvSGI"); } }
        void APIENTRY S_glGetColorTableParameterivSGI(GLenum target, GLenum pname, GLint *params) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGetColorTableParameterivSGI"); } }
        void APIENTRY S_glFinishTextureSUNX(void) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glFinishTextureSUNX"); } }
        void APIENTRY S_glGlobalAlphaFactorbSUN(GLbyte factor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGlobalAlphaFactorbSUN"); } }
        void APIENTRY S_glGlobalAlphaFactorsSUN(GLshort factor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGlobalAlphaFactorsSUN"); } }
        void APIENTRY S_glGlobalAlphaFactoriSUN(GLint factor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGlobalAlphaFactoriSUN"); } }
        void APIENTRY S_glGlobalAlphaFactorfSUN(GLfloat factor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGlobalAlphaFactorfSUN"); } }
        void APIENTRY S_glGlobalAlphaFactordSUN(GLdouble factor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGlobalAlphaFactordSUN"); } }
        void APIENTRY S_glGlobalAlphaFactorubSUN(GLubyte factor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGlobalAlphaFactorubSUN"); } }
        void APIENTRY S_glGlobalAlphaFactorusSUN(GLushort factor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGlobalAlphaFactorusSUN"); } }
        void APIENTRY S_glGlobalAlphaFactoruiSUN(GLuint factor) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glGlobalAlphaFactoruiSUN"); } }
        void APIENTRY S_glDrawMeshArraysSUN(GLenum mode, GLint first, GLsizei count, GLsizei width) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glDrawMeshArraysSUN"); } }
        void APIENTRY S_glReplacementCodeuiSUN(GLuint code) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiSUN"); } }
        void APIENTRY S_glReplacementCodeusSUN(GLushort code) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeusSUN"); } }
        void APIENTRY S_glReplacementCodeubSUN(GLubyte code) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeubSUN"); } }
        void APIENTRY S_glReplacementCodeuivSUN(const GLuint *code) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuivSUN"); } }
        void APIENTRY S_glReplacementCodeusvSUN(const GLushort *code) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeusvSUN"); } }
        void APIENTRY S_glReplacementCodeubvSUN(const GLubyte *code) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeubvSUN"); } }
        void APIENTRY S_glReplacementCodePointerSUN(GLenum type, GLsizei stride, const void **pointer) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodePointerSUN"); } }
        void APIENTRY S_glColor4ubVertex2fSUN(GLubyte r, GLubyte g, GLubyte b, GLubyte a, GLfloat x, GLfloat y) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor4ubVertex2fSUN"); } }
        void APIENTRY S_glColor4ubVertex2fvSUN(const GLubyte *c, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor4ubVertex2fvSUN"); } }
        void APIENTRY S_glColor4ubVertex3fSUN(GLubyte r, GLubyte g, GLubyte b, GLubyte a, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor4ubVertex3fSUN"); } }
        void APIENTRY S_glColor4ubVertex3fvSUN(const GLubyte *c, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor4ubVertex3fvSUN"); } }
        void APIENTRY S_glColor3fVertex3fSUN(GLfloat r, GLfloat g, GLfloat b, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor3fVertex3fSUN"); } }
        void APIENTRY S_glColor3fVertex3fvSUN(const GLfloat *c, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor3fVertex3fvSUN"); } }
        void APIENTRY S_glNormal3fVertex3fSUN(GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormal3fVertex3fSUN"); } }
        void APIENTRY S_glNormal3fVertex3fvSUN(const GLfloat *n, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glNormal3fVertex3fvSUN"); } }
        void APIENTRY S_glColor4fNormal3fVertex3fSUN(GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor4fNormal3fVertex3fSUN"); } }
        void APIENTRY S_glColor4fNormal3fVertex3fvSUN(const GLfloat *c, const GLfloat *n, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glColor4fNormal3fVertex3fvSUN"); } }
        void APIENTRY S_glTexCoord2fVertex3fSUN(GLfloat s, GLfloat t, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2fVertex3fSUN"); } }
        void APIENTRY S_glTexCoord2fVertex3fvSUN(const GLfloat *tc, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2fVertex3fvSUN"); } }
        void APIENTRY S_glTexCoord4fVertex4fSUN(GLfloat s, GLfloat t, GLfloat p, GLfloat q, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord4fVertex4fSUN"); } }
        void APIENTRY S_glTexCoord4fVertex4fvSUN(const GLfloat *tc, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord4fVertex4fvSUN"); } }
        void APIENTRY S_glTexCoord2fColor4ubVertex3fSUN(GLfloat s, GLfloat t, GLubyte r, GLubyte g, GLubyte b, GLubyte a, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2fColor4ubVertex3fSUN"); } }
        void APIENTRY S_glTexCoord2fColor4ubVertex3fvSUN(const GLfloat *tc, const GLubyte *c, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2fColor4ubVertex3fvSUN"); } }
        void APIENTRY S_glTexCoord2fColor3fVertex3fSUN(GLfloat s, GLfloat t, GLfloat r, GLfloat g, GLfloat b, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2fColor3fVertex3fSUN"); } }
        void APIENTRY S_glTexCoord2fColor3fVertex3fvSUN(const GLfloat *tc, const GLfloat *c, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2fColor3fVertex3fvSUN"); } }
        void APIENTRY S_glTexCoord2fNormal3fVertex3fSUN(GLfloat s, GLfloat t, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2fNormal3fVertex3fSUN"); } }
        void APIENTRY S_glTexCoord2fNormal3fVertex3fvSUN(const GLfloat *tc, const GLfloat *n, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2fNormal3fVertex3fvSUN"); } }
        void APIENTRY S_glTexCoord2fColor4fNormal3fVertex3fSUN(GLfloat s, GLfloat t, GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2fColor4fNormal3fVertex3fSUN"); } }
        void APIENTRY S_glTexCoord2fColor4fNormal3fVertex3fvSUN(const GLfloat *tc, const GLfloat *c, const GLfloat *n, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord2fColor4fNormal3fVertex3fvSUN"); } }
        void APIENTRY S_glTexCoord4fColor4fNormal3fVertex4fSUN(GLfloat s, GLfloat t, GLfloat p, GLfloat q, GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord4fColor4fNormal3fVertex4fSUN"); } }
        void APIENTRY S_glTexCoord4fColor4fNormal3fVertex4fvSUN(const GLfloat *tc, const GLfloat *c, const GLfloat *n, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glTexCoord4fColor4fNormal3fVertex4fvSUN"); } }
        void APIENTRY S_glReplacementCodeuiVertex3fSUN(GLuint rc, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiVertex3fSUN"); } }
        void APIENTRY S_glReplacementCodeuiVertex3fvSUN(const GLuint *rc, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiVertex3fvSUN"); } }
        void APIENTRY S_glReplacementCodeuiColor4ubVertex3fSUN(GLuint rc, GLubyte r, GLubyte g, GLubyte b, GLubyte a, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiColor4ubVertex3fSUN"); } }
        void APIENTRY S_glReplacementCodeuiColor4ubVertex3fvSUN(const GLuint *rc, const GLubyte *c, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiColor4ubVertex3fvSUN"); } }
        void APIENTRY S_glReplacementCodeuiColor3fVertex3fSUN(GLuint rc, GLfloat r, GLfloat g, GLfloat b, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiColor3fVertex3fSUN"); } }
        void APIENTRY S_glReplacementCodeuiColor3fVertex3fvSUN(const GLuint *rc, const GLfloat *c, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiColor3fVertex3fvSUN"); } }
        void APIENTRY S_glReplacementCodeuiNormal3fVertex3fSUN(GLuint rc, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiNormal3fVertex3fSUN"); } }
        void APIENTRY S_glReplacementCodeuiNormal3fVertex3fvSUN(const GLuint *rc, const GLfloat *n, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiNormal3fVertex3fvSUN"); } }
        void APIENTRY S_glReplacementCodeuiColor4fNormal3fVertex3fSUN(GLuint rc, GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiColor4fNormal3fVertex3fSUN"); } }
        void APIENTRY S_glReplacementCodeuiColor4fNormal3fVertex3fvSUN(const GLuint *rc, const GLfloat *c, const GLfloat *n, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiColor4fNormal3fVertex3fvSUN"); } }
        void APIENTRY S_glReplacementCodeuiTexCoord2fVertex3fSUN(GLuint rc, GLfloat s, GLfloat t, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiTexCoord2fVertex3fSUN"); } }
        void APIENTRY S_glReplacementCodeuiTexCoord2fVertex3fvSUN(const GLuint *rc, const GLfloat *tc, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiTexCoord2fVertex3fvSUN"); } }
        void APIENTRY S_glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN(GLuint rc, GLfloat s, GLfloat t, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN"); } }
        void APIENTRY S_glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN(const GLuint *rc, const GLfloat *tc, const GLfloat *n, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN"); } }
        void APIENTRY S_glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN(GLuint rc, GLfloat s, GLfloat t, GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN"); } }
        void APIENTRY S_glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN(const GLuint *rc, const GLfloat *tc, const GLfloat *c, const GLfloat *n, const GLfloat *v) { static bool cqWarned = false; if (!cqWarned) { cqWarned = true; Missing("glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN"); } }
    }

    int InstallMissingStubs()
    {
        int count = 0;
        if (!glad_glCullFace) { glad_glCullFace = &S_glCullFace; ++count; }
        if (!glad_glFrontFace) { glad_glFrontFace = &S_glFrontFace; ++count; }
        if (!glad_glHint) { glad_glHint = &S_glHint; ++count; }
        if (!glad_glLineWidth) { glad_glLineWidth = &S_glLineWidth; ++count; }
        if (!glad_glPointSize) { glad_glPointSize = &S_glPointSize; ++count; }
        if (!glad_glPolygonMode) { glad_glPolygonMode = &S_glPolygonMode; ++count; }
        if (!glad_glScissor) { glad_glScissor = &S_glScissor; ++count; }
        if (!glad_glTexParameterf) { glad_glTexParameterf = &S_glTexParameterf; ++count; }
        if (!glad_glTexParameterfv) { glad_glTexParameterfv = &S_glTexParameterfv; ++count; }
        if (!glad_glTexParameteri) { glad_glTexParameteri = &S_glTexParameteri; ++count; }
        if (!glad_glTexParameteriv) { glad_glTexParameteriv = &S_glTexParameteriv; ++count; }
        if (!glad_glTexImage1D) { glad_glTexImage1D = &S_glTexImage1D; ++count; }
        if (!glad_glTexImage2D) { glad_glTexImage2D = &S_glTexImage2D; ++count; }
        if (!glad_glDrawBuffer) { glad_glDrawBuffer = &S_glDrawBuffer; ++count; }
        if (!glad_glClear) { glad_glClear = &S_glClear; ++count; }
        if (!glad_glClearColor) { glad_glClearColor = &S_glClearColor; ++count; }
        if (!glad_glClearStencil) { glad_glClearStencil = &S_glClearStencil; ++count; }
        if (!glad_glClearDepth) { glad_glClearDepth = &S_glClearDepth; ++count; }
        if (!glad_glStencilMask) { glad_glStencilMask = &S_glStencilMask; ++count; }
        if (!glad_glColorMask) { glad_glColorMask = &S_glColorMask; ++count; }
        if (!glad_glDepthMask) { glad_glDepthMask = &S_glDepthMask; ++count; }
        if (!glad_glDisable) { glad_glDisable = &S_glDisable; ++count; }
        if (!glad_glEnable) { glad_glEnable = &S_glEnable; ++count; }
        if (!glad_glFinish) { glad_glFinish = &S_glFinish; ++count; }
        if (!glad_glFlush) { glad_glFlush = &S_glFlush; ++count; }
        if (!glad_glBlendFunc) { glad_glBlendFunc = &S_glBlendFunc; ++count; }
        if (!glad_glLogicOp) { glad_glLogicOp = &S_glLogicOp; ++count; }
        if (!glad_glStencilFunc) { glad_glStencilFunc = &S_glStencilFunc; ++count; }
        if (!glad_glStencilOp) { glad_glStencilOp = &S_glStencilOp; ++count; }
        if (!glad_glDepthFunc) { glad_glDepthFunc = &S_glDepthFunc; ++count; }
        if (!glad_glPixelStoref) { glad_glPixelStoref = &S_glPixelStoref; ++count; }
        if (!glad_glPixelStorei) { glad_glPixelStorei = &S_glPixelStorei; ++count; }
        if (!glad_glReadBuffer) { glad_glReadBuffer = &S_glReadBuffer; ++count; }
        if (!glad_glReadPixels) { glad_glReadPixels = &S_glReadPixels; ++count; }
        if (!glad_glGetBooleanv) { glad_glGetBooleanv = &S_glGetBooleanv; ++count; }
        if (!glad_glGetDoublev) { glad_glGetDoublev = &S_glGetDoublev; ++count; }
        if (!glad_glGetError) { glad_glGetError = &S_glGetError; ++count; }
        if (!glad_glGetFloatv) { glad_glGetFloatv = &S_glGetFloatv; ++count; }
        if (!glad_glGetIntegerv) { glad_glGetIntegerv = &S_glGetIntegerv; ++count; }
        if (!glad_glGetString) { glad_glGetString = &S_glGetString; ++count; }
        if (!glad_glGetTexImage) { glad_glGetTexImage = &S_glGetTexImage; ++count; }
        if (!glad_glGetTexParameterfv) { glad_glGetTexParameterfv = &S_glGetTexParameterfv; ++count; }
        if (!glad_glGetTexParameteriv) { glad_glGetTexParameteriv = &S_glGetTexParameteriv; ++count; }
        if (!glad_glGetTexLevelParameterfv) { glad_glGetTexLevelParameterfv = &S_glGetTexLevelParameterfv; ++count; }
        if (!glad_glGetTexLevelParameteriv) { glad_glGetTexLevelParameteriv = &S_glGetTexLevelParameteriv; ++count; }
        if (!glad_glIsEnabled) { glad_glIsEnabled = &S_glIsEnabled; ++count; }
        if (!glad_glDepthRange) { glad_glDepthRange = &S_glDepthRange; ++count; }
        if (!glad_glViewport) { glad_glViewport = &S_glViewport; ++count; }
        if (!glad_glDrawArrays) { glad_glDrawArrays = &S_glDrawArrays; ++count; }
        if (!glad_glDrawElements) { glad_glDrawElements = &S_glDrawElements; ++count; }
        if (!glad_glPolygonOffset) { glad_glPolygonOffset = &S_glPolygonOffset; ++count; }
        if (!glad_glCopyTexImage1D) { glad_glCopyTexImage1D = &S_glCopyTexImage1D; ++count; }
        if (!glad_glCopyTexImage2D) { glad_glCopyTexImage2D = &S_glCopyTexImage2D; ++count; }
        if (!glad_glCopyTexSubImage1D) { glad_glCopyTexSubImage1D = &S_glCopyTexSubImage1D; ++count; }
        if (!glad_glCopyTexSubImage2D) { glad_glCopyTexSubImage2D = &S_glCopyTexSubImage2D; ++count; }
        if (!glad_glTexSubImage1D) { glad_glTexSubImage1D = &S_glTexSubImage1D; ++count; }
        if (!glad_glTexSubImage2D) { glad_glTexSubImage2D = &S_glTexSubImage2D; ++count; }
        if (!glad_glBindTexture) { glad_glBindTexture = &S_glBindTexture; ++count; }
        if (!glad_glDeleteTextures) { glad_glDeleteTextures = &S_glDeleteTextures; ++count; }
        if (!glad_glGenTextures) { glad_glGenTextures = &S_glGenTextures; ++count; }
        if (!glad_glIsTexture) { glad_glIsTexture = &S_glIsTexture; ++count; }
        if (!glad_glDrawRangeElements) { glad_glDrawRangeElements = &S_glDrawRangeElements; ++count; }
        if (!glad_glTexImage3D) { glad_glTexImage3D = &S_glTexImage3D; ++count; }
        if (!glad_glTexSubImage3D) { glad_glTexSubImage3D = &S_glTexSubImage3D; ++count; }
        if (!glad_glCopyTexSubImage3D) { glad_glCopyTexSubImage3D = &S_glCopyTexSubImage3D; ++count; }
        if (!glad_glActiveTexture) { glad_glActiveTexture = &S_glActiveTexture; ++count; }
        if (!glad_glSampleCoverage) { glad_glSampleCoverage = &S_glSampleCoverage; ++count; }
        if (!glad_glCompressedTexImage3D) { glad_glCompressedTexImage3D = &S_glCompressedTexImage3D; ++count; }
        if (!glad_glCompressedTexImage2D) { glad_glCompressedTexImage2D = &S_glCompressedTexImage2D; ++count; }
        if (!glad_glCompressedTexImage1D) { glad_glCompressedTexImage1D = &S_glCompressedTexImage1D; ++count; }
        if (!glad_glCompressedTexSubImage3D) { glad_glCompressedTexSubImage3D = &S_glCompressedTexSubImage3D; ++count; }
        if (!glad_glCompressedTexSubImage2D) { glad_glCompressedTexSubImage2D = &S_glCompressedTexSubImage2D; ++count; }
        if (!glad_glCompressedTexSubImage1D) { glad_glCompressedTexSubImage1D = &S_glCompressedTexSubImage1D; ++count; }
        if (!glad_glGetCompressedTexImage) { glad_glGetCompressedTexImage = &S_glGetCompressedTexImage; ++count; }
        if (!glad_glBlendFuncSeparate) { glad_glBlendFuncSeparate = &S_glBlendFuncSeparate; ++count; }
        if (!glad_glMultiDrawArrays) { glad_glMultiDrawArrays = &S_glMultiDrawArrays; ++count; }
        if (!glad_glMultiDrawElements) { glad_glMultiDrawElements = &S_glMultiDrawElements; ++count; }
        if (!glad_glPointParameterf) { glad_glPointParameterf = &S_glPointParameterf; ++count; }
        if (!glad_glPointParameterfv) { glad_glPointParameterfv = &S_glPointParameterfv; ++count; }
        if (!glad_glPointParameteri) { glad_glPointParameteri = &S_glPointParameteri; ++count; }
        if (!glad_glPointParameteriv) { glad_glPointParameteriv = &S_glPointParameteriv; ++count; }
        if (!glad_glBlendColor) { glad_glBlendColor = &S_glBlendColor; ++count; }
        if (!glad_glBlendEquation) { glad_glBlendEquation = &S_glBlendEquation; ++count; }
        if (!glad_glGenQueries) { glad_glGenQueries = &S_glGenQueries; ++count; }
        if (!glad_glDeleteQueries) { glad_glDeleteQueries = &S_glDeleteQueries; ++count; }
        if (!glad_glIsQuery) { glad_glIsQuery = &S_glIsQuery; ++count; }
        if (!glad_glBeginQuery) { glad_glBeginQuery = &S_glBeginQuery; ++count; }
        if (!glad_glEndQuery) { glad_glEndQuery = &S_glEndQuery; ++count; }
        if (!glad_glGetQueryiv) { glad_glGetQueryiv = &S_glGetQueryiv; ++count; }
        if (!glad_glGetQueryObjectiv) { glad_glGetQueryObjectiv = &S_glGetQueryObjectiv; ++count; }
        if (!glad_glGetQueryObjectuiv) { glad_glGetQueryObjectuiv = &S_glGetQueryObjectuiv; ++count; }
        if (!glad_glBindBuffer) { glad_glBindBuffer = &S_glBindBuffer; ++count; }
        if (!glad_glDeleteBuffers) { glad_glDeleteBuffers = &S_glDeleteBuffers; ++count; }
        if (!glad_glGenBuffers) { glad_glGenBuffers = &S_glGenBuffers; ++count; }
        if (!glad_glIsBuffer) { glad_glIsBuffer = &S_glIsBuffer; ++count; }
        if (!glad_glBufferData) { glad_glBufferData = &S_glBufferData; ++count; }
        if (!glad_glBufferSubData) { glad_glBufferSubData = &S_glBufferSubData; ++count; }
        if (!glad_glGetBufferSubData) { glad_glGetBufferSubData = &S_glGetBufferSubData; ++count; }
        if (!glad_glMapBuffer) { glad_glMapBuffer = &S_glMapBuffer; ++count; }
        if (!glad_glUnmapBuffer) { glad_glUnmapBuffer = &S_glUnmapBuffer; ++count; }
        if (!glad_glGetBufferParameteriv) { glad_glGetBufferParameteriv = &S_glGetBufferParameteriv; ++count; }
        if (!glad_glGetBufferPointerv) { glad_glGetBufferPointerv = &S_glGetBufferPointerv; ++count; }
        if (!glad_glBlendEquationSeparate) { glad_glBlendEquationSeparate = &S_glBlendEquationSeparate; ++count; }
        if (!glad_glDrawBuffers) { glad_glDrawBuffers = &S_glDrawBuffers; ++count; }
        if (!glad_glStencilOpSeparate) { glad_glStencilOpSeparate = &S_glStencilOpSeparate; ++count; }
        if (!glad_glStencilFuncSeparate) { glad_glStencilFuncSeparate = &S_glStencilFuncSeparate; ++count; }
        if (!glad_glStencilMaskSeparate) { glad_glStencilMaskSeparate = &S_glStencilMaskSeparate; ++count; }
        if (!glad_glAttachShader) { glad_glAttachShader = &S_glAttachShader; ++count; }
        if (!glad_glBindAttribLocation) { glad_glBindAttribLocation = &S_glBindAttribLocation; ++count; }
        if (!glad_glCompileShader) { glad_glCompileShader = &S_glCompileShader; ++count; }
        if (!glad_glCreateProgram) { glad_glCreateProgram = &S_glCreateProgram; ++count; }
        if (!glad_glCreateShader) { glad_glCreateShader = &S_glCreateShader; ++count; }
        if (!glad_glDeleteProgram) { glad_glDeleteProgram = &S_glDeleteProgram; ++count; }
        if (!glad_glDeleteShader) { glad_glDeleteShader = &S_glDeleteShader; ++count; }
        if (!glad_glDetachShader) { glad_glDetachShader = &S_glDetachShader; ++count; }
        if (!glad_glDisableVertexAttribArray) { glad_glDisableVertexAttribArray = &S_glDisableVertexAttribArray; ++count; }
        if (!glad_glEnableVertexAttribArray) { glad_glEnableVertexAttribArray = &S_glEnableVertexAttribArray; ++count; }
        if (!glad_glGetActiveAttrib) { glad_glGetActiveAttrib = &S_glGetActiveAttrib; ++count; }
        if (!glad_glGetActiveUniform) { glad_glGetActiveUniform = &S_glGetActiveUniform; ++count; }
        if (!glad_glGetAttachedShaders) { glad_glGetAttachedShaders = &S_glGetAttachedShaders; ++count; }
        if (!glad_glGetAttribLocation) { glad_glGetAttribLocation = &S_glGetAttribLocation; ++count; }
        if (!glad_glGetProgramiv) { glad_glGetProgramiv = &S_glGetProgramiv; ++count; }
        if (!glad_glGetProgramInfoLog) { glad_glGetProgramInfoLog = &S_glGetProgramInfoLog; ++count; }
        if (!glad_glGetShaderiv) { glad_glGetShaderiv = &S_glGetShaderiv; ++count; }
        if (!glad_glGetShaderInfoLog) { glad_glGetShaderInfoLog = &S_glGetShaderInfoLog; ++count; }
        if (!glad_glGetShaderSource) { glad_glGetShaderSource = &S_glGetShaderSource; ++count; }
        if (!glad_glGetUniformLocation) { glad_glGetUniformLocation = &S_glGetUniformLocation; ++count; }
        if (!glad_glGetUniformfv) { glad_glGetUniformfv = &S_glGetUniformfv; ++count; }
        if (!glad_glGetUniformiv) { glad_glGetUniformiv = &S_glGetUniformiv; ++count; }
        if (!glad_glGetVertexAttribdv) { glad_glGetVertexAttribdv = &S_glGetVertexAttribdv; ++count; }
        if (!glad_glGetVertexAttribfv) { glad_glGetVertexAttribfv = &S_glGetVertexAttribfv; ++count; }
        if (!glad_glGetVertexAttribiv) { glad_glGetVertexAttribiv = &S_glGetVertexAttribiv; ++count; }
        if (!glad_glGetVertexAttribPointerv) { glad_glGetVertexAttribPointerv = &S_glGetVertexAttribPointerv; ++count; }
        if (!glad_glIsProgram) { glad_glIsProgram = &S_glIsProgram; ++count; }
        if (!glad_glIsShader) { glad_glIsShader = &S_glIsShader; ++count; }
        if (!glad_glLinkProgram) { glad_glLinkProgram = &S_glLinkProgram; ++count; }
        if (!glad_glShaderSource) { glad_glShaderSource = &S_glShaderSource; ++count; }
        if (!glad_glUseProgram) { glad_glUseProgram = &S_glUseProgram; ++count; }
        if (!glad_glUniform1f) { glad_glUniform1f = &S_glUniform1f; ++count; }
        if (!glad_glUniform2f) { glad_glUniform2f = &S_glUniform2f; ++count; }
        if (!glad_glUniform3f) { glad_glUniform3f = &S_glUniform3f; ++count; }
        if (!glad_glUniform4f) { glad_glUniform4f = &S_glUniform4f; ++count; }
        if (!glad_glUniform1i) { glad_glUniform1i = &S_glUniform1i; ++count; }
        if (!glad_glUniform2i) { glad_glUniform2i = &S_glUniform2i; ++count; }
        if (!glad_glUniform3i) { glad_glUniform3i = &S_glUniform3i; ++count; }
        if (!glad_glUniform4i) { glad_glUniform4i = &S_glUniform4i; ++count; }
        if (!glad_glUniform1fv) { glad_glUniform1fv = &S_glUniform1fv; ++count; }
        if (!glad_glUniform2fv) { glad_glUniform2fv = &S_glUniform2fv; ++count; }
        if (!glad_glUniform3fv) { glad_glUniform3fv = &S_glUniform3fv; ++count; }
        if (!glad_glUniform4fv) { glad_glUniform4fv = &S_glUniform4fv; ++count; }
        if (!glad_glUniform1iv) { glad_glUniform1iv = &S_glUniform1iv; ++count; }
        if (!glad_glUniform2iv) { glad_glUniform2iv = &S_glUniform2iv; ++count; }
        if (!glad_glUniform3iv) { glad_glUniform3iv = &S_glUniform3iv; ++count; }
        if (!glad_glUniform4iv) { glad_glUniform4iv = &S_glUniform4iv; ++count; }
        if (!glad_glUniformMatrix2fv) { glad_glUniformMatrix2fv = &S_glUniformMatrix2fv; ++count; }
        if (!glad_glUniformMatrix3fv) { glad_glUniformMatrix3fv = &S_glUniformMatrix3fv; ++count; }
        if (!glad_glUniformMatrix4fv) { glad_glUniformMatrix4fv = &S_glUniformMatrix4fv; ++count; }
        if (!glad_glValidateProgram) { glad_glValidateProgram = &S_glValidateProgram; ++count; }
        if (!glad_glVertexAttrib1d) { glad_glVertexAttrib1d = &S_glVertexAttrib1d; ++count; }
        if (!glad_glVertexAttrib1dv) { glad_glVertexAttrib1dv = &S_glVertexAttrib1dv; ++count; }
        if (!glad_glVertexAttrib1f) { glad_glVertexAttrib1f = &S_glVertexAttrib1f; ++count; }
        if (!glad_glVertexAttrib1fv) { glad_glVertexAttrib1fv = &S_glVertexAttrib1fv; ++count; }
        if (!glad_glVertexAttrib1s) { glad_glVertexAttrib1s = &S_glVertexAttrib1s; ++count; }
        if (!glad_glVertexAttrib1sv) { glad_glVertexAttrib1sv = &S_glVertexAttrib1sv; ++count; }
        if (!glad_glVertexAttrib2d) { glad_glVertexAttrib2d = &S_glVertexAttrib2d; ++count; }
        if (!glad_glVertexAttrib2dv) { glad_glVertexAttrib2dv = &S_glVertexAttrib2dv; ++count; }
        if (!glad_glVertexAttrib2f) { glad_glVertexAttrib2f = &S_glVertexAttrib2f; ++count; }
        if (!glad_glVertexAttrib2fv) { glad_glVertexAttrib2fv = &S_glVertexAttrib2fv; ++count; }
        if (!glad_glVertexAttrib2s) { glad_glVertexAttrib2s = &S_glVertexAttrib2s; ++count; }
        if (!glad_glVertexAttrib2sv) { glad_glVertexAttrib2sv = &S_glVertexAttrib2sv; ++count; }
        if (!glad_glVertexAttrib3d) { glad_glVertexAttrib3d = &S_glVertexAttrib3d; ++count; }
        if (!glad_glVertexAttrib3dv) { glad_glVertexAttrib3dv = &S_glVertexAttrib3dv; ++count; }
        if (!glad_glVertexAttrib3f) { glad_glVertexAttrib3f = &S_glVertexAttrib3f; ++count; }
        if (!glad_glVertexAttrib3fv) { glad_glVertexAttrib3fv = &S_glVertexAttrib3fv; ++count; }
        if (!glad_glVertexAttrib3s) { glad_glVertexAttrib3s = &S_glVertexAttrib3s; ++count; }
        if (!glad_glVertexAttrib3sv) { glad_glVertexAttrib3sv = &S_glVertexAttrib3sv; ++count; }
        if (!glad_glVertexAttrib4Nbv) { glad_glVertexAttrib4Nbv = &S_glVertexAttrib4Nbv; ++count; }
        if (!glad_glVertexAttrib4Niv) { glad_glVertexAttrib4Niv = &S_glVertexAttrib4Niv; ++count; }
        if (!glad_glVertexAttrib4Nsv) { glad_glVertexAttrib4Nsv = &S_glVertexAttrib4Nsv; ++count; }
        if (!glad_glVertexAttrib4Nub) { glad_glVertexAttrib4Nub = &S_glVertexAttrib4Nub; ++count; }
        if (!glad_glVertexAttrib4Nubv) { glad_glVertexAttrib4Nubv = &S_glVertexAttrib4Nubv; ++count; }
        if (!glad_glVertexAttrib4Nuiv) { glad_glVertexAttrib4Nuiv = &S_glVertexAttrib4Nuiv; ++count; }
        if (!glad_glVertexAttrib4Nusv) { glad_glVertexAttrib4Nusv = &S_glVertexAttrib4Nusv; ++count; }
        if (!glad_glVertexAttrib4bv) { glad_glVertexAttrib4bv = &S_glVertexAttrib4bv; ++count; }
        if (!glad_glVertexAttrib4d) { glad_glVertexAttrib4d = &S_glVertexAttrib4d; ++count; }
        if (!glad_glVertexAttrib4dv) { glad_glVertexAttrib4dv = &S_glVertexAttrib4dv; ++count; }
        if (!glad_glVertexAttrib4f) { glad_glVertexAttrib4f = &S_glVertexAttrib4f; ++count; }
        if (!glad_glVertexAttrib4fv) { glad_glVertexAttrib4fv = &S_glVertexAttrib4fv; ++count; }
        if (!glad_glVertexAttrib4iv) { glad_glVertexAttrib4iv = &S_glVertexAttrib4iv; ++count; }
        if (!glad_glVertexAttrib4s) { glad_glVertexAttrib4s = &S_glVertexAttrib4s; ++count; }
        if (!glad_glVertexAttrib4sv) { glad_glVertexAttrib4sv = &S_glVertexAttrib4sv; ++count; }
        if (!glad_glVertexAttrib4ubv) { glad_glVertexAttrib4ubv = &S_glVertexAttrib4ubv; ++count; }
        if (!glad_glVertexAttrib4uiv) { glad_glVertexAttrib4uiv = &S_glVertexAttrib4uiv; ++count; }
        if (!glad_glVertexAttrib4usv) { glad_glVertexAttrib4usv = &S_glVertexAttrib4usv; ++count; }
        if (!glad_glVertexAttribPointer) { glad_glVertexAttribPointer = &S_glVertexAttribPointer; ++count; }
        if (!glad_glUniformMatrix2x3fv) { glad_glUniformMatrix2x3fv = &S_glUniformMatrix2x3fv; ++count; }
        if (!glad_glUniformMatrix3x2fv) { glad_glUniformMatrix3x2fv = &S_glUniformMatrix3x2fv; ++count; }
        if (!glad_glUniformMatrix2x4fv) { glad_glUniformMatrix2x4fv = &S_glUniformMatrix2x4fv; ++count; }
        if (!glad_glUniformMatrix4x2fv) { glad_glUniformMatrix4x2fv = &S_glUniformMatrix4x2fv; ++count; }
        if (!glad_glUniformMatrix3x4fv) { glad_glUniformMatrix3x4fv = &S_glUniformMatrix3x4fv; ++count; }
        if (!glad_glUniformMatrix4x3fv) { glad_glUniformMatrix4x3fv = &S_glUniformMatrix4x3fv; ++count; }
        if (!glad_glColorMaski) { glad_glColorMaski = &S_glColorMaski; ++count; }
        if (!glad_glGetBooleani_v) { glad_glGetBooleani_v = &S_glGetBooleani_v; ++count; }
        if (!glad_glGetIntegeri_v) { glad_glGetIntegeri_v = &S_glGetIntegeri_v; ++count; }
        if (!glad_glEnablei) { glad_glEnablei = &S_glEnablei; ++count; }
        if (!glad_glDisablei) { glad_glDisablei = &S_glDisablei; ++count; }
        if (!glad_glIsEnabledi) { glad_glIsEnabledi = &S_glIsEnabledi; ++count; }
        if (!glad_glBeginTransformFeedback) { glad_glBeginTransformFeedback = &S_glBeginTransformFeedback; ++count; }
        if (!glad_glEndTransformFeedback) { glad_glEndTransformFeedback = &S_glEndTransformFeedback; ++count; }
        if (!glad_glBindBufferRange) { glad_glBindBufferRange = &S_glBindBufferRange; ++count; }
        if (!glad_glBindBufferBase) { glad_glBindBufferBase = &S_glBindBufferBase; ++count; }
        if (!glad_glTransformFeedbackVaryings) { glad_glTransformFeedbackVaryings = &S_glTransformFeedbackVaryings; ++count; }
        if (!glad_glGetTransformFeedbackVarying) { glad_glGetTransformFeedbackVarying = &S_glGetTransformFeedbackVarying; ++count; }
        if (!glad_glClampColor) { glad_glClampColor = &S_glClampColor; ++count; }
        if (!glad_glBeginConditionalRender) { glad_glBeginConditionalRender = &S_glBeginConditionalRender; ++count; }
        if (!glad_glEndConditionalRender) { glad_glEndConditionalRender = &S_glEndConditionalRender; ++count; }
        if (!glad_glVertexAttribIPointer) { glad_glVertexAttribIPointer = &S_glVertexAttribIPointer; ++count; }
        if (!glad_glGetVertexAttribIiv) { glad_glGetVertexAttribIiv = &S_glGetVertexAttribIiv; ++count; }
        if (!glad_glGetVertexAttribIuiv) { glad_glGetVertexAttribIuiv = &S_glGetVertexAttribIuiv; ++count; }
        if (!glad_glVertexAttribI1i) { glad_glVertexAttribI1i = &S_glVertexAttribI1i; ++count; }
        if (!glad_glVertexAttribI2i) { glad_glVertexAttribI2i = &S_glVertexAttribI2i; ++count; }
        if (!glad_glVertexAttribI3i) { glad_glVertexAttribI3i = &S_glVertexAttribI3i; ++count; }
        if (!glad_glVertexAttribI4i) { glad_glVertexAttribI4i = &S_glVertexAttribI4i; ++count; }
        if (!glad_glVertexAttribI1ui) { glad_glVertexAttribI1ui = &S_glVertexAttribI1ui; ++count; }
        if (!glad_glVertexAttribI2ui) { glad_glVertexAttribI2ui = &S_glVertexAttribI2ui; ++count; }
        if (!glad_glVertexAttribI3ui) { glad_glVertexAttribI3ui = &S_glVertexAttribI3ui; ++count; }
        if (!glad_glVertexAttribI4ui) { glad_glVertexAttribI4ui = &S_glVertexAttribI4ui; ++count; }
        if (!glad_glVertexAttribI1iv) { glad_glVertexAttribI1iv = &S_glVertexAttribI1iv; ++count; }
        if (!glad_glVertexAttribI2iv) { glad_glVertexAttribI2iv = &S_glVertexAttribI2iv; ++count; }
        if (!glad_glVertexAttribI3iv) { glad_glVertexAttribI3iv = &S_glVertexAttribI3iv; ++count; }
        if (!glad_glVertexAttribI4iv) { glad_glVertexAttribI4iv = &S_glVertexAttribI4iv; ++count; }
        if (!glad_glVertexAttribI1uiv) { glad_glVertexAttribI1uiv = &S_glVertexAttribI1uiv; ++count; }
        if (!glad_glVertexAttribI2uiv) { glad_glVertexAttribI2uiv = &S_glVertexAttribI2uiv; ++count; }
        if (!glad_glVertexAttribI3uiv) { glad_glVertexAttribI3uiv = &S_glVertexAttribI3uiv; ++count; }
        if (!glad_glVertexAttribI4uiv) { glad_glVertexAttribI4uiv = &S_glVertexAttribI4uiv; ++count; }
        if (!glad_glVertexAttribI4bv) { glad_glVertexAttribI4bv = &S_glVertexAttribI4bv; ++count; }
        if (!glad_glVertexAttribI4sv) { glad_glVertexAttribI4sv = &S_glVertexAttribI4sv; ++count; }
        if (!glad_glVertexAttribI4ubv) { glad_glVertexAttribI4ubv = &S_glVertexAttribI4ubv; ++count; }
        if (!glad_glVertexAttribI4usv) { glad_glVertexAttribI4usv = &S_glVertexAttribI4usv; ++count; }
        if (!glad_glGetUniformuiv) { glad_glGetUniformuiv = &S_glGetUniformuiv; ++count; }
        if (!glad_glBindFragDataLocation) { glad_glBindFragDataLocation = &S_glBindFragDataLocation; ++count; }
        if (!glad_glGetFragDataLocation) { glad_glGetFragDataLocation = &S_glGetFragDataLocation; ++count; }
        if (!glad_glUniform1ui) { glad_glUniform1ui = &S_glUniform1ui; ++count; }
        if (!glad_glUniform2ui) { glad_glUniform2ui = &S_glUniform2ui; ++count; }
        if (!glad_glUniform3ui) { glad_glUniform3ui = &S_glUniform3ui; ++count; }
        if (!glad_glUniform4ui) { glad_glUniform4ui = &S_glUniform4ui; ++count; }
        if (!glad_glUniform1uiv) { glad_glUniform1uiv = &S_glUniform1uiv; ++count; }
        if (!glad_glUniform2uiv) { glad_glUniform2uiv = &S_glUniform2uiv; ++count; }
        if (!glad_glUniform3uiv) { glad_glUniform3uiv = &S_glUniform3uiv; ++count; }
        if (!glad_glUniform4uiv) { glad_glUniform4uiv = &S_glUniform4uiv; ++count; }
        if (!glad_glTexParameterIiv) { glad_glTexParameterIiv = &S_glTexParameterIiv; ++count; }
        if (!glad_glTexParameterIuiv) { glad_glTexParameterIuiv = &S_glTexParameterIuiv; ++count; }
        if (!glad_glGetTexParameterIiv) { glad_glGetTexParameterIiv = &S_glGetTexParameterIiv; ++count; }
        if (!glad_glGetTexParameterIuiv) { glad_glGetTexParameterIuiv = &S_glGetTexParameterIuiv; ++count; }
        if (!glad_glClearBufferiv) { glad_glClearBufferiv = &S_glClearBufferiv; ++count; }
        if (!glad_glClearBufferuiv) { glad_glClearBufferuiv = &S_glClearBufferuiv; ++count; }
        if (!glad_glClearBufferfv) { glad_glClearBufferfv = &S_glClearBufferfv; ++count; }
        if (!glad_glClearBufferfi) { glad_glClearBufferfi = &S_glClearBufferfi; ++count; }
        if (!glad_glGetStringi) { glad_glGetStringi = &S_glGetStringi; ++count; }
        if (!glad_glIsRenderbuffer) { glad_glIsRenderbuffer = &S_glIsRenderbuffer; ++count; }
        if (!glad_glBindRenderbuffer) { glad_glBindRenderbuffer = &S_glBindRenderbuffer; ++count; }
        if (!glad_glDeleteRenderbuffers) { glad_glDeleteRenderbuffers = &S_glDeleteRenderbuffers; ++count; }
        if (!glad_glGenRenderbuffers) { glad_glGenRenderbuffers = &S_glGenRenderbuffers; ++count; }
        if (!glad_glRenderbufferStorage) { glad_glRenderbufferStorage = &S_glRenderbufferStorage; ++count; }
        if (!glad_glGetRenderbufferParameteriv) { glad_glGetRenderbufferParameteriv = &S_glGetRenderbufferParameteriv; ++count; }
        if (!glad_glIsFramebuffer) { glad_glIsFramebuffer = &S_glIsFramebuffer; ++count; }
        if (!glad_glBindFramebuffer) { glad_glBindFramebuffer = &S_glBindFramebuffer; ++count; }
        if (!glad_glDeleteFramebuffers) { glad_glDeleteFramebuffers = &S_glDeleteFramebuffers; ++count; }
        if (!glad_glGenFramebuffers) { glad_glGenFramebuffers = &S_glGenFramebuffers; ++count; }
        if (!glad_glCheckFramebufferStatus) { glad_glCheckFramebufferStatus = &S_glCheckFramebufferStatus; ++count; }
        if (!glad_glFramebufferTexture1D) { glad_glFramebufferTexture1D = &S_glFramebufferTexture1D; ++count; }
        if (!glad_glFramebufferTexture2D) { glad_glFramebufferTexture2D = &S_glFramebufferTexture2D; ++count; }
        if (!glad_glFramebufferTexture3D) { glad_glFramebufferTexture3D = &S_glFramebufferTexture3D; ++count; }
        if (!glad_glFramebufferRenderbuffer) { glad_glFramebufferRenderbuffer = &S_glFramebufferRenderbuffer; ++count; }
        if (!glad_glGetFramebufferAttachmentParameteriv) { glad_glGetFramebufferAttachmentParameteriv = &S_glGetFramebufferAttachmentParameteriv; ++count; }
        if (!glad_glGenerateMipmap) { glad_glGenerateMipmap = &S_glGenerateMipmap; ++count; }
        if (!glad_glBlitFramebuffer) { glad_glBlitFramebuffer = &S_glBlitFramebuffer; ++count; }
        if (!glad_glRenderbufferStorageMultisample) { glad_glRenderbufferStorageMultisample = &S_glRenderbufferStorageMultisample; ++count; }
        if (!glad_glFramebufferTextureLayer) { glad_glFramebufferTextureLayer = &S_glFramebufferTextureLayer; ++count; }
        if (!glad_glMapBufferRange) { glad_glMapBufferRange = &S_glMapBufferRange; ++count; }
        if (!glad_glFlushMappedBufferRange) { glad_glFlushMappedBufferRange = &S_glFlushMappedBufferRange; ++count; }
        if (!glad_glBindVertexArray) { glad_glBindVertexArray = &S_glBindVertexArray; ++count; }
        if (!glad_glDeleteVertexArrays) { glad_glDeleteVertexArrays = &S_glDeleteVertexArrays; ++count; }
        if (!glad_glGenVertexArrays) { glad_glGenVertexArrays = &S_glGenVertexArrays; ++count; }
        if (!glad_glIsVertexArray) { glad_glIsVertexArray = &S_glIsVertexArray; ++count; }
        if (!glad_glDrawArraysInstanced) { glad_glDrawArraysInstanced = &S_glDrawArraysInstanced; ++count; }
        if (!glad_glDrawElementsInstanced) { glad_glDrawElementsInstanced = &S_glDrawElementsInstanced; ++count; }
        if (!glad_glTexBuffer) { glad_glTexBuffer = &S_glTexBuffer; ++count; }
        if (!glad_glPrimitiveRestartIndex) { glad_glPrimitiveRestartIndex = &S_glPrimitiveRestartIndex; ++count; }
        if (!glad_glCopyBufferSubData) { glad_glCopyBufferSubData = &S_glCopyBufferSubData; ++count; }
        if (!glad_glGetUniformIndices) { glad_glGetUniformIndices = &S_glGetUniformIndices; ++count; }
        if (!glad_glGetActiveUniformsiv) { glad_glGetActiveUniformsiv = &S_glGetActiveUniformsiv; ++count; }
        if (!glad_glGetActiveUniformName) { glad_glGetActiveUniformName = &S_glGetActiveUniformName; ++count; }
        if (!glad_glGetUniformBlockIndex) { glad_glGetUniformBlockIndex = &S_glGetUniformBlockIndex; ++count; }
        if (!glad_glGetActiveUniformBlockiv) { glad_glGetActiveUniformBlockiv = &S_glGetActiveUniformBlockiv; ++count; }
        if (!glad_glGetActiveUniformBlockName) { glad_glGetActiveUniformBlockName = &S_glGetActiveUniformBlockName; ++count; }
        if (!glad_glUniformBlockBinding) { glad_glUniformBlockBinding = &S_glUniformBlockBinding; ++count; }
        if (!glad_glDrawElementsBaseVertex) { glad_glDrawElementsBaseVertex = &S_glDrawElementsBaseVertex; ++count; }
        if (!glad_glDrawRangeElementsBaseVertex) { glad_glDrawRangeElementsBaseVertex = &S_glDrawRangeElementsBaseVertex; ++count; }
        if (!glad_glDrawElementsInstancedBaseVertex) { glad_glDrawElementsInstancedBaseVertex = &S_glDrawElementsInstancedBaseVertex; ++count; }
        if (!glad_glMultiDrawElementsBaseVertex) { glad_glMultiDrawElementsBaseVertex = &S_glMultiDrawElementsBaseVertex; ++count; }
        if (!glad_glProvokingVertex) { glad_glProvokingVertex = &S_glProvokingVertex; ++count; }
        if (!glad_glFenceSync) { glad_glFenceSync = &S_glFenceSync; ++count; }
        if (!glad_glIsSync) { glad_glIsSync = &S_glIsSync; ++count; }
        if (!glad_glDeleteSync) { glad_glDeleteSync = &S_glDeleteSync; ++count; }
        if (!glad_glClientWaitSync) { glad_glClientWaitSync = &S_glClientWaitSync; ++count; }
        if (!glad_glWaitSync) { glad_glWaitSync = &S_glWaitSync; ++count; }
        if (!glad_glGetInteger64v) { glad_glGetInteger64v = &S_glGetInteger64v; ++count; }
        if (!glad_glGetSynciv) { glad_glGetSynciv = &S_glGetSynciv; ++count; }
        if (!glad_glGetInteger64i_v) { glad_glGetInteger64i_v = &S_glGetInteger64i_v; ++count; }
        if (!glad_glGetBufferParameteri64v) { glad_glGetBufferParameteri64v = &S_glGetBufferParameteri64v; ++count; }
        if (!glad_glFramebufferTexture) { glad_glFramebufferTexture = &S_glFramebufferTexture; ++count; }
        if (!glad_glTexImage2DMultisample) { glad_glTexImage2DMultisample = &S_glTexImage2DMultisample; ++count; }
        if (!glad_glTexImage3DMultisample) { glad_glTexImage3DMultisample = &S_glTexImage3DMultisample; ++count; }
        if (!glad_glGetMultisamplefv) { glad_glGetMultisamplefv = &S_glGetMultisamplefv; ++count; }
        if (!glad_glSampleMaski) { glad_glSampleMaski = &S_glSampleMaski; ++count; }
        if (!glad_glBindFragDataLocationIndexed) { glad_glBindFragDataLocationIndexed = &S_glBindFragDataLocationIndexed; ++count; }
        if (!glad_glGetFragDataIndex) { glad_glGetFragDataIndex = &S_glGetFragDataIndex; ++count; }
        if (!glad_glGenSamplers) { glad_glGenSamplers = &S_glGenSamplers; ++count; }
        if (!glad_glDeleteSamplers) { glad_glDeleteSamplers = &S_glDeleteSamplers; ++count; }
        if (!glad_glIsSampler) { glad_glIsSampler = &S_glIsSampler; ++count; }
        if (!glad_glBindSampler) { glad_glBindSampler = &S_glBindSampler; ++count; }
        if (!glad_glSamplerParameteri) { glad_glSamplerParameteri = &S_glSamplerParameteri; ++count; }
        if (!glad_glSamplerParameteriv) { glad_glSamplerParameteriv = &S_glSamplerParameteriv; ++count; }
        if (!glad_glSamplerParameterf) { glad_glSamplerParameterf = &S_glSamplerParameterf; ++count; }
        if (!glad_glSamplerParameterfv) { glad_glSamplerParameterfv = &S_glSamplerParameterfv; ++count; }
        if (!glad_glSamplerParameterIiv) { glad_glSamplerParameterIiv = &S_glSamplerParameterIiv; ++count; }
        if (!glad_glSamplerParameterIuiv) { glad_glSamplerParameterIuiv = &S_glSamplerParameterIuiv; ++count; }
        if (!glad_glGetSamplerParameteriv) { glad_glGetSamplerParameteriv = &S_glGetSamplerParameteriv; ++count; }
        if (!glad_glGetSamplerParameterIiv) { glad_glGetSamplerParameterIiv = &S_glGetSamplerParameterIiv; ++count; }
        if (!glad_glGetSamplerParameterfv) { glad_glGetSamplerParameterfv = &S_glGetSamplerParameterfv; ++count; }
        if (!glad_glGetSamplerParameterIuiv) { glad_glGetSamplerParameterIuiv = &S_glGetSamplerParameterIuiv; ++count; }
        if (!glad_glQueryCounter) { glad_glQueryCounter = &S_glQueryCounter; ++count; }
        if (!glad_glGetQueryObjecti64v) { glad_glGetQueryObjecti64v = &S_glGetQueryObjecti64v; ++count; }
        if (!glad_glGetQueryObjectui64v) { glad_glGetQueryObjectui64v = &S_glGetQueryObjectui64v; ++count; }
        if (!glad_glVertexAttribDivisor) { glad_glVertexAttribDivisor = &S_glVertexAttribDivisor; ++count; }
        if (!glad_glVertexAttribP1ui) { glad_glVertexAttribP1ui = &S_glVertexAttribP1ui; ++count; }
        if (!glad_glVertexAttribP1uiv) { glad_glVertexAttribP1uiv = &S_glVertexAttribP1uiv; ++count; }
        if (!glad_glVertexAttribP2ui) { glad_glVertexAttribP2ui = &S_glVertexAttribP2ui; ++count; }
        if (!glad_glVertexAttribP2uiv) { glad_glVertexAttribP2uiv = &S_glVertexAttribP2uiv; ++count; }
        if (!glad_glVertexAttribP3ui) { glad_glVertexAttribP3ui = &S_glVertexAttribP3ui; ++count; }
        if (!glad_glVertexAttribP3uiv) { glad_glVertexAttribP3uiv = &S_glVertexAttribP3uiv; ++count; }
        if (!glad_glVertexAttribP4ui) { glad_glVertexAttribP4ui = &S_glVertexAttribP4ui; ++count; }
        if (!glad_glVertexAttribP4uiv) { glad_glVertexAttribP4uiv = &S_glVertexAttribP4uiv; ++count; }
        if (!glad_glVertexP2ui) { glad_glVertexP2ui = &S_glVertexP2ui; ++count; }
        if (!glad_glVertexP2uiv) { glad_glVertexP2uiv = &S_glVertexP2uiv; ++count; }
        if (!glad_glVertexP3ui) { glad_glVertexP3ui = &S_glVertexP3ui; ++count; }
        if (!glad_glVertexP3uiv) { glad_glVertexP3uiv = &S_glVertexP3uiv; ++count; }
        if (!glad_glVertexP4ui) { glad_glVertexP4ui = &S_glVertexP4ui; ++count; }
        if (!glad_glVertexP4uiv) { glad_glVertexP4uiv = &S_glVertexP4uiv; ++count; }
        if (!glad_glTexCoordP1ui) { glad_glTexCoordP1ui = &S_glTexCoordP1ui; ++count; }
        if (!glad_glTexCoordP1uiv) { glad_glTexCoordP1uiv = &S_glTexCoordP1uiv; ++count; }
        if (!glad_glTexCoordP2ui) { glad_glTexCoordP2ui = &S_glTexCoordP2ui; ++count; }
        if (!glad_glTexCoordP2uiv) { glad_glTexCoordP2uiv = &S_glTexCoordP2uiv; ++count; }
        if (!glad_glTexCoordP3ui) { glad_glTexCoordP3ui = &S_glTexCoordP3ui; ++count; }
        if (!glad_glTexCoordP3uiv) { glad_glTexCoordP3uiv = &S_glTexCoordP3uiv; ++count; }
        if (!glad_glTexCoordP4ui) { glad_glTexCoordP4ui = &S_glTexCoordP4ui; ++count; }
        if (!glad_glTexCoordP4uiv) { glad_glTexCoordP4uiv = &S_glTexCoordP4uiv; ++count; }
        if (!glad_glMultiTexCoordP1ui) { glad_glMultiTexCoordP1ui = &S_glMultiTexCoordP1ui; ++count; }
        if (!glad_glMultiTexCoordP1uiv) { glad_glMultiTexCoordP1uiv = &S_glMultiTexCoordP1uiv; ++count; }
        if (!glad_glMultiTexCoordP2ui) { glad_glMultiTexCoordP2ui = &S_glMultiTexCoordP2ui; ++count; }
        if (!glad_glMultiTexCoordP2uiv) { glad_glMultiTexCoordP2uiv = &S_glMultiTexCoordP2uiv; ++count; }
        if (!glad_glMultiTexCoordP3ui) { glad_glMultiTexCoordP3ui = &S_glMultiTexCoordP3ui; ++count; }
        if (!glad_glMultiTexCoordP3uiv) { glad_glMultiTexCoordP3uiv = &S_glMultiTexCoordP3uiv; ++count; }
        if (!glad_glMultiTexCoordP4ui) { glad_glMultiTexCoordP4ui = &S_glMultiTexCoordP4ui; ++count; }
        if (!glad_glMultiTexCoordP4uiv) { glad_glMultiTexCoordP4uiv = &S_glMultiTexCoordP4uiv; ++count; }
        if (!glad_glNormalP3ui) { glad_glNormalP3ui = &S_glNormalP3ui; ++count; }
        if (!glad_glNormalP3uiv) { glad_glNormalP3uiv = &S_glNormalP3uiv; ++count; }
        if (!glad_glColorP3ui) { glad_glColorP3ui = &S_glColorP3ui; ++count; }
        if (!glad_glColorP3uiv) { glad_glColorP3uiv = &S_glColorP3uiv; ++count; }
        if (!glad_glColorP4ui) { glad_glColorP4ui = &S_glColorP4ui; ++count; }
        if (!glad_glColorP4uiv) { glad_glColorP4uiv = &S_glColorP4uiv; ++count; }
        if (!glad_glSecondaryColorP3ui) { glad_glSecondaryColorP3ui = &S_glSecondaryColorP3ui; ++count; }
        if (!glad_glSecondaryColorP3uiv) { glad_glSecondaryColorP3uiv = &S_glSecondaryColorP3uiv; ++count; }
        if (!glad_glMinSampleShading) { glad_glMinSampleShading = &S_glMinSampleShading; ++count; }
        if (!glad_glBlendEquationi) { glad_glBlendEquationi = &S_glBlendEquationi; ++count; }
        if (!glad_glBlendEquationSeparatei) { glad_glBlendEquationSeparatei = &S_glBlendEquationSeparatei; ++count; }
        if (!glad_glBlendFunci) { glad_glBlendFunci = &S_glBlendFunci; ++count; }
        if (!glad_glBlendFuncSeparatei) { glad_glBlendFuncSeparatei = &S_glBlendFuncSeparatei; ++count; }
        if (!glad_glDrawArraysIndirect) { glad_glDrawArraysIndirect = &S_glDrawArraysIndirect; ++count; }
        if (!glad_glDrawElementsIndirect) { glad_glDrawElementsIndirect = &S_glDrawElementsIndirect; ++count; }
        if (!glad_glUniform1d) { glad_glUniform1d = &S_glUniform1d; ++count; }
        if (!glad_glUniform2d) { glad_glUniform2d = &S_glUniform2d; ++count; }
        if (!glad_glUniform3d) { glad_glUniform3d = &S_glUniform3d; ++count; }
        if (!glad_glUniform4d) { glad_glUniform4d = &S_glUniform4d; ++count; }
        if (!glad_glUniform1dv) { glad_glUniform1dv = &S_glUniform1dv; ++count; }
        if (!glad_glUniform2dv) { glad_glUniform2dv = &S_glUniform2dv; ++count; }
        if (!glad_glUniform3dv) { glad_glUniform3dv = &S_glUniform3dv; ++count; }
        if (!glad_glUniform4dv) { glad_glUniform4dv = &S_glUniform4dv; ++count; }
        if (!glad_glUniformMatrix2dv) { glad_glUniformMatrix2dv = &S_glUniformMatrix2dv; ++count; }
        if (!glad_glUniformMatrix3dv) { glad_glUniformMatrix3dv = &S_glUniformMatrix3dv; ++count; }
        if (!glad_glUniformMatrix4dv) { glad_glUniformMatrix4dv = &S_glUniformMatrix4dv; ++count; }
        if (!glad_glUniformMatrix2x3dv) { glad_glUniformMatrix2x3dv = &S_glUniformMatrix2x3dv; ++count; }
        if (!glad_glUniformMatrix2x4dv) { glad_glUniformMatrix2x4dv = &S_glUniformMatrix2x4dv; ++count; }
        if (!glad_glUniformMatrix3x2dv) { glad_glUniformMatrix3x2dv = &S_glUniformMatrix3x2dv; ++count; }
        if (!glad_glUniformMatrix3x4dv) { glad_glUniformMatrix3x4dv = &S_glUniformMatrix3x4dv; ++count; }
        if (!glad_glUniformMatrix4x2dv) { glad_glUniformMatrix4x2dv = &S_glUniformMatrix4x2dv; ++count; }
        if (!glad_glUniformMatrix4x3dv) { glad_glUniformMatrix4x3dv = &S_glUniformMatrix4x3dv; ++count; }
        if (!glad_glGetUniformdv) { glad_glGetUniformdv = &S_glGetUniformdv; ++count; }
        if (!glad_glGetSubroutineUniformLocation) { glad_glGetSubroutineUniformLocation = &S_glGetSubroutineUniformLocation; ++count; }
        if (!glad_glGetSubroutineIndex) { glad_glGetSubroutineIndex = &S_glGetSubroutineIndex; ++count; }
        if (!glad_glGetActiveSubroutineUniformiv) { glad_glGetActiveSubroutineUniformiv = &S_glGetActiveSubroutineUniformiv; ++count; }
        if (!glad_glGetActiveSubroutineUniformName) { glad_glGetActiveSubroutineUniformName = &S_glGetActiveSubroutineUniformName; ++count; }
        if (!glad_glGetActiveSubroutineName) { glad_glGetActiveSubroutineName = &S_glGetActiveSubroutineName; ++count; }
        if (!glad_glUniformSubroutinesuiv) { glad_glUniformSubroutinesuiv = &S_glUniformSubroutinesuiv; ++count; }
        if (!glad_glGetUniformSubroutineuiv) { glad_glGetUniformSubroutineuiv = &S_glGetUniformSubroutineuiv; ++count; }
        if (!glad_glGetProgramStageiv) { glad_glGetProgramStageiv = &S_glGetProgramStageiv; ++count; }
        if (!glad_glPatchParameteri) { glad_glPatchParameteri = &S_glPatchParameteri; ++count; }
        if (!glad_glPatchParameterfv) { glad_glPatchParameterfv = &S_glPatchParameterfv; ++count; }
        if (!glad_glBindTransformFeedback) { glad_glBindTransformFeedback = &S_glBindTransformFeedback; ++count; }
        if (!glad_glDeleteTransformFeedbacks) { glad_glDeleteTransformFeedbacks = &S_glDeleteTransformFeedbacks; ++count; }
        if (!glad_glGenTransformFeedbacks) { glad_glGenTransformFeedbacks = &S_glGenTransformFeedbacks; ++count; }
        if (!glad_glIsTransformFeedback) { glad_glIsTransformFeedback = &S_glIsTransformFeedback; ++count; }
        if (!glad_glPauseTransformFeedback) { glad_glPauseTransformFeedback = &S_glPauseTransformFeedback; ++count; }
        if (!glad_glResumeTransformFeedback) { glad_glResumeTransformFeedback = &S_glResumeTransformFeedback; ++count; }
        if (!glad_glDrawTransformFeedback) { glad_glDrawTransformFeedback = &S_glDrawTransformFeedback; ++count; }
        if (!glad_glDrawTransformFeedbackStream) { glad_glDrawTransformFeedbackStream = &S_glDrawTransformFeedbackStream; ++count; }
        if (!glad_glBeginQueryIndexed) { glad_glBeginQueryIndexed = &S_glBeginQueryIndexed; ++count; }
        if (!glad_glEndQueryIndexed) { glad_glEndQueryIndexed = &S_glEndQueryIndexed; ++count; }
        if (!glad_glGetQueryIndexediv) { glad_glGetQueryIndexediv = &S_glGetQueryIndexediv; ++count; }
        if (!glad_glReleaseShaderCompiler) { glad_glReleaseShaderCompiler = &S_glReleaseShaderCompiler; ++count; }
        if (!glad_glShaderBinary) { glad_glShaderBinary = &S_glShaderBinary; ++count; }
        if (!glad_glGetShaderPrecisionFormat) { glad_glGetShaderPrecisionFormat = &S_glGetShaderPrecisionFormat; ++count; }
        if (!glad_glDepthRangef) { glad_glDepthRangef = &S_glDepthRangef; ++count; }
        if (!glad_glClearDepthf) { glad_glClearDepthf = &S_glClearDepthf; ++count; }
        if (!glad_glGetProgramBinary) { glad_glGetProgramBinary = &S_glGetProgramBinary; ++count; }
        if (!glad_glProgramBinary) { glad_glProgramBinary = &S_glProgramBinary; ++count; }
        if (!glad_glProgramParameteri) { glad_glProgramParameteri = &S_glProgramParameteri; ++count; }
        if (!glad_glUseProgramStages) { glad_glUseProgramStages = &S_glUseProgramStages; ++count; }
        if (!glad_glActiveShaderProgram) { glad_glActiveShaderProgram = &S_glActiveShaderProgram; ++count; }
        if (!glad_glCreateShaderProgramv) { glad_glCreateShaderProgramv = &S_glCreateShaderProgramv; ++count; }
        if (!glad_glBindProgramPipeline) { glad_glBindProgramPipeline = &S_glBindProgramPipeline; ++count; }
        if (!glad_glDeleteProgramPipelines) { glad_glDeleteProgramPipelines = &S_glDeleteProgramPipelines; ++count; }
        if (!glad_glGenProgramPipelines) { glad_glGenProgramPipelines = &S_glGenProgramPipelines; ++count; }
        if (!glad_glIsProgramPipeline) { glad_glIsProgramPipeline = &S_glIsProgramPipeline; ++count; }
        if (!glad_glGetProgramPipelineiv) { glad_glGetProgramPipelineiv = &S_glGetProgramPipelineiv; ++count; }
        if (!glad_glProgramUniform1i) { glad_glProgramUniform1i = &S_glProgramUniform1i; ++count; }
        if (!glad_glProgramUniform1iv) { glad_glProgramUniform1iv = &S_glProgramUniform1iv; ++count; }
        if (!glad_glProgramUniform1f) { glad_glProgramUniform1f = &S_glProgramUniform1f; ++count; }
        if (!glad_glProgramUniform1fv) { glad_glProgramUniform1fv = &S_glProgramUniform1fv; ++count; }
        if (!glad_glProgramUniform1d) { glad_glProgramUniform1d = &S_glProgramUniform1d; ++count; }
        if (!glad_glProgramUniform1dv) { glad_glProgramUniform1dv = &S_glProgramUniform1dv; ++count; }
        if (!glad_glProgramUniform1ui) { glad_glProgramUniform1ui = &S_glProgramUniform1ui; ++count; }
        if (!glad_glProgramUniform1uiv) { glad_glProgramUniform1uiv = &S_glProgramUniform1uiv; ++count; }
        if (!glad_glProgramUniform2i) { glad_glProgramUniform2i = &S_glProgramUniform2i; ++count; }
        if (!glad_glProgramUniform2iv) { glad_glProgramUniform2iv = &S_glProgramUniform2iv; ++count; }
        if (!glad_glProgramUniform2f) { glad_glProgramUniform2f = &S_glProgramUniform2f; ++count; }
        if (!glad_glProgramUniform2fv) { glad_glProgramUniform2fv = &S_glProgramUniform2fv; ++count; }
        if (!glad_glProgramUniform2d) { glad_glProgramUniform2d = &S_glProgramUniform2d; ++count; }
        if (!glad_glProgramUniform2dv) { glad_glProgramUniform2dv = &S_glProgramUniform2dv; ++count; }
        if (!glad_glProgramUniform2ui) { glad_glProgramUniform2ui = &S_glProgramUniform2ui; ++count; }
        if (!glad_glProgramUniform2uiv) { glad_glProgramUniform2uiv = &S_glProgramUniform2uiv; ++count; }
        if (!glad_glProgramUniform3i) { glad_glProgramUniform3i = &S_glProgramUniform3i; ++count; }
        if (!glad_glProgramUniform3iv) { glad_glProgramUniform3iv = &S_glProgramUniform3iv; ++count; }
        if (!glad_glProgramUniform3f) { glad_glProgramUniform3f = &S_glProgramUniform3f; ++count; }
        if (!glad_glProgramUniform3fv) { glad_glProgramUniform3fv = &S_glProgramUniform3fv; ++count; }
        if (!glad_glProgramUniform3d) { glad_glProgramUniform3d = &S_glProgramUniform3d; ++count; }
        if (!glad_glProgramUniform3dv) { glad_glProgramUniform3dv = &S_glProgramUniform3dv; ++count; }
        if (!glad_glProgramUniform3ui) { glad_glProgramUniform3ui = &S_glProgramUniform3ui; ++count; }
        if (!glad_glProgramUniform3uiv) { glad_glProgramUniform3uiv = &S_glProgramUniform3uiv; ++count; }
        if (!glad_glProgramUniform4i) { glad_glProgramUniform4i = &S_glProgramUniform4i; ++count; }
        if (!glad_glProgramUniform4iv) { glad_glProgramUniform4iv = &S_glProgramUniform4iv; ++count; }
        if (!glad_glProgramUniform4f) { glad_glProgramUniform4f = &S_glProgramUniform4f; ++count; }
        if (!glad_glProgramUniform4fv) { glad_glProgramUniform4fv = &S_glProgramUniform4fv; ++count; }
        if (!glad_glProgramUniform4d) { glad_glProgramUniform4d = &S_glProgramUniform4d; ++count; }
        if (!glad_glProgramUniform4dv) { glad_glProgramUniform4dv = &S_glProgramUniform4dv; ++count; }
        if (!glad_glProgramUniform4ui) { glad_glProgramUniform4ui = &S_glProgramUniform4ui; ++count; }
        if (!glad_glProgramUniform4uiv) { glad_glProgramUniform4uiv = &S_glProgramUniform4uiv; ++count; }
        if (!glad_glProgramUniformMatrix2fv) { glad_glProgramUniformMatrix2fv = &S_glProgramUniformMatrix2fv; ++count; }
        if (!glad_glProgramUniformMatrix3fv) { glad_glProgramUniformMatrix3fv = &S_glProgramUniformMatrix3fv; ++count; }
        if (!glad_glProgramUniformMatrix4fv) { glad_glProgramUniformMatrix4fv = &S_glProgramUniformMatrix4fv; ++count; }
        if (!glad_glProgramUniformMatrix2dv) { glad_glProgramUniformMatrix2dv = &S_glProgramUniformMatrix2dv; ++count; }
        if (!glad_glProgramUniformMatrix3dv) { glad_glProgramUniformMatrix3dv = &S_glProgramUniformMatrix3dv; ++count; }
        if (!glad_glProgramUniformMatrix4dv) { glad_glProgramUniformMatrix4dv = &S_glProgramUniformMatrix4dv; ++count; }
        if (!glad_glProgramUniformMatrix2x3fv) { glad_glProgramUniformMatrix2x3fv = &S_glProgramUniformMatrix2x3fv; ++count; }
        if (!glad_glProgramUniformMatrix3x2fv) { glad_glProgramUniformMatrix3x2fv = &S_glProgramUniformMatrix3x2fv; ++count; }
        if (!glad_glProgramUniformMatrix2x4fv) { glad_glProgramUniformMatrix2x4fv = &S_glProgramUniformMatrix2x4fv; ++count; }
        if (!glad_glProgramUniformMatrix4x2fv) { glad_glProgramUniformMatrix4x2fv = &S_glProgramUniformMatrix4x2fv; ++count; }
        if (!glad_glProgramUniformMatrix3x4fv) { glad_glProgramUniformMatrix3x4fv = &S_glProgramUniformMatrix3x4fv; ++count; }
        if (!glad_glProgramUniformMatrix4x3fv) { glad_glProgramUniformMatrix4x3fv = &S_glProgramUniformMatrix4x3fv; ++count; }
        if (!glad_glProgramUniformMatrix2x3dv) { glad_glProgramUniformMatrix2x3dv = &S_glProgramUniformMatrix2x3dv; ++count; }
        if (!glad_glProgramUniformMatrix3x2dv) { glad_glProgramUniformMatrix3x2dv = &S_glProgramUniformMatrix3x2dv; ++count; }
        if (!glad_glProgramUniformMatrix2x4dv) { glad_glProgramUniformMatrix2x4dv = &S_glProgramUniformMatrix2x4dv; ++count; }
        if (!glad_glProgramUniformMatrix4x2dv) { glad_glProgramUniformMatrix4x2dv = &S_glProgramUniformMatrix4x2dv; ++count; }
        if (!glad_glProgramUniformMatrix3x4dv) { glad_glProgramUniformMatrix3x4dv = &S_glProgramUniformMatrix3x4dv; ++count; }
        if (!glad_glProgramUniformMatrix4x3dv) { glad_glProgramUniformMatrix4x3dv = &S_glProgramUniformMatrix4x3dv; ++count; }
        if (!glad_glValidateProgramPipeline) { glad_glValidateProgramPipeline = &S_glValidateProgramPipeline; ++count; }
        if (!glad_glGetProgramPipelineInfoLog) { glad_glGetProgramPipelineInfoLog = &S_glGetProgramPipelineInfoLog; ++count; }
        if (!glad_glVertexAttribL1d) { glad_glVertexAttribL1d = &S_glVertexAttribL1d; ++count; }
        if (!glad_glVertexAttribL2d) { glad_glVertexAttribL2d = &S_glVertexAttribL2d; ++count; }
        if (!glad_glVertexAttribL3d) { glad_glVertexAttribL3d = &S_glVertexAttribL3d; ++count; }
        if (!glad_glVertexAttribL4d) { glad_glVertexAttribL4d = &S_glVertexAttribL4d; ++count; }
        if (!glad_glVertexAttribL1dv) { glad_glVertexAttribL1dv = &S_glVertexAttribL1dv; ++count; }
        if (!glad_glVertexAttribL2dv) { glad_glVertexAttribL2dv = &S_glVertexAttribL2dv; ++count; }
        if (!glad_glVertexAttribL3dv) { glad_glVertexAttribL3dv = &S_glVertexAttribL3dv; ++count; }
        if (!glad_glVertexAttribL4dv) { glad_glVertexAttribL4dv = &S_glVertexAttribL4dv; ++count; }
        if (!glad_glVertexAttribLPointer) { glad_glVertexAttribLPointer = &S_glVertexAttribLPointer; ++count; }
        if (!glad_glGetVertexAttribLdv) { glad_glGetVertexAttribLdv = &S_glGetVertexAttribLdv; ++count; }
        if (!glad_glViewportArrayv) { glad_glViewportArrayv = &S_glViewportArrayv; ++count; }
        if (!glad_glViewportIndexedf) { glad_glViewportIndexedf = &S_glViewportIndexedf; ++count; }
        if (!glad_glViewportIndexedfv) { glad_glViewportIndexedfv = &S_glViewportIndexedfv; ++count; }
        if (!glad_glScissorArrayv) { glad_glScissorArrayv = &S_glScissorArrayv; ++count; }
        if (!glad_glScissorIndexed) { glad_glScissorIndexed = &S_glScissorIndexed; ++count; }
        if (!glad_glScissorIndexedv) { glad_glScissorIndexedv = &S_glScissorIndexedv; ++count; }
        if (!glad_glDepthRangeArrayv) { glad_glDepthRangeArrayv = &S_glDepthRangeArrayv; ++count; }
        if (!glad_glDepthRangeIndexed) { glad_glDepthRangeIndexed = &S_glDepthRangeIndexed; ++count; }
        if (!glad_glGetFloati_v) { glad_glGetFloati_v = &S_glGetFloati_v; ++count; }
        if (!glad_glGetDoublei_v) { glad_glGetDoublei_v = &S_glGetDoublei_v; ++count; }
        if (!glad_glDrawArraysInstancedBaseInstance) { glad_glDrawArraysInstancedBaseInstance = &S_glDrawArraysInstancedBaseInstance; ++count; }
        if (!glad_glDrawElementsInstancedBaseInstance) { glad_glDrawElementsInstancedBaseInstance = &S_glDrawElementsInstancedBaseInstance; ++count; }
        if (!glad_glDrawElementsInstancedBaseVertexBaseInstance) { glad_glDrawElementsInstancedBaseVertexBaseInstance = &S_glDrawElementsInstancedBaseVertexBaseInstance; ++count; }
        if (!glad_glGetInternalformativ) { glad_glGetInternalformativ = &S_glGetInternalformativ; ++count; }
        if (!glad_glGetActiveAtomicCounterBufferiv) { glad_glGetActiveAtomicCounterBufferiv = &S_glGetActiveAtomicCounterBufferiv; ++count; }
        if (!glad_glBindImageTexture) { glad_glBindImageTexture = &S_glBindImageTexture; ++count; }
        if (!glad_glMemoryBarrier) { glad_glMemoryBarrier = &S_glMemoryBarrier; ++count; }
        if (!glad_glTexStorage1D) { glad_glTexStorage1D = &S_glTexStorage1D; ++count; }
        if (!glad_glTexStorage2D) { glad_glTexStorage2D = &S_glTexStorage2D; ++count; }
        if (!glad_glTexStorage3D) { glad_glTexStorage3D = &S_glTexStorage3D; ++count; }
        if (!glad_glDrawTransformFeedbackInstanced) { glad_glDrawTransformFeedbackInstanced = &S_glDrawTransformFeedbackInstanced; ++count; }
        if (!glad_glDrawTransformFeedbackStreamInstanced) { glad_glDrawTransformFeedbackStreamInstanced = &S_glDrawTransformFeedbackStreamInstanced; ++count; }
        if (!glad_glClearBufferData) { glad_glClearBufferData = &S_glClearBufferData; ++count; }
        if (!glad_glClearBufferSubData) { glad_glClearBufferSubData = &S_glClearBufferSubData; ++count; }
        if (!glad_glDispatchCompute) { glad_glDispatchCompute = &S_glDispatchCompute; ++count; }
        if (!glad_glDispatchComputeIndirect) { glad_glDispatchComputeIndirect = &S_glDispatchComputeIndirect; ++count; }
        if (!glad_glCopyImageSubData) { glad_glCopyImageSubData = &S_glCopyImageSubData; ++count; }
        if (!glad_glFramebufferParameteri) { glad_glFramebufferParameteri = &S_glFramebufferParameteri; ++count; }
        if (!glad_glGetFramebufferParameteriv) { glad_glGetFramebufferParameteriv = &S_glGetFramebufferParameteriv; ++count; }
        if (!glad_glGetInternalformati64v) { glad_glGetInternalformati64v = &S_glGetInternalformati64v; ++count; }
        if (!glad_glInvalidateTexSubImage) { glad_glInvalidateTexSubImage = &S_glInvalidateTexSubImage; ++count; }
        if (!glad_glInvalidateTexImage) { glad_glInvalidateTexImage = &S_glInvalidateTexImage; ++count; }
        if (!glad_glInvalidateBufferSubData) { glad_glInvalidateBufferSubData = &S_glInvalidateBufferSubData; ++count; }
        if (!glad_glInvalidateBufferData) { glad_glInvalidateBufferData = &S_glInvalidateBufferData; ++count; }
        if (!glad_glInvalidateFramebuffer) { glad_glInvalidateFramebuffer = &S_glInvalidateFramebuffer; ++count; }
        if (!glad_glInvalidateSubFramebuffer) { glad_glInvalidateSubFramebuffer = &S_glInvalidateSubFramebuffer; ++count; }
        if (!glad_glMultiDrawArraysIndirect) { glad_glMultiDrawArraysIndirect = &S_glMultiDrawArraysIndirect; ++count; }
        if (!glad_glMultiDrawElementsIndirect) { glad_glMultiDrawElementsIndirect = &S_glMultiDrawElementsIndirect; ++count; }
        if (!glad_glGetProgramInterfaceiv) { glad_glGetProgramInterfaceiv = &S_glGetProgramInterfaceiv; ++count; }
        if (!glad_glGetProgramResourceIndex) { glad_glGetProgramResourceIndex = &S_glGetProgramResourceIndex; ++count; }
        if (!glad_glGetProgramResourceName) { glad_glGetProgramResourceName = &S_glGetProgramResourceName; ++count; }
        if (!glad_glGetProgramResourceiv) { glad_glGetProgramResourceiv = &S_glGetProgramResourceiv; ++count; }
        if (!glad_glGetProgramResourceLocation) { glad_glGetProgramResourceLocation = &S_glGetProgramResourceLocation; ++count; }
        if (!glad_glGetProgramResourceLocationIndex) { glad_glGetProgramResourceLocationIndex = &S_glGetProgramResourceLocationIndex; ++count; }
        if (!glad_glShaderStorageBlockBinding) { glad_glShaderStorageBlockBinding = &S_glShaderStorageBlockBinding; ++count; }
        if (!glad_glTexBufferRange) { glad_glTexBufferRange = &S_glTexBufferRange; ++count; }
        if (!glad_glTexStorage2DMultisample) { glad_glTexStorage2DMultisample = &S_glTexStorage2DMultisample; ++count; }
        if (!glad_glTexStorage3DMultisample) { glad_glTexStorage3DMultisample = &S_glTexStorage3DMultisample; ++count; }
        if (!glad_glTextureView) { glad_glTextureView = &S_glTextureView; ++count; }
        if (!glad_glBindVertexBuffer) { glad_glBindVertexBuffer = &S_glBindVertexBuffer; ++count; }
        if (!glad_glVertexAttribFormat) { glad_glVertexAttribFormat = &S_glVertexAttribFormat; ++count; }
        if (!glad_glVertexAttribIFormat) { glad_glVertexAttribIFormat = &S_glVertexAttribIFormat; ++count; }
        if (!glad_glVertexAttribLFormat) { glad_glVertexAttribLFormat = &S_glVertexAttribLFormat; ++count; }
        if (!glad_glVertexAttribBinding) { glad_glVertexAttribBinding = &S_glVertexAttribBinding; ++count; }
        if (!glad_glVertexBindingDivisor) { glad_glVertexBindingDivisor = &S_glVertexBindingDivisor; ++count; }
        if (!glad_glDebugMessageControl) { glad_glDebugMessageControl = &S_glDebugMessageControl; ++count; }
        if (!glad_glDebugMessageInsert) { glad_glDebugMessageInsert = &S_glDebugMessageInsert; ++count; }
        if (!glad_glDebugMessageCallback) { glad_glDebugMessageCallback = &S_glDebugMessageCallback; ++count; }
        if (!glad_glGetDebugMessageLog) { glad_glGetDebugMessageLog = &S_glGetDebugMessageLog; ++count; }
        if (!glad_glPushDebugGroup) { glad_glPushDebugGroup = &S_glPushDebugGroup; ++count; }
        if (!glad_glPopDebugGroup) { glad_glPopDebugGroup = &S_glPopDebugGroup; ++count; }
        if (!glad_glObjectLabel) { glad_glObjectLabel = &S_glObjectLabel; ++count; }
        if (!glad_glGetObjectLabel) { glad_glGetObjectLabel = &S_glGetObjectLabel; ++count; }
        if (!glad_glObjectPtrLabel) { glad_glObjectPtrLabel = &S_glObjectPtrLabel; ++count; }
        if (!glad_glGetObjectPtrLabel) { glad_glGetObjectPtrLabel = &S_glGetObjectPtrLabel; ++count; }
        if (!glad_glGetPointerv) { glad_glGetPointerv = &S_glGetPointerv; ++count; }
        if (!glad_glBufferStorage) { glad_glBufferStorage = &S_glBufferStorage; ++count; }
        if (!glad_glClearTexImage) { glad_glClearTexImage = &S_glClearTexImage; ++count; }
        if (!glad_glClearTexSubImage) { glad_glClearTexSubImage = &S_glClearTexSubImage; ++count; }
        if (!glad_glBindBuffersBase) { glad_glBindBuffersBase = &S_glBindBuffersBase; ++count; }
        if (!glad_glBindBuffersRange) { glad_glBindBuffersRange = &S_glBindBuffersRange; ++count; }
        if (!glad_glBindTextures) { glad_glBindTextures = &S_glBindTextures; ++count; }
        if (!glad_glBindSamplers) { glad_glBindSamplers = &S_glBindSamplers; ++count; }
        if (!glad_glBindImageTextures) { glad_glBindImageTextures = &S_glBindImageTextures; ++count; }
        if (!glad_glBindVertexBuffers) { glad_glBindVertexBuffers = &S_glBindVertexBuffers; ++count; }
        if (!glad_glClipControl) { glad_glClipControl = &S_glClipControl; ++count; }
        if (!glad_glCreateTransformFeedbacks) { glad_glCreateTransformFeedbacks = &S_glCreateTransformFeedbacks; ++count; }
        if (!glad_glTransformFeedbackBufferBase) { glad_glTransformFeedbackBufferBase = &S_glTransformFeedbackBufferBase; ++count; }
        if (!glad_glTransformFeedbackBufferRange) { glad_glTransformFeedbackBufferRange = &S_glTransformFeedbackBufferRange; ++count; }
        if (!glad_glGetTransformFeedbackiv) { glad_glGetTransformFeedbackiv = &S_glGetTransformFeedbackiv; ++count; }
        if (!glad_glGetTransformFeedbacki_v) { glad_glGetTransformFeedbacki_v = &S_glGetTransformFeedbacki_v; ++count; }
        if (!glad_glGetTransformFeedbacki64_v) { glad_glGetTransformFeedbacki64_v = &S_glGetTransformFeedbacki64_v; ++count; }
        if (!glad_glCreateBuffers) { glad_glCreateBuffers = &S_glCreateBuffers; ++count; }
        if (!glad_glNamedBufferStorage) { glad_glNamedBufferStorage = &S_glNamedBufferStorage; ++count; }
        if (!glad_glNamedBufferData) { glad_glNamedBufferData = &S_glNamedBufferData; ++count; }
        if (!glad_glNamedBufferSubData) { glad_glNamedBufferSubData = &S_glNamedBufferSubData; ++count; }
        if (!glad_glCopyNamedBufferSubData) { glad_glCopyNamedBufferSubData = &S_glCopyNamedBufferSubData; ++count; }
        if (!glad_glClearNamedBufferData) { glad_glClearNamedBufferData = &S_glClearNamedBufferData; ++count; }
        if (!glad_glClearNamedBufferSubData) { glad_glClearNamedBufferSubData = &S_glClearNamedBufferSubData; ++count; }
        if (!glad_glMapNamedBuffer) { glad_glMapNamedBuffer = &S_glMapNamedBuffer; ++count; }
        if (!glad_glMapNamedBufferRange) { glad_glMapNamedBufferRange = &S_glMapNamedBufferRange; ++count; }
        if (!glad_glUnmapNamedBuffer) { glad_glUnmapNamedBuffer = &S_glUnmapNamedBuffer; ++count; }
        if (!glad_glFlushMappedNamedBufferRange) { glad_glFlushMappedNamedBufferRange = &S_glFlushMappedNamedBufferRange; ++count; }
        if (!glad_glGetNamedBufferParameteriv) { glad_glGetNamedBufferParameteriv = &S_glGetNamedBufferParameteriv; ++count; }
        if (!glad_glGetNamedBufferParameteri64v) { glad_glGetNamedBufferParameteri64v = &S_glGetNamedBufferParameteri64v; ++count; }
        if (!glad_glGetNamedBufferPointerv) { glad_glGetNamedBufferPointerv = &S_glGetNamedBufferPointerv; ++count; }
        if (!glad_glGetNamedBufferSubData) { glad_glGetNamedBufferSubData = &S_glGetNamedBufferSubData; ++count; }
        if (!glad_glCreateFramebuffers) { glad_glCreateFramebuffers = &S_glCreateFramebuffers; ++count; }
        if (!glad_glNamedFramebufferRenderbuffer) { glad_glNamedFramebufferRenderbuffer = &S_glNamedFramebufferRenderbuffer; ++count; }
        if (!glad_glNamedFramebufferParameteri) { glad_glNamedFramebufferParameteri = &S_glNamedFramebufferParameteri; ++count; }
        if (!glad_glNamedFramebufferTexture) { glad_glNamedFramebufferTexture = &S_glNamedFramebufferTexture; ++count; }
        if (!glad_glNamedFramebufferTextureLayer) { glad_glNamedFramebufferTextureLayer = &S_glNamedFramebufferTextureLayer; ++count; }
        if (!glad_glNamedFramebufferDrawBuffer) { glad_glNamedFramebufferDrawBuffer = &S_glNamedFramebufferDrawBuffer; ++count; }
        if (!glad_glNamedFramebufferDrawBuffers) { glad_glNamedFramebufferDrawBuffers = &S_glNamedFramebufferDrawBuffers; ++count; }
        if (!glad_glNamedFramebufferReadBuffer) { glad_glNamedFramebufferReadBuffer = &S_glNamedFramebufferReadBuffer; ++count; }
        if (!glad_glInvalidateNamedFramebufferData) { glad_glInvalidateNamedFramebufferData = &S_glInvalidateNamedFramebufferData; ++count; }
        if (!glad_glInvalidateNamedFramebufferSubData) { glad_glInvalidateNamedFramebufferSubData = &S_glInvalidateNamedFramebufferSubData; ++count; }
        if (!glad_glClearNamedFramebufferiv) { glad_glClearNamedFramebufferiv = &S_glClearNamedFramebufferiv; ++count; }
        if (!glad_glClearNamedFramebufferuiv) { glad_glClearNamedFramebufferuiv = &S_glClearNamedFramebufferuiv; ++count; }
        if (!glad_glClearNamedFramebufferfv) { glad_glClearNamedFramebufferfv = &S_glClearNamedFramebufferfv; ++count; }
        if (!glad_glClearNamedFramebufferfi) { glad_glClearNamedFramebufferfi = &S_glClearNamedFramebufferfi; ++count; }
        if (!glad_glBlitNamedFramebuffer) { glad_glBlitNamedFramebuffer = &S_glBlitNamedFramebuffer; ++count; }
        if (!glad_glCheckNamedFramebufferStatus) { glad_glCheckNamedFramebufferStatus = &S_glCheckNamedFramebufferStatus; ++count; }
        if (!glad_glGetNamedFramebufferParameteriv) { glad_glGetNamedFramebufferParameteriv = &S_glGetNamedFramebufferParameteriv; ++count; }
        if (!glad_glGetNamedFramebufferAttachmentParameteriv) { glad_glGetNamedFramebufferAttachmentParameteriv = &S_glGetNamedFramebufferAttachmentParameteriv; ++count; }
        if (!glad_glCreateRenderbuffers) { glad_glCreateRenderbuffers = &S_glCreateRenderbuffers; ++count; }
        if (!glad_glNamedRenderbufferStorage) { glad_glNamedRenderbufferStorage = &S_glNamedRenderbufferStorage; ++count; }
        if (!glad_glNamedRenderbufferStorageMultisample) { glad_glNamedRenderbufferStorageMultisample = &S_glNamedRenderbufferStorageMultisample; ++count; }
        if (!glad_glGetNamedRenderbufferParameteriv) { glad_glGetNamedRenderbufferParameteriv = &S_glGetNamedRenderbufferParameteriv; ++count; }
        if (!glad_glCreateTextures) { glad_glCreateTextures = &S_glCreateTextures; ++count; }
        if (!glad_glTextureBuffer) { glad_glTextureBuffer = &S_glTextureBuffer; ++count; }
        if (!glad_glTextureBufferRange) { glad_glTextureBufferRange = &S_glTextureBufferRange; ++count; }
        if (!glad_glTextureStorage1D) { glad_glTextureStorage1D = &S_glTextureStorage1D; ++count; }
        if (!glad_glTextureStorage2D) { glad_glTextureStorage2D = &S_glTextureStorage2D; ++count; }
        if (!glad_glTextureStorage3D) { glad_glTextureStorage3D = &S_glTextureStorage3D; ++count; }
        if (!glad_glTextureStorage2DMultisample) { glad_glTextureStorage2DMultisample = &S_glTextureStorage2DMultisample; ++count; }
        if (!glad_glTextureStorage3DMultisample) { glad_glTextureStorage3DMultisample = &S_glTextureStorage3DMultisample; ++count; }
        if (!glad_glTextureSubImage1D) { glad_glTextureSubImage1D = &S_glTextureSubImage1D; ++count; }
        if (!glad_glTextureSubImage2D) { glad_glTextureSubImage2D = &S_glTextureSubImage2D; ++count; }
        if (!glad_glTextureSubImage3D) { glad_glTextureSubImage3D = &S_glTextureSubImage3D; ++count; }
        if (!glad_glCompressedTextureSubImage1D) { glad_glCompressedTextureSubImage1D = &S_glCompressedTextureSubImage1D; ++count; }
        if (!glad_glCompressedTextureSubImage2D) { glad_glCompressedTextureSubImage2D = &S_glCompressedTextureSubImage2D; ++count; }
        if (!glad_glCompressedTextureSubImage3D) { glad_glCompressedTextureSubImage3D = &S_glCompressedTextureSubImage3D; ++count; }
        if (!glad_glCopyTextureSubImage1D) { glad_glCopyTextureSubImage1D = &S_glCopyTextureSubImage1D; ++count; }
        if (!glad_glCopyTextureSubImage2D) { glad_glCopyTextureSubImage2D = &S_glCopyTextureSubImage2D; ++count; }
        if (!glad_glCopyTextureSubImage3D) { glad_glCopyTextureSubImage3D = &S_glCopyTextureSubImage3D; ++count; }
        if (!glad_glTextureParameterf) { glad_glTextureParameterf = &S_glTextureParameterf; ++count; }
        if (!glad_glTextureParameterfv) { glad_glTextureParameterfv = &S_glTextureParameterfv; ++count; }
        if (!glad_glTextureParameteri) { glad_glTextureParameteri = &S_glTextureParameteri; ++count; }
        if (!glad_glTextureParameterIiv) { glad_glTextureParameterIiv = &S_glTextureParameterIiv; ++count; }
        if (!glad_glTextureParameterIuiv) { glad_glTextureParameterIuiv = &S_glTextureParameterIuiv; ++count; }
        if (!glad_glTextureParameteriv) { glad_glTextureParameteriv = &S_glTextureParameteriv; ++count; }
        if (!glad_glGenerateTextureMipmap) { glad_glGenerateTextureMipmap = &S_glGenerateTextureMipmap; ++count; }
        if (!glad_glBindTextureUnit) { glad_glBindTextureUnit = &S_glBindTextureUnit; ++count; }
        if (!glad_glGetTextureImage) { glad_glGetTextureImage = &S_glGetTextureImage; ++count; }
        if (!glad_glGetCompressedTextureImage) { glad_glGetCompressedTextureImage = &S_glGetCompressedTextureImage; ++count; }
        if (!glad_glGetTextureLevelParameterfv) { glad_glGetTextureLevelParameterfv = &S_glGetTextureLevelParameterfv; ++count; }
        if (!glad_glGetTextureLevelParameteriv) { glad_glGetTextureLevelParameteriv = &S_glGetTextureLevelParameteriv; ++count; }
        if (!glad_glGetTextureParameterfv) { glad_glGetTextureParameterfv = &S_glGetTextureParameterfv; ++count; }
        if (!glad_glGetTextureParameterIiv) { glad_glGetTextureParameterIiv = &S_glGetTextureParameterIiv; ++count; }
        if (!glad_glGetTextureParameterIuiv) { glad_glGetTextureParameterIuiv = &S_glGetTextureParameterIuiv; ++count; }
        if (!glad_glGetTextureParameteriv) { glad_glGetTextureParameteriv = &S_glGetTextureParameteriv; ++count; }
        if (!glad_glCreateVertexArrays) { glad_glCreateVertexArrays = &S_glCreateVertexArrays; ++count; }
        if (!glad_glDisableVertexArrayAttrib) { glad_glDisableVertexArrayAttrib = &S_glDisableVertexArrayAttrib; ++count; }
        if (!glad_glEnableVertexArrayAttrib) { glad_glEnableVertexArrayAttrib = &S_glEnableVertexArrayAttrib; ++count; }
        if (!glad_glVertexArrayElementBuffer) { glad_glVertexArrayElementBuffer = &S_glVertexArrayElementBuffer; ++count; }
        if (!glad_glVertexArrayVertexBuffer) { glad_glVertexArrayVertexBuffer = &S_glVertexArrayVertexBuffer; ++count; }
        if (!glad_glVertexArrayVertexBuffers) { glad_glVertexArrayVertexBuffers = &S_glVertexArrayVertexBuffers; ++count; }
        if (!glad_glVertexArrayAttribBinding) { glad_glVertexArrayAttribBinding = &S_glVertexArrayAttribBinding; ++count; }
        if (!glad_glVertexArrayAttribFormat) { glad_glVertexArrayAttribFormat = &S_glVertexArrayAttribFormat; ++count; }
        if (!glad_glVertexArrayAttribIFormat) { glad_glVertexArrayAttribIFormat = &S_glVertexArrayAttribIFormat; ++count; }
        if (!glad_glVertexArrayAttribLFormat) { glad_glVertexArrayAttribLFormat = &S_glVertexArrayAttribLFormat; ++count; }
        if (!glad_glVertexArrayBindingDivisor) { glad_glVertexArrayBindingDivisor = &S_glVertexArrayBindingDivisor; ++count; }
        if (!glad_glGetVertexArrayiv) { glad_glGetVertexArrayiv = &S_glGetVertexArrayiv; ++count; }
        if (!glad_glGetVertexArrayIndexediv) { glad_glGetVertexArrayIndexediv = &S_glGetVertexArrayIndexediv; ++count; }
        if (!glad_glGetVertexArrayIndexed64iv) { glad_glGetVertexArrayIndexed64iv = &S_glGetVertexArrayIndexed64iv; ++count; }
        if (!glad_glCreateSamplers) { glad_glCreateSamplers = &S_glCreateSamplers; ++count; }
        if (!glad_glCreateProgramPipelines) { glad_glCreateProgramPipelines = &S_glCreateProgramPipelines; ++count; }
        if (!glad_glCreateQueries) { glad_glCreateQueries = &S_glCreateQueries; ++count; }
        if (!glad_glGetQueryBufferObjecti64v) { glad_glGetQueryBufferObjecti64v = &S_glGetQueryBufferObjecti64v; ++count; }
        if (!glad_glGetQueryBufferObjectiv) { glad_glGetQueryBufferObjectiv = &S_glGetQueryBufferObjectiv; ++count; }
        if (!glad_glGetQueryBufferObjectui64v) { glad_glGetQueryBufferObjectui64v = &S_glGetQueryBufferObjectui64v; ++count; }
        if (!glad_glGetQueryBufferObjectuiv) { glad_glGetQueryBufferObjectuiv = &S_glGetQueryBufferObjectuiv; ++count; }
        if (!glad_glMemoryBarrierByRegion) { glad_glMemoryBarrierByRegion = &S_glMemoryBarrierByRegion; ++count; }
        if (!glad_glGetTextureSubImage) { glad_glGetTextureSubImage = &S_glGetTextureSubImage; ++count; }
        if (!glad_glGetCompressedTextureSubImage) { glad_glGetCompressedTextureSubImage = &S_glGetCompressedTextureSubImage; ++count; }
        if (!glad_glGetGraphicsResetStatus) { glad_glGetGraphicsResetStatus = &S_glGetGraphicsResetStatus; ++count; }
        if (!glad_glGetnCompressedTexImage) { glad_glGetnCompressedTexImage = &S_glGetnCompressedTexImage; ++count; }
        if (!glad_glGetnTexImage) { glad_glGetnTexImage = &S_glGetnTexImage; ++count; }
        if (!glad_glGetnUniformdv) { glad_glGetnUniformdv = &S_glGetnUniformdv; ++count; }
        if (!glad_glGetnUniformfv) { glad_glGetnUniformfv = &S_glGetnUniformfv; ++count; }
        if (!glad_glGetnUniformiv) { glad_glGetnUniformiv = &S_glGetnUniformiv; ++count; }
        if (!glad_glGetnUniformuiv) { glad_glGetnUniformuiv = &S_glGetnUniformuiv; ++count; }
        if (!glad_glReadnPixels) { glad_glReadnPixels = &S_glReadnPixels; ++count; }
        if (!glad_glGetnMapdv) { glad_glGetnMapdv = &S_glGetnMapdv; ++count; }
        if (!glad_glGetnMapfv) { glad_glGetnMapfv = &S_glGetnMapfv; ++count; }
        if (!glad_glGetnMapiv) { glad_glGetnMapiv = &S_glGetnMapiv; ++count; }
        if (!glad_glGetnPixelMapfv) { glad_glGetnPixelMapfv = &S_glGetnPixelMapfv; ++count; }
        if (!glad_glGetnPixelMapuiv) { glad_glGetnPixelMapuiv = &S_glGetnPixelMapuiv; ++count; }
        if (!glad_glGetnPixelMapusv) { glad_glGetnPixelMapusv = &S_glGetnPixelMapusv; ++count; }
        if (!glad_glGetnPolygonStipple) { glad_glGetnPolygonStipple = &S_glGetnPolygonStipple; ++count; }
        if (!glad_glGetnColorTable) { glad_glGetnColorTable = &S_glGetnColorTable; ++count; }
        if (!glad_glGetnConvolutionFilter) { glad_glGetnConvolutionFilter = &S_glGetnConvolutionFilter; ++count; }
        if (!glad_glGetnSeparableFilter) { glad_glGetnSeparableFilter = &S_glGetnSeparableFilter; ++count; }
        if (!glad_glGetnHistogram) { glad_glGetnHistogram = &S_glGetnHistogram; ++count; }
        if (!glad_glGetnMinmax) { glad_glGetnMinmax = &S_glGetnMinmax; ++count; }
        if (!glad_glTextureBarrier) { glad_glTextureBarrier = &S_glTextureBarrier; ++count; }
        if (!glad_glSpecializeShader) { glad_glSpecializeShader = &S_glSpecializeShader; ++count; }
        if (!glad_glMultiDrawArraysIndirectCount) { glad_glMultiDrawArraysIndirectCount = &S_glMultiDrawArraysIndirectCount; ++count; }
        if (!glad_glMultiDrawElementsIndirectCount) { glad_glMultiDrawElementsIndirectCount = &S_glMultiDrawElementsIndirectCount; ++count; }
        if (!glad_glPolygonOffsetClamp) { glad_glPolygonOffsetClamp = &S_glPolygonOffsetClamp; ++count; }
        if (!glad_glTbufferMask3DFX) { glad_glTbufferMask3DFX = &S_glTbufferMask3DFX; ++count; }
        if (!glad_glDebugMessageEnableAMD) { glad_glDebugMessageEnableAMD = &S_glDebugMessageEnableAMD; ++count; }
        if (!glad_glDebugMessageInsertAMD) { glad_glDebugMessageInsertAMD = &S_glDebugMessageInsertAMD; ++count; }
        if (!glad_glDebugMessageCallbackAMD) { glad_glDebugMessageCallbackAMD = &S_glDebugMessageCallbackAMD; ++count; }
        if (!glad_glGetDebugMessageLogAMD) { glad_glGetDebugMessageLogAMD = &S_glGetDebugMessageLogAMD; ++count; }
        if (!glad_glBlendFuncIndexedAMD) { glad_glBlendFuncIndexedAMD = &S_glBlendFuncIndexedAMD; ++count; }
        if (!glad_glBlendFuncSeparateIndexedAMD) { glad_glBlendFuncSeparateIndexedAMD = &S_glBlendFuncSeparateIndexedAMD; ++count; }
        if (!glad_glBlendEquationIndexedAMD) { glad_glBlendEquationIndexedAMD = &S_glBlendEquationIndexedAMD; ++count; }
        if (!glad_glBlendEquationSeparateIndexedAMD) { glad_glBlendEquationSeparateIndexedAMD = &S_glBlendEquationSeparateIndexedAMD; ++count; }
        if (!glad_glRenderbufferStorageMultisampleAdvancedAMD) { glad_glRenderbufferStorageMultisampleAdvancedAMD = &S_glRenderbufferStorageMultisampleAdvancedAMD; ++count; }
        if (!glad_glNamedRenderbufferStorageMultisampleAdvancedAMD) { glad_glNamedRenderbufferStorageMultisampleAdvancedAMD = &S_glNamedRenderbufferStorageMultisampleAdvancedAMD; ++count; }
        if (!glad_glFramebufferSamplePositionsfvAMD) { glad_glFramebufferSamplePositionsfvAMD = &S_glFramebufferSamplePositionsfvAMD; ++count; }
        if (!glad_glNamedFramebufferSamplePositionsfvAMD) { glad_glNamedFramebufferSamplePositionsfvAMD = &S_glNamedFramebufferSamplePositionsfvAMD; ++count; }
        if (!glad_glGetFramebufferParameterfvAMD) { glad_glGetFramebufferParameterfvAMD = &S_glGetFramebufferParameterfvAMD; ++count; }
        if (!glad_glGetNamedFramebufferParameterfvAMD) { glad_glGetNamedFramebufferParameterfvAMD = &S_glGetNamedFramebufferParameterfvAMD; ++count; }
        if (!glad_glUniform1i64NV) { glad_glUniform1i64NV = &S_glUniform1i64NV; ++count; }
        if (!glad_glUniform2i64NV) { glad_glUniform2i64NV = &S_glUniform2i64NV; ++count; }
        if (!glad_glUniform3i64NV) { glad_glUniform3i64NV = &S_glUniform3i64NV; ++count; }
        if (!glad_glUniform4i64NV) { glad_glUniform4i64NV = &S_glUniform4i64NV; ++count; }
        if (!glad_glUniform1i64vNV) { glad_glUniform1i64vNV = &S_glUniform1i64vNV; ++count; }
        if (!glad_glUniform2i64vNV) { glad_glUniform2i64vNV = &S_glUniform2i64vNV; ++count; }
        if (!glad_glUniform3i64vNV) { glad_glUniform3i64vNV = &S_glUniform3i64vNV; ++count; }
        if (!glad_glUniform4i64vNV) { glad_glUniform4i64vNV = &S_glUniform4i64vNV; ++count; }
        if (!glad_glUniform1ui64NV) { glad_glUniform1ui64NV = &S_glUniform1ui64NV; ++count; }
        if (!glad_glUniform2ui64NV) { glad_glUniform2ui64NV = &S_glUniform2ui64NV; ++count; }
        if (!glad_glUniform3ui64NV) { glad_glUniform3ui64NV = &S_glUniform3ui64NV; ++count; }
        if (!glad_glUniform4ui64NV) { glad_glUniform4ui64NV = &S_glUniform4ui64NV; ++count; }
        if (!glad_glUniform1ui64vNV) { glad_glUniform1ui64vNV = &S_glUniform1ui64vNV; ++count; }
        if (!glad_glUniform2ui64vNV) { glad_glUniform2ui64vNV = &S_glUniform2ui64vNV; ++count; }
        if (!glad_glUniform3ui64vNV) { glad_glUniform3ui64vNV = &S_glUniform3ui64vNV; ++count; }
        if (!glad_glUniform4ui64vNV) { glad_glUniform4ui64vNV = &S_glUniform4ui64vNV; ++count; }
        if (!glad_glGetUniformi64vNV) { glad_glGetUniformi64vNV = &S_glGetUniformi64vNV; ++count; }
        if (!glad_glGetUniformui64vNV) { glad_glGetUniformui64vNV = &S_glGetUniformui64vNV; ++count; }
        if (!glad_glProgramUniform1i64NV) { glad_glProgramUniform1i64NV = &S_glProgramUniform1i64NV; ++count; }
        if (!glad_glProgramUniform2i64NV) { glad_glProgramUniform2i64NV = &S_glProgramUniform2i64NV; ++count; }
        if (!glad_glProgramUniform3i64NV) { glad_glProgramUniform3i64NV = &S_glProgramUniform3i64NV; ++count; }
        if (!glad_glProgramUniform4i64NV) { glad_glProgramUniform4i64NV = &S_glProgramUniform4i64NV; ++count; }
        if (!glad_glProgramUniform1i64vNV) { glad_glProgramUniform1i64vNV = &S_glProgramUniform1i64vNV; ++count; }
        if (!glad_glProgramUniform2i64vNV) { glad_glProgramUniform2i64vNV = &S_glProgramUniform2i64vNV; ++count; }
        if (!glad_glProgramUniform3i64vNV) { glad_glProgramUniform3i64vNV = &S_glProgramUniform3i64vNV; ++count; }
        if (!glad_glProgramUniform4i64vNV) { glad_glProgramUniform4i64vNV = &S_glProgramUniform4i64vNV; ++count; }
        if (!glad_glProgramUniform1ui64NV) { glad_glProgramUniform1ui64NV = &S_glProgramUniform1ui64NV; ++count; }
        if (!glad_glProgramUniform2ui64NV) { glad_glProgramUniform2ui64NV = &S_glProgramUniform2ui64NV; ++count; }
        if (!glad_glProgramUniform3ui64NV) { glad_glProgramUniform3ui64NV = &S_glProgramUniform3ui64NV; ++count; }
        if (!glad_glProgramUniform4ui64NV) { glad_glProgramUniform4ui64NV = &S_glProgramUniform4ui64NV; ++count; }
        if (!glad_glProgramUniform1ui64vNV) { glad_glProgramUniform1ui64vNV = &S_glProgramUniform1ui64vNV; ++count; }
        if (!glad_glProgramUniform2ui64vNV) { glad_glProgramUniform2ui64vNV = &S_glProgramUniform2ui64vNV; ++count; }
        if (!glad_glProgramUniform3ui64vNV) { glad_glProgramUniform3ui64vNV = &S_glProgramUniform3ui64vNV; ++count; }
        if (!glad_glProgramUniform4ui64vNV) { glad_glProgramUniform4ui64vNV = &S_glProgramUniform4ui64vNV; ++count; }
        if (!glad_glVertexAttribParameteriAMD) { glad_glVertexAttribParameteriAMD = &S_glVertexAttribParameteriAMD; ++count; }
        if (!glad_glMultiDrawArraysIndirectAMD) { glad_glMultiDrawArraysIndirectAMD = &S_glMultiDrawArraysIndirectAMD; ++count; }
        if (!glad_glMultiDrawElementsIndirectAMD) { glad_glMultiDrawElementsIndirectAMD = &S_glMultiDrawElementsIndirectAMD; ++count; }
        if (!glad_glGenNamesAMD) { glad_glGenNamesAMD = &S_glGenNamesAMD; ++count; }
        if (!glad_glDeleteNamesAMD) { glad_glDeleteNamesAMD = &S_glDeleteNamesAMD; ++count; }
        if (!glad_glIsNameAMD) { glad_glIsNameAMD = &S_glIsNameAMD; ++count; }
        if (!glad_glQueryObjectParameteruiAMD) { glad_glQueryObjectParameteruiAMD = &S_glQueryObjectParameteruiAMD; ++count; }
        if (!glad_glGetPerfMonitorGroupsAMD) { glad_glGetPerfMonitorGroupsAMD = &S_glGetPerfMonitorGroupsAMD; ++count; }
        if (!glad_glGetPerfMonitorCountersAMD) { glad_glGetPerfMonitorCountersAMD = &S_glGetPerfMonitorCountersAMD; ++count; }
        if (!glad_glGetPerfMonitorGroupStringAMD) { glad_glGetPerfMonitorGroupStringAMD = &S_glGetPerfMonitorGroupStringAMD; ++count; }
        if (!glad_glGetPerfMonitorCounterStringAMD) { glad_glGetPerfMonitorCounterStringAMD = &S_glGetPerfMonitorCounterStringAMD; ++count; }
        if (!glad_glGetPerfMonitorCounterInfoAMD) { glad_glGetPerfMonitorCounterInfoAMD = &S_glGetPerfMonitorCounterInfoAMD; ++count; }
        if (!glad_glGenPerfMonitorsAMD) { glad_glGenPerfMonitorsAMD = &S_glGenPerfMonitorsAMD; ++count; }
        if (!glad_glDeletePerfMonitorsAMD) { glad_glDeletePerfMonitorsAMD = &S_glDeletePerfMonitorsAMD; ++count; }
        if (!glad_glSelectPerfMonitorCountersAMD) { glad_glSelectPerfMonitorCountersAMD = &S_glSelectPerfMonitorCountersAMD; ++count; }
        if (!glad_glBeginPerfMonitorAMD) { glad_glBeginPerfMonitorAMD = &S_glBeginPerfMonitorAMD; ++count; }
        if (!glad_glEndPerfMonitorAMD) { glad_glEndPerfMonitorAMD = &S_glEndPerfMonitorAMD; ++count; }
        if (!glad_glGetPerfMonitorCounterDataAMD) { glad_glGetPerfMonitorCounterDataAMD = &S_glGetPerfMonitorCounterDataAMD; ++count; }
        if (!glad_glSetMultisamplefvAMD) { glad_glSetMultisamplefvAMD = &S_glSetMultisamplefvAMD; ++count; }
        if (!glad_glTexStorageSparseAMD) { glad_glTexStorageSparseAMD = &S_glTexStorageSparseAMD; ++count; }
        if (!glad_glTextureStorageSparseAMD) { glad_glTextureStorageSparseAMD = &S_glTextureStorageSparseAMD; ++count; }
        if (!glad_glStencilOpValueAMD) { glad_glStencilOpValueAMD = &S_glStencilOpValueAMD; ++count; }
        if (!glad_glTessellationFactorAMD) { glad_glTessellationFactorAMD = &S_glTessellationFactorAMD; ++count; }
        if (!glad_glTessellationModeAMD) { glad_glTessellationModeAMD = &S_glTessellationModeAMD; ++count; }
        if (!glad_glElementPointerAPPLE) { glad_glElementPointerAPPLE = &S_glElementPointerAPPLE; ++count; }
        if (!glad_glDrawElementArrayAPPLE) { glad_glDrawElementArrayAPPLE = &S_glDrawElementArrayAPPLE; ++count; }
        if (!glad_glDrawRangeElementArrayAPPLE) { glad_glDrawRangeElementArrayAPPLE = &S_glDrawRangeElementArrayAPPLE; ++count; }
        if (!glad_glMultiDrawElementArrayAPPLE) { glad_glMultiDrawElementArrayAPPLE = &S_glMultiDrawElementArrayAPPLE; ++count; }
        if (!glad_glMultiDrawRangeElementArrayAPPLE) { glad_glMultiDrawRangeElementArrayAPPLE = &S_glMultiDrawRangeElementArrayAPPLE; ++count; }
        if (!glad_glGenFencesAPPLE) { glad_glGenFencesAPPLE = &S_glGenFencesAPPLE; ++count; }
        if (!glad_glDeleteFencesAPPLE) { glad_glDeleteFencesAPPLE = &S_glDeleteFencesAPPLE; ++count; }
        if (!glad_glSetFenceAPPLE) { glad_glSetFenceAPPLE = &S_glSetFenceAPPLE; ++count; }
        if (!glad_glIsFenceAPPLE) { glad_glIsFenceAPPLE = &S_glIsFenceAPPLE; ++count; }
        if (!glad_glTestFenceAPPLE) { glad_glTestFenceAPPLE = &S_glTestFenceAPPLE; ++count; }
        if (!glad_glFinishFenceAPPLE) { glad_glFinishFenceAPPLE = &S_glFinishFenceAPPLE; ++count; }
        if (!glad_glTestObjectAPPLE) { glad_glTestObjectAPPLE = &S_glTestObjectAPPLE; ++count; }
        if (!glad_glFinishObjectAPPLE) { glad_glFinishObjectAPPLE = &S_glFinishObjectAPPLE; ++count; }
        if (!glad_glBufferParameteriAPPLE) { glad_glBufferParameteriAPPLE = &S_glBufferParameteriAPPLE; ++count; }
        if (!glad_glFlushMappedBufferRangeAPPLE) { glad_glFlushMappedBufferRangeAPPLE = &S_glFlushMappedBufferRangeAPPLE; ++count; }
        if (!glad_glObjectPurgeableAPPLE) { glad_glObjectPurgeableAPPLE = &S_glObjectPurgeableAPPLE; ++count; }
        if (!glad_glObjectUnpurgeableAPPLE) { glad_glObjectUnpurgeableAPPLE = &S_glObjectUnpurgeableAPPLE; ++count; }
        if (!glad_glGetObjectParameterivAPPLE) { glad_glGetObjectParameterivAPPLE = &S_glGetObjectParameterivAPPLE; ++count; }
        if (!glad_glTextureRangeAPPLE) { glad_glTextureRangeAPPLE = &S_glTextureRangeAPPLE; ++count; }
        if (!glad_glGetTexParameterPointervAPPLE) { glad_glGetTexParameterPointervAPPLE = &S_glGetTexParameterPointervAPPLE; ++count; }
        if (!glad_glBindVertexArrayAPPLE) { glad_glBindVertexArrayAPPLE = &S_glBindVertexArrayAPPLE; ++count; }
        if (!glad_glDeleteVertexArraysAPPLE) { glad_glDeleteVertexArraysAPPLE = &S_glDeleteVertexArraysAPPLE; ++count; }
        if (!glad_glGenVertexArraysAPPLE) { glad_glGenVertexArraysAPPLE = &S_glGenVertexArraysAPPLE; ++count; }
        if (!glad_glIsVertexArrayAPPLE) { glad_glIsVertexArrayAPPLE = &S_glIsVertexArrayAPPLE; ++count; }
        if (!glad_glVertexArrayRangeAPPLE) { glad_glVertexArrayRangeAPPLE = &S_glVertexArrayRangeAPPLE; ++count; }
        if (!glad_glFlushVertexArrayRangeAPPLE) { glad_glFlushVertexArrayRangeAPPLE = &S_glFlushVertexArrayRangeAPPLE; ++count; }
        if (!glad_glVertexArrayParameteriAPPLE) { glad_glVertexArrayParameteriAPPLE = &S_glVertexArrayParameteriAPPLE; ++count; }
        if (!glad_glEnableVertexAttribAPPLE) { glad_glEnableVertexAttribAPPLE = &S_glEnableVertexAttribAPPLE; ++count; }
        if (!glad_glDisableVertexAttribAPPLE) { glad_glDisableVertexAttribAPPLE = &S_glDisableVertexAttribAPPLE; ++count; }
        if (!glad_glIsVertexAttribEnabledAPPLE) { glad_glIsVertexAttribEnabledAPPLE = &S_glIsVertexAttribEnabledAPPLE; ++count; }
        if (!glad_glMapVertexAttrib1dAPPLE) { glad_glMapVertexAttrib1dAPPLE = &S_glMapVertexAttrib1dAPPLE; ++count; }
        if (!glad_glMapVertexAttrib1fAPPLE) { glad_glMapVertexAttrib1fAPPLE = &S_glMapVertexAttrib1fAPPLE; ++count; }
        if (!glad_glMapVertexAttrib2dAPPLE) { glad_glMapVertexAttrib2dAPPLE = &S_glMapVertexAttrib2dAPPLE; ++count; }
        if (!glad_glMapVertexAttrib2fAPPLE) { glad_glMapVertexAttrib2fAPPLE = &S_glMapVertexAttrib2fAPPLE; ++count; }
        if (!glad_glPrimitiveBoundingBoxARB) { glad_glPrimitiveBoundingBoxARB = &S_glPrimitiveBoundingBoxARB; ++count; }
        if (!glad_glGetTextureHandleARB) { glad_glGetTextureHandleARB = &S_glGetTextureHandleARB; ++count; }
        if (!glad_glGetTextureSamplerHandleARB) { glad_glGetTextureSamplerHandleARB = &S_glGetTextureSamplerHandleARB; ++count; }
        if (!glad_glMakeTextureHandleResidentARB) { glad_glMakeTextureHandleResidentARB = &S_glMakeTextureHandleResidentARB; ++count; }
        if (!glad_glMakeTextureHandleNonResidentARB) { glad_glMakeTextureHandleNonResidentARB = &S_glMakeTextureHandleNonResidentARB; ++count; }
        if (!glad_glGetImageHandleARB) { glad_glGetImageHandleARB = &S_glGetImageHandleARB; ++count; }
        if (!glad_glMakeImageHandleResidentARB) { glad_glMakeImageHandleResidentARB = &S_glMakeImageHandleResidentARB; ++count; }
        if (!glad_glMakeImageHandleNonResidentARB) { glad_glMakeImageHandleNonResidentARB = &S_glMakeImageHandleNonResidentARB; ++count; }
        if (!glad_glUniformHandleui64ARB) { glad_glUniformHandleui64ARB = &S_glUniformHandleui64ARB; ++count; }
        if (!glad_glUniformHandleui64vARB) { glad_glUniformHandleui64vARB = &S_glUniformHandleui64vARB; ++count; }
        if (!glad_glProgramUniformHandleui64ARB) { glad_glProgramUniformHandleui64ARB = &S_glProgramUniformHandleui64ARB; ++count; }
        if (!glad_glProgramUniformHandleui64vARB) { glad_glProgramUniformHandleui64vARB = &S_glProgramUniformHandleui64vARB; ++count; }
        if (!glad_glIsTextureHandleResidentARB) { glad_glIsTextureHandleResidentARB = &S_glIsTextureHandleResidentARB; ++count; }
        if (!glad_glIsImageHandleResidentARB) { glad_glIsImageHandleResidentARB = &S_glIsImageHandleResidentARB; ++count; }
        if (!glad_glVertexAttribL1ui64ARB) { glad_glVertexAttribL1ui64ARB = &S_glVertexAttribL1ui64ARB; ++count; }
        if (!glad_glVertexAttribL1ui64vARB) { glad_glVertexAttribL1ui64vARB = &S_glVertexAttribL1ui64vARB; ++count; }
        if (!glad_glGetVertexAttribLui64vARB) { glad_glGetVertexAttribLui64vARB = &S_glGetVertexAttribLui64vARB; ++count; }
        if (!glad_glCreateSyncFromCLeventARB) { glad_glCreateSyncFromCLeventARB = &S_glCreateSyncFromCLeventARB; ++count; }
        if (!glad_glClampColorARB) { glad_glClampColorARB = &S_glClampColorARB; ++count; }
        if (!glad_glDispatchComputeGroupSizeARB) { glad_glDispatchComputeGroupSizeARB = &S_glDispatchComputeGroupSizeARB; ++count; }
        if (!glad_glDebugMessageControlARB) { glad_glDebugMessageControlARB = &S_glDebugMessageControlARB; ++count; }
        if (!glad_glDebugMessageInsertARB) { glad_glDebugMessageInsertARB = &S_glDebugMessageInsertARB; ++count; }
        if (!glad_glDebugMessageCallbackARB) { glad_glDebugMessageCallbackARB = &S_glDebugMessageCallbackARB; ++count; }
        if (!glad_glGetDebugMessageLogARB) { glad_glGetDebugMessageLogARB = &S_glGetDebugMessageLogARB; ++count; }
        if (!glad_glDrawBuffersARB) { glad_glDrawBuffersARB = &S_glDrawBuffersARB; ++count; }
        if (!glad_glBlendEquationiARB) { glad_glBlendEquationiARB = &S_glBlendEquationiARB; ++count; }
        if (!glad_glBlendEquationSeparateiARB) { glad_glBlendEquationSeparateiARB = &S_glBlendEquationSeparateiARB; ++count; }
        if (!glad_glBlendFunciARB) { glad_glBlendFunciARB = &S_glBlendFunciARB; ++count; }
        if (!glad_glBlendFuncSeparateiARB) { glad_glBlendFuncSeparateiARB = &S_glBlendFuncSeparateiARB; ++count; }
        if (!glad_glDrawArraysInstancedARB) { glad_glDrawArraysInstancedARB = &S_glDrawArraysInstancedARB; ++count; }
        if (!glad_glDrawElementsInstancedARB) { glad_glDrawElementsInstancedARB = &S_glDrawElementsInstancedARB; ++count; }
        if (!glad_glProgramStringARB) { glad_glProgramStringARB = &S_glProgramStringARB; ++count; }
        if (!glad_glBindProgramARB) { glad_glBindProgramARB = &S_glBindProgramARB; ++count; }
        if (!glad_glDeleteProgramsARB) { glad_glDeleteProgramsARB = &S_glDeleteProgramsARB; ++count; }
        if (!glad_glGenProgramsARB) { glad_glGenProgramsARB = &S_glGenProgramsARB; ++count; }
        if (!glad_glProgramEnvParameter4dARB) { glad_glProgramEnvParameter4dARB = &S_glProgramEnvParameter4dARB; ++count; }
        if (!glad_glProgramEnvParameter4dvARB) { glad_glProgramEnvParameter4dvARB = &S_glProgramEnvParameter4dvARB; ++count; }
        if (!glad_glProgramEnvParameter4fARB) { glad_glProgramEnvParameter4fARB = &S_glProgramEnvParameter4fARB; ++count; }
        if (!glad_glProgramEnvParameter4fvARB) { glad_glProgramEnvParameter4fvARB = &S_glProgramEnvParameter4fvARB; ++count; }
        if (!glad_glProgramLocalParameter4dARB) { glad_glProgramLocalParameter4dARB = &S_glProgramLocalParameter4dARB; ++count; }
        if (!glad_glProgramLocalParameter4dvARB) { glad_glProgramLocalParameter4dvARB = &S_glProgramLocalParameter4dvARB; ++count; }
        if (!glad_glProgramLocalParameter4fARB) { glad_glProgramLocalParameter4fARB = &S_glProgramLocalParameter4fARB; ++count; }
        if (!glad_glProgramLocalParameter4fvARB) { glad_glProgramLocalParameter4fvARB = &S_glProgramLocalParameter4fvARB; ++count; }
        if (!glad_glGetProgramEnvParameterdvARB) { glad_glGetProgramEnvParameterdvARB = &S_glGetProgramEnvParameterdvARB; ++count; }
        if (!glad_glGetProgramEnvParameterfvARB) { glad_glGetProgramEnvParameterfvARB = &S_glGetProgramEnvParameterfvARB; ++count; }
        if (!glad_glGetProgramLocalParameterdvARB) { glad_glGetProgramLocalParameterdvARB = &S_glGetProgramLocalParameterdvARB; ++count; }
        if (!glad_glGetProgramLocalParameterfvARB) { glad_glGetProgramLocalParameterfvARB = &S_glGetProgramLocalParameterfvARB; ++count; }
        if (!glad_glGetProgramivARB) { glad_glGetProgramivARB = &S_glGetProgramivARB; ++count; }
        if (!glad_glGetProgramStringARB) { glad_glGetProgramStringARB = &S_glGetProgramStringARB; ++count; }
        if (!glad_glIsProgramARB) { glad_glIsProgramARB = &S_glIsProgramARB; ++count; }
        if (!glad_glProgramParameteriARB) { glad_glProgramParameteriARB = &S_glProgramParameteriARB; ++count; }
        if (!glad_glFramebufferTextureARB) { glad_glFramebufferTextureARB = &S_glFramebufferTextureARB; ++count; }
        if (!glad_glFramebufferTextureLayerARB) { glad_glFramebufferTextureLayerARB = &S_glFramebufferTextureLayerARB; ++count; }
        if (!glad_glFramebufferTextureFaceARB) { glad_glFramebufferTextureFaceARB = &S_glFramebufferTextureFaceARB; ++count; }
        if (!glad_glSpecializeShaderARB) { glad_glSpecializeShaderARB = &S_glSpecializeShaderARB; ++count; }
        if (!glad_glUniform1i64ARB) { glad_glUniform1i64ARB = &S_glUniform1i64ARB; ++count; }
        if (!glad_glUniform2i64ARB) { glad_glUniform2i64ARB = &S_glUniform2i64ARB; ++count; }
        if (!glad_glUniform3i64ARB) { glad_glUniform3i64ARB = &S_glUniform3i64ARB; ++count; }
        if (!glad_glUniform4i64ARB) { glad_glUniform4i64ARB = &S_glUniform4i64ARB; ++count; }
        if (!glad_glUniform1i64vARB) { glad_glUniform1i64vARB = &S_glUniform1i64vARB; ++count; }
        if (!glad_glUniform2i64vARB) { glad_glUniform2i64vARB = &S_glUniform2i64vARB; ++count; }
        if (!glad_glUniform3i64vARB) { glad_glUniform3i64vARB = &S_glUniform3i64vARB; ++count; }
        if (!glad_glUniform4i64vARB) { glad_glUniform4i64vARB = &S_glUniform4i64vARB; ++count; }
        if (!glad_glUniform1ui64ARB) { glad_glUniform1ui64ARB = &S_glUniform1ui64ARB; ++count; }
        if (!glad_glUniform2ui64ARB) { glad_glUniform2ui64ARB = &S_glUniform2ui64ARB; ++count; }
        if (!glad_glUniform3ui64ARB) { glad_glUniform3ui64ARB = &S_glUniform3ui64ARB; ++count; }
        if (!glad_glUniform4ui64ARB) { glad_glUniform4ui64ARB = &S_glUniform4ui64ARB; ++count; }
        if (!glad_glUniform1ui64vARB) { glad_glUniform1ui64vARB = &S_glUniform1ui64vARB; ++count; }
        if (!glad_glUniform2ui64vARB) { glad_glUniform2ui64vARB = &S_glUniform2ui64vARB; ++count; }
        if (!glad_glUniform3ui64vARB) { glad_glUniform3ui64vARB = &S_glUniform3ui64vARB; ++count; }
        if (!glad_glUniform4ui64vARB) { glad_glUniform4ui64vARB = &S_glUniform4ui64vARB; ++count; }
        if (!glad_glGetUniformi64vARB) { glad_glGetUniformi64vARB = &S_glGetUniformi64vARB; ++count; }
        if (!glad_glGetUniformui64vARB) { glad_glGetUniformui64vARB = &S_glGetUniformui64vARB; ++count; }
        if (!glad_glGetnUniformi64vARB) { glad_glGetnUniformi64vARB = &S_glGetnUniformi64vARB; ++count; }
        if (!glad_glGetnUniformui64vARB) { glad_glGetnUniformui64vARB = &S_glGetnUniformui64vARB; ++count; }
        if (!glad_glProgramUniform1i64ARB) { glad_glProgramUniform1i64ARB = &S_glProgramUniform1i64ARB; ++count; }
        if (!glad_glProgramUniform2i64ARB) { glad_glProgramUniform2i64ARB = &S_glProgramUniform2i64ARB; ++count; }
        if (!glad_glProgramUniform3i64ARB) { glad_glProgramUniform3i64ARB = &S_glProgramUniform3i64ARB; ++count; }
        if (!glad_glProgramUniform4i64ARB) { glad_glProgramUniform4i64ARB = &S_glProgramUniform4i64ARB; ++count; }
        if (!glad_glProgramUniform1i64vARB) { glad_glProgramUniform1i64vARB = &S_glProgramUniform1i64vARB; ++count; }
        if (!glad_glProgramUniform2i64vARB) { glad_glProgramUniform2i64vARB = &S_glProgramUniform2i64vARB; ++count; }
        if (!glad_glProgramUniform3i64vARB) { glad_glProgramUniform3i64vARB = &S_glProgramUniform3i64vARB; ++count; }
        if (!glad_glProgramUniform4i64vARB) { glad_glProgramUniform4i64vARB = &S_glProgramUniform4i64vARB; ++count; }
        if (!glad_glProgramUniform1ui64ARB) { glad_glProgramUniform1ui64ARB = &S_glProgramUniform1ui64ARB; ++count; }
        if (!glad_glProgramUniform2ui64ARB) { glad_glProgramUniform2ui64ARB = &S_glProgramUniform2ui64ARB; ++count; }
        if (!glad_glProgramUniform3ui64ARB) { glad_glProgramUniform3ui64ARB = &S_glProgramUniform3ui64ARB; ++count; }
        if (!glad_glProgramUniform4ui64ARB) { glad_glProgramUniform4ui64ARB = &S_glProgramUniform4ui64ARB; ++count; }
        if (!glad_glProgramUniform1ui64vARB) { glad_glProgramUniform1ui64vARB = &S_glProgramUniform1ui64vARB; ++count; }
        if (!glad_glProgramUniform2ui64vARB) { glad_glProgramUniform2ui64vARB = &S_glProgramUniform2ui64vARB; ++count; }
        if (!glad_glProgramUniform3ui64vARB) { glad_glProgramUniform3ui64vARB = &S_glProgramUniform3ui64vARB; ++count; }
        if (!glad_glProgramUniform4ui64vARB) { glad_glProgramUniform4ui64vARB = &S_glProgramUniform4ui64vARB; ++count; }
        if (!glad_glColorTable) { glad_glColorTable = &S_glColorTable; ++count; }
        if (!glad_glColorTableParameterfv) { glad_glColorTableParameterfv = &S_glColorTableParameterfv; ++count; }
        if (!glad_glColorTableParameteriv) { glad_glColorTableParameteriv = &S_glColorTableParameteriv; ++count; }
        if (!glad_glCopyColorTable) { glad_glCopyColorTable = &S_glCopyColorTable; ++count; }
        if (!glad_glGetColorTable) { glad_glGetColorTable = &S_glGetColorTable; ++count; }
        if (!glad_glGetColorTableParameterfv) { glad_glGetColorTableParameterfv = &S_glGetColorTableParameterfv; ++count; }
        if (!glad_glGetColorTableParameteriv) { glad_glGetColorTableParameteriv = &S_glGetColorTableParameteriv; ++count; }
        if (!glad_glColorSubTable) { glad_glColorSubTable = &S_glColorSubTable; ++count; }
        if (!glad_glCopyColorSubTable) { glad_glCopyColorSubTable = &S_glCopyColorSubTable; ++count; }
        if (!glad_glConvolutionFilter1D) { glad_glConvolutionFilter1D = &S_glConvolutionFilter1D; ++count; }
        if (!glad_glConvolutionFilter2D) { glad_glConvolutionFilter2D = &S_glConvolutionFilter2D; ++count; }
        if (!glad_glConvolutionParameterf) { glad_glConvolutionParameterf = &S_glConvolutionParameterf; ++count; }
        if (!glad_glConvolutionParameterfv) { glad_glConvolutionParameterfv = &S_glConvolutionParameterfv; ++count; }
        if (!glad_glConvolutionParameteri) { glad_glConvolutionParameteri = &S_glConvolutionParameteri; ++count; }
        if (!glad_glConvolutionParameteriv) { glad_glConvolutionParameteriv = &S_glConvolutionParameteriv; ++count; }
        if (!glad_glCopyConvolutionFilter1D) { glad_glCopyConvolutionFilter1D = &S_glCopyConvolutionFilter1D; ++count; }
        if (!glad_glCopyConvolutionFilter2D) { glad_glCopyConvolutionFilter2D = &S_glCopyConvolutionFilter2D; ++count; }
        if (!glad_glGetConvolutionFilter) { glad_glGetConvolutionFilter = &S_glGetConvolutionFilter; ++count; }
        if (!glad_glGetConvolutionParameterfv) { glad_glGetConvolutionParameterfv = &S_glGetConvolutionParameterfv; ++count; }
        if (!glad_glGetConvolutionParameteriv) { glad_glGetConvolutionParameteriv = &S_glGetConvolutionParameteriv; ++count; }
        if (!glad_glGetSeparableFilter) { glad_glGetSeparableFilter = &S_glGetSeparableFilter; ++count; }
        if (!glad_glSeparableFilter2D) { glad_glSeparableFilter2D = &S_glSeparableFilter2D; ++count; }
        if (!glad_glGetHistogram) { glad_glGetHistogram = &S_glGetHistogram; ++count; }
        if (!glad_glGetHistogramParameterfv) { glad_glGetHistogramParameterfv = &S_glGetHistogramParameterfv; ++count; }
        if (!glad_glGetHistogramParameteriv) { glad_glGetHistogramParameteriv = &S_glGetHistogramParameteriv; ++count; }
        if (!glad_glGetMinmax) { glad_glGetMinmax = &S_glGetMinmax; ++count; }
        if (!glad_glGetMinmaxParameterfv) { glad_glGetMinmaxParameterfv = &S_glGetMinmaxParameterfv; ++count; }
        if (!glad_glGetMinmaxParameteriv) { glad_glGetMinmaxParameteriv = &S_glGetMinmaxParameteriv; ++count; }
        if (!glad_glHistogram) { glad_glHistogram = &S_glHistogram; ++count; }
        if (!glad_glMinmax) { glad_glMinmax = &S_glMinmax; ++count; }
        if (!glad_glResetHistogram) { glad_glResetHistogram = &S_glResetHistogram; ++count; }
        if (!glad_glResetMinmax) { glad_glResetMinmax = &S_glResetMinmax; ++count; }
        if (!glad_glMultiDrawArraysIndirectCountARB) { glad_glMultiDrawArraysIndirectCountARB = &S_glMultiDrawArraysIndirectCountARB; ++count; }
        if (!glad_glMultiDrawElementsIndirectCountARB) { glad_glMultiDrawElementsIndirectCountARB = &S_glMultiDrawElementsIndirectCountARB; ++count; }
        if (!glad_glVertexAttribDivisorARB) { glad_glVertexAttribDivisorARB = &S_glVertexAttribDivisorARB; ++count; }
        if (!glad_glCurrentPaletteMatrixARB) { glad_glCurrentPaletteMatrixARB = &S_glCurrentPaletteMatrixARB; ++count; }
        if (!glad_glMatrixIndexubvARB) { glad_glMatrixIndexubvARB = &S_glMatrixIndexubvARB; ++count; }
        if (!glad_glMatrixIndexusvARB) { glad_glMatrixIndexusvARB = &S_glMatrixIndexusvARB; ++count; }
        if (!glad_glMatrixIndexuivARB) { glad_glMatrixIndexuivARB = &S_glMatrixIndexuivARB; ++count; }
        if (!glad_glMatrixIndexPointerARB) { glad_glMatrixIndexPointerARB = &S_glMatrixIndexPointerARB; ++count; }
        if (!glad_glSampleCoverageARB) { glad_glSampleCoverageARB = &S_glSampleCoverageARB; ++count; }
        if (!glad_glActiveTextureARB) { glad_glActiveTextureARB = &S_glActiveTextureARB; ++count; }
        if (!glad_glClientActiveTextureARB) { glad_glClientActiveTextureARB = &S_glClientActiveTextureARB; ++count; }
        if (!glad_glMultiTexCoord1dARB) { glad_glMultiTexCoord1dARB = &S_glMultiTexCoord1dARB; ++count; }
        if (!glad_glMultiTexCoord1dvARB) { glad_glMultiTexCoord1dvARB = &S_glMultiTexCoord1dvARB; ++count; }
        if (!glad_glMultiTexCoord1fARB) { glad_glMultiTexCoord1fARB = &S_glMultiTexCoord1fARB; ++count; }
        if (!glad_glMultiTexCoord1fvARB) { glad_glMultiTexCoord1fvARB = &S_glMultiTexCoord1fvARB; ++count; }
        if (!glad_glMultiTexCoord1iARB) { glad_glMultiTexCoord1iARB = &S_glMultiTexCoord1iARB; ++count; }
        if (!glad_glMultiTexCoord1ivARB) { glad_glMultiTexCoord1ivARB = &S_glMultiTexCoord1ivARB; ++count; }
        if (!glad_glMultiTexCoord1sARB) { glad_glMultiTexCoord1sARB = &S_glMultiTexCoord1sARB; ++count; }
        if (!glad_glMultiTexCoord1svARB) { glad_glMultiTexCoord1svARB = &S_glMultiTexCoord1svARB; ++count; }
        if (!glad_glMultiTexCoord2dARB) { glad_glMultiTexCoord2dARB = &S_glMultiTexCoord2dARB; ++count; }
        if (!glad_glMultiTexCoord2dvARB) { glad_glMultiTexCoord2dvARB = &S_glMultiTexCoord2dvARB; ++count; }
        if (!glad_glMultiTexCoord2fARB) { glad_glMultiTexCoord2fARB = &S_glMultiTexCoord2fARB; ++count; }
        if (!glad_glMultiTexCoord2fvARB) { glad_glMultiTexCoord2fvARB = &S_glMultiTexCoord2fvARB; ++count; }
        if (!glad_glMultiTexCoord2iARB) { glad_glMultiTexCoord2iARB = &S_glMultiTexCoord2iARB; ++count; }
        if (!glad_glMultiTexCoord2ivARB) { glad_glMultiTexCoord2ivARB = &S_glMultiTexCoord2ivARB; ++count; }
        if (!glad_glMultiTexCoord2sARB) { glad_glMultiTexCoord2sARB = &S_glMultiTexCoord2sARB; ++count; }
        if (!glad_glMultiTexCoord2svARB) { glad_glMultiTexCoord2svARB = &S_glMultiTexCoord2svARB; ++count; }
        if (!glad_glMultiTexCoord3dARB) { glad_glMultiTexCoord3dARB = &S_glMultiTexCoord3dARB; ++count; }
        if (!glad_glMultiTexCoord3dvARB) { glad_glMultiTexCoord3dvARB = &S_glMultiTexCoord3dvARB; ++count; }
        if (!glad_glMultiTexCoord3fARB) { glad_glMultiTexCoord3fARB = &S_glMultiTexCoord3fARB; ++count; }
        if (!glad_glMultiTexCoord3fvARB) { glad_glMultiTexCoord3fvARB = &S_glMultiTexCoord3fvARB; ++count; }
        if (!glad_glMultiTexCoord3iARB) { glad_glMultiTexCoord3iARB = &S_glMultiTexCoord3iARB; ++count; }
        if (!glad_glMultiTexCoord3ivARB) { glad_glMultiTexCoord3ivARB = &S_glMultiTexCoord3ivARB; ++count; }
        if (!glad_glMultiTexCoord3sARB) { glad_glMultiTexCoord3sARB = &S_glMultiTexCoord3sARB; ++count; }
        if (!glad_glMultiTexCoord3svARB) { glad_glMultiTexCoord3svARB = &S_glMultiTexCoord3svARB; ++count; }
        if (!glad_glMultiTexCoord4dARB) { glad_glMultiTexCoord4dARB = &S_glMultiTexCoord4dARB; ++count; }
        if (!glad_glMultiTexCoord4dvARB) { glad_glMultiTexCoord4dvARB = &S_glMultiTexCoord4dvARB; ++count; }
        if (!glad_glMultiTexCoord4fARB) { glad_glMultiTexCoord4fARB = &S_glMultiTexCoord4fARB; ++count; }
        if (!glad_glMultiTexCoord4fvARB) { glad_glMultiTexCoord4fvARB = &S_glMultiTexCoord4fvARB; ++count; }
        if (!glad_glMultiTexCoord4iARB) { glad_glMultiTexCoord4iARB = &S_glMultiTexCoord4iARB; ++count; }
        if (!glad_glMultiTexCoord4ivARB) { glad_glMultiTexCoord4ivARB = &S_glMultiTexCoord4ivARB; ++count; }
        if (!glad_glMultiTexCoord4sARB) { glad_glMultiTexCoord4sARB = &S_glMultiTexCoord4sARB; ++count; }
        if (!glad_glMultiTexCoord4svARB) { glad_glMultiTexCoord4svARB = &S_glMultiTexCoord4svARB; ++count; }
        if (!glad_glGenQueriesARB) { glad_glGenQueriesARB = &S_glGenQueriesARB; ++count; }
        if (!glad_glDeleteQueriesARB) { glad_glDeleteQueriesARB = &S_glDeleteQueriesARB; ++count; }
        if (!glad_glIsQueryARB) { glad_glIsQueryARB = &S_glIsQueryARB; ++count; }
        if (!glad_glBeginQueryARB) { glad_glBeginQueryARB = &S_glBeginQueryARB; ++count; }
        if (!glad_glEndQueryARB) { glad_glEndQueryARB = &S_glEndQueryARB; ++count; }
        if (!glad_glGetQueryivARB) { glad_glGetQueryivARB = &S_glGetQueryivARB; ++count; }
        if (!glad_glGetQueryObjectivARB) { glad_glGetQueryObjectivARB = &S_glGetQueryObjectivARB; ++count; }
        if (!glad_glGetQueryObjectuivARB) { glad_glGetQueryObjectuivARB = &S_glGetQueryObjectuivARB; ++count; }
        if (!glad_glMaxShaderCompilerThreadsARB) { glad_glMaxShaderCompilerThreadsARB = &S_glMaxShaderCompilerThreadsARB; ++count; }
        if (!glad_glPointParameterfARB) { glad_glPointParameterfARB = &S_glPointParameterfARB; ++count; }
        if (!glad_glPointParameterfvARB) { glad_glPointParameterfvARB = &S_glPointParameterfvARB; ++count; }
        if (!glad_glGetGraphicsResetStatusARB) { glad_glGetGraphicsResetStatusARB = &S_glGetGraphicsResetStatusARB; ++count; }
        if (!glad_glGetnTexImageARB) { glad_glGetnTexImageARB = &S_glGetnTexImageARB; ++count; }
        if (!glad_glReadnPixelsARB) { glad_glReadnPixelsARB = &S_glReadnPixelsARB; ++count; }
        if (!glad_glGetnCompressedTexImageARB) { glad_glGetnCompressedTexImageARB = &S_glGetnCompressedTexImageARB; ++count; }
        if (!glad_glGetnUniformfvARB) { glad_glGetnUniformfvARB = &S_glGetnUniformfvARB; ++count; }
        if (!glad_glGetnUniformivARB) { glad_glGetnUniformivARB = &S_glGetnUniformivARB; ++count; }
        if (!glad_glGetnUniformuivARB) { glad_glGetnUniformuivARB = &S_glGetnUniformuivARB; ++count; }
        if (!glad_glGetnUniformdvARB) { glad_glGetnUniformdvARB = &S_glGetnUniformdvARB; ++count; }
        if (!glad_glGetnMapdvARB) { glad_glGetnMapdvARB = &S_glGetnMapdvARB; ++count; }
        if (!glad_glGetnMapfvARB) { glad_glGetnMapfvARB = &S_glGetnMapfvARB; ++count; }
        if (!glad_glGetnMapivARB) { glad_glGetnMapivARB = &S_glGetnMapivARB; ++count; }
        if (!glad_glGetnPixelMapfvARB) { glad_glGetnPixelMapfvARB = &S_glGetnPixelMapfvARB; ++count; }
        if (!glad_glGetnPixelMapuivARB) { glad_glGetnPixelMapuivARB = &S_glGetnPixelMapuivARB; ++count; }
        if (!glad_glGetnPixelMapusvARB) { glad_glGetnPixelMapusvARB = &S_glGetnPixelMapusvARB; ++count; }
        if (!glad_glGetnPolygonStippleARB) { glad_glGetnPolygonStippleARB = &S_glGetnPolygonStippleARB; ++count; }
        if (!glad_glGetnColorTableARB) { glad_glGetnColorTableARB = &S_glGetnColorTableARB; ++count; }
        if (!glad_glGetnConvolutionFilterARB) { glad_glGetnConvolutionFilterARB = &S_glGetnConvolutionFilterARB; ++count; }
        if (!glad_glGetnSeparableFilterARB) { glad_glGetnSeparableFilterARB = &S_glGetnSeparableFilterARB; ++count; }
        if (!glad_glGetnHistogramARB) { glad_glGetnHistogramARB = &S_glGetnHistogramARB; ++count; }
        if (!glad_glGetnMinmaxARB) { glad_glGetnMinmaxARB = &S_glGetnMinmaxARB; ++count; }
        if (!glad_glFramebufferSampleLocationsfvARB) { glad_glFramebufferSampleLocationsfvARB = &S_glFramebufferSampleLocationsfvARB; ++count; }
        if (!glad_glNamedFramebufferSampleLocationsfvARB) { glad_glNamedFramebufferSampleLocationsfvARB = &S_glNamedFramebufferSampleLocationsfvARB; ++count; }
        if (!glad_glEvaluateDepthValuesARB) { glad_glEvaluateDepthValuesARB = &S_glEvaluateDepthValuesARB; ++count; }
        if (!glad_glMinSampleShadingARB) { glad_glMinSampleShadingARB = &S_glMinSampleShadingARB; ++count; }
        if (!glad_glDeleteObjectARB) { glad_glDeleteObjectARB = &S_glDeleteObjectARB; ++count; }
        if (!glad_glGetHandleARB) { glad_glGetHandleARB = &S_glGetHandleARB; ++count; }
        if (!glad_glDetachObjectARB) { glad_glDetachObjectARB = &S_glDetachObjectARB; ++count; }
        if (!glad_glCreateShaderObjectARB) { glad_glCreateShaderObjectARB = &S_glCreateShaderObjectARB; ++count; }
        if (!glad_glShaderSourceARB) { glad_glShaderSourceARB = &S_glShaderSourceARB; ++count; }
        if (!glad_glCompileShaderARB) { glad_glCompileShaderARB = &S_glCompileShaderARB; ++count; }
        if (!glad_glCreateProgramObjectARB) { glad_glCreateProgramObjectARB = &S_glCreateProgramObjectARB; ++count; }
        if (!glad_glAttachObjectARB) { glad_glAttachObjectARB = &S_glAttachObjectARB; ++count; }
        if (!glad_glLinkProgramARB) { glad_glLinkProgramARB = &S_glLinkProgramARB; ++count; }
        if (!glad_glUseProgramObjectARB) { glad_glUseProgramObjectARB = &S_glUseProgramObjectARB; ++count; }
        if (!glad_glValidateProgramARB) { glad_glValidateProgramARB = &S_glValidateProgramARB; ++count; }
        if (!glad_glUniform1fARB) { glad_glUniform1fARB = &S_glUniform1fARB; ++count; }
        if (!glad_glUniform2fARB) { glad_glUniform2fARB = &S_glUniform2fARB; ++count; }
        if (!glad_glUniform3fARB) { glad_glUniform3fARB = &S_glUniform3fARB; ++count; }
        if (!glad_glUniform4fARB) { glad_glUniform4fARB = &S_glUniform4fARB; ++count; }
        if (!glad_glUniform1iARB) { glad_glUniform1iARB = &S_glUniform1iARB; ++count; }
        if (!glad_glUniform2iARB) { glad_glUniform2iARB = &S_glUniform2iARB; ++count; }
        if (!glad_glUniform3iARB) { glad_glUniform3iARB = &S_glUniform3iARB; ++count; }
        if (!glad_glUniform4iARB) { glad_glUniform4iARB = &S_glUniform4iARB; ++count; }
        if (!glad_glUniform1fvARB) { glad_glUniform1fvARB = &S_glUniform1fvARB; ++count; }
        if (!glad_glUniform2fvARB) { glad_glUniform2fvARB = &S_glUniform2fvARB; ++count; }
        if (!glad_glUniform3fvARB) { glad_glUniform3fvARB = &S_glUniform3fvARB; ++count; }
        if (!glad_glUniform4fvARB) { glad_glUniform4fvARB = &S_glUniform4fvARB; ++count; }
        if (!glad_glUniform1ivARB) { glad_glUniform1ivARB = &S_glUniform1ivARB; ++count; }
        if (!glad_glUniform2ivARB) { glad_glUniform2ivARB = &S_glUniform2ivARB; ++count; }
        if (!glad_glUniform3ivARB) { glad_glUniform3ivARB = &S_glUniform3ivARB; ++count; }
        if (!glad_glUniform4ivARB) { glad_glUniform4ivARB = &S_glUniform4ivARB; ++count; }
        if (!glad_glUniformMatrix2fvARB) { glad_glUniformMatrix2fvARB = &S_glUniformMatrix2fvARB; ++count; }
        if (!glad_glUniformMatrix3fvARB) { glad_glUniformMatrix3fvARB = &S_glUniformMatrix3fvARB; ++count; }
        if (!glad_glUniformMatrix4fvARB) { glad_glUniformMatrix4fvARB = &S_glUniformMatrix4fvARB; ++count; }
        if (!glad_glGetObjectParameterfvARB) { glad_glGetObjectParameterfvARB = &S_glGetObjectParameterfvARB; ++count; }
        if (!glad_glGetObjectParameterivARB) { glad_glGetObjectParameterivARB = &S_glGetObjectParameterivARB; ++count; }
        if (!glad_glGetInfoLogARB) { glad_glGetInfoLogARB = &S_glGetInfoLogARB; ++count; }
        if (!glad_glGetAttachedObjectsARB) { glad_glGetAttachedObjectsARB = &S_glGetAttachedObjectsARB; ++count; }
        if (!glad_glGetUniformLocationARB) { glad_glGetUniformLocationARB = &S_glGetUniformLocationARB; ++count; }
        if (!glad_glGetActiveUniformARB) { glad_glGetActiveUniformARB = &S_glGetActiveUniformARB; ++count; }
        if (!glad_glGetUniformfvARB) { glad_glGetUniformfvARB = &S_glGetUniformfvARB; ++count; }
        if (!glad_glGetUniformivARB) { glad_glGetUniformivARB = &S_glGetUniformivARB; ++count; }
        if (!glad_glGetShaderSourceARB) { glad_glGetShaderSourceARB = &S_glGetShaderSourceARB; ++count; }
        if (!glad_glNamedStringARB) { glad_glNamedStringARB = &S_glNamedStringARB; ++count; }
        if (!glad_glDeleteNamedStringARB) { glad_glDeleteNamedStringARB = &S_glDeleteNamedStringARB; ++count; }
        if (!glad_glCompileShaderIncludeARB) { glad_glCompileShaderIncludeARB = &S_glCompileShaderIncludeARB; ++count; }
        if (!glad_glIsNamedStringARB) { glad_glIsNamedStringARB = &S_glIsNamedStringARB; ++count; }
        if (!glad_glGetNamedStringARB) { glad_glGetNamedStringARB = &S_glGetNamedStringARB; ++count; }
        if (!glad_glGetNamedStringivARB) { glad_glGetNamedStringivARB = &S_glGetNamedStringivARB; ++count; }
        if (!glad_glBufferPageCommitmentARB) { glad_glBufferPageCommitmentARB = &S_glBufferPageCommitmentARB; ++count; }
        if (!glad_glNamedBufferPageCommitmentEXT) { glad_glNamedBufferPageCommitmentEXT = &S_glNamedBufferPageCommitmentEXT; ++count; }
        if (!glad_glNamedBufferPageCommitmentARB) { glad_glNamedBufferPageCommitmentARB = &S_glNamedBufferPageCommitmentARB; ++count; }
        if (!glad_glTexPageCommitmentARB) { glad_glTexPageCommitmentARB = &S_glTexPageCommitmentARB; ++count; }
        if (!glad_glTexBufferARB) { glad_glTexBufferARB = &S_glTexBufferARB; ++count; }
        if (!glad_glCompressedTexImage3DARB) { glad_glCompressedTexImage3DARB = &S_glCompressedTexImage3DARB; ++count; }
        if (!glad_glCompressedTexImage2DARB) { glad_glCompressedTexImage2DARB = &S_glCompressedTexImage2DARB; ++count; }
        if (!glad_glCompressedTexImage1DARB) { glad_glCompressedTexImage1DARB = &S_glCompressedTexImage1DARB; ++count; }
        if (!glad_glCompressedTexSubImage3DARB) { glad_glCompressedTexSubImage3DARB = &S_glCompressedTexSubImage3DARB; ++count; }
        if (!glad_glCompressedTexSubImage2DARB) { glad_glCompressedTexSubImage2DARB = &S_glCompressedTexSubImage2DARB; ++count; }
        if (!glad_glCompressedTexSubImage1DARB) { glad_glCompressedTexSubImage1DARB = &S_glCompressedTexSubImage1DARB; ++count; }
        if (!glad_glGetCompressedTexImageARB) { glad_glGetCompressedTexImageARB = &S_glGetCompressedTexImageARB; ++count; }
        if (!glad_glLoadTransposeMatrixfARB) { glad_glLoadTransposeMatrixfARB = &S_glLoadTransposeMatrixfARB; ++count; }
        if (!glad_glLoadTransposeMatrixdARB) { glad_glLoadTransposeMatrixdARB = &S_glLoadTransposeMatrixdARB; ++count; }
        if (!glad_glMultTransposeMatrixfARB) { glad_glMultTransposeMatrixfARB = &S_glMultTransposeMatrixfARB; ++count; }
        if (!glad_glMultTransposeMatrixdARB) { glad_glMultTransposeMatrixdARB = &S_glMultTransposeMatrixdARB; ++count; }
        if (!glad_glWeightbvARB) { glad_glWeightbvARB = &S_glWeightbvARB; ++count; }
        if (!glad_glWeightsvARB) { glad_glWeightsvARB = &S_glWeightsvARB; ++count; }
        if (!glad_glWeightivARB) { glad_glWeightivARB = &S_glWeightivARB; ++count; }
        if (!glad_glWeightfvARB) { glad_glWeightfvARB = &S_glWeightfvARB; ++count; }
        if (!glad_glWeightdvARB) { glad_glWeightdvARB = &S_glWeightdvARB; ++count; }
        if (!glad_glWeightubvARB) { glad_glWeightubvARB = &S_glWeightubvARB; ++count; }
        if (!glad_glWeightusvARB) { glad_glWeightusvARB = &S_glWeightusvARB; ++count; }
        if (!glad_glWeightuivARB) { glad_glWeightuivARB = &S_glWeightuivARB; ++count; }
        if (!glad_glWeightPointerARB) { glad_glWeightPointerARB = &S_glWeightPointerARB; ++count; }
        if (!glad_glVertexBlendARB) { glad_glVertexBlendARB = &S_glVertexBlendARB; ++count; }
        if (!glad_glBindBufferARB) { glad_glBindBufferARB = &S_glBindBufferARB; ++count; }
        if (!glad_glDeleteBuffersARB) { glad_glDeleteBuffersARB = &S_glDeleteBuffersARB; ++count; }
        if (!glad_glGenBuffersARB) { glad_glGenBuffersARB = &S_glGenBuffersARB; ++count; }
        if (!glad_glIsBufferARB) { glad_glIsBufferARB = &S_glIsBufferARB; ++count; }
        if (!glad_glBufferDataARB) { glad_glBufferDataARB = &S_glBufferDataARB; ++count; }
        if (!glad_glBufferSubDataARB) { glad_glBufferSubDataARB = &S_glBufferSubDataARB; ++count; }
        if (!glad_glGetBufferSubDataARB) { glad_glGetBufferSubDataARB = &S_glGetBufferSubDataARB; ++count; }
        if (!glad_glMapBufferARB) { glad_glMapBufferARB = &S_glMapBufferARB; ++count; }
        if (!glad_glUnmapBufferARB) { glad_glUnmapBufferARB = &S_glUnmapBufferARB; ++count; }
        if (!glad_glGetBufferParameterivARB) { glad_glGetBufferParameterivARB = &S_glGetBufferParameterivARB; ++count; }
        if (!glad_glGetBufferPointervARB) { glad_glGetBufferPointervARB = &S_glGetBufferPointervARB; ++count; }
        if (!glad_glVertexAttrib1dARB) { glad_glVertexAttrib1dARB = &S_glVertexAttrib1dARB; ++count; }
        if (!glad_glVertexAttrib1dvARB) { glad_glVertexAttrib1dvARB = &S_glVertexAttrib1dvARB; ++count; }
        if (!glad_glVertexAttrib1fARB) { glad_glVertexAttrib1fARB = &S_glVertexAttrib1fARB; ++count; }
        if (!glad_glVertexAttrib1fvARB) { glad_glVertexAttrib1fvARB = &S_glVertexAttrib1fvARB; ++count; }
        if (!glad_glVertexAttrib1sARB) { glad_glVertexAttrib1sARB = &S_glVertexAttrib1sARB; ++count; }
        if (!glad_glVertexAttrib1svARB) { glad_glVertexAttrib1svARB = &S_glVertexAttrib1svARB; ++count; }
        if (!glad_glVertexAttrib2dARB) { glad_glVertexAttrib2dARB = &S_glVertexAttrib2dARB; ++count; }
        if (!glad_glVertexAttrib2dvARB) { glad_glVertexAttrib2dvARB = &S_glVertexAttrib2dvARB; ++count; }
        if (!glad_glVertexAttrib2fARB) { glad_glVertexAttrib2fARB = &S_glVertexAttrib2fARB; ++count; }
        if (!glad_glVertexAttrib2fvARB) { glad_glVertexAttrib2fvARB = &S_glVertexAttrib2fvARB; ++count; }
        if (!glad_glVertexAttrib2sARB) { glad_glVertexAttrib2sARB = &S_glVertexAttrib2sARB; ++count; }
        if (!glad_glVertexAttrib2svARB) { glad_glVertexAttrib2svARB = &S_glVertexAttrib2svARB; ++count; }
        if (!glad_glVertexAttrib3dARB) { glad_glVertexAttrib3dARB = &S_glVertexAttrib3dARB; ++count; }
        if (!glad_glVertexAttrib3dvARB) { glad_glVertexAttrib3dvARB = &S_glVertexAttrib3dvARB; ++count; }
        if (!glad_glVertexAttrib3fARB) { glad_glVertexAttrib3fARB = &S_glVertexAttrib3fARB; ++count; }
        if (!glad_glVertexAttrib3fvARB) { glad_glVertexAttrib3fvARB = &S_glVertexAttrib3fvARB; ++count; }
        if (!glad_glVertexAttrib3sARB) { glad_glVertexAttrib3sARB = &S_glVertexAttrib3sARB; ++count; }
        if (!glad_glVertexAttrib3svARB) { glad_glVertexAttrib3svARB = &S_glVertexAttrib3svARB; ++count; }
        if (!glad_glVertexAttrib4NbvARB) { glad_glVertexAttrib4NbvARB = &S_glVertexAttrib4NbvARB; ++count; }
        if (!glad_glVertexAttrib4NivARB) { glad_glVertexAttrib4NivARB = &S_glVertexAttrib4NivARB; ++count; }
        if (!glad_glVertexAttrib4NsvARB) { glad_glVertexAttrib4NsvARB = &S_glVertexAttrib4NsvARB; ++count; }
        if (!glad_glVertexAttrib4NubARB) { glad_glVertexAttrib4NubARB = &S_glVertexAttrib4NubARB; ++count; }
        if (!glad_glVertexAttrib4NubvARB) { glad_glVertexAttrib4NubvARB = &S_glVertexAttrib4NubvARB; ++count; }
        if (!glad_glVertexAttrib4NuivARB) { glad_glVertexAttrib4NuivARB = &S_glVertexAttrib4NuivARB; ++count; }
        if (!glad_glVertexAttrib4NusvARB) { glad_glVertexAttrib4NusvARB = &S_glVertexAttrib4NusvARB; ++count; }
        if (!glad_glVertexAttrib4bvARB) { glad_glVertexAttrib4bvARB = &S_glVertexAttrib4bvARB; ++count; }
        if (!glad_glVertexAttrib4dARB) { glad_glVertexAttrib4dARB = &S_glVertexAttrib4dARB; ++count; }
        if (!glad_glVertexAttrib4dvARB) { glad_glVertexAttrib4dvARB = &S_glVertexAttrib4dvARB; ++count; }
        if (!glad_glVertexAttrib4fARB) { glad_glVertexAttrib4fARB = &S_glVertexAttrib4fARB; ++count; }
        if (!glad_glVertexAttrib4fvARB) { glad_glVertexAttrib4fvARB = &S_glVertexAttrib4fvARB; ++count; }
        if (!glad_glVertexAttrib4ivARB) { glad_glVertexAttrib4ivARB = &S_glVertexAttrib4ivARB; ++count; }
        if (!glad_glVertexAttrib4sARB) { glad_glVertexAttrib4sARB = &S_glVertexAttrib4sARB; ++count; }
        if (!glad_glVertexAttrib4svARB) { glad_glVertexAttrib4svARB = &S_glVertexAttrib4svARB; ++count; }
        if (!glad_glVertexAttrib4ubvARB) { glad_glVertexAttrib4ubvARB = &S_glVertexAttrib4ubvARB; ++count; }
        if (!glad_glVertexAttrib4uivARB) { glad_glVertexAttrib4uivARB = &S_glVertexAttrib4uivARB; ++count; }
        if (!glad_glVertexAttrib4usvARB) { glad_glVertexAttrib4usvARB = &S_glVertexAttrib4usvARB; ++count; }
        if (!glad_glVertexAttribPointerARB) { glad_glVertexAttribPointerARB = &S_glVertexAttribPointerARB; ++count; }
        if (!glad_glEnableVertexAttribArrayARB) { glad_glEnableVertexAttribArrayARB = &S_glEnableVertexAttribArrayARB; ++count; }
        if (!glad_glDisableVertexAttribArrayARB) { glad_glDisableVertexAttribArrayARB = &S_glDisableVertexAttribArrayARB; ++count; }
        if (!glad_glGetVertexAttribdvARB) { glad_glGetVertexAttribdvARB = &S_glGetVertexAttribdvARB; ++count; }
        if (!glad_glGetVertexAttribfvARB) { glad_glGetVertexAttribfvARB = &S_glGetVertexAttribfvARB; ++count; }
        if (!glad_glGetVertexAttribivARB) { glad_glGetVertexAttribivARB = &S_glGetVertexAttribivARB; ++count; }
        if (!glad_glGetVertexAttribPointervARB) { glad_glGetVertexAttribPointervARB = &S_glGetVertexAttribPointervARB; ++count; }
        if (!glad_glBindAttribLocationARB) { glad_glBindAttribLocationARB = &S_glBindAttribLocationARB; ++count; }
        if (!glad_glGetActiveAttribARB) { glad_glGetActiveAttribARB = &S_glGetActiveAttribARB; ++count; }
        if (!glad_glGetAttribLocationARB) { glad_glGetAttribLocationARB = &S_glGetAttribLocationARB; ++count; }
        if (!glad_glDepthRangeArraydvNV) { glad_glDepthRangeArraydvNV = &S_glDepthRangeArraydvNV; ++count; }
        if (!glad_glDepthRangeIndexeddNV) { glad_glDepthRangeIndexeddNV = &S_glDepthRangeIndexeddNV; ++count; }
        if (!glad_glWindowPos2dARB) { glad_glWindowPos2dARB = &S_glWindowPos2dARB; ++count; }
        if (!glad_glWindowPos2dvARB) { glad_glWindowPos2dvARB = &S_glWindowPos2dvARB; ++count; }
        if (!glad_glWindowPos2fARB) { glad_glWindowPos2fARB = &S_glWindowPos2fARB; ++count; }
        if (!glad_glWindowPos2fvARB) { glad_glWindowPos2fvARB = &S_glWindowPos2fvARB; ++count; }
        if (!glad_glWindowPos2iARB) { glad_glWindowPos2iARB = &S_glWindowPos2iARB; ++count; }
        if (!glad_glWindowPos2ivARB) { glad_glWindowPos2ivARB = &S_glWindowPos2ivARB; ++count; }
        if (!glad_glWindowPos2sARB) { glad_glWindowPos2sARB = &S_glWindowPos2sARB; ++count; }
        if (!glad_glWindowPos2svARB) { glad_glWindowPos2svARB = &S_glWindowPos2svARB; ++count; }
        if (!glad_glWindowPos3dARB) { glad_glWindowPos3dARB = &S_glWindowPos3dARB; ++count; }
        if (!glad_glWindowPos3dvARB) { glad_glWindowPos3dvARB = &S_glWindowPos3dvARB; ++count; }
        if (!glad_glWindowPos3fARB) { glad_glWindowPos3fARB = &S_glWindowPos3fARB; ++count; }
        if (!glad_glWindowPos3fvARB) { glad_glWindowPos3fvARB = &S_glWindowPos3fvARB; ++count; }
        if (!glad_glWindowPos3iARB) { glad_glWindowPos3iARB = &S_glWindowPos3iARB; ++count; }
        if (!glad_glWindowPos3ivARB) { glad_glWindowPos3ivARB = &S_glWindowPos3ivARB; ++count; }
        if (!glad_glWindowPos3sARB) { glad_glWindowPos3sARB = &S_glWindowPos3sARB; ++count; }
        if (!glad_glWindowPos3svARB) { glad_glWindowPos3svARB = &S_glWindowPos3svARB; ++count; }
        if (!glad_glDrawBuffersATI) { glad_glDrawBuffersATI = &S_glDrawBuffersATI; ++count; }
        if (!glad_glElementPointerATI) { glad_glElementPointerATI = &S_glElementPointerATI; ++count; }
        if (!glad_glDrawElementArrayATI) { glad_glDrawElementArrayATI = &S_glDrawElementArrayATI; ++count; }
        if (!glad_glDrawRangeElementArrayATI) { glad_glDrawRangeElementArrayATI = &S_glDrawRangeElementArrayATI; ++count; }
        if (!glad_glTexBumpParameterivATI) { glad_glTexBumpParameterivATI = &S_glTexBumpParameterivATI; ++count; }
        if (!glad_glTexBumpParameterfvATI) { glad_glTexBumpParameterfvATI = &S_glTexBumpParameterfvATI; ++count; }
        if (!glad_glGetTexBumpParameterivATI) { glad_glGetTexBumpParameterivATI = &S_glGetTexBumpParameterivATI; ++count; }
        if (!glad_glGetTexBumpParameterfvATI) { glad_glGetTexBumpParameterfvATI = &S_glGetTexBumpParameterfvATI; ++count; }
        if (!glad_glGenFragmentShadersATI) { glad_glGenFragmentShadersATI = &S_glGenFragmentShadersATI; ++count; }
        if (!glad_glBindFragmentShaderATI) { glad_glBindFragmentShaderATI = &S_glBindFragmentShaderATI; ++count; }
        if (!glad_glDeleteFragmentShaderATI) { glad_glDeleteFragmentShaderATI = &S_glDeleteFragmentShaderATI; ++count; }
        if (!glad_glBeginFragmentShaderATI) { glad_glBeginFragmentShaderATI = &S_glBeginFragmentShaderATI; ++count; }
        if (!glad_glEndFragmentShaderATI) { glad_glEndFragmentShaderATI = &S_glEndFragmentShaderATI; ++count; }
        if (!glad_glPassTexCoordATI) { glad_glPassTexCoordATI = &S_glPassTexCoordATI; ++count; }
        if (!glad_glSampleMapATI) { glad_glSampleMapATI = &S_glSampleMapATI; ++count; }
        if (!glad_glColorFragmentOp1ATI) { glad_glColorFragmentOp1ATI = &S_glColorFragmentOp1ATI; ++count; }
        if (!glad_glColorFragmentOp2ATI) { glad_glColorFragmentOp2ATI = &S_glColorFragmentOp2ATI; ++count; }
        if (!glad_glColorFragmentOp3ATI) { glad_glColorFragmentOp3ATI = &S_glColorFragmentOp3ATI; ++count; }
        if (!glad_glAlphaFragmentOp1ATI) { glad_glAlphaFragmentOp1ATI = &S_glAlphaFragmentOp1ATI; ++count; }
        if (!glad_glAlphaFragmentOp2ATI) { glad_glAlphaFragmentOp2ATI = &S_glAlphaFragmentOp2ATI; ++count; }
        if (!glad_glAlphaFragmentOp3ATI) { glad_glAlphaFragmentOp3ATI = &S_glAlphaFragmentOp3ATI; ++count; }
        if (!glad_glSetFragmentShaderConstantATI) { glad_glSetFragmentShaderConstantATI = &S_glSetFragmentShaderConstantATI; ++count; }
        if (!glad_glMapObjectBufferATI) { glad_glMapObjectBufferATI = &S_glMapObjectBufferATI; ++count; }
        if (!glad_glUnmapObjectBufferATI) { glad_glUnmapObjectBufferATI = &S_glUnmapObjectBufferATI; ++count; }
        if (!glad_glPNTrianglesiATI) { glad_glPNTrianglesiATI = &S_glPNTrianglesiATI; ++count; }
        if (!glad_glPNTrianglesfATI) { glad_glPNTrianglesfATI = &S_glPNTrianglesfATI; ++count; }
        if (!glad_glStencilOpSeparateATI) { glad_glStencilOpSeparateATI = &S_glStencilOpSeparateATI; ++count; }
        if (!glad_glStencilFuncSeparateATI) { glad_glStencilFuncSeparateATI = &S_glStencilFuncSeparateATI; ++count; }
        if (!glad_glNewObjectBufferATI) { glad_glNewObjectBufferATI = &S_glNewObjectBufferATI; ++count; }
        if (!glad_glIsObjectBufferATI) { glad_glIsObjectBufferATI = &S_glIsObjectBufferATI; ++count; }
        if (!glad_glUpdateObjectBufferATI) { glad_glUpdateObjectBufferATI = &S_glUpdateObjectBufferATI; ++count; }
        if (!glad_glGetObjectBufferfvATI) { glad_glGetObjectBufferfvATI = &S_glGetObjectBufferfvATI; ++count; }
        if (!glad_glGetObjectBufferivATI) { glad_glGetObjectBufferivATI = &S_glGetObjectBufferivATI; ++count; }
        if (!glad_glFreeObjectBufferATI) { glad_glFreeObjectBufferATI = &S_glFreeObjectBufferATI; ++count; }
        if (!glad_glArrayObjectATI) { glad_glArrayObjectATI = &S_glArrayObjectATI; ++count; }
        if (!glad_glGetArrayObjectfvATI) { glad_glGetArrayObjectfvATI = &S_glGetArrayObjectfvATI; ++count; }
        if (!glad_glGetArrayObjectivATI) { glad_glGetArrayObjectivATI = &S_glGetArrayObjectivATI; ++count; }
        if (!glad_glVariantArrayObjectATI) { glad_glVariantArrayObjectATI = &S_glVariantArrayObjectATI; ++count; }
        if (!glad_glGetVariantArrayObjectfvATI) { glad_glGetVariantArrayObjectfvATI = &S_glGetVariantArrayObjectfvATI; ++count; }
        if (!glad_glGetVariantArrayObjectivATI) { glad_glGetVariantArrayObjectivATI = &S_glGetVariantArrayObjectivATI; ++count; }
        if (!glad_glVertexAttribArrayObjectATI) { glad_glVertexAttribArrayObjectATI = &S_glVertexAttribArrayObjectATI; ++count; }
        if (!glad_glGetVertexAttribArrayObjectfvATI) { glad_glGetVertexAttribArrayObjectfvATI = &S_glGetVertexAttribArrayObjectfvATI; ++count; }
        if (!glad_glGetVertexAttribArrayObjectivATI) { glad_glGetVertexAttribArrayObjectivATI = &S_glGetVertexAttribArrayObjectivATI; ++count; }
        if (!glad_glVertexStream1sATI) { glad_glVertexStream1sATI = &S_glVertexStream1sATI; ++count; }
        if (!glad_glVertexStream1svATI) { glad_glVertexStream1svATI = &S_glVertexStream1svATI; ++count; }
        if (!glad_glVertexStream1iATI) { glad_glVertexStream1iATI = &S_glVertexStream1iATI; ++count; }
        if (!glad_glVertexStream1ivATI) { glad_glVertexStream1ivATI = &S_glVertexStream1ivATI; ++count; }
        if (!glad_glVertexStream1fATI) { glad_glVertexStream1fATI = &S_glVertexStream1fATI; ++count; }
        if (!glad_glVertexStream1fvATI) { glad_glVertexStream1fvATI = &S_glVertexStream1fvATI; ++count; }
        if (!glad_glVertexStream1dATI) { glad_glVertexStream1dATI = &S_glVertexStream1dATI; ++count; }
        if (!glad_glVertexStream1dvATI) { glad_glVertexStream1dvATI = &S_glVertexStream1dvATI; ++count; }
        if (!glad_glVertexStream2sATI) { glad_glVertexStream2sATI = &S_glVertexStream2sATI; ++count; }
        if (!glad_glVertexStream2svATI) { glad_glVertexStream2svATI = &S_glVertexStream2svATI; ++count; }
        if (!glad_glVertexStream2iATI) { glad_glVertexStream2iATI = &S_glVertexStream2iATI; ++count; }
        if (!glad_glVertexStream2ivATI) { glad_glVertexStream2ivATI = &S_glVertexStream2ivATI; ++count; }
        if (!glad_glVertexStream2fATI) { glad_glVertexStream2fATI = &S_glVertexStream2fATI; ++count; }
        if (!glad_glVertexStream2fvATI) { glad_glVertexStream2fvATI = &S_glVertexStream2fvATI; ++count; }
        if (!glad_glVertexStream2dATI) { glad_glVertexStream2dATI = &S_glVertexStream2dATI; ++count; }
        if (!glad_glVertexStream2dvATI) { glad_glVertexStream2dvATI = &S_glVertexStream2dvATI; ++count; }
        if (!glad_glVertexStream3sATI) { glad_glVertexStream3sATI = &S_glVertexStream3sATI; ++count; }
        if (!glad_glVertexStream3svATI) { glad_glVertexStream3svATI = &S_glVertexStream3svATI; ++count; }
        if (!glad_glVertexStream3iATI) { glad_glVertexStream3iATI = &S_glVertexStream3iATI; ++count; }
        if (!glad_glVertexStream3ivATI) { glad_glVertexStream3ivATI = &S_glVertexStream3ivATI; ++count; }
        if (!glad_glVertexStream3fATI) { glad_glVertexStream3fATI = &S_glVertexStream3fATI; ++count; }
        if (!glad_glVertexStream3fvATI) { glad_glVertexStream3fvATI = &S_glVertexStream3fvATI; ++count; }
        if (!glad_glVertexStream3dATI) { glad_glVertexStream3dATI = &S_glVertexStream3dATI; ++count; }
        if (!glad_glVertexStream3dvATI) { glad_glVertexStream3dvATI = &S_glVertexStream3dvATI; ++count; }
        if (!glad_glVertexStream4sATI) { glad_glVertexStream4sATI = &S_glVertexStream4sATI; ++count; }
        if (!glad_glVertexStream4svATI) { glad_glVertexStream4svATI = &S_glVertexStream4svATI; ++count; }
        if (!glad_glVertexStream4iATI) { glad_glVertexStream4iATI = &S_glVertexStream4iATI; ++count; }
        if (!glad_glVertexStream4ivATI) { glad_glVertexStream4ivATI = &S_glVertexStream4ivATI; ++count; }
        if (!glad_glVertexStream4fATI) { glad_glVertexStream4fATI = &S_glVertexStream4fATI; ++count; }
        if (!glad_glVertexStream4fvATI) { glad_glVertexStream4fvATI = &S_glVertexStream4fvATI; ++count; }
        if (!glad_glVertexStream4dATI) { glad_glVertexStream4dATI = &S_glVertexStream4dATI; ++count; }
        if (!glad_glVertexStream4dvATI) { glad_glVertexStream4dvATI = &S_glVertexStream4dvATI; ++count; }
        if (!glad_glNormalStream3bATI) { glad_glNormalStream3bATI = &S_glNormalStream3bATI; ++count; }
        if (!glad_glNormalStream3bvATI) { glad_glNormalStream3bvATI = &S_glNormalStream3bvATI; ++count; }
        if (!glad_glNormalStream3sATI) { glad_glNormalStream3sATI = &S_glNormalStream3sATI; ++count; }
        if (!glad_glNormalStream3svATI) { glad_glNormalStream3svATI = &S_glNormalStream3svATI; ++count; }
        if (!glad_glNormalStream3iATI) { glad_glNormalStream3iATI = &S_glNormalStream3iATI; ++count; }
        if (!glad_glNormalStream3ivATI) { glad_glNormalStream3ivATI = &S_glNormalStream3ivATI; ++count; }
        if (!glad_glNormalStream3fATI) { glad_glNormalStream3fATI = &S_glNormalStream3fATI; ++count; }
        if (!glad_glNormalStream3fvATI) { glad_glNormalStream3fvATI = &S_glNormalStream3fvATI; ++count; }
        if (!glad_glNormalStream3dATI) { glad_glNormalStream3dATI = &S_glNormalStream3dATI; ++count; }
        if (!glad_glNormalStream3dvATI) { glad_glNormalStream3dvATI = &S_glNormalStream3dvATI; ++count; }
        if (!glad_glClientActiveVertexStreamATI) { glad_glClientActiveVertexStreamATI = &S_glClientActiveVertexStreamATI; ++count; }
        if (!glad_glVertexBlendEnviATI) { glad_glVertexBlendEnviATI = &S_glVertexBlendEnviATI; ++count; }
        if (!glad_glVertexBlendEnvfATI) { glad_glVertexBlendEnvfATI = &S_glVertexBlendEnvfATI; ++count; }
        if (!glad_glEGLImageTargetTexStorageEXT) { glad_glEGLImageTargetTexStorageEXT = &S_glEGLImageTargetTexStorageEXT; ++count; }
        if (!glad_glEGLImageTargetTextureStorageEXT) { glad_glEGLImageTargetTextureStorageEXT = &S_glEGLImageTargetTextureStorageEXT; ++count; }
        if (!glad_glUniformBufferEXT) { glad_glUniformBufferEXT = &S_glUniformBufferEXT; ++count; }
        if (!glad_glGetUniformBufferSizeEXT) { glad_glGetUniformBufferSizeEXT = &S_glGetUniformBufferSizeEXT; ++count; }
        if (!glad_glGetUniformOffsetEXT) { glad_glGetUniformOffsetEXT = &S_glGetUniformOffsetEXT; ++count; }
        if (!glad_glBlendColorEXT) { glad_glBlendColorEXT = &S_glBlendColorEXT; ++count; }
        if (!glad_glBlendEquationSeparateEXT) { glad_glBlendEquationSeparateEXT = &S_glBlendEquationSeparateEXT; ++count; }
        if (!glad_glBlendFuncSeparateEXT) { glad_glBlendFuncSeparateEXT = &S_glBlendFuncSeparateEXT; ++count; }
        if (!glad_glBlendEquationEXT) { glad_glBlendEquationEXT = &S_glBlendEquationEXT; ++count; }
        if (!glad_glColorSubTableEXT) { glad_glColorSubTableEXT = &S_glColorSubTableEXT; ++count; }
        if (!glad_glCopyColorSubTableEXT) { glad_glCopyColorSubTableEXT = &S_glCopyColorSubTableEXT; ++count; }
        if (!glad_glLockArraysEXT) { glad_glLockArraysEXT = &S_glLockArraysEXT; ++count; }
        if (!glad_glUnlockArraysEXT) { glad_glUnlockArraysEXT = &S_glUnlockArraysEXT; ++count; }
        if (!glad_glConvolutionFilter1DEXT) { glad_glConvolutionFilter1DEXT = &S_glConvolutionFilter1DEXT; ++count; }
        if (!glad_glConvolutionFilter2DEXT) { glad_glConvolutionFilter2DEXT = &S_glConvolutionFilter2DEXT; ++count; }
        if (!glad_glConvolutionParameterfEXT) { glad_glConvolutionParameterfEXT = &S_glConvolutionParameterfEXT; ++count; }
        if (!glad_glConvolutionParameterfvEXT) { glad_glConvolutionParameterfvEXT = &S_glConvolutionParameterfvEXT; ++count; }
        if (!glad_glConvolutionParameteriEXT) { glad_glConvolutionParameteriEXT = &S_glConvolutionParameteriEXT; ++count; }
        if (!glad_glConvolutionParameterivEXT) { glad_glConvolutionParameterivEXT = &S_glConvolutionParameterivEXT; ++count; }
        if (!glad_glCopyConvolutionFilter1DEXT) { glad_glCopyConvolutionFilter1DEXT = &S_glCopyConvolutionFilter1DEXT; ++count; }
        if (!glad_glCopyConvolutionFilter2DEXT) { glad_glCopyConvolutionFilter2DEXT = &S_glCopyConvolutionFilter2DEXT; ++count; }
        if (!glad_glGetConvolutionFilterEXT) { glad_glGetConvolutionFilterEXT = &S_glGetConvolutionFilterEXT; ++count; }
        if (!glad_glGetConvolutionParameterfvEXT) { glad_glGetConvolutionParameterfvEXT = &S_glGetConvolutionParameterfvEXT; ++count; }
        if (!glad_glGetConvolutionParameterivEXT) { glad_glGetConvolutionParameterivEXT = &S_glGetConvolutionParameterivEXT; ++count; }
        if (!glad_glGetSeparableFilterEXT) { glad_glGetSeparableFilterEXT = &S_glGetSeparableFilterEXT; ++count; }
        if (!glad_glSeparableFilter2DEXT) { glad_glSeparableFilter2DEXT = &S_glSeparableFilter2DEXT; ++count; }
        if (!glad_glTangent3bEXT) { glad_glTangent3bEXT = &S_glTangent3bEXT; ++count; }
        if (!glad_glTangent3bvEXT) { glad_glTangent3bvEXT = &S_glTangent3bvEXT; ++count; }
        if (!glad_glTangent3dEXT) { glad_glTangent3dEXT = &S_glTangent3dEXT; ++count; }
        if (!glad_glTangent3dvEXT) { glad_glTangent3dvEXT = &S_glTangent3dvEXT; ++count; }
        if (!glad_glTangent3fEXT) { glad_glTangent3fEXT = &S_glTangent3fEXT; ++count; }
        if (!glad_glTangent3fvEXT) { glad_glTangent3fvEXT = &S_glTangent3fvEXT; ++count; }
        if (!glad_glTangent3iEXT) { glad_glTangent3iEXT = &S_glTangent3iEXT; ++count; }
        if (!glad_glTangent3ivEXT) { glad_glTangent3ivEXT = &S_glTangent3ivEXT; ++count; }
        if (!glad_glTangent3sEXT) { glad_glTangent3sEXT = &S_glTangent3sEXT; ++count; }
        if (!glad_glTangent3svEXT) { glad_glTangent3svEXT = &S_glTangent3svEXT; ++count; }
        if (!glad_glBinormal3bEXT) { glad_glBinormal3bEXT = &S_glBinormal3bEXT; ++count; }
        if (!glad_glBinormal3bvEXT) { glad_glBinormal3bvEXT = &S_glBinormal3bvEXT; ++count; }
        if (!glad_glBinormal3dEXT) { glad_glBinormal3dEXT = &S_glBinormal3dEXT; ++count; }
        if (!glad_glBinormal3dvEXT) { glad_glBinormal3dvEXT = &S_glBinormal3dvEXT; ++count; }
        if (!glad_glBinormal3fEXT) { glad_glBinormal3fEXT = &S_glBinormal3fEXT; ++count; }
        if (!glad_glBinormal3fvEXT) { glad_glBinormal3fvEXT = &S_glBinormal3fvEXT; ++count; }
        if (!glad_glBinormal3iEXT) { glad_glBinormal3iEXT = &S_glBinormal3iEXT; ++count; }
        if (!glad_glBinormal3ivEXT) { glad_glBinormal3ivEXT = &S_glBinormal3ivEXT; ++count; }
        if (!glad_glBinormal3sEXT) { glad_glBinormal3sEXT = &S_glBinormal3sEXT; ++count; }
        if (!glad_glBinormal3svEXT) { glad_glBinormal3svEXT = &S_glBinormal3svEXT; ++count; }
        if (!glad_glTangentPointerEXT) { glad_glTangentPointerEXT = &S_glTangentPointerEXT; ++count; }
        if (!glad_glBinormalPointerEXT) { glad_glBinormalPointerEXT = &S_glBinormalPointerEXT; ++count; }
        if (!glad_glCopyTexImage1DEXT) { glad_glCopyTexImage1DEXT = &S_glCopyTexImage1DEXT; ++count; }
        if (!glad_glCopyTexImage2DEXT) { glad_glCopyTexImage2DEXT = &S_glCopyTexImage2DEXT; ++count; }
        if (!glad_glCopyTexSubImage1DEXT) { glad_glCopyTexSubImage1DEXT = &S_glCopyTexSubImage1DEXT; ++count; }
        if (!glad_glCopyTexSubImage2DEXT) { glad_glCopyTexSubImage2DEXT = &S_glCopyTexSubImage2DEXT; ++count; }
        if (!glad_glCopyTexSubImage3DEXT) { glad_glCopyTexSubImage3DEXT = &S_glCopyTexSubImage3DEXT; ++count; }
        if (!glad_glCullParameterdvEXT) { glad_glCullParameterdvEXT = &S_glCullParameterdvEXT; ++count; }
        if (!glad_glCullParameterfvEXT) { glad_glCullParameterfvEXT = &S_glCullParameterfvEXT; ++count; }
        if (!glad_glLabelObjectEXT) { glad_glLabelObjectEXT = &S_glLabelObjectEXT; ++count; }
        if (!glad_glGetObjectLabelEXT) { glad_glGetObjectLabelEXT = &S_glGetObjectLabelEXT; ++count; }
        if (!glad_glInsertEventMarkerEXT) { glad_glInsertEventMarkerEXT = &S_glInsertEventMarkerEXT; ++count; }
        if (!glad_glPushGroupMarkerEXT) { glad_glPushGroupMarkerEXT = &S_glPushGroupMarkerEXT; ++count; }
        if (!glad_glPopGroupMarkerEXT) { glad_glPopGroupMarkerEXT = &S_glPopGroupMarkerEXT; ++count; }
        if (!glad_glDepthBoundsEXT) { glad_glDepthBoundsEXT = &S_glDepthBoundsEXT; ++count; }
        if (!glad_glMatrixLoadfEXT) { glad_glMatrixLoadfEXT = &S_glMatrixLoadfEXT; ++count; }
        if (!glad_glMatrixLoaddEXT) { glad_glMatrixLoaddEXT = &S_glMatrixLoaddEXT; ++count; }
        if (!glad_glMatrixMultfEXT) { glad_glMatrixMultfEXT = &S_glMatrixMultfEXT; ++count; }
        if (!glad_glMatrixMultdEXT) { glad_glMatrixMultdEXT = &S_glMatrixMultdEXT; ++count; }
        if (!glad_glMatrixLoadIdentityEXT) { glad_glMatrixLoadIdentityEXT = &S_glMatrixLoadIdentityEXT; ++count; }
        if (!glad_glMatrixRotatefEXT) { glad_glMatrixRotatefEXT = &S_glMatrixRotatefEXT; ++count; }
        if (!glad_glMatrixRotatedEXT) { glad_glMatrixRotatedEXT = &S_glMatrixRotatedEXT; ++count; }
        if (!glad_glMatrixScalefEXT) { glad_glMatrixScalefEXT = &S_glMatrixScalefEXT; ++count; }
        if (!glad_glMatrixScaledEXT) { glad_glMatrixScaledEXT = &S_glMatrixScaledEXT; ++count; }
        if (!glad_glMatrixTranslatefEXT) { glad_glMatrixTranslatefEXT = &S_glMatrixTranslatefEXT; ++count; }
        if (!glad_glMatrixTranslatedEXT) { glad_glMatrixTranslatedEXT = &S_glMatrixTranslatedEXT; ++count; }
        if (!glad_glMatrixFrustumEXT) { glad_glMatrixFrustumEXT = &S_glMatrixFrustumEXT; ++count; }
        if (!glad_glMatrixOrthoEXT) { glad_glMatrixOrthoEXT = &S_glMatrixOrthoEXT; ++count; }
        if (!glad_glMatrixPopEXT) { glad_glMatrixPopEXT = &S_glMatrixPopEXT; ++count; }
        if (!glad_glMatrixPushEXT) { glad_glMatrixPushEXT = &S_glMatrixPushEXT; ++count; }
        if (!glad_glClientAttribDefaultEXT) { glad_glClientAttribDefaultEXT = &S_glClientAttribDefaultEXT; ++count; }
        if (!glad_glPushClientAttribDefaultEXT) { glad_glPushClientAttribDefaultEXT = &S_glPushClientAttribDefaultEXT; ++count; }
        if (!glad_glTextureParameterfEXT) { glad_glTextureParameterfEXT = &S_glTextureParameterfEXT; ++count; }
        if (!glad_glTextureParameterfvEXT) { glad_glTextureParameterfvEXT = &S_glTextureParameterfvEXT; ++count; }
        if (!glad_glTextureParameteriEXT) { glad_glTextureParameteriEXT = &S_glTextureParameteriEXT; ++count; }
        if (!glad_glTextureParameterivEXT) { glad_glTextureParameterivEXT = &S_glTextureParameterivEXT; ++count; }
        if (!glad_glTextureImage1DEXT) { glad_glTextureImage1DEXT = &S_glTextureImage1DEXT; ++count; }
        if (!glad_glTextureImage2DEXT) { glad_glTextureImage2DEXT = &S_glTextureImage2DEXT; ++count; }
        if (!glad_glTextureSubImage1DEXT) { glad_glTextureSubImage1DEXT = &S_glTextureSubImage1DEXT; ++count; }
        if (!glad_glTextureSubImage2DEXT) { glad_glTextureSubImage2DEXT = &S_glTextureSubImage2DEXT; ++count; }
        if (!glad_glCopyTextureImage1DEXT) { glad_glCopyTextureImage1DEXT = &S_glCopyTextureImage1DEXT; ++count; }
        if (!glad_glCopyTextureImage2DEXT) { glad_glCopyTextureImage2DEXT = &S_glCopyTextureImage2DEXT; ++count; }
        if (!glad_glCopyTextureSubImage1DEXT) { glad_glCopyTextureSubImage1DEXT = &S_glCopyTextureSubImage1DEXT; ++count; }
        if (!glad_glCopyTextureSubImage2DEXT) { glad_glCopyTextureSubImage2DEXT = &S_glCopyTextureSubImage2DEXT; ++count; }
        if (!glad_glGetTextureImageEXT) { glad_glGetTextureImageEXT = &S_glGetTextureImageEXT; ++count; }
        if (!glad_glGetTextureParameterfvEXT) { glad_glGetTextureParameterfvEXT = &S_glGetTextureParameterfvEXT; ++count; }
        if (!glad_glGetTextureParameterivEXT) { glad_glGetTextureParameterivEXT = &S_glGetTextureParameterivEXT; ++count; }
        if (!glad_glGetTextureLevelParameterfvEXT) { glad_glGetTextureLevelParameterfvEXT = &S_glGetTextureLevelParameterfvEXT; ++count; }
        if (!glad_glGetTextureLevelParameterivEXT) { glad_glGetTextureLevelParameterivEXT = &S_glGetTextureLevelParameterivEXT; ++count; }
        if (!glad_glTextureImage3DEXT) { glad_glTextureImage3DEXT = &S_glTextureImage3DEXT; ++count; }
        if (!glad_glTextureSubImage3DEXT) { glad_glTextureSubImage3DEXT = &S_glTextureSubImage3DEXT; ++count; }
        if (!glad_glCopyTextureSubImage3DEXT) { glad_glCopyTextureSubImage3DEXT = &S_glCopyTextureSubImage3DEXT; ++count; }
        if (!glad_glBindMultiTextureEXT) { glad_glBindMultiTextureEXT = &S_glBindMultiTextureEXT; ++count; }
        if (!glad_glMultiTexCoordPointerEXT) { glad_glMultiTexCoordPointerEXT = &S_glMultiTexCoordPointerEXT; ++count; }
        if (!glad_glMultiTexEnvfEXT) { glad_glMultiTexEnvfEXT = &S_glMultiTexEnvfEXT; ++count; }
        if (!glad_glMultiTexEnvfvEXT) { glad_glMultiTexEnvfvEXT = &S_glMultiTexEnvfvEXT; ++count; }
        if (!glad_glMultiTexEnviEXT) { glad_glMultiTexEnviEXT = &S_glMultiTexEnviEXT; ++count; }
        if (!glad_glMultiTexEnvivEXT) { glad_glMultiTexEnvivEXT = &S_glMultiTexEnvivEXT; ++count; }
        if (!glad_glMultiTexGendEXT) { glad_glMultiTexGendEXT = &S_glMultiTexGendEXT; ++count; }
        if (!glad_glMultiTexGendvEXT) { glad_glMultiTexGendvEXT = &S_glMultiTexGendvEXT; ++count; }
        if (!glad_glMultiTexGenfEXT) { glad_glMultiTexGenfEXT = &S_glMultiTexGenfEXT; ++count; }
        if (!glad_glMultiTexGenfvEXT) { glad_glMultiTexGenfvEXT = &S_glMultiTexGenfvEXT; ++count; }
        if (!glad_glMultiTexGeniEXT) { glad_glMultiTexGeniEXT = &S_glMultiTexGeniEXT; ++count; }
        if (!glad_glMultiTexGenivEXT) { glad_glMultiTexGenivEXT = &S_glMultiTexGenivEXT; ++count; }
        if (!glad_glGetMultiTexEnvfvEXT) { glad_glGetMultiTexEnvfvEXT = &S_glGetMultiTexEnvfvEXT; ++count; }
        if (!glad_glGetMultiTexEnvivEXT) { glad_glGetMultiTexEnvivEXT = &S_glGetMultiTexEnvivEXT; ++count; }
        if (!glad_glGetMultiTexGendvEXT) { glad_glGetMultiTexGendvEXT = &S_glGetMultiTexGendvEXT; ++count; }
        if (!glad_glGetMultiTexGenfvEXT) { glad_glGetMultiTexGenfvEXT = &S_glGetMultiTexGenfvEXT; ++count; }
        if (!glad_glGetMultiTexGenivEXT) { glad_glGetMultiTexGenivEXT = &S_glGetMultiTexGenivEXT; ++count; }
        if (!glad_glMultiTexParameteriEXT) { glad_glMultiTexParameteriEXT = &S_glMultiTexParameteriEXT; ++count; }
        if (!glad_glMultiTexParameterivEXT) { glad_glMultiTexParameterivEXT = &S_glMultiTexParameterivEXT; ++count; }
        if (!glad_glMultiTexParameterfEXT) { glad_glMultiTexParameterfEXT = &S_glMultiTexParameterfEXT; ++count; }
        if (!glad_glMultiTexParameterfvEXT) { glad_glMultiTexParameterfvEXT = &S_glMultiTexParameterfvEXT; ++count; }
        if (!glad_glMultiTexImage1DEXT) { glad_glMultiTexImage1DEXT = &S_glMultiTexImage1DEXT; ++count; }
        if (!glad_glMultiTexImage2DEXT) { glad_glMultiTexImage2DEXT = &S_glMultiTexImage2DEXT; ++count; }
        if (!glad_glMultiTexSubImage1DEXT) { glad_glMultiTexSubImage1DEXT = &S_glMultiTexSubImage1DEXT; ++count; }
        if (!glad_glMultiTexSubImage2DEXT) { glad_glMultiTexSubImage2DEXT = &S_glMultiTexSubImage2DEXT; ++count; }
        if (!glad_glCopyMultiTexImage1DEXT) { glad_glCopyMultiTexImage1DEXT = &S_glCopyMultiTexImage1DEXT; ++count; }
        if (!glad_glCopyMultiTexImage2DEXT) { glad_glCopyMultiTexImage2DEXT = &S_glCopyMultiTexImage2DEXT; ++count; }
        if (!glad_glCopyMultiTexSubImage1DEXT) { glad_glCopyMultiTexSubImage1DEXT = &S_glCopyMultiTexSubImage1DEXT; ++count; }
        if (!glad_glCopyMultiTexSubImage2DEXT) { glad_glCopyMultiTexSubImage2DEXT = &S_glCopyMultiTexSubImage2DEXT; ++count; }
        if (!glad_glGetMultiTexImageEXT) { glad_glGetMultiTexImageEXT = &S_glGetMultiTexImageEXT; ++count; }
        if (!glad_glGetMultiTexParameterfvEXT) { glad_glGetMultiTexParameterfvEXT = &S_glGetMultiTexParameterfvEXT; ++count; }
        if (!glad_glGetMultiTexParameterivEXT) { glad_glGetMultiTexParameterivEXT = &S_glGetMultiTexParameterivEXT; ++count; }
        if (!glad_glGetMultiTexLevelParameterfvEXT) { glad_glGetMultiTexLevelParameterfvEXT = &S_glGetMultiTexLevelParameterfvEXT; ++count; }
        if (!glad_glGetMultiTexLevelParameterivEXT) { glad_glGetMultiTexLevelParameterivEXT = &S_glGetMultiTexLevelParameterivEXT; ++count; }
        if (!glad_glMultiTexImage3DEXT) { glad_glMultiTexImage3DEXT = &S_glMultiTexImage3DEXT; ++count; }
        if (!glad_glMultiTexSubImage3DEXT) { glad_glMultiTexSubImage3DEXT = &S_glMultiTexSubImage3DEXT; ++count; }
        if (!glad_glCopyMultiTexSubImage3DEXT) { glad_glCopyMultiTexSubImage3DEXT = &S_glCopyMultiTexSubImage3DEXT; ++count; }
        if (!glad_glEnableClientStateIndexedEXT) { glad_glEnableClientStateIndexedEXT = &S_glEnableClientStateIndexedEXT; ++count; }
        if (!glad_glDisableClientStateIndexedEXT) { glad_glDisableClientStateIndexedEXT = &S_glDisableClientStateIndexedEXT; ++count; }
        if (!glad_glGetFloatIndexedvEXT) { glad_glGetFloatIndexedvEXT = &S_glGetFloatIndexedvEXT; ++count; }
        if (!glad_glGetDoubleIndexedvEXT) { glad_glGetDoubleIndexedvEXT = &S_glGetDoubleIndexedvEXT; ++count; }
        if (!glad_glGetPointerIndexedvEXT) { glad_glGetPointerIndexedvEXT = &S_glGetPointerIndexedvEXT; ++count; }
        if (!glad_glEnableIndexedEXT) { glad_glEnableIndexedEXT = &S_glEnableIndexedEXT; ++count; }
        if (!glad_glDisableIndexedEXT) { glad_glDisableIndexedEXT = &S_glDisableIndexedEXT; ++count; }
        if (!glad_glIsEnabledIndexedEXT) { glad_glIsEnabledIndexedEXT = &S_glIsEnabledIndexedEXT; ++count; }
        if (!glad_glGetIntegerIndexedvEXT) { glad_glGetIntegerIndexedvEXT = &S_glGetIntegerIndexedvEXT; ++count; }
        if (!glad_glGetBooleanIndexedvEXT) { glad_glGetBooleanIndexedvEXT = &S_glGetBooleanIndexedvEXT; ++count; }
        if (!glad_glCompressedTextureImage3DEXT) { glad_glCompressedTextureImage3DEXT = &S_glCompressedTextureImage3DEXT; ++count; }
        if (!glad_glCompressedTextureImage2DEXT) { glad_glCompressedTextureImage2DEXT = &S_glCompressedTextureImage2DEXT; ++count; }
        if (!glad_glCompressedTextureImage1DEXT) { glad_glCompressedTextureImage1DEXT = &S_glCompressedTextureImage1DEXT; ++count; }
        if (!glad_glCompressedTextureSubImage3DEXT) { glad_glCompressedTextureSubImage3DEXT = &S_glCompressedTextureSubImage3DEXT; ++count; }
        if (!glad_glCompressedTextureSubImage2DEXT) { glad_glCompressedTextureSubImage2DEXT = &S_glCompressedTextureSubImage2DEXT; ++count; }
        if (!glad_glCompressedTextureSubImage1DEXT) { glad_glCompressedTextureSubImage1DEXT = &S_glCompressedTextureSubImage1DEXT; ++count; }
        if (!glad_glGetCompressedTextureImageEXT) { glad_glGetCompressedTextureImageEXT = &S_glGetCompressedTextureImageEXT; ++count; }
        if (!glad_glCompressedMultiTexImage3DEXT) { glad_glCompressedMultiTexImage3DEXT = &S_glCompressedMultiTexImage3DEXT; ++count; }
        if (!glad_glCompressedMultiTexImage2DEXT) { glad_glCompressedMultiTexImage2DEXT = &S_glCompressedMultiTexImage2DEXT; ++count; }
        if (!glad_glCompressedMultiTexImage1DEXT) { glad_glCompressedMultiTexImage1DEXT = &S_glCompressedMultiTexImage1DEXT; ++count; }
        if (!glad_glCompressedMultiTexSubImage3DEXT) { glad_glCompressedMultiTexSubImage3DEXT = &S_glCompressedMultiTexSubImage3DEXT; ++count; }
        if (!glad_glCompressedMultiTexSubImage2DEXT) { glad_glCompressedMultiTexSubImage2DEXT = &S_glCompressedMultiTexSubImage2DEXT; ++count; }
        if (!glad_glCompressedMultiTexSubImage1DEXT) { glad_glCompressedMultiTexSubImage1DEXT = &S_glCompressedMultiTexSubImage1DEXT; ++count; }
        if (!glad_glGetCompressedMultiTexImageEXT) { glad_glGetCompressedMultiTexImageEXT = &S_glGetCompressedMultiTexImageEXT; ++count; }
        if (!glad_glMatrixLoadTransposefEXT) { glad_glMatrixLoadTransposefEXT = &S_glMatrixLoadTransposefEXT; ++count; }
        if (!glad_glMatrixLoadTransposedEXT) { glad_glMatrixLoadTransposedEXT = &S_glMatrixLoadTransposedEXT; ++count; }
        if (!glad_glMatrixMultTransposefEXT) { glad_glMatrixMultTransposefEXT = &S_glMatrixMultTransposefEXT; ++count; }
        if (!glad_glMatrixMultTransposedEXT) { glad_glMatrixMultTransposedEXT = &S_glMatrixMultTransposedEXT; ++count; }
        if (!glad_glNamedBufferDataEXT) { glad_glNamedBufferDataEXT = &S_glNamedBufferDataEXT; ++count; }
        if (!glad_glNamedBufferSubDataEXT) { glad_glNamedBufferSubDataEXT = &S_glNamedBufferSubDataEXT; ++count; }
        if (!glad_glMapNamedBufferEXT) { glad_glMapNamedBufferEXT = &S_glMapNamedBufferEXT; ++count; }
        if (!glad_glUnmapNamedBufferEXT) { glad_glUnmapNamedBufferEXT = &S_glUnmapNamedBufferEXT; ++count; }
        if (!glad_glGetNamedBufferParameterivEXT) { glad_glGetNamedBufferParameterivEXT = &S_glGetNamedBufferParameterivEXT; ++count; }
        if (!glad_glGetNamedBufferPointervEXT) { glad_glGetNamedBufferPointervEXT = &S_glGetNamedBufferPointervEXT; ++count; }
        if (!glad_glGetNamedBufferSubDataEXT) { glad_glGetNamedBufferSubDataEXT = &S_glGetNamedBufferSubDataEXT; ++count; }
        if (!glad_glProgramUniform1fEXT) { glad_glProgramUniform1fEXT = &S_glProgramUniform1fEXT; ++count; }
        if (!glad_glProgramUniform2fEXT) { glad_glProgramUniform2fEXT = &S_glProgramUniform2fEXT; ++count; }
        if (!glad_glProgramUniform3fEXT) { glad_glProgramUniform3fEXT = &S_glProgramUniform3fEXT; ++count; }
        if (!glad_glProgramUniform4fEXT) { glad_glProgramUniform4fEXT = &S_glProgramUniform4fEXT; ++count; }
        if (!glad_glProgramUniform1iEXT) { glad_glProgramUniform1iEXT = &S_glProgramUniform1iEXT; ++count; }
        if (!glad_glProgramUniform2iEXT) { glad_glProgramUniform2iEXT = &S_glProgramUniform2iEXT; ++count; }
        if (!glad_glProgramUniform3iEXT) { glad_glProgramUniform3iEXT = &S_glProgramUniform3iEXT; ++count; }
        if (!glad_glProgramUniform4iEXT) { glad_glProgramUniform4iEXT = &S_glProgramUniform4iEXT; ++count; }
        if (!glad_glProgramUniform1fvEXT) { glad_glProgramUniform1fvEXT = &S_glProgramUniform1fvEXT; ++count; }
        if (!glad_glProgramUniform2fvEXT) { glad_glProgramUniform2fvEXT = &S_glProgramUniform2fvEXT; ++count; }
        if (!glad_glProgramUniform3fvEXT) { glad_glProgramUniform3fvEXT = &S_glProgramUniform3fvEXT; ++count; }
        if (!glad_glProgramUniform4fvEXT) { glad_glProgramUniform4fvEXT = &S_glProgramUniform4fvEXT; ++count; }
        if (!glad_glProgramUniform1ivEXT) { glad_glProgramUniform1ivEXT = &S_glProgramUniform1ivEXT; ++count; }
        if (!glad_glProgramUniform2ivEXT) { glad_glProgramUniform2ivEXT = &S_glProgramUniform2ivEXT; ++count; }
        if (!glad_glProgramUniform3ivEXT) { glad_glProgramUniform3ivEXT = &S_glProgramUniform3ivEXT; ++count; }
        if (!glad_glProgramUniform4ivEXT) { glad_glProgramUniform4ivEXT = &S_glProgramUniform4ivEXT; ++count; }
        if (!glad_glProgramUniformMatrix2fvEXT) { glad_glProgramUniformMatrix2fvEXT = &S_glProgramUniformMatrix2fvEXT; ++count; }
        if (!glad_glProgramUniformMatrix3fvEXT) { glad_glProgramUniformMatrix3fvEXT = &S_glProgramUniformMatrix3fvEXT; ++count; }
        if (!glad_glProgramUniformMatrix4fvEXT) { glad_glProgramUniformMatrix4fvEXT = &S_glProgramUniformMatrix4fvEXT; ++count; }
        if (!glad_glProgramUniformMatrix2x3fvEXT) { glad_glProgramUniformMatrix2x3fvEXT = &S_glProgramUniformMatrix2x3fvEXT; ++count; }
        if (!glad_glProgramUniformMatrix3x2fvEXT) { glad_glProgramUniformMatrix3x2fvEXT = &S_glProgramUniformMatrix3x2fvEXT; ++count; }
        if (!glad_glProgramUniformMatrix2x4fvEXT) { glad_glProgramUniformMatrix2x4fvEXT = &S_glProgramUniformMatrix2x4fvEXT; ++count; }
        if (!glad_glProgramUniformMatrix4x2fvEXT) { glad_glProgramUniformMatrix4x2fvEXT = &S_glProgramUniformMatrix4x2fvEXT; ++count; }
        if (!glad_glProgramUniformMatrix3x4fvEXT) { glad_glProgramUniformMatrix3x4fvEXT = &S_glProgramUniformMatrix3x4fvEXT; ++count; }
        if (!glad_glProgramUniformMatrix4x3fvEXT) { glad_glProgramUniformMatrix4x3fvEXT = &S_glProgramUniformMatrix4x3fvEXT; ++count; }
        if (!glad_glTextureBufferEXT) { glad_glTextureBufferEXT = &S_glTextureBufferEXT; ++count; }
        if (!glad_glMultiTexBufferEXT) { glad_glMultiTexBufferEXT = &S_glMultiTexBufferEXT; ++count; }
        if (!glad_glTextureParameterIivEXT) { glad_glTextureParameterIivEXT = &S_glTextureParameterIivEXT; ++count; }
        if (!glad_glTextureParameterIuivEXT) { glad_glTextureParameterIuivEXT = &S_glTextureParameterIuivEXT; ++count; }
        if (!glad_glGetTextureParameterIivEXT) { glad_glGetTextureParameterIivEXT = &S_glGetTextureParameterIivEXT; ++count; }
        if (!glad_glGetTextureParameterIuivEXT) { glad_glGetTextureParameterIuivEXT = &S_glGetTextureParameterIuivEXT; ++count; }
        if (!glad_glMultiTexParameterIivEXT) { glad_glMultiTexParameterIivEXT = &S_glMultiTexParameterIivEXT; ++count; }
        if (!glad_glMultiTexParameterIuivEXT) { glad_glMultiTexParameterIuivEXT = &S_glMultiTexParameterIuivEXT; ++count; }
        if (!glad_glGetMultiTexParameterIivEXT) { glad_glGetMultiTexParameterIivEXT = &S_glGetMultiTexParameterIivEXT; ++count; }
        if (!glad_glGetMultiTexParameterIuivEXT) { glad_glGetMultiTexParameterIuivEXT = &S_glGetMultiTexParameterIuivEXT; ++count; }
        if (!glad_glProgramUniform1uiEXT) { glad_glProgramUniform1uiEXT = &S_glProgramUniform1uiEXT; ++count; }
        if (!glad_glProgramUniform2uiEXT) { glad_glProgramUniform2uiEXT = &S_glProgramUniform2uiEXT; ++count; }
        if (!glad_glProgramUniform3uiEXT) { glad_glProgramUniform3uiEXT = &S_glProgramUniform3uiEXT; ++count; }
        if (!glad_glProgramUniform4uiEXT) { glad_glProgramUniform4uiEXT = &S_glProgramUniform4uiEXT; ++count; }
        if (!glad_glProgramUniform1uivEXT) { glad_glProgramUniform1uivEXT = &S_glProgramUniform1uivEXT; ++count; }
        if (!glad_glProgramUniform2uivEXT) { glad_glProgramUniform2uivEXT = &S_glProgramUniform2uivEXT; ++count; }
        if (!glad_glProgramUniform3uivEXT) { glad_glProgramUniform3uivEXT = &S_glProgramUniform3uivEXT; ++count; }
        if (!glad_glProgramUniform4uivEXT) { glad_glProgramUniform4uivEXT = &S_glProgramUniform4uivEXT; ++count; }
        if (!glad_glNamedProgramLocalParameters4fvEXT) { glad_glNamedProgramLocalParameters4fvEXT = &S_glNamedProgramLocalParameters4fvEXT; ++count; }
        if (!glad_glNamedProgramLocalParameterI4iEXT) { glad_glNamedProgramLocalParameterI4iEXT = &S_glNamedProgramLocalParameterI4iEXT; ++count; }
        if (!glad_glNamedProgramLocalParameterI4ivEXT) { glad_glNamedProgramLocalParameterI4ivEXT = &S_glNamedProgramLocalParameterI4ivEXT; ++count; }
        if (!glad_glNamedProgramLocalParametersI4ivEXT) { glad_glNamedProgramLocalParametersI4ivEXT = &S_glNamedProgramLocalParametersI4ivEXT; ++count; }
        if (!glad_glNamedProgramLocalParameterI4uiEXT) { glad_glNamedProgramLocalParameterI4uiEXT = &S_glNamedProgramLocalParameterI4uiEXT; ++count; }
        if (!glad_glNamedProgramLocalParameterI4uivEXT) { glad_glNamedProgramLocalParameterI4uivEXT = &S_glNamedProgramLocalParameterI4uivEXT; ++count; }
        if (!glad_glNamedProgramLocalParametersI4uivEXT) { glad_glNamedProgramLocalParametersI4uivEXT = &S_glNamedProgramLocalParametersI4uivEXT; ++count; }
        if (!glad_glGetNamedProgramLocalParameterIivEXT) { glad_glGetNamedProgramLocalParameterIivEXT = &S_glGetNamedProgramLocalParameterIivEXT; ++count; }
        if (!glad_glGetNamedProgramLocalParameterIuivEXT) { glad_glGetNamedProgramLocalParameterIuivEXT = &S_glGetNamedProgramLocalParameterIuivEXT; ++count; }
        if (!glad_glEnableClientStateiEXT) { glad_glEnableClientStateiEXT = &S_glEnableClientStateiEXT; ++count; }
        if (!glad_glDisableClientStateiEXT) { glad_glDisableClientStateiEXT = &S_glDisableClientStateiEXT; ++count; }
        if (!glad_glGetFloati_vEXT) { glad_glGetFloati_vEXT = &S_glGetFloati_vEXT; ++count; }
        if (!glad_glGetDoublei_vEXT) { glad_glGetDoublei_vEXT = &S_glGetDoublei_vEXT; ++count; }
        if (!glad_glGetPointeri_vEXT) { glad_glGetPointeri_vEXT = &S_glGetPointeri_vEXT; ++count; }
        if (!glad_glNamedProgramStringEXT) { glad_glNamedProgramStringEXT = &S_glNamedProgramStringEXT; ++count; }
        if (!glad_glNamedProgramLocalParameter4dEXT) { glad_glNamedProgramLocalParameter4dEXT = &S_glNamedProgramLocalParameter4dEXT; ++count; }
        if (!glad_glNamedProgramLocalParameter4dvEXT) { glad_glNamedProgramLocalParameter4dvEXT = &S_glNamedProgramLocalParameter4dvEXT; ++count; }
        if (!glad_glNamedProgramLocalParameter4fEXT) { glad_glNamedProgramLocalParameter4fEXT = &S_glNamedProgramLocalParameter4fEXT; ++count; }
        if (!glad_glNamedProgramLocalParameter4fvEXT) { glad_glNamedProgramLocalParameter4fvEXT = &S_glNamedProgramLocalParameter4fvEXT; ++count; }
        if (!glad_glGetNamedProgramLocalParameterdvEXT) { glad_glGetNamedProgramLocalParameterdvEXT = &S_glGetNamedProgramLocalParameterdvEXT; ++count; }
        if (!glad_glGetNamedProgramLocalParameterfvEXT) { glad_glGetNamedProgramLocalParameterfvEXT = &S_glGetNamedProgramLocalParameterfvEXT; ++count; }
        if (!glad_glGetNamedProgramivEXT) { glad_glGetNamedProgramivEXT = &S_glGetNamedProgramivEXT; ++count; }
        if (!glad_glGetNamedProgramStringEXT) { glad_glGetNamedProgramStringEXT = &S_glGetNamedProgramStringEXT; ++count; }
        if (!glad_glNamedRenderbufferStorageEXT) { glad_glNamedRenderbufferStorageEXT = &S_glNamedRenderbufferStorageEXT; ++count; }
        if (!glad_glGetNamedRenderbufferParameterivEXT) { glad_glGetNamedRenderbufferParameterivEXT = &S_glGetNamedRenderbufferParameterivEXT; ++count; }
        if (!glad_glNamedRenderbufferStorageMultisampleEXT) { glad_glNamedRenderbufferStorageMultisampleEXT = &S_glNamedRenderbufferStorageMultisampleEXT; ++count; }
        if (!glad_glNamedRenderbufferStorageMultisampleCoverageEXT) { glad_glNamedRenderbufferStorageMultisampleCoverageEXT = &S_glNamedRenderbufferStorageMultisampleCoverageEXT; ++count; }
        if (!glad_glCheckNamedFramebufferStatusEXT) { glad_glCheckNamedFramebufferStatusEXT = &S_glCheckNamedFramebufferStatusEXT; ++count; }
        if (!glad_glNamedFramebufferTexture1DEXT) { glad_glNamedFramebufferTexture1DEXT = &S_glNamedFramebufferTexture1DEXT; ++count; }
        if (!glad_glNamedFramebufferTexture2DEXT) { glad_glNamedFramebufferTexture2DEXT = &S_glNamedFramebufferTexture2DEXT; ++count; }
        if (!glad_glNamedFramebufferTexture3DEXT) { glad_glNamedFramebufferTexture3DEXT = &S_glNamedFramebufferTexture3DEXT; ++count; }
        if (!glad_glNamedFramebufferRenderbufferEXT) { glad_glNamedFramebufferRenderbufferEXT = &S_glNamedFramebufferRenderbufferEXT; ++count; }
        if (!glad_glGetNamedFramebufferAttachmentParameterivEXT) { glad_glGetNamedFramebufferAttachmentParameterivEXT = &S_glGetNamedFramebufferAttachmentParameterivEXT; ++count; }
        if (!glad_glGenerateTextureMipmapEXT) { glad_glGenerateTextureMipmapEXT = &S_glGenerateTextureMipmapEXT; ++count; }
        if (!glad_glGenerateMultiTexMipmapEXT) { glad_glGenerateMultiTexMipmapEXT = &S_glGenerateMultiTexMipmapEXT; ++count; }
        if (!glad_glFramebufferDrawBufferEXT) { glad_glFramebufferDrawBufferEXT = &S_glFramebufferDrawBufferEXT; ++count; }
        if (!glad_glFramebufferDrawBuffersEXT) { glad_glFramebufferDrawBuffersEXT = &S_glFramebufferDrawBuffersEXT; ++count; }
        if (!glad_glFramebufferReadBufferEXT) { glad_glFramebufferReadBufferEXT = &S_glFramebufferReadBufferEXT; ++count; }
        if (!glad_glGetFramebufferParameterivEXT) { glad_glGetFramebufferParameterivEXT = &S_glGetFramebufferParameterivEXT; ++count; }
        if (!glad_glNamedCopyBufferSubDataEXT) { glad_glNamedCopyBufferSubDataEXT = &S_glNamedCopyBufferSubDataEXT; ++count; }
        if (!glad_glNamedFramebufferTextureEXT) { glad_glNamedFramebufferTextureEXT = &S_glNamedFramebufferTextureEXT; ++count; }
        if (!glad_glNamedFramebufferTextureLayerEXT) { glad_glNamedFramebufferTextureLayerEXT = &S_glNamedFramebufferTextureLayerEXT; ++count; }
        if (!glad_glNamedFramebufferTextureFaceEXT) { glad_glNamedFramebufferTextureFaceEXT = &S_glNamedFramebufferTextureFaceEXT; ++count; }
        if (!glad_glTextureRenderbufferEXT) { glad_glTextureRenderbufferEXT = &S_glTextureRenderbufferEXT; ++count; }
        if (!glad_glMultiTexRenderbufferEXT) { glad_glMultiTexRenderbufferEXT = &S_glMultiTexRenderbufferEXT; ++count; }
        if (!glad_glVertexArrayVertexOffsetEXT) { glad_glVertexArrayVertexOffsetEXT = &S_glVertexArrayVertexOffsetEXT; ++count; }
        if (!glad_glVertexArrayColorOffsetEXT) { glad_glVertexArrayColorOffsetEXT = &S_glVertexArrayColorOffsetEXT; ++count; }
        if (!glad_glVertexArrayEdgeFlagOffsetEXT) { glad_glVertexArrayEdgeFlagOffsetEXT = &S_glVertexArrayEdgeFlagOffsetEXT; ++count; }
        if (!glad_glVertexArrayIndexOffsetEXT) { glad_glVertexArrayIndexOffsetEXT = &S_glVertexArrayIndexOffsetEXT; ++count; }
        if (!glad_glVertexArrayNormalOffsetEXT) { glad_glVertexArrayNormalOffsetEXT = &S_glVertexArrayNormalOffsetEXT; ++count; }
        if (!glad_glVertexArrayTexCoordOffsetEXT) { glad_glVertexArrayTexCoordOffsetEXT = &S_glVertexArrayTexCoordOffsetEXT; ++count; }
        if (!glad_glVertexArrayMultiTexCoordOffsetEXT) { glad_glVertexArrayMultiTexCoordOffsetEXT = &S_glVertexArrayMultiTexCoordOffsetEXT; ++count; }
        if (!glad_glVertexArrayFogCoordOffsetEXT) { glad_glVertexArrayFogCoordOffsetEXT = &S_glVertexArrayFogCoordOffsetEXT; ++count; }
        if (!glad_glVertexArraySecondaryColorOffsetEXT) { glad_glVertexArraySecondaryColorOffsetEXT = &S_glVertexArraySecondaryColorOffsetEXT; ++count; }
        if (!glad_glVertexArrayVertexAttribOffsetEXT) { glad_glVertexArrayVertexAttribOffsetEXT = &S_glVertexArrayVertexAttribOffsetEXT; ++count; }
        if (!glad_glVertexArrayVertexAttribIOffsetEXT) { glad_glVertexArrayVertexAttribIOffsetEXT = &S_glVertexArrayVertexAttribIOffsetEXT; ++count; }
        if (!glad_glEnableVertexArrayEXT) { glad_glEnableVertexArrayEXT = &S_glEnableVertexArrayEXT; ++count; }
        if (!glad_glDisableVertexArrayEXT) { glad_glDisableVertexArrayEXT = &S_glDisableVertexArrayEXT; ++count; }
        if (!glad_glEnableVertexArrayAttribEXT) { glad_glEnableVertexArrayAttribEXT = &S_glEnableVertexArrayAttribEXT; ++count; }
        if (!glad_glDisableVertexArrayAttribEXT) { glad_glDisableVertexArrayAttribEXT = &S_glDisableVertexArrayAttribEXT; ++count; }
        if (!glad_glGetVertexArrayIntegervEXT) { glad_glGetVertexArrayIntegervEXT = &S_glGetVertexArrayIntegervEXT; ++count; }
        if (!glad_glGetVertexArrayPointervEXT) { glad_glGetVertexArrayPointervEXT = &S_glGetVertexArrayPointervEXT; ++count; }
        if (!glad_glGetVertexArrayIntegeri_vEXT) { glad_glGetVertexArrayIntegeri_vEXT = &S_glGetVertexArrayIntegeri_vEXT; ++count; }
        if (!glad_glGetVertexArrayPointeri_vEXT) { glad_glGetVertexArrayPointeri_vEXT = &S_glGetVertexArrayPointeri_vEXT; ++count; }
        if (!glad_glMapNamedBufferRangeEXT) { glad_glMapNamedBufferRangeEXT = &S_glMapNamedBufferRangeEXT; ++count; }
        if (!glad_glFlushMappedNamedBufferRangeEXT) { glad_glFlushMappedNamedBufferRangeEXT = &S_glFlushMappedNamedBufferRangeEXT; ++count; }
        if (!glad_glNamedBufferStorageEXT) { glad_glNamedBufferStorageEXT = &S_glNamedBufferStorageEXT; ++count; }
        if (!glad_glClearNamedBufferDataEXT) { glad_glClearNamedBufferDataEXT = &S_glClearNamedBufferDataEXT; ++count; }
        if (!glad_glClearNamedBufferSubDataEXT) { glad_glClearNamedBufferSubDataEXT = &S_glClearNamedBufferSubDataEXT; ++count; }
        if (!glad_glNamedFramebufferParameteriEXT) { glad_glNamedFramebufferParameteriEXT = &S_glNamedFramebufferParameteriEXT; ++count; }
        if (!glad_glGetNamedFramebufferParameterivEXT) { glad_glGetNamedFramebufferParameterivEXT = &S_glGetNamedFramebufferParameterivEXT; ++count; }
        if (!glad_glProgramUniform1dEXT) { glad_glProgramUniform1dEXT = &S_glProgramUniform1dEXT; ++count; }
        if (!glad_glProgramUniform2dEXT) { glad_glProgramUniform2dEXT = &S_glProgramUniform2dEXT; ++count; }
        if (!glad_glProgramUniform3dEXT) { glad_glProgramUniform3dEXT = &S_glProgramUniform3dEXT; ++count; }
        if (!glad_glProgramUniform4dEXT) { glad_glProgramUniform4dEXT = &S_glProgramUniform4dEXT; ++count; }
        if (!glad_glProgramUniform1dvEXT) { glad_glProgramUniform1dvEXT = &S_glProgramUniform1dvEXT; ++count; }
        if (!glad_glProgramUniform2dvEXT) { glad_glProgramUniform2dvEXT = &S_glProgramUniform2dvEXT; ++count; }
        if (!glad_glProgramUniform3dvEXT) { glad_glProgramUniform3dvEXT = &S_glProgramUniform3dvEXT; ++count; }
        if (!glad_glProgramUniform4dvEXT) { glad_glProgramUniform4dvEXT = &S_glProgramUniform4dvEXT; ++count; }
        if (!glad_glProgramUniformMatrix2dvEXT) { glad_glProgramUniformMatrix2dvEXT = &S_glProgramUniformMatrix2dvEXT; ++count; }
        if (!glad_glProgramUniformMatrix3dvEXT) { glad_glProgramUniformMatrix3dvEXT = &S_glProgramUniformMatrix3dvEXT; ++count; }
        if (!glad_glProgramUniformMatrix4dvEXT) { glad_glProgramUniformMatrix4dvEXT = &S_glProgramUniformMatrix4dvEXT; ++count; }
        if (!glad_glProgramUniformMatrix2x3dvEXT) { glad_glProgramUniformMatrix2x3dvEXT = &S_glProgramUniformMatrix2x3dvEXT; ++count; }
        if (!glad_glProgramUniformMatrix2x4dvEXT) { glad_glProgramUniformMatrix2x4dvEXT = &S_glProgramUniformMatrix2x4dvEXT; ++count; }
        if (!glad_glProgramUniformMatrix3x2dvEXT) { glad_glProgramUniformMatrix3x2dvEXT = &S_glProgramUniformMatrix3x2dvEXT; ++count; }
        if (!glad_glProgramUniformMatrix3x4dvEXT) { glad_glProgramUniformMatrix3x4dvEXT = &S_glProgramUniformMatrix3x4dvEXT; ++count; }
        if (!glad_glProgramUniformMatrix4x2dvEXT) { glad_glProgramUniformMatrix4x2dvEXT = &S_glProgramUniformMatrix4x2dvEXT; ++count; }
        if (!glad_glProgramUniformMatrix4x3dvEXT) { glad_glProgramUniformMatrix4x3dvEXT = &S_glProgramUniformMatrix4x3dvEXT; ++count; }
        if (!glad_glTextureBufferRangeEXT) { glad_glTextureBufferRangeEXT = &S_glTextureBufferRangeEXT; ++count; }
        if (!glad_glTextureStorage1DEXT) { glad_glTextureStorage1DEXT = &S_glTextureStorage1DEXT; ++count; }
        if (!glad_glTextureStorage2DEXT) { glad_glTextureStorage2DEXT = &S_glTextureStorage2DEXT; ++count; }
        if (!glad_glTextureStorage3DEXT) { glad_glTextureStorage3DEXT = &S_glTextureStorage3DEXT; ++count; }
        if (!glad_glTextureStorage2DMultisampleEXT) { glad_glTextureStorage2DMultisampleEXT = &S_glTextureStorage2DMultisampleEXT; ++count; }
        if (!glad_glTextureStorage3DMultisampleEXT) { glad_glTextureStorage3DMultisampleEXT = &S_glTextureStorage3DMultisampleEXT; ++count; }
        if (!glad_glVertexArrayBindVertexBufferEXT) { glad_glVertexArrayBindVertexBufferEXT = &S_glVertexArrayBindVertexBufferEXT; ++count; }
        if (!glad_glVertexArrayVertexAttribFormatEXT) { glad_glVertexArrayVertexAttribFormatEXT = &S_glVertexArrayVertexAttribFormatEXT; ++count; }
        if (!glad_glVertexArrayVertexAttribIFormatEXT) { glad_glVertexArrayVertexAttribIFormatEXT = &S_glVertexArrayVertexAttribIFormatEXT; ++count; }
        if (!glad_glVertexArrayVertexAttribLFormatEXT) { glad_glVertexArrayVertexAttribLFormatEXT = &S_glVertexArrayVertexAttribLFormatEXT; ++count; }
        if (!glad_glVertexArrayVertexAttribBindingEXT) { glad_glVertexArrayVertexAttribBindingEXT = &S_glVertexArrayVertexAttribBindingEXT; ++count; }
        if (!glad_glVertexArrayVertexBindingDivisorEXT) { glad_glVertexArrayVertexBindingDivisorEXT = &S_glVertexArrayVertexBindingDivisorEXT; ++count; }
        if (!glad_glVertexArrayVertexAttribLOffsetEXT) { glad_glVertexArrayVertexAttribLOffsetEXT = &S_glVertexArrayVertexAttribLOffsetEXT; ++count; }
        if (!glad_glTexturePageCommitmentEXT) { glad_glTexturePageCommitmentEXT = &S_glTexturePageCommitmentEXT; ++count; }
        if (!glad_glVertexArrayVertexAttribDivisorEXT) { glad_glVertexArrayVertexAttribDivisorEXT = &S_glVertexArrayVertexAttribDivisorEXT; ++count; }
        if (!glad_glColorMaskIndexedEXT) { glad_glColorMaskIndexedEXT = &S_glColorMaskIndexedEXT; ++count; }
        if (!glad_glDrawArraysInstancedEXT) { glad_glDrawArraysInstancedEXT = &S_glDrawArraysInstancedEXT; ++count; }
        if (!glad_glDrawElementsInstancedEXT) { glad_glDrawElementsInstancedEXT = &S_glDrawElementsInstancedEXT; ++count; }
        if (!glad_glDrawRangeElementsEXT) { glad_glDrawRangeElementsEXT = &S_glDrawRangeElementsEXT; ++count; }
        if (!glad_glBufferStorageExternalEXT) { glad_glBufferStorageExternalEXT = &S_glBufferStorageExternalEXT; ++count; }
        if (!glad_glNamedBufferStorageExternalEXT) { glad_glNamedBufferStorageExternalEXT = &S_glNamedBufferStorageExternalEXT; ++count; }
        if (!glad_glFogCoordfEXT) { glad_glFogCoordfEXT = &S_glFogCoordfEXT; ++count; }
        if (!glad_glFogCoordfvEXT) { glad_glFogCoordfvEXT = &S_glFogCoordfvEXT; ++count; }
        if (!glad_glFogCoorddEXT) { glad_glFogCoorddEXT = &S_glFogCoorddEXT; ++count; }
        if (!glad_glFogCoorddvEXT) { glad_glFogCoorddvEXT = &S_glFogCoorddvEXT; ++count; }
        if (!glad_glFogCoordPointerEXT) { glad_glFogCoordPointerEXT = &S_glFogCoordPointerEXT; ++count; }
        if (!glad_glGetFragmentShadingRatesEXT) { glad_glGetFragmentShadingRatesEXT = &S_glGetFragmentShadingRatesEXT; ++count; }
        if (!glad_glShadingRateEXT) { glad_glShadingRateEXT = &S_glShadingRateEXT; ++count; }
        if (!glad_glShadingRateCombinerOpsEXT) { glad_glShadingRateCombinerOpsEXT = &S_glShadingRateCombinerOpsEXT; ++count; }
        if (!glad_glFramebufferShadingRateEXT) { glad_glFramebufferShadingRateEXT = &S_glFramebufferShadingRateEXT; ++count; }
        if (!glad_glBlitFramebufferEXT) { glad_glBlitFramebufferEXT = &S_glBlitFramebufferEXT; ++count; }
        if (!glad_glBlitFramebufferLayersEXT) { glad_glBlitFramebufferLayersEXT = &S_glBlitFramebufferLayersEXT; ++count; }
        if (!glad_glBlitFramebufferLayerEXT) { glad_glBlitFramebufferLayerEXT = &S_glBlitFramebufferLayerEXT; ++count; }
        if (!glad_glRenderbufferStorageMultisampleEXT) { glad_glRenderbufferStorageMultisampleEXT = &S_glRenderbufferStorageMultisampleEXT; ++count; }
        if (!glad_glIsRenderbufferEXT) { glad_glIsRenderbufferEXT = &S_glIsRenderbufferEXT; ++count; }
        if (!glad_glBindRenderbufferEXT) { glad_glBindRenderbufferEXT = &S_glBindRenderbufferEXT; ++count; }
        if (!glad_glDeleteRenderbuffersEXT) { glad_glDeleteRenderbuffersEXT = &S_glDeleteRenderbuffersEXT; ++count; }
        if (!glad_glGenRenderbuffersEXT) { glad_glGenRenderbuffersEXT = &S_glGenRenderbuffersEXT; ++count; }
        if (!glad_glRenderbufferStorageEXT) { glad_glRenderbufferStorageEXT = &S_glRenderbufferStorageEXT; ++count; }
        if (!glad_glGetRenderbufferParameterivEXT) { glad_glGetRenderbufferParameterivEXT = &S_glGetRenderbufferParameterivEXT; ++count; }
        if (!glad_glIsFramebufferEXT) { glad_glIsFramebufferEXT = &S_glIsFramebufferEXT; ++count; }
        if (!glad_glBindFramebufferEXT) { glad_glBindFramebufferEXT = &S_glBindFramebufferEXT; ++count; }
        if (!glad_glDeleteFramebuffersEXT) { glad_glDeleteFramebuffersEXT = &S_glDeleteFramebuffersEXT; ++count; }
        if (!glad_glGenFramebuffersEXT) { glad_glGenFramebuffersEXT = &S_glGenFramebuffersEXT; ++count; }
        if (!glad_glCheckFramebufferStatusEXT) { glad_glCheckFramebufferStatusEXT = &S_glCheckFramebufferStatusEXT; ++count; }
        if (!glad_glFramebufferTexture1DEXT) { glad_glFramebufferTexture1DEXT = &S_glFramebufferTexture1DEXT; ++count; }
        if (!glad_glFramebufferTexture2DEXT) { glad_glFramebufferTexture2DEXT = &S_glFramebufferTexture2DEXT; ++count; }
        if (!glad_glFramebufferTexture3DEXT) { glad_glFramebufferTexture3DEXT = &S_glFramebufferTexture3DEXT; ++count; }
        if (!glad_glFramebufferRenderbufferEXT) { glad_glFramebufferRenderbufferEXT = &S_glFramebufferRenderbufferEXT; ++count; }
        if (!glad_glGetFramebufferAttachmentParameterivEXT) { glad_glGetFramebufferAttachmentParameterivEXT = &S_glGetFramebufferAttachmentParameterivEXT; ++count; }
        if (!glad_glGenerateMipmapEXT) { glad_glGenerateMipmapEXT = &S_glGenerateMipmapEXT; ++count; }
        if (!glad_glProgramParameteriEXT) { glad_glProgramParameteriEXT = &S_glProgramParameteriEXT; ++count; }
        if (!glad_glProgramEnvParameters4fvEXT) { glad_glProgramEnvParameters4fvEXT = &S_glProgramEnvParameters4fvEXT; ++count; }
        if (!glad_glProgramLocalParameters4fvEXT) { glad_glProgramLocalParameters4fvEXT = &S_glProgramLocalParameters4fvEXT; ++count; }
        if (!glad_glGetUniformuivEXT) { glad_glGetUniformuivEXT = &S_glGetUniformuivEXT; ++count; }
        if (!glad_glBindFragDataLocationEXT) { glad_glBindFragDataLocationEXT = &S_glBindFragDataLocationEXT; ++count; }
        if (!glad_glGetFragDataLocationEXT) { glad_glGetFragDataLocationEXT = &S_glGetFragDataLocationEXT; ++count; }
        if (!glad_glUniform1uiEXT) { glad_glUniform1uiEXT = &S_glUniform1uiEXT; ++count; }
        if (!glad_glUniform2uiEXT) { glad_glUniform2uiEXT = &S_glUniform2uiEXT; ++count; }
        if (!glad_glUniform3uiEXT) { glad_glUniform3uiEXT = &S_glUniform3uiEXT; ++count; }
        if (!glad_glUniform4uiEXT) { glad_glUniform4uiEXT = &S_glUniform4uiEXT; ++count; }
        if (!glad_glUniform1uivEXT) { glad_glUniform1uivEXT = &S_glUniform1uivEXT; ++count; }
        if (!glad_glUniform2uivEXT) { glad_glUniform2uivEXT = &S_glUniform2uivEXT; ++count; }
        if (!glad_glUniform3uivEXT) { glad_glUniform3uivEXT = &S_glUniform3uivEXT; ++count; }
        if (!glad_glUniform4uivEXT) { glad_glUniform4uivEXT = &S_glUniform4uivEXT; ++count; }
        if (!glad_glVertexAttribI1iEXT) { glad_glVertexAttribI1iEXT = &S_glVertexAttribI1iEXT; ++count; }
        if (!glad_glVertexAttribI2iEXT) { glad_glVertexAttribI2iEXT = &S_glVertexAttribI2iEXT; ++count; }
        if (!glad_glVertexAttribI3iEXT) { glad_glVertexAttribI3iEXT = &S_glVertexAttribI3iEXT; ++count; }
        if (!glad_glVertexAttribI4iEXT) { glad_glVertexAttribI4iEXT = &S_glVertexAttribI4iEXT; ++count; }
        if (!glad_glVertexAttribI1uiEXT) { glad_glVertexAttribI1uiEXT = &S_glVertexAttribI1uiEXT; ++count; }
        if (!glad_glVertexAttribI2uiEXT) { glad_glVertexAttribI2uiEXT = &S_glVertexAttribI2uiEXT; ++count; }
        if (!glad_glVertexAttribI3uiEXT) { glad_glVertexAttribI3uiEXT = &S_glVertexAttribI3uiEXT; ++count; }
        if (!glad_glVertexAttribI4uiEXT) { glad_glVertexAttribI4uiEXT = &S_glVertexAttribI4uiEXT; ++count; }
        if (!glad_glVertexAttribI1ivEXT) { glad_glVertexAttribI1ivEXT = &S_glVertexAttribI1ivEXT; ++count; }
        if (!glad_glVertexAttribI2ivEXT) { glad_glVertexAttribI2ivEXT = &S_glVertexAttribI2ivEXT; ++count; }
        if (!glad_glVertexAttribI3ivEXT) { glad_glVertexAttribI3ivEXT = &S_glVertexAttribI3ivEXT; ++count; }
        if (!glad_glVertexAttribI4ivEXT) { glad_glVertexAttribI4ivEXT = &S_glVertexAttribI4ivEXT; ++count; }
        if (!glad_glVertexAttribI1uivEXT) { glad_glVertexAttribI1uivEXT = &S_glVertexAttribI1uivEXT; ++count; }
        if (!glad_glVertexAttribI2uivEXT) { glad_glVertexAttribI2uivEXT = &S_glVertexAttribI2uivEXT; ++count; }
        if (!glad_glVertexAttribI3uivEXT) { glad_glVertexAttribI3uivEXT = &S_glVertexAttribI3uivEXT; ++count; }
        if (!glad_glVertexAttribI4uivEXT) { glad_glVertexAttribI4uivEXT = &S_glVertexAttribI4uivEXT; ++count; }
        if (!glad_glVertexAttribI4bvEXT) { glad_glVertexAttribI4bvEXT = &S_glVertexAttribI4bvEXT; ++count; }
        if (!glad_glVertexAttribI4svEXT) { glad_glVertexAttribI4svEXT = &S_glVertexAttribI4svEXT; ++count; }
        if (!glad_glVertexAttribI4ubvEXT) { glad_glVertexAttribI4ubvEXT = &S_glVertexAttribI4ubvEXT; ++count; }
        if (!glad_glVertexAttribI4usvEXT) { glad_glVertexAttribI4usvEXT = &S_glVertexAttribI4usvEXT; ++count; }
        if (!glad_glVertexAttribIPointerEXT) { glad_glVertexAttribIPointerEXT = &S_glVertexAttribIPointerEXT; ++count; }
        if (!glad_glGetVertexAttribIivEXT) { glad_glGetVertexAttribIivEXT = &S_glGetVertexAttribIivEXT; ++count; }
        if (!glad_glGetVertexAttribIuivEXT) { glad_glGetVertexAttribIuivEXT = &S_glGetVertexAttribIuivEXT; ++count; }
        if (!glad_glGetHistogramEXT) { glad_glGetHistogramEXT = &S_glGetHistogramEXT; ++count; }
        if (!glad_glGetHistogramParameterfvEXT) { glad_glGetHistogramParameterfvEXT = &S_glGetHistogramParameterfvEXT; ++count; }
        if (!glad_glGetHistogramParameterivEXT) { glad_glGetHistogramParameterivEXT = &S_glGetHistogramParameterivEXT; ++count; }
        if (!glad_glGetMinmaxEXT) { glad_glGetMinmaxEXT = &S_glGetMinmaxEXT; ++count; }
        if (!glad_glGetMinmaxParameterfvEXT) { glad_glGetMinmaxParameterfvEXT = &S_glGetMinmaxParameterfvEXT; ++count; }
        if (!glad_glGetMinmaxParameterivEXT) { glad_glGetMinmaxParameterivEXT = &S_glGetMinmaxParameterivEXT; ++count; }
        if (!glad_glHistogramEXT) { glad_glHistogramEXT = &S_glHistogramEXT; ++count; }
        if (!glad_glMinmaxEXT) { glad_glMinmaxEXT = &S_glMinmaxEXT; ++count; }
        if (!glad_glResetHistogramEXT) { glad_glResetHistogramEXT = &S_glResetHistogramEXT; ++count; }
        if (!glad_glResetMinmaxEXT) { glad_glResetMinmaxEXT = &S_glResetMinmaxEXT; ++count; }
        if (!glad_glIndexFuncEXT) { glad_glIndexFuncEXT = &S_glIndexFuncEXT; ++count; }
        if (!glad_glIndexMaterialEXT) { glad_glIndexMaterialEXT = &S_glIndexMaterialEXT; ++count; }
        if (!glad_glApplyTextureEXT) { glad_glApplyTextureEXT = &S_glApplyTextureEXT; ++count; }
        if (!glad_glTextureLightEXT) { glad_glTextureLightEXT = &S_glTextureLightEXT; ++count; }
        if (!glad_glTextureMaterialEXT) { glad_glTextureMaterialEXT = &S_glTextureMaterialEXT; ++count; }
        if (!glad_glGetUnsignedBytevEXT) { glad_glGetUnsignedBytevEXT = &S_glGetUnsignedBytevEXT; ++count; }
        if (!glad_glGetUnsignedBytei_vEXT) { glad_glGetUnsignedBytei_vEXT = &S_glGetUnsignedBytei_vEXT; ++count; }
        if (!glad_glDeleteMemoryObjectsEXT) { glad_glDeleteMemoryObjectsEXT = &S_glDeleteMemoryObjectsEXT; ++count; }
        if (!glad_glIsMemoryObjectEXT) { glad_glIsMemoryObjectEXT = &S_glIsMemoryObjectEXT; ++count; }
        if (!glad_glCreateMemoryObjectsEXT) { glad_glCreateMemoryObjectsEXT = &S_glCreateMemoryObjectsEXT; ++count; }
        if (!glad_glMemoryObjectParameterivEXT) { glad_glMemoryObjectParameterivEXT = &S_glMemoryObjectParameterivEXT; ++count; }
        if (!glad_glGetMemoryObjectParameterivEXT) { glad_glGetMemoryObjectParameterivEXT = &S_glGetMemoryObjectParameterivEXT; ++count; }
        if (!glad_glTexStorageMem2DEXT) { glad_glTexStorageMem2DEXT = &S_glTexStorageMem2DEXT; ++count; }
        if (!glad_glTexStorageMem2DMultisampleEXT) { glad_glTexStorageMem2DMultisampleEXT = &S_glTexStorageMem2DMultisampleEXT; ++count; }
        if (!glad_glTexStorageMem3DEXT) { glad_glTexStorageMem3DEXT = &S_glTexStorageMem3DEXT; ++count; }
        if (!glad_glTexStorageMem3DMultisampleEXT) { glad_glTexStorageMem3DMultisampleEXT = &S_glTexStorageMem3DMultisampleEXT; ++count; }
        if (!glad_glBufferStorageMemEXT) { glad_glBufferStorageMemEXT = &S_glBufferStorageMemEXT; ++count; }
        if (!glad_glTextureStorageMem2DEXT) { glad_glTextureStorageMem2DEXT = &S_glTextureStorageMem2DEXT; ++count; }
        if (!glad_glTextureStorageMem2DMultisampleEXT) { glad_glTextureStorageMem2DMultisampleEXT = &S_glTextureStorageMem2DMultisampleEXT; ++count; }
        if (!glad_glTextureStorageMem3DEXT) { glad_glTextureStorageMem3DEXT = &S_glTextureStorageMem3DEXT; ++count; }
        if (!glad_glTextureStorageMem3DMultisampleEXT) { glad_glTextureStorageMem3DMultisampleEXT = &S_glTextureStorageMem3DMultisampleEXT; ++count; }
        if (!glad_glNamedBufferStorageMemEXT) { glad_glNamedBufferStorageMemEXT = &S_glNamedBufferStorageMemEXT; ++count; }
        if (!glad_glTexStorageMem1DEXT) { glad_glTexStorageMem1DEXT = &S_glTexStorageMem1DEXT; ++count; }
        if (!glad_glTextureStorageMem1DEXT) { glad_glTextureStorageMem1DEXT = &S_glTextureStorageMem1DEXT; ++count; }
        if (!glad_glImportMemoryFdEXT) { glad_glImportMemoryFdEXT = &S_glImportMemoryFdEXT; ++count; }
        if (!glad_glImportMemoryWin32HandleEXT) { glad_glImportMemoryWin32HandleEXT = &S_glImportMemoryWin32HandleEXT; ++count; }
        if (!glad_glImportMemoryWin32NameEXT) { glad_glImportMemoryWin32NameEXT = &S_glImportMemoryWin32NameEXT; ++count; }
        if (!glad_glDrawMeshTasksEXT) { glad_glDrawMeshTasksEXT = &S_glDrawMeshTasksEXT; ++count; }
        if (!glad_glDrawMeshTasksIndirectEXT) { glad_glDrawMeshTasksIndirectEXT = &S_glDrawMeshTasksIndirectEXT; ++count; }
        if (!glad_glMultiDrawMeshTasksIndirectEXT) { glad_glMultiDrawMeshTasksIndirectEXT = &S_glMultiDrawMeshTasksIndirectEXT; ++count; }
        if (!glad_glMultiDrawMeshTasksIndirectCountEXT) { glad_glMultiDrawMeshTasksIndirectCountEXT = &S_glMultiDrawMeshTasksIndirectCountEXT; ++count; }
        if (!glad_glMultiDrawArraysEXT) { glad_glMultiDrawArraysEXT = &S_glMultiDrawArraysEXT; ++count; }
        if (!glad_glMultiDrawElementsEXT) { glad_glMultiDrawElementsEXT = &S_glMultiDrawElementsEXT; ++count; }
        if (!glad_glSampleMaskEXT) { glad_glSampleMaskEXT = &S_glSampleMaskEXT; ++count; }
        if (!glad_glSamplePatternEXT) { glad_glSamplePatternEXT = &S_glSamplePatternEXT; ++count; }
        if (!glad_glColorTableEXT) { glad_glColorTableEXT = &S_glColorTableEXT; ++count; }
        if (!glad_glGetColorTableEXT) { glad_glGetColorTableEXT = &S_glGetColorTableEXT; ++count; }
        if (!glad_glGetColorTableParameterivEXT) { glad_glGetColorTableParameterivEXT = &S_glGetColorTableParameterivEXT; ++count; }
        if (!glad_glGetColorTableParameterfvEXT) { glad_glGetColorTableParameterfvEXT = &S_glGetColorTableParameterfvEXT; ++count; }
        if (!glad_glPixelTransformParameteriEXT) { glad_glPixelTransformParameteriEXT = &S_glPixelTransformParameteriEXT; ++count; }
        if (!glad_glPixelTransformParameterfEXT) { glad_glPixelTransformParameterfEXT = &S_glPixelTransformParameterfEXT; ++count; }
        if (!glad_glPixelTransformParameterivEXT) { glad_glPixelTransformParameterivEXT = &S_glPixelTransformParameterivEXT; ++count; }
        if (!glad_glPixelTransformParameterfvEXT) { glad_glPixelTransformParameterfvEXT = &S_glPixelTransformParameterfvEXT; ++count; }
        if (!glad_glGetPixelTransformParameterivEXT) { glad_glGetPixelTransformParameterivEXT = &S_glGetPixelTransformParameterivEXT; ++count; }
        if (!glad_glGetPixelTransformParameterfvEXT) { glad_glGetPixelTransformParameterfvEXT = &S_glGetPixelTransformParameterfvEXT; ++count; }
        if (!glad_glPointParameterfEXT) { glad_glPointParameterfEXT = &S_glPointParameterfEXT; ++count; }
        if (!glad_glPointParameterfvEXT) { glad_glPointParameterfvEXT = &S_glPointParameterfvEXT; ++count; }
        if (!glad_glPolygonOffsetEXT) { glad_glPolygonOffsetEXT = &S_glPolygonOffsetEXT; ++count; }
        if (!glad_glPolygonOffsetClampEXT) { glad_glPolygonOffsetClampEXT = &S_glPolygonOffsetClampEXT; ++count; }
        if (!glad_glProvokingVertexEXT) { glad_glProvokingVertexEXT = &S_glProvokingVertexEXT; ++count; }
        if (!glad_glRasterSamplesEXT) { glad_glRasterSamplesEXT = &S_glRasterSamplesEXT; ++count; }
        if (!glad_glSecondaryColor3bEXT) { glad_glSecondaryColor3bEXT = &S_glSecondaryColor3bEXT; ++count; }
        if (!glad_glSecondaryColor3bvEXT) { glad_glSecondaryColor3bvEXT = &S_glSecondaryColor3bvEXT; ++count; }
        if (!glad_glSecondaryColor3dEXT) { glad_glSecondaryColor3dEXT = &S_glSecondaryColor3dEXT; ++count; }
        if (!glad_glSecondaryColor3dvEXT) { glad_glSecondaryColor3dvEXT = &S_glSecondaryColor3dvEXT; ++count; }
        if (!glad_glSecondaryColor3fEXT) { glad_glSecondaryColor3fEXT = &S_glSecondaryColor3fEXT; ++count; }
        if (!glad_glSecondaryColor3fvEXT) { glad_glSecondaryColor3fvEXT = &S_glSecondaryColor3fvEXT; ++count; }
        if (!glad_glSecondaryColor3iEXT) { glad_glSecondaryColor3iEXT = &S_glSecondaryColor3iEXT; ++count; }
        if (!glad_glSecondaryColor3ivEXT) { glad_glSecondaryColor3ivEXT = &S_glSecondaryColor3ivEXT; ++count; }
        if (!glad_glSecondaryColor3sEXT) { glad_glSecondaryColor3sEXT = &S_glSecondaryColor3sEXT; ++count; }
        if (!glad_glSecondaryColor3svEXT) { glad_glSecondaryColor3svEXT = &S_glSecondaryColor3svEXT; ++count; }
        if (!glad_glSecondaryColor3ubEXT) { glad_glSecondaryColor3ubEXT = &S_glSecondaryColor3ubEXT; ++count; }
        if (!glad_glSecondaryColor3ubvEXT) { glad_glSecondaryColor3ubvEXT = &S_glSecondaryColor3ubvEXT; ++count; }
        if (!glad_glSecondaryColor3uiEXT) { glad_glSecondaryColor3uiEXT = &S_glSecondaryColor3uiEXT; ++count; }
        if (!glad_glSecondaryColor3uivEXT) { glad_glSecondaryColor3uivEXT = &S_glSecondaryColor3uivEXT; ++count; }
        if (!glad_glSecondaryColor3usEXT) { glad_glSecondaryColor3usEXT = &S_glSecondaryColor3usEXT; ++count; }
        if (!glad_glSecondaryColor3usvEXT) { glad_glSecondaryColor3usvEXT = &S_glSecondaryColor3usvEXT; ++count; }
        if (!glad_glSecondaryColorPointerEXT) { glad_glSecondaryColorPointerEXT = &S_glSecondaryColorPointerEXT; ++count; }
        if (!glad_glGenSemaphoresEXT) { glad_glGenSemaphoresEXT = &S_glGenSemaphoresEXT; ++count; }
        if (!glad_glDeleteSemaphoresEXT) { glad_glDeleteSemaphoresEXT = &S_glDeleteSemaphoresEXT; ++count; }
        if (!glad_glIsSemaphoreEXT) { glad_glIsSemaphoreEXT = &S_glIsSemaphoreEXT; ++count; }
        if (!glad_glSemaphoreParameterui64vEXT) { glad_glSemaphoreParameterui64vEXT = &S_glSemaphoreParameterui64vEXT; ++count; }
        if (!glad_glGetSemaphoreParameterui64vEXT) { glad_glGetSemaphoreParameterui64vEXT = &S_glGetSemaphoreParameterui64vEXT; ++count; }
        if (!glad_glWaitSemaphoreEXT) { glad_glWaitSemaphoreEXT = &S_glWaitSemaphoreEXT; ++count; }
        if (!glad_glSignalSemaphoreEXT) { glad_glSignalSemaphoreEXT = &S_glSignalSemaphoreEXT; ++count; }
        if (!glad_glImportSemaphoreFdEXT) { glad_glImportSemaphoreFdEXT = &S_glImportSemaphoreFdEXT; ++count; }
        if (!glad_glImportSemaphoreWin32HandleEXT) { glad_glImportSemaphoreWin32HandleEXT = &S_glImportSemaphoreWin32HandleEXT; ++count; }
        if (!glad_glImportSemaphoreWin32NameEXT) { glad_glImportSemaphoreWin32NameEXT = &S_glImportSemaphoreWin32NameEXT; ++count; }
        if (!glad_glUseShaderProgramEXT) { glad_glUseShaderProgramEXT = &S_glUseShaderProgramEXT; ++count; }
        if (!glad_glActiveProgramEXT) { glad_glActiveProgramEXT = &S_glActiveProgramEXT; ++count; }
        if (!glad_glCreateShaderProgramEXT) { glad_glCreateShaderProgramEXT = &S_glCreateShaderProgramEXT; ++count; }
        if (!glad_glActiveShaderProgramEXT) { glad_glActiveShaderProgramEXT = &S_glActiveShaderProgramEXT; ++count; }
        if (!glad_glBindProgramPipelineEXT) { glad_glBindProgramPipelineEXT = &S_glBindProgramPipelineEXT; ++count; }
        if (!glad_glCreateShaderProgramvEXT) { glad_glCreateShaderProgramvEXT = &S_glCreateShaderProgramvEXT; ++count; }
        if (!glad_glDeleteProgramPipelinesEXT) { glad_glDeleteProgramPipelinesEXT = &S_glDeleteProgramPipelinesEXT; ++count; }
        if (!glad_glGenProgramPipelinesEXT) { glad_glGenProgramPipelinesEXT = &S_glGenProgramPipelinesEXT; ++count; }
        if (!glad_glGetProgramPipelineInfoLogEXT) { glad_glGetProgramPipelineInfoLogEXT = &S_glGetProgramPipelineInfoLogEXT; ++count; }
        if (!glad_glGetProgramPipelineivEXT) { glad_glGetProgramPipelineivEXT = &S_glGetProgramPipelineivEXT; ++count; }
        if (!glad_glIsProgramPipelineEXT) { glad_glIsProgramPipelineEXT = &S_glIsProgramPipelineEXT; ++count; }
        if (!glad_glUseProgramStagesEXT) { glad_glUseProgramStagesEXT = &S_glUseProgramStagesEXT; ++count; }
        if (!glad_glValidateProgramPipelineEXT) { glad_glValidateProgramPipelineEXT = &S_glValidateProgramPipelineEXT; ++count; }
        if (!glad_glFramebufferFetchBarrierEXT) { glad_glFramebufferFetchBarrierEXT = &S_glFramebufferFetchBarrierEXT; ++count; }
        if (!glad_glBindImageTextureEXT) { glad_glBindImageTextureEXT = &S_glBindImageTextureEXT; ++count; }
        if (!glad_glMemoryBarrierEXT) { glad_glMemoryBarrierEXT = &S_glMemoryBarrierEXT; ++count; }
        if (!glad_glStencilClearTagEXT) { glad_glStencilClearTagEXT = &S_glStencilClearTagEXT; ++count; }
        if (!glad_glActiveStencilFaceEXT) { glad_glActiveStencilFaceEXT = &S_glActiveStencilFaceEXT; ++count; }
        if (!glad_glTexSubImage1DEXT) { glad_glTexSubImage1DEXT = &S_glTexSubImage1DEXT; ++count; }
        if (!glad_glTexSubImage2DEXT) { glad_glTexSubImage2DEXT = &S_glTexSubImage2DEXT; ++count; }
        if (!glad_glTexImage3DEXT) { glad_glTexImage3DEXT = &S_glTexImage3DEXT; ++count; }
        if (!glad_glTexSubImage3DEXT) { glad_glTexSubImage3DEXT = &S_glTexSubImage3DEXT; ++count; }
        if (!glad_glFramebufferTextureLayerEXT) { glad_glFramebufferTextureLayerEXT = &S_glFramebufferTextureLayerEXT; ++count; }
        if (!glad_glTexBufferEXT) { glad_glTexBufferEXT = &S_glTexBufferEXT; ++count; }
        if (!glad_glTexParameterIivEXT) { glad_glTexParameterIivEXT = &S_glTexParameterIivEXT; ++count; }
        if (!glad_glTexParameterIuivEXT) { glad_glTexParameterIuivEXT = &S_glTexParameterIuivEXT; ++count; }
        if (!glad_glGetTexParameterIivEXT) { glad_glGetTexParameterIivEXT = &S_glGetTexParameterIivEXT; ++count; }
        if (!glad_glGetTexParameterIuivEXT) { glad_glGetTexParameterIuivEXT = &S_glGetTexParameterIuivEXT; ++count; }
        if (!glad_glClearColorIiEXT) { glad_glClearColorIiEXT = &S_glClearColorIiEXT; ++count; }
        if (!glad_glClearColorIuiEXT) { glad_glClearColorIuiEXT = &S_glClearColorIuiEXT; ++count; }
        if (!glad_glAreTexturesResidentEXT) { glad_glAreTexturesResidentEXT = &S_glAreTexturesResidentEXT; ++count; }
        if (!glad_glBindTextureEXT) { glad_glBindTextureEXT = &S_glBindTextureEXT; ++count; }
        if (!glad_glDeleteTexturesEXT) { glad_glDeleteTexturesEXT = &S_glDeleteTexturesEXT; ++count; }
        if (!glad_glGenTexturesEXT) { glad_glGenTexturesEXT = &S_glGenTexturesEXT; ++count; }
        if (!glad_glIsTextureEXT) { glad_glIsTextureEXT = &S_glIsTextureEXT; ++count; }
        if (!glad_glPrioritizeTexturesEXT) { glad_glPrioritizeTexturesEXT = &S_glPrioritizeTexturesEXT; ++count; }
        if (!glad_glTextureNormalEXT) { glad_glTextureNormalEXT = &S_glTextureNormalEXT; ++count; }
        if (!glad_glTexStorage1DEXT) { glad_glTexStorage1DEXT = &S_glTexStorage1DEXT; ++count; }
        if (!glad_glTexStorage2DEXT) { glad_glTexStorage2DEXT = &S_glTexStorage2DEXT; ++count; }
        if (!glad_glTexStorage3DEXT) { glad_glTexStorage3DEXT = &S_glTexStorage3DEXT; ++count; }
        if (!glad_glGetQueryObjecti64vEXT) { glad_glGetQueryObjecti64vEXT = &S_glGetQueryObjecti64vEXT; ++count; }
        if (!glad_glGetQueryObjectui64vEXT) { glad_glGetQueryObjectui64vEXT = &S_glGetQueryObjectui64vEXT; ++count; }
        if (!glad_glBeginTransformFeedbackEXT) { glad_glBeginTransformFeedbackEXT = &S_glBeginTransformFeedbackEXT; ++count; }
        if (!glad_glEndTransformFeedbackEXT) { glad_glEndTransformFeedbackEXT = &S_glEndTransformFeedbackEXT; ++count; }
        if (!glad_glBindBufferRangeEXT) { glad_glBindBufferRangeEXT = &S_glBindBufferRangeEXT; ++count; }
        if (!glad_glBindBufferOffsetEXT) { glad_glBindBufferOffsetEXT = &S_glBindBufferOffsetEXT; ++count; }
        if (!glad_glBindBufferBaseEXT) { glad_glBindBufferBaseEXT = &S_glBindBufferBaseEXT; ++count; }
        if (!glad_glTransformFeedbackVaryingsEXT) { glad_glTransformFeedbackVaryingsEXT = &S_glTransformFeedbackVaryingsEXT; ++count; }
        if (!glad_glGetTransformFeedbackVaryingEXT) { glad_glGetTransformFeedbackVaryingEXT = &S_glGetTransformFeedbackVaryingEXT; ++count; }
        if (!glad_glArrayElementEXT) { glad_glArrayElementEXT = &S_glArrayElementEXT; ++count; }
        if (!glad_glColorPointerEXT) { glad_glColorPointerEXT = &S_glColorPointerEXT; ++count; }
        if (!glad_glDrawArraysEXT) { glad_glDrawArraysEXT = &S_glDrawArraysEXT; ++count; }
        if (!glad_glEdgeFlagPointerEXT) { glad_glEdgeFlagPointerEXT = &S_glEdgeFlagPointerEXT; ++count; }
        if (!glad_glGetPointervEXT) { glad_glGetPointervEXT = &S_glGetPointervEXT; ++count; }
        if (!glad_glIndexPointerEXT) { glad_glIndexPointerEXT = &S_glIndexPointerEXT; ++count; }
        if (!glad_glNormalPointerEXT) { glad_glNormalPointerEXT = &S_glNormalPointerEXT; ++count; }
        if (!glad_glTexCoordPointerEXT) { glad_glTexCoordPointerEXT = &S_glTexCoordPointerEXT; ++count; }
        if (!glad_glVertexPointerEXT) { glad_glVertexPointerEXT = &S_glVertexPointerEXT; ++count; }
        if (!glad_glVertexAttribL1dEXT) { glad_glVertexAttribL1dEXT = &S_glVertexAttribL1dEXT; ++count; }
        if (!glad_glVertexAttribL2dEXT) { glad_glVertexAttribL2dEXT = &S_glVertexAttribL2dEXT; ++count; }
        if (!glad_glVertexAttribL3dEXT) { glad_glVertexAttribL3dEXT = &S_glVertexAttribL3dEXT; ++count; }
        if (!glad_glVertexAttribL4dEXT) { glad_glVertexAttribL4dEXT = &S_glVertexAttribL4dEXT; ++count; }
        if (!glad_glVertexAttribL1dvEXT) { glad_glVertexAttribL1dvEXT = &S_glVertexAttribL1dvEXT; ++count; }
        if (!glad_glVertexAttribL2dvEXT) { glad_glVertexAttribL2dvEXT = &S_glVertexAttribL2dvEXT; ++count; }
        if (!glad_glVertexAttribL3dvEXT) { glad_glVertexAttribL3dvEXT = &S_glVertexAttribL3dvEXT; ++count; }
        if (!glad_glVertexAttribL4dvEXT) { glad_glVertexAttribL4dvEXT = &S_glVertexAttribL4dvEXT; ++count; }
        if (!glad_glVertexAttribLPointerEXT) { glad_glVertexAttribLPointerEXT = &S_glVertexAttribLPointerEXT; ++count; }
        if (!glad_glGetVertexAttribLdvEXT) { glad_glGetVertexAttribLdvEXT = &S_glGetVertexAttribLdvEXT; ++count; }
        if (!glad_glBeginVertexShaderEXT) { glad_glBeginVertexShaderEXT = &S_glBeginVertexShaderEXT; ++count; }
        if (!glad_glEndVertexShaderEXT) { glad_glEndVertexShaderEXT = &S_glEndVertexShaderEXT; ++count; }
        if (!glad_glBindVertexShaderEXT) { glad_glBindVertexShaderEXT = &S_glBindVertexShaderEXT; ++count; }
        if (!glad_glGenVertexShadersEXT) { glad_glGenVertexShadersEXT = &S_glGenVertexShadersEXT; ++count; }
        if (!glad_glDeleteVertexShaderEXT) { glad_glDeleteVertexShaderEXT = &S_glDeleteVertexShaderEXT; ++count; }
        if (!glad_glShaderOp1EXT) { glad_glShaderOp1EXT = &S_glShaderOp1EXT; ++count; }
        if (!glad_glShaderOp2EXT) { glad_glShaderOp2EXT = &S_glShaderOp2EXT; ++count; }
        if (!glad_glShaderOp3EXT) { glad_glShaderOp3EXT = &S_glShaderOp3EXT; ++count; }
        if (!glad_glSwizzleEXT) { glad_glSwizzleEXT = &S_glSwizzleEXT; ++count; }
        if (!glad_glWriteMaskEXT) { glad_glWriteMaskEXT = &S_glWriteMaskEXT; ++count; }
        if (!glad_glInsertComponentEXT) { glad_glInsertComponentEXT = &S_glInsertComponentEXT; ++count; }
        if (!glad_glExtractComponentEXT) { glad_glExtractComponentEXT = &S_glExtractComponentEXT; ++count; }
        if (!glad_glGenSymbolsEXT) { glad_glGenSymbolsEXT = &S_glGenSymbolsEXT; ++count; }
        if (!glad_glSetInvariantEXT) { glad_glSetInvariantEXT = &S_glSetInvariantEXT; ++count; }
        if (!glad_glSetLocalConstantEXT) { glad_glSetLocalConstantEXT = &S_glSetLocalConstantEXT; ++count; }
        if (!glad_glVariantbvEXT) { glad_glVariantbvEXT = &S_glVariantbvEXT; ++count; }
        if (!glad_glVariantsvEXT) { glad_glVariantsvEXT = &S_glVariantsvEXT; ++count; }
        if (!glad_glVariantivEXT) { glad_glVariantivEXT = &S_glVariantivEXT; ++count; }
        if (!glad_glVariantfvEXT) { glad_glVariantfvEXT = &S_glVariantfvEXT; ++count; }
        if (!glad_glVariantdvEXT) { glad_glVariantdvEXT = &S_glVariantdvEXT; ++count; }
        if (!glad_glVariantubvEXT) { glad_glVariantubvEXT = &S_glVariantubvEXT; ++count; }
        if (!glad_glVariantusvEXT) { glad_glVariantusvEXT = &S_glVariantusvEXT; ++count; }
        if (!glad_glVariantuivEXT) { glad_glVariantuivEXT = &S_glVariantuivEXT; ++count; }
        if (!glad_glVariantPointerEXT) { glad_glVariantPointerEXT = &S_glVariantPointerEXT; ++count; }
        if (!glad_glEnableVariantClientStateEXT) { glad_glEnableVariantClientStateEXT = &S_glEnableVariantClientStateEXT; ++count; }
        if (!glad_glDisableVariantClientStateEXT) { glad_glDisableVariantClientStateEXT = &S_glDisableVariantClientStateEXT; ++count; }
        if (!glad_glBindLightParameterEXT) { glad_glBindLightParameterEXT = &S_glBindLightParameterEXT; ++count; }
        if (!glad_glBindMaterialParameterEXT) { glad_glBindMaterialParameterEXT = &S_glBindMaterialParameterEXT; ++count; }
        if (!glad_glBindTexGenParameterEXT) { glad_glBindTexGenParameterEXT = &S_glBindTexGenParameterEXT; ++count; }
        if (!glad_glBindTextureUnitParameterEXT) { glad_glBindTextureUnitParameterEXT = &S_glBindTextureUnitParameterEXT; ++count; }
        if (!glad_glBindParameterEXT) { glad_glBindParameterEXT = &S_glBindParameterEXT; ++count; }
        if (!glad_glIsVariantEnabledEXT) { glad_glIsVariantEnabledEXT = &S_glIsVariantEnabledEXT; ++count; }
        if (!glad_glGetVariantBooleanvEXT) { glad_glGetVariantBooleanvEXT = &S_glGetVariantBooleanvEXT; ++count; }
        if (!glad_glGetVariantIntegervEXT) { glad_glGetVariantIntegervEXT = &S_glGetVariantIntegervEXT; ++count; }
        if (!glad_glGetVariantFloatvEXT) { glad_glGetVariantFloatvEXT = &S_glGetVariantFloatvEXT; ++count; }
        if (!glad_glGetVariantPointervEXT) { glad_glGetVariantPointervEXT = &S_glGetVariantPointervEXT; ++count; }
        if (!glad_glGetInvariantBooleanvEXT) { glad_glGetInvariantBooleanvEXT = &S_glGetInvariantBooleanvEXT; ++count; }
        if (!glad_glGetInvariantIntegervEXT) { glad_glGetInvariantIntegervEXT = &S_glGetInvariantIntegervEXT; ++count; }
        if (!glad_glGetInvariantFloatvEXT) { glad_glGetInvariantFloatvEXT = &S_glGetInvariantFloatvEXT; ++count; }
        if (!glad_glGetLocalConstantBooleanvEXT) { glad_glGetLocalConstantBooleanvEXT = &S_glGetLocalConstantBooleanvEXT; ++count; }
        if (!glad_glGetLocalConstantIntegervEXT) { glad_glGetLocalConstantIntegervEXT = &S_glGetLocalConstantIntegervEXT; ++count; }
        if (!glad_glGetLocalConstantFloatvEXT) { glad_glGetLocalConstantFloatvEXT = &S_glGetLocalConstantFloatvEXT; ++count; }
        if (!glad_glVertexWeightfEXT) { glad_glVertexWeightfEXT = &S_glVertexWeightfEXT; ++count; }
        if (!glad_glVertexWeightfvEXT) { glad_glVertexWeightfvEXT = &S_glVertexWeightfvEXT; ++count; }
        if (!glad_glVertexWeightPointerEXT) { glad_glVertexWeightPointerEXT = &S_glVertexWeightPointerEXT; ++count; }
        if (!glad_glAcquireKeyedMutexWin32EXT) { glad_glAcquireKeyedMutexWin32EXT = &S_glAcquireKeyedMutexWin32EXT; ++count; }
        if (!glad_glReleaseKeyedMutexWin32EXT) { glad_glReleaseKeyedMutexWin32EXT = &S_glReleaseKeyedMutexWin32EXT; ++count; }
        if (!glad_glWindowRectanglesEXT) { glad_glWindowRectanglesEXT = &S_glWindowRectanglesEXT; ++count; }
        if (!glad_glImportSyncEXT) { glad_glImportSyncEXT = &S_glImportSyncEXT; ++count; }
        if (!glad_glFrameTerminatorGREMEDY) { glad_glFrameTerminatorGREMEDY = &S_glFrameTerminatorGREMEDY; ++count; }
        if (!glad_glStringMarkerGREMEDY) { glad_glStringMarkerGREMEDY = &S_glStringMarkerGREMEDY; ++count; }
        if (!glad_glImageTransformParameteriHP) { glad_glImageTransformParameteriHP = &S_glImageTransformParameteriHP; ++count; }
        if (!glad_glImageTransformParameterfHP) { glad_glImageTransformParameterfHP = &S_glImageTransformParameterfHP; ++count; }
        if (!glad_glImageTransformParameterivHP) { glad_glImageTransformParameterivHP = &S_glImageTransformParameterivHP; ++count; }
        if (!glad_glImageTransformParameterfvHP) { glad_glImageTransformParameterfvHP = &S_glImageTransformParameterfvHP; ++count; }
        if (!glad_glGetImageTransformParameterivHP) { glad_glGetImageTransformParameterivHP = &S_glGetImageTransformParameterivHP; ++count; }
        if (!glad_glGetImageTransformParameterfvHP) { glad_glGetImageTransformParameterfvHP = &S_glGetImageTransformParameterfvHP; ++count; }
        if (!glad_glMultiModeDrawArraysIBM) { glad_glMultiModeDrawArraysIBM = &S_glMultiModeDrawArraysIBM; ++count; }
        if (!glad_glMultiModeDrawElementsIBM) { glad_glMultiModeDrawElementsIBM = &S_glMultiModeDrawElementsIBM; ++count; }
        if (!glad_glFlushStaticDataIBM) { glad_glFlushStaticDataIBM = &S_glFlushStaticDataIBM; ++count; }
        if (!glad_glColorPointerListIBM) { glad_glColorPointerListIBM = &S_glColorPointerListIBM; ++count; }
        if (!glad_glSecondaryColorPointerListIBM) { glad_glSecondaryColorPointerListIBM = &S_glSecondaryColorPointerListIBM; ++count; }
        if (!glad_glEdgeFlagPointerListIBM) { glad_glEdgeFlagPointerListIBM = &S_glEdgeFlagPointerListIBM; ++count; }
        if (!glad_glFogCoordPointerListIBM) { glad_glFogCoordPointerListIBM = &S_glFogCoordPointerListIBM; ++count; }
        if (!glad_glIndexPointerListIBM) { glad_glIndexPointerListIBM = &S_glIndexPointerListIBM; ++count; }
        if (!glad_glNormalPointerListIBM) { glad_glNormalPointerListIBM = &S_glNormalPointerListIBM; ++count; }
        if (!glad_glTexCoordPointerListIBM) { glad_glTexCoordPointerListIBM = &S_glTexCoordPointerListIBM; ++count; }
        if (!glad_glVertexPointerListIBM) { glad_glVertexPointerListIBM = &S_glVertexPointerListIBM; ++count; }
        if (!glad_glBlendFuncSeparateINGR) { glad_glBlendFuncSeparateINGR = &S_glBlendFuncSeparateINGR; ++count; }
        if (!glad_glApplyFramebufferAttachmentCMAAINTEL) { glad_glApplyFramebufferAttachmentCMAAINTEL = &S_glApplyFramebufferAttachmentCMAAINTEL; ++count; }
        if (!glad_glSyncTextureINTEL) { glad_glSyncTextureINTEL = &S_glSyncTextureINTEL; ++count; }
        if (!glad_glUnmapTexture2DINTEL) { glad_glUnmapTexture2DINTEL = &S_glUnmapTexture2DINTEL; ++count; }
        if (!glad_glMapTexture2DINTEL) { glad_glMapTexture2DINTEL = &S_glMapTexture2DINTEL; ++count; }
        if (!glad_glVertexPointervINTEL) { glad_glVertexPointervINTEL = &S_glVertexPointervINTEL; ++count; }
        if (!glad_glNormalPointervINTEL) { glad_glNormalPointervINTEL = &S_glNormalPointervINTEL; ++count; }
        if (!glad_glColorPointervINTEL) { glad_glColorPointervINTEL = &S_glColorPointervINTEL; ++count; }
        if (!glad_glTexCoordPointervINTEL) { glad_glTexCoordPointervINTEL = &S_glTexCoordPointervINTEL; ++count; }
        if (!glad_glBeginPerfQueryINTEL) { glad_glBeginPerfQueryINTEL = &S_glBeginPerfQueryINTEL; ++count; }
        if (!glad_glCreatePerfQueryINTEL) { glad_glCreatePerfQueryINTEL = &S_glCreatePerfQueryINTEL; ++count; }
        if (!glad_glDeletePerfQueryINTEL) { glad_glDeletePerfQueryINTEL = &S_glDeletePerfQueryINTEL; ++count; }
        if (!glad_glEndPerfQueryINTEL) { glad_glEndPerfQueryINTEL = &S_glEndPerfQueryINTEL; ++count; }
        if (!glad_glGetFirstPerfQueryIdINTEL) { glad_glGetFirstPerfQueryIdINTEL = &S_glGetFirstPerfQueryIdINTEL; ++count; }
        if (!glad_glGetNextPerfQueryIdINTEL) { glad_glGetNextPerfQueryIdINTEL = &S_glGetNextPerfQueryIdINTEL; ++count; }
        if (!glad_glGetPerfCounterInfoINTEL) { glad_glGetPerfCounterInfoINTEL = &S_glGetPerfCounterInfoINTEL; ++count; }
        if (!glad_glGetPerfQueryDataINTEL) { glad_glGetPerfQueryDataINTEL = &S_glGetPerfQueryDataINTEL; ++count; }
        if (!glad_glGetPerfQueryIdByNameINTEL) { glad_glGetPerfQueryIdByNameINTEL = &S_glGetPerfQueryIdByNameINTEL; ++count; }
        if (!glad_glGetPerfQueryInfoINTEL) { glad_glGetPerfQueryInfoINTEL = &S_glGetPerfQueryInfoINTEL; ++count; }
        if (!glad_glBlendBarrierKHR) { glad_glBlendBarrierKHR = &S_glBlendBarrierKHR; ++count; }
        if (!glad_glDebugMessageControlKHR) { glad_glDebugMessageControlKHR = &S_glDebugMessageControlKHR; ++count; }
        if (!glad_glDebugMessageInsertKHR) { glad_glDebugMessageInsertKHR = &S_glDebugMessageInsertKHR; ++count; }
        if (!glad_glDebugMessageCallbackKHR) { glad_glDebugMessageCallbackKHR = &S_glDebugMessageCallbackKHR; ++count; }
        if (!glad_glGetDebugMessageLogKHR) { glad_glGetDebugMessageLogKHR = &S_glGetDebugMessageLogKHR; ++count; }
        if (!glad_glPushDebugGroupKHR) { glad_glPushDebugGroupKHR = &S_glPushDebugGroupKHR; ++count; }
        if (!glad_glPopDebugGroupKHR) { glad_glPopDebugGroupKHR = &S_glPopDebugGroupKHR; ++count; }
        if (!glad_glObjectLabelKHR) { glad_glObjectLabelKHR = &S_glObjectLabelKHR; ++count; }
        if (!glad_glGetObjectLabelKHR) { glad_glGetObjectLabelKHR = &S_glGetObjectLabelKHR; ++count; }
        if (!glad_glObjectPtrLabelKHR) { glad_glObjectPtrLabelKHR = &S_glObjectPtrLabelKHR; ++count; }
        if (!glad_glGetObjectPtrLabelKHR) { glad_glGetObjectPtrLabelKHR = &S_glGetObjectPtrLabelKHR; ++count; }
        if (!glad_glGetPointervKHR) { glad_glGetPointervKHR = &S_glGetPointervKHR; ++count; }
        if (!glad_glMaxShaderCompilerThreadsKHR) { glad_glMaxShaderCompilerThreadsKHR = &S_glMaxShaderCompilerThreadsKHR; ++count; }
        if (!glad_glGetGraphicsResetStatusKHR) { glad_glGetGraphicsResetStatusKHR = &S_glGetGraphicsResetStatusKHR; ++count; }
        if (!glad_glReadnPixelsKHR) { glad_glReadnPixelsKHR = &S_glReadnPixelsKHR; ++count; }
        if (!glad_glGetnUniformfvKHR) { glad_glGetnUniformfvKHR = &S_glGetnUniformfvKHR; ++count; }
        if (!glad_glGetnUniformivKHR) { glad_glGetnUniformivKHR = &S_glGetnUniformivKHR; ++count; }
        if (!glad_glGetnUniformuivKHR) { glad_glGetnUniformuivKHR = &S_glGetnUniformuivKHR; ++count; }
        if (!glad_glFramebufferParameteriMESA) { glad_glFramebufferParameteriMESA = &S_glFramebufferParameteriMESA; ++count; }
        if (!glad_glGetFramebufferParameterivMESA) { glad_glGetFramebufferParameterivMESA = &S_glGetFramebufferParameterivMESA; ++count; }
        if (!glad_glResizeBuffersMESA) { glad_glResizeBuffersMESA = &S_glResizeBuffersMESA; ++count; }
        if (!glad_glWindowPos2dMESA) { glad_glWindowPos2dMESA = &S_glWindowPos2dMESA; ++count; }
        if (!glad_glWindowPos2dvMESA) { glad_glWindowPos2dvMESA = &S_glWindowPos2dvMESA; ++count; }
        if (!glad_glWindowPos2fMESA) { glad_glWindowPos2fMESA = &S_glWindowPos2fMESA; ++count; }
        if (!glad_glWindowPos2fvMESA) { glad_glWindowPos2fvMESA = &S_glWindowPos2fvMESA; ++count; }
        if (!glad_glWindowPos2iMESA) { glad_glWindowPos2iMESA = &S_glWindowPos2iMESA; ++count; }
        if (!glad_glWindowPos2ivMESA) { glad_glWindowPos2ivMESA = &S_glWindowPos2ivMESA; ++count; }
        if (!glad_glWindowPos2sMESA) { glad_glWindowPos2sMESA = &S_glWindowPos2sMESA; ++count; }
        if (!glad_glWindowPos2svMESA) { glad_glWindowPos2svMESA = &S_glWindowPos2svMESA; ++count; }
        if (!glad_glWindowPos3dMESA) { glad_glWindowPos3dMESA = &S_glWindowPos3dMESA; ++count; }
        if (!glad_glWindowPos3dvMESA) { glad_glWindowPos3dvMESA = &S_glWindowPos3dvMESA; ++count; }
        if (!glad_glWindowPos3fMESA) { glad_glWindowPos3fMESA = &S_glWindowPos3fMESA; ++count; }
        if (!glad_glWindowPos3fvMESA) { glad_glWindowPos3fvMESA = &S_glWindowPos3fvMESA; ++count; }
        if (!glad_glWindowPos3iMESA) { glad_glWindowPos3iMESA = &S_glWindowPos3iMESA; ++count; }
        if (!glad_glWindowPos3ivMESA) { glad_glWindowPos3ivMESA = &S_glWindowPos3ivMESA; ++count; }
        if (!glad_glWindowPos3sMESA) { glad_glWindowPos3sMESA = &S_glWindowPos3sMESA; ++count; }
        if (!glad_glWindowPos3svMESA) { glad_glWindowPos3svMESA = &S_glWindowPos3svMESA; ++count; }
        if (!glad_glWindowPos4dMESA) { glad_glWindowPos4dMESA = &S_glWindowPos4dMESA; ++count; }
        if (!glad_glWindowPos4dvMESA) { glad_glWindowPos4dvMESA = &S_glWindowPos4dvMESA; ++count; }
        if (!glad_glWindowPos4fMESA) { glad_glWindowPos4fMESA = &S_glWindowPos4fMESA; ++count; }
        if (!glad_glWindowPos4fvMESA) { glad_glWindowPos4fvMESA = &S_glWindowPos4fvMESA; ++count; }
        if (!glad_glWindowPos4iMESA) { glad_glWindowPos4iMESA = &S_glWindowPos4iMESA; ++count; }
        if (!glad_glWindowPos4ivMESA) { glad_glWindowPos4ivMESA = &S_glWindowPos4ivMESA; ++count; }
        if (!glad_glWindowPos4sMESA) { glad_glWindowPos4sMESA = &S_glWindowPos4sMESA; ++count; }
        if (!glad_glWindowPos4svMESA) { glad_glWindowPos4svMESA = &S_glWindowPos4svMESA; ++count; }
        if (!glad_glBeginConditionalRenderNVX) { glad_glBeginConditionalRenderNVX = &S_glBeginConditionalRenderNVX; ++count; }
        if (!glad_glEndConditionalRenderNVX) { glad_glEndConditionalRenderNVX = &S_glEndConditionalRenderNVX; ++count; }
        if (!glad_glUploadGpuMaskNVX) { glad_glUploadGpuMaskNVX = &S_glUploadGpuMaskNVX; ++count; }
        if (!glad_glMulticastViewportArrayvNVX) { glad_glMulticastViewportArrayvNVX = &S_glMulticastViewportArrayvNVX; ++count; }
        if (!glad_glMulticastViewportPositionWScaleNVX) { glad_glMulticastViewportPositionWScaleNVX = &S_glMulticastViewportPositionWScaleNVX; ++count; }
        if (!glad_glMulticastScissorArrayvNVX) { glad_glMulticastScissorArrayvNVX = &S_glMulticastScissorArrayvNVX; ++count; }
        if (!glad_glAsyncCopyBufferSubDataNVX) { glad_glAsyncCopyBufferSubDataNVX = &S_glAsyncCopyBufferSubDataNVX; ++count; }
        if (!glad_glAsyncCopyImageSubDataNVX) { glad_glAsyncCopyImageSubDataNVX = &S_glAsyncCopyImageSubDataNVX; ++count; }
        if (!glad_glLGPUNamedBufferSubDataNVX) { glad_glLGPUNamedBufferSubDataNVX = &S_glLGPUNamedBufferSubDataNVX; ++count; }
        if (!glad_glLGPUCopyImageSubDataNVX) { glad_glLGPUCopyImageSubDataNVX = &S_glLGPUCopyImageSubDataNVX; ++count; }
        if (!glad_glLGPUInterlockNVX) { glad_glLGPUInterlockNVX = &S_glLGPUInterlockNVX; ++count; }
        if (!glad_glCreateProgressFenceNVX) { glad_glCreateProgressFenceNVX = &S_glCreateProgressFenceNVX; ++count; }
        if (!glad_glSignalSemaphoreui64NVX) { glad_glSignalSemaphoreui64NVX = &S_glSignalSemaphoreui64NVX; ++count; }
        if (!glad_glWaitSemaphoreui64NVX) { glad_glWaitSemaphoreui64NVX = &S_glWaitSemaphoreui64NVX; ++count; }
        if (!glad_glClientWaitSemaphoreui64NVX) { glad_glClientWaitSemaphoreui64NVX = &S_glClientWaitSemaphoreui64NVX; ++count; }
        if (!glad_glAlphaToCoverageDitherControlNV) { glad_glAlphaToCoverageDitherControlNV = &S_glAlphaToCoverageDitherControlNV; ++count; }
        if (!glad_glMultiDrawArraysIndirectBindlessNV) { glad_glMultiDrawArraysIndirectBindlessNV = &S_glMultiDrawArraysIndirectBindlessNV; ++count; }
        if (!glad_glMultiDrawElementsIndirectBindlessNV) { glad_glMultiDrawElementsIndirectBindlessNV = &S_glMultiDrawElementsIndirectBindlessNV; ++count; }
        if (!glad_glMultiDrawArraysIndirectBindlessCountNV) { glad_glMultiDrawArraysIndirectBindlessCountNV = &S_glMultiDrawArraysIndirectBindlessCountNV; ++count; }
        if (!glad_glMultiDrawElementsIndirectBindlessCountNV) { glad_glMultiDrawElementsIndirectBindlessCountNV = &S_glMultiDrawElementsIndirectBindlessCountNV; ++count; }
        if (!glad_glGetTextureHandleNV) { glad_glGetTextureHandleNV = &S_glGetTextureHandleNV; ++count; }
        if (!glad_glGetTextureSamplerHandleNV) { glad_glGetTextureSamplerHandleNV = &S_glGetTextureSamplerHandleNV; ++count; }
        if (!glad_glMakeTextureHandleResidentNV) { glad_glMakeTextureHandleResidentNV = &S_glMakeTextureHandleResidentNV; ++count; }
        if (!glad_glMakeTextureHandleNonResidentNV) { glad_glMakeTextureHandleNonResidentNV = &S_glMakeTextureHandleNonResidentNV; ++count; }
        if (!glad_glGetImageHandleNV) { glad_glGetImageHandleNV = &S_glGetImageHandleNV; ++count; }
        if (!glad_glMakeImageHandleResidentNV) { glad_glMakeImageHandleResidentNV = &S_glMakeImageHandleResidentNV; ++count; }
        if (!glad_glMakeImageHandleNonResidentNV) { glad_glMakeImageHandleNonResidentNV = &S_glMakeImageHandleNonResidentNV; ++count; }
        if (!glad_glUniformHandleui64NV) { glad_glUniformHandleui64NV = &S_glUniformHandleui64NV; ++count; }
        if (!glad_glUniformHandleui64vNV) { glad_glUniformHandleui64vNV = &S_glUniformHandleui64vNV; ++count; }
        if (!glad_glProgramUniformHandleui64NV) { glad_glProgramUniformHandleui64NV = &S_glProgramUniformHandleui64NV; ++count; }
        if (!glad_glProgramUniformHandleui64vNV) { glad_glProgramUniformHandleui64vNV = &S_glProgramUniformHandleui64vNV; ++count; }
        if (!glad_glIsTextureHandleResidentNV) { glad_glIsTextureHandleResidentNV = &S_glIsTextureHandleResidentNV; ++count; }
        if (!glad_glIsImageHandleResidentNV) { glad_glIsImageHandleResidentNV = &S_glIsImageHandleResidentNV; ++count; }
        if (!glad_glBlendParameteriNV) { glad_glBlendParameteriNV = &S_glBlendParameteriNV; ++count; }
        if (!glad_glBlendBarrierNV) { glad_glBlendBarrierNV = &S_glBlendBarrierNV; ++count; }
        if (!glad_glViewportPositionWScaleNV) { glad_glViewportPositionWScaleNV = &S_glViewportPositionWScaleNV; ++count; }
        if (!glad_glCreateStatesNV) { glad_glCreateStatesNV = &S_glCreateStatesNV; ++count; }
        if (!glad_glDeleteStatesNV) { glad_glDeleteStatesNV = &S_glDeleteStatesNV; ++count; }
        if (!glad_glIsStateNV) { glad_glIsStateNV = &S_glIsStateNV; ++count; }
        if (!glad_glStateCaptureNV) { glad_glStateCaptureNV = &S_glStateCaptureNV; ++count; }
        if (!glad_glGetCommandHeaderNV) { glad_glGetCommandHeaderNV = &S_glGetCommandHeaderNV; ++count; }
        if (!glad_glGetStageIndexNV) { glad_glGetStageIndexNV = &S_glGetStageIndexNV; ++count; }
        if (!glad_glDrawCommandsNV) { glad_glDrawCommandsNV = &S_glDrawCommandsNV; ++count; }
        if (!glad_glDrawCommandsAddressNV) { glad_glDrawCommandsAddressNV = &S_glDrawCommandsAddressNV; ++count; }
        if (!glad_glDrawCommandsStatesNV) { glad_glDrawCommandsStatesNV = &S_glDrawCommandsStatesNV; ++count; }
        if (!glad_glDrawCommandsStatesAddressNV) { glad_glDrawCommandsStatesAddressNV = &S_glDrawCommandsStatesAddressNV; ++count; }
        if (!glad_glCreateCommandListsNV) { glad_glCreateCommandListsNV = &S_glCreateCommandListsNV; ++count; }
        if (!glad_glDeleteCommandListsNV) { glad_glDeleteCommandListsNV = &S_glDeleteCommandListsNV; ++count; }
        if (!glad_glIsCommandListNV) { glad_glIsCommandListNV = &S_glIsCommandListNV; ++count; }
        if (!glad_glListDrawCommandsStatesClientNV) { glad_glListDrawCommandsStatesClientNV = &S_glListDrawCommandsStatesClientNV; ++count; }
        if (!glad_glCommandListSegmentsNV) { glad_glCommandListSegmentsNV = &S_glCommandListSegmentsNV; ++count; }
        if (!glad_glCompileCommandListNV) { glad_glCompileCommandListNV = &S_glCompileCommandListNV; ++count; }
        if (!glad_glCallCommandListNV) { glad_glCallCommandListNV = &S_glCallCommandListNV; ++count; }
        if (!glad_glBeginConditionalRenderNV) { glad_glBeginConditionalRenderNV = &S_glBeginConditionalRenderNV; ++count; }
        if (!glad_glEndConditionalRenderNV) { glad_glEndConditionalRenderNV = &S_glEndConditionalRenderNV; ++count; }
        if (!glad_glSubpixelPrecisionBiasNV) { glad_glSubpixelPrecisionBiasNV = &S_glSubpixelPrecisionBiasNV; ++count; }
        if (!glad_glConservativeRasterParameterfNV) { glad_glConservativeRasterParameterfNV = &S_glConservativeRasterParameterfNV; ++count; }
        if (!glad_glConservativeRasterParameteriNV) { glad_glConservativeRasterParameteriNV = &S_glConservativeRasterParameteriNV; ++count; }
        if (!glad_glCopyImageSubDataNV) { glad_glCopyImageSubDataNV = &S_glCopyImageSubDataNV; ++count; }
        if (!glad_glDepthRangedNV) { glad_glDepthRangedNV = &S_glDepthRangedNV; ++count; }
        if (!glad_glClearDepthdNV) { glad_glClearDepthdNV = &S_glClearDepthdNV; ++count; }
        if (!glad_glDepthBoundsdNV) { glad_glDepthBoundsdNV = &S_glDepthBoundsdNV; ++count; }
        if (!glad_glDrawTextureNV) { glad_glDrawTextureNV = &S_glDrawTextureNV; ++count; }
        if (!glad_glDrawVkImageNV) { glad_glDrawVkImageNV = &S_glDrawVkImageNV; ++count; }
        if (!glad_glGetVkProcAddrNV) { glad_glGetVkProcAddrNV = &S_glGetVkProcAddrNV; ++count; }
        if (!glad_glWaitVkSemaphoreNV) { glad_glWaitVkSemaphoreNV = &S_glWaitVkSemaphoreNV; ++count; }
        if (!glad_glSignalVkSemaphoreNV) { glad_glSignalVkSemaphoreNV = &S_glSignalVkSemaphoreNV; ++count; }
        if (!glad_glSignalVkFenceNV) { glad_glSignalVkFenceNV = &S_glSignalVkFenceNV; ++count; }
        if (!glad_glMapControlPointsNV) { glad_glMapControlPointsNV = &S_glMapControlPointsNV; ++count; }
        if (!glad_glMapParameterivNV) { glad_glMapParameterivNV = &S_glMapParameterivNV; ++count; }
        if (!glad_glMapParameterfvNV) { glad_glMapParameterfvNV = &S_glMapParameterfvNV; ++count; }
        if (!glad_glGetMapControlPointsNV) { glad_glGetMapControlPointsNV = &S_glGetMapControlPointsNV; ++count; }
        if (!glad_glGetMapParameterivNV) { glad_glGetMapParameterivNV = &S_glGetMapParameterivNV; ++count; }
        if (!glad_glGetMapParameterfvNV) { glad_glGetMapParameterfvNV = &S_glGetMapParameterfvNV; ++count; }
        if (!glad_glGetMapAttribParameterivNV) { glad_glGetMapAttribParameterivNV = &S_glGetMapAttribParameterivNV; ++count; }
        if (!glad_glGetMapAttribParameterfvNV) { glad_glGetMapAttribParameterfvNV = &S_glGetMapAttribParameterfvNV; ++count; }
        if (!glad_glEvalMapsNV) { glad_glEvalMapsNV = &S_glEvalMapsNV; ++count; }
        if (!glad_glGetMultisamplefvNV) { glad_glGetMultisamplefvNV = &S_glGetMultisamplefvNV; ++count; }
        if (!glad_glSampleMaskIndexedNV) { glad_glSampleMaskIndexedNV = &S_glSampleMaskIndexedNV; ++count; }
        if (!glad_glTexRenderbufferNV) { glad_glTexRenderbufferNV = &S_glTexRenderbufferNV; ++count; }
        if (!glad_glDeleteFencesNV) { glad_glDeleteFencesNV = &S_glDeleteFencesNV; ++count; }
        if (!glad_glGenFencesNV) { glad_glGenFencesNV = &S_glGenFencesNV; ++count; }
        if (!glad_glIsFenceNV) { glad_glIsFenceNV = &S_glIsFenceNV; ++count; }
        if (!glad_glTestFenceNV) { glad_glTestFenceNV = &S_glTestFenceNV; ++count; }
        if (!glad_glGetFenceivNV) { glad_glGetFenceivNV = &S_glGetFenceivNV; ++count; }
        if (!glad_glFinishFenceNV) { glad_glFinishFenceNV = &S_glFinishFenceNV; ++count; }
        if (!glad_glSetFenceNV) { glad_glSetFenceNV = &S_glSetFenceNV; ++count; }
        if (!glad_glFragmentCoverageColorNV) { glad_glFragmentCoverageColorNV = &S_glFragmentCoverageColorNV; ++count; }
        if (!glad_glProgramNamedParameter4fNV) { glad_glProgramNamedParameter4fNV = &S_glProgramNamedParameter4fNV; ++count; }
        if (!glad_glProgramNamedParameter4fvNV) { glad_glProgramNamedParameter4fvNV = &S_glProgramNamedParameter4fvNV; ++count; }
        if (!glad_glProgramNamedParameter4dNV) { glad_glProgramNamedParameter4dNV = &S_glProgramNamedParameter4dNV; ++count; }
        if (!glad_glProgramNamedParameter4dvNV) { glad_glProgramNamedParameter4dvNV = &S_glProgramNamedParameter4dvNV; ++count; }
        if (!glad_glGetProgramNamedParameterfvNV) { glad_glGetProgramNamedParameterfvNV = &S_glGetProgramNamedParameterfvNV; ++count; }
        if (!glad_glGetProgramNamedParameterdvNV) { glad_glGetProgramNamedParameterdvNV = &S_glGetProgramNamedParameterdvNV; ++count; }
        if (!glad_glCoverageModulationTableNV) { glad_glCoverageModulationTableNV = &S_glCoverageModulationTableNV; ++count; }
        if (!glad_glGetCoverageModulationTableNV) { glad_glGetCoverageModulationTableNV = &S_glGetCoverageModulationTableNV; ++count; }
        if (!glad_glCoverageModulationNV) { glad_glCoverageModulationNV = &S_glCoverageModulationNV; ++count; }
        if (!glad_glRenderbufferStorageMultisampleCoverageNV) { glad_glRenderbufferStorageMultisampleCoverageNV = &S_glRenderbufferStorageMultisampleCoverageNV; ++count; }
        if (!glad_glProgramVertexLimitNV) { glad_glProgramVertexLimitNV = &S_glProgramVertexLimitNV; ++count; }
        if (!glad_glFramebufferTextureEXT) { glad_glFramebufferTextureEXT = &S_glFramebufferTextureEXT; ++count; }
        if (!glad_glFramebufferTextureFaceEXT) { glad_glFramebufferTextureFaceEXT = &S_glFramebufferTextureFaceEXT; ++count; }
        if (!glad_glRenderGpuMaskNV) { glad_glRenderGpuMaskNV = &S_glRenderGpuMaskNV; ++count; }
        if (!glad_glMulticastBufferSubDataNV) { glad_glMulticastBufferSubDataNV = &S_glMulticastBufferSubDataNV; ++count; }
        if (!glad_glMulticastCopyBufferSubDataNV) { glad_glMulticastCopyBufferSubDataNV = &S_glMulticastCopyBufferSubDataNV; ++count; }
        if (!glad_glMulticastCopyImageSubDataNV) { glad_glMulticastCopyImageSubDataNV = &S_glMulticastCopyImageSubDataNV; ++count; }
        if (!glad_glMulticastBlitFramebufferNV) { glad_glMulticastBlitFramebufferNV = &S_glMulticastBlitFramebufferNV; ++count; }
        if (!glad_glMulticastFramebufferSampleLocationsfvNV) { glad_glMulticastFramebufferSampleLocationsfvNV = &S_glMulticastFramebufferSampleLocationsfvNV; ++count; }
        if (!glad_glMulticastBarrierNV) { glad_glMulticastBarrierNV = &S_glMulticastBarrierNV; ++count; }
        if (!glad_glMulticastWaitSyncNV) { glad_glMulticastWaitSyncNV = &S_glMulticastWaitSyncNV; ++count; }
        if (!glad_glMulticastGetQueryObjectivNV) { glad_glMulticastGetQueryObjectivNV = &S_glMulticastGetQueryObjectivNV; ++count; }
        if (!glad_glMulticastGetQueryObjectuivNV) { glad_glMulticastGetQueryObjectuivNV = &S_glMulticastGetQueryObjectuivNV; ++count; }
        if (!glad_glMulticastGetQueryObjecti64vNV) { glad_glMulticastGetQueryObjecti64vNV = &S_glMulticastGetQueryObjecti64vNV; ++count; }
        if (!glad_glMulticastGetQueryObjectui64vNV) { glad_glMulticastGetQueryObjectui64vNV = &S_glMulticastGetQueryObjectui64vNV; ++count; }
        if (!glad_glProgramLocalParameterI4iNV) { glad_glProgramLocalParameterI4iNV = &S_glProgramLocalParameterI4iNV; ++count; }
        if (!glad_glProgramLocalParameterI4ivNV) { glad_glProgramLocalParameterI4ivNV = &S_glProgramLocalParameterI4ivNV; ++count; }
        if (!glad_glProgramLocalParametersI4ivNV) { glad_glProgramLocalParametersI4ivNV = &S_glProgramLocalParametersI4ivNV; ++count; }
        if (!glad_glProgramLocalParameterI4uiNV) { glad_glProgramLocalParameterI4uiNV = &S_glProgramLocalParameterI4uiNV; ++count; }
        if (!glad_glProgramLocalParameterI4uivNV) { glad_glProgramLocalParameterI4uivNV = &S_glProgramLocalParameterI4uivNV; ++count; }
        if (!glad_glProgramLocalParametersI4uivNV) { glad_glProgramLocalParametersI4uivNV = &S_glProgramLocalParametersI4uivNV; ++count; }
        if (!glad_glProgramEnvParameterI4iNV) { glad_glProgramEnvParameterI4iNV = &S_glProgramEnvParameterI4iNV; ++count; }
        if (!glad_glProgramEnvParameterI4ivNV) { glad_glProgramEnvParameterI4ivNV = &S_glProgramEnvParameterI4ivNV; ++count; }
        if (!glad_glProgramEnvParametersI4ivNV) { glad_glProgramEnvParametersI4ivNV = &S_glProgramEnvParametersI4ivNV; ++count; }
        if (!glad_glProgramEnvParameterI4uiNV) { glad_glProgramEnvParameterI4uiNV = &S_glProgramEnvParameterI4uiNV; ++count; }
        if (!glad_glProgramEnvParameterI4uivNV) { glad_glProgramEnvParameterI4uivNV = &S_glProgramEnvParameterI4uivNV; ++count; }
        if (!glad_glProgramEnvParametersI4uivNV) { glad_glProgramEnvParametersI4uivNV = &S_glProgramEnvParametersI4uivNV; ++count; }
        if (!glad_glGetProgramLocalParameterIivNV) { glad_glGetProgramLocalParameterIivNV = &S_glGetProgramLocalParameterIivNV; ++count; }
        if (!glad_glGetProgramLocalParameterIuivNV) { glad_glGetProgramLocalParameterIuivNV = &S_glGetProgramLocalParameterIuivNV; ++count; }
        if (!glad_glGetProgramEnvParameterIivNV) { glad_glGetProgramEnvParameterIivNV = &S_glGetProgramEnvParameterIivNV; ++count; }
        if (!glad_glGetProgramEnvParameterIuivNV) { glad_glGetProgramEnvParameterIuivNV = &S_glGetProgramEnvParameterIuivNV; ++count; }
        if (!glad_glProgramSubroutineParametersuivNV) { glad_glProgramSubroutineParametersuivNV = &S_glProgramSubroutineParametersuivNV; ++count; }
        if (!glad_glGetProgramSubroutineParameteruivNV) { glad_glGetProgramSubroutineParameteruivNV = &S_glGetProgramSubroutineParameteruivNV; ++count; }
        if (!glad_glVertex2hNV) { glad_glVertex2hNV = &S_glVertex2hNV; ++count; }
        if (!glad_glVertex2hvNV) { glad_glVertex2hvNV = &S_glVertex2hvNV; ++count; }
        if (!glad_glVertex3hNV) { glad_glVertex3hNV = &S_glVertex3hNV; ++count; }
        if (!glad_glVertex3hvNV) { glad_glVertex3hvNV = &S_glVertex3hvNV; ++count; }
        if (!glad_glVertex4hNV) { glad_glVertex4hNV = &S_glVertex4hNV; ++count; }
        if (!glad_glVertex4hvNV) { glad_glVertex4hvNV = &S_glVertex4hvNV; ++count; }
        if (!glad_glNormal3hNV) { glad_glNormal3hNV = &S_glNormal3hNV; ++count; }
        if (!glad_glNormal3hvNV) { glad_glNormal3hvNV = &S_glNormal3hvNV; ++count; }
        if (!glad_glColor3hNV) { glad_glColor3hNV = &S_glColor3hNV; ++count; }
        if (!glad_glColor3hvNV) { glad_glColor3hvNV = &S_glColor3hvNV; ++count; }
        if (!glad_glColor4hNV) { glad_glColor4hNV = &S_glColor4hNV; ++count; }
        if (!glad_glColor4hvNV) { glad_glColor4hvNV = &S_glColor4hvNV; ++count; }
        if (!glad_glTexCoord1hNV) { glad_glTexCoord1hNV = &S_glTexCoord1hNV; ++count; }
        if (!glad_glTexCoord1hvNV) { glad_glTexCoord1hvNV = &S_glTexCoord1hvNV; ++count; }
        if (!glad_glTexCoord2hNV) { glad_glTexCoord2hNV = &S_glTexCoord2hNV; ++count; }
        if (!glad_glTexCoord2hvNV) { glad_glTexCoord2hvNV = &S_glTexCoord2hvNV; ++count; }
        if (!glad_glTexCoord3hNV) { glad_glTexCoord3hNV = &S_glTexCoord3hNV; ++count; }
        if (!glad_glTexCoord3hvNV) { glad_glTexCoord3hvNV = &S_glTexCoord3hvNV; ++count; }
        if (!glad_glTexCoord4hNV) { glad_glTexCoord4hNV = &S_glTexCoord4hNV; ++count; }
        if (!glad_glTexCoord4hvNV) { glad_glTexCoord4hvNV = &S_glTexCoord4hvNV; ++count; }
        if (!glad_glMultiTexCoord1hNV) { glad_glMultiTexCoord1hNV = &S_glMultiTexCoord1hNV; ++count; }
        if (!glad_glMultiTexCoord1hvNV) { glad_glMultiTexCoord1hvNV = &S_glMultiTexCoord1hvNV; ++count; }
        if (!glad_glMultiTexCoord2hNV) { glad_glMultiTexCoord2hNV = &S_glMultiTexCoord2hNV; ++count; }
        if (!glad_glMultiTexCoord2hvNV) { glad_glMultiTexCoord2hvNV = &S_glMultiTexCoord2hvNV; ++count; }
        if (!glad_glMultiTexCoord3hNV) { glad_glMultiTexCoord3hNV = &S_glMultiTexCoord3hNV; ++count; }
        if (!glad_glMultiTexCoord3hvNV) { glad_glMultiTexCoord3hvNV = &S_glMultiTexCoord3hvNV; ++count; }
        if (!glad_glMultiTexCoord4hNV) { glad_glMultiTexCoord4hNV = &S_glMultiTexCoord4hNV; ++count; }
        if (!glad_glMultiTexCoord4hvNV) { glad_glMultiTexCoord4hvNV = &S_glMultiTexCoord4hvNV; ++count; }
        if (!glad_glVertexAttrib1hNV) { glad_glVertexAttrib1hNV = &S_glVertexAttrib1hNV; ++count; }
        if (!glad_glVertexAttrib1hvNV) { glad_glVertexAttrib1hvNV = &S_glVertexAttrib1hvNV; ++count; }
        if (!glad_glVertexAttrib2hNV) { glad_glVertexAttrib2hNV = &S_glVertexAttrib2hNV; ++count; }
        if (!glad_glVertexAttrib2hvNV) { glad_glVertexAttrib2hvNV = &S_glVertexAttrib2hvNV; ++count; }
        if (!glad_glVertexAttrib3hNV) { glad_glVertexAttrib3hNV = &S_glVertexAttrib3hNV; ++count; }
        if (!glad_glVertexAttrib3hvNV) { glad_glVertexAttrib3hvNV = &S_glVertexAttrib3hvNV; ++count; }
        if (!glad_glVertexAttrib4hNV) { glad_glVertexAttrib4hNV = &S_glVertexAttrib4hNV; ++count; }
        if (!glad_glVertexAttrib4hvNV) { glad_glVertexAttrib4hvNV = &S_glVertexAttrib4hvNV; ++count; }
        if (!glad_glVertexAttribs1hvNV) { glad_glVertexAttribs1hvNV = &S_glVertexAttribs1hvNV; ++count; }
        if (!glad_glVertexAttribs2hvNV) { glad_glVertexAttribs2hvNV = &S_glVertexAttribs2hvNV; ++count; }
        if (!glad_glVertexAttribs3hvNV) { glad_glVertexAttribs3hvNV = &S_glVertexAttribs3hvNV; ++count; }
        if (!glad_glVertexAttribs4hvNV) { glad_glVertexAttribs4hvNV = &S_glVertexAttribs4hvNV; ++count; }
        if (!glad_glFogCoordhNV) { glad_glFogCoordhNV = &S_glFogCoordhNV; ++count; }
        if (!glad_glFogCoordhvNV) { glad_glFogCoordhvNV = &S_glFogCoordhvNV; ++count; }
        if (!glad_glSecondaryColor3hNV) { glad_glSecondaryColor3hNV = &S_glSecondaryColor3hNV; ++count; }
        if (!glad_glSecondaryColor3hvNV) { glad_glSecondaryColor3hvNV = &S_glSecondaryColor3hvNV; ++count; }
        if (!glad_glVertexWeighthNV) { glad_glVertexWeighthNV = &S_glVertexWeighthNV; ++count; }
        if (!glad_glVertexWeighthvNV) { glad_glVertexWeighthvNV = &S_glVertexWeighthvNV; ++count; }
        if (!glad_glGetInternalformatSampleivNV) { glad_glGetInternalformatSampleivNV = &S_glGetInternalformatSampleivNV; ++count; }
        if (!glad_glGetMemoryObjectDetachedResourcesuivNV) { glad_glGetMemoryObjectDetachedResourcesuivNV = &S_glGetMemoryObjectDetachedResourcesuivNV; ++count; }
        if (!glad_glResetMemoryObjectParameterNV) { glad_glResetMemoryObjectParameterNV = &S_glResetMemoryObjectParameterNV; ++count; }
        if (!glad_glTexAttachMemoryNV) { glad_glTexAttachMemoryNV = &S_glTexAttachMemoryNV; ++count; }
        if (!glad_glBufferAttachMemoryNV) { glad_glBufferAttachMemoryNV = &S_glBufferAttachMemoryNV; ++count; }
        if (!glad_glTextureAttachMemoryNV) { glad_glTextureAttachMemoryNV = &S_glTextureAttachMemoryNV; ++count; }
        if (!glad_glNamedBufferAttachMemoryNV) { glad_glNamedBufferAttachMemoryNV = &S_glNamedBufferAttachMemoryNV; ++count; }
        if (!glad_glBufferPageCommitmentMemNV) { glad_glBufferPageCommitmentMemNV = &S_glBufferPageCommitmentMemNV; ++count; }
        if (!glad_glTexPageCommitmentMemNV) { glad_glTexPageCommitmentMemNV = &S_glTexPageCommitmentMemNV; ++count; }
        if (!glad_glNamedBufferPageCommitmentMemNV) { glad_glNamedBufferPageCommitmentMemNV = &S_glNamedBufferPageCommitmentMemNV; ++count; }
        if (!glad_glTexturePageCommitmentMemNV) { glad_glTexturePageCommitmentMemNV = &S_glTexturePageCommitmentMemNV; ++count; }
        if (!glad_glDrawMeshTasksNV) { glad_glDrawMeshTasksNV = &S_glDrawMeshTasksNV; ++count; }
        if (!glad_glDrawMeshTasksIndirectNV) { glad_glDrawMeshTasksIndirectNV = &S_glDrawMeshTasksIndirectNV; ++count; }
        if (!glad_glMultiDrawMeshTasksIndirectNV) { glad_glMultiDrawMeshTasksIndirectNV = &S_glMultiDrawMeshTasksIndirectNV; ++count; }
        if (!glad_glMultiDrawMeshTasksIndirectCountNV) { glad_glMultiDrawMeshTasksIndirectCountNV = &S_glMultiDrawMeshTasksIndirectCountNV; ++count; }
        if (!glad_glGenOcclusionQueriesNV) { glad_glGenOcclusionQueriesNV = &S_glGenOcclusionQueriesNV; ++count; }
        if (!glad_glDeleteOcclusionQueriesNV) { glad_glDeleteOcclusionQueriesNV = &S_glDeleteOcclusionQueriesNV; ++count; }
        if (!glad_glIsOcclusionQueryNV) { glad_glIsOcclusionQueryNV = &S_glIsOcclusionQueryNV; ++count; }
        if (!glad_glBeginOcclusionQueryNV) { glad_glBeginOcclusionQueryNV = &S_glBeginOcclusionQueryNV; ++count; }
        if (!glad_glEndOcclusionQueryNV) { glad_glEndOcclusionQueryNV = &S_glEndOcclusionQueryNV; ++count; }
        if (!glad_glGetOcclusionQueryivNV) { glad_glGetOcclusionQueryivNV = &S_glGetOcclusionQueryivNV; ++count; }
        if (!glad_glGetOcclusionQueryuivNV) { glad_glGetOcclusionQueryuivNV = &S_glGetOcclusionQueryuivNV; ++count; }
        if (!glad_glProgramBufferParametersfvNV) { glad_glProgramBufferParametersfvNV = &S_glProgramBufferParametersfvNV; ++count; }
        if (!glad_glProgramBufferParametersIivNV) { glad_glProgramBufferParametersIivNV = &S_glProgramBufferParametersIivNV; ++count; }
        if (!glad_glProgramBufferParametersIuivNV) { glad_glProgramBufferParametersIuivNV = &S_glProgramBufferParametersIuivNV; ++count; }
        if (!glad_glGenPathsNV) { glad_glGenPathsNV = &S_glGenPathsNV; ++count; }
        if (!glad_glDeletePathsNV) { glad_glDeletePathsNV = &S_glDeletePathsNV; ++count; }
        if (!glad_glIsPathNV) { glad_glIsPathNV = &S_glIsPathNV; ++count; }
        if (!glad_glPathCommandsNV) { glad_glPathCommandsNV = &S_glPathCommandsNV; ++count; }
        if (!glad_glPathCoordsNV) { glad_glPathCoordsNV = &S_glPathCoordsNV; ++count; }
        if (!glad_glPathSubCommandsNV) { glad_glPathSubCommandsNV = &S_glPathSubCommandsNV; ++count; }
        if (!glad_glPathSubCoordsNV) { glad_glPathSubCoordsNV = &S_glPathSubCoordsNV; ++count; }
        if (!glad_glPathStringNV) { glad_glPathStringNV = &S_glPathStringNV; ++count; }
        if (!glad_glPathGlyphsNV) { glad_glPathGlyphsNV = &S_glPathGlyphsNV; ++count; }
        if (!glad_glPathGlyphRangeNV) { glad_glPathGlyphRangeNV = &S_glPathGlyphRangeNV; ++count; }
        if (!glad_glWeightPathsNV) { glad_glWeightPathsNV = &S_glWeightPathsNV; ++count; }
        if (!glad_glCopyPathNV) { glad_glCopyPathNV = &S_glCopyPathNV; ++count; }
        if (!glad_glInterpolatePathsNV) { glad_glInterpolatePathsNV = &S_glInterpolatePathsNV; ++count; }
        if (!glad_glTransformPathNV) { glad_glTransformPathNV = &S_glTransformPathNV; ++count; }
        if (!glad_glPathParameterivNV) { glad_glPathParameterivNV = &S_glPathParameterivNV; ++count; }
        if (!glad_glPathParameteriNV) { glad_glPathParameteriNV = &S_glPathParameteriNV; ++count; }
        if (!glad_glPathParameterfvNV) { glad_glPathParameterfvNV = &S_glPathParameterfvNV; ++count; }
        if (!glad_glPathParameterfNV) { glad_glPathParameterfNV = &S_glPathParameterfNV; ++count; }
        if (!glad_glPathDashArrayNV) { glad_glPathDashArrayNV = &S_glPathDashArrayNV; ++count; }
        if (!glad_glPathStencilFuncNV) { glad_glPathStencilFuncNV = &S_glPathStencilFuncNV; ++count; }
        if (!glad_glPathStencilDepthOffsetNV) { glad_glPathStencilDepthOffsetNV = &S_glPathStencilDepthOffsetNV; ++count; }
        if (!glad_glStencilFillPathNV) { glad_glStencilFillPathNV = &S_glStencilFillPathNV; ++count; }
        if (!glad_glStencilStrokePathNV) { glad_glStencilStrokePathNV = &S_glStencilStrokePathNV; ++count; }
        if (!glad_glStencilFillPathInstancedNV) { glad_glStencilFillPathInstancedNV = &S_glStencilFillPathInstancedNV; ++count; }
        if (!glad_glStencilStrokePathInstancedNV) { glad_glStencilStrokePathInstancedNV = &S_glStencilStrokePathInstancedNV; ++count; }
        if (!glad_glPathCoverDepthFuncNV) { glad_glPathCoverDepthFuncNV = &S_glPathCoverDepthFuncNV; ++count; }
        if (!glad_glCoverFillPathNV) { glad_glCoverFillPathNV = &S_glCoverFillPathNV; ++count; }
        if (!glad_glCoverStrokePathNV) { glad_glCoverStrokePathNV = &S_glCoverStrokePathNV; ++count; }
        if (!glad_glCoverFillPathInstancedNV) { glad_glCoverFillPathInstancedNV = &S_glCoverFillPathInstancedNV; ++count; }
        if (!glad_glCoverStrokePathInstancedNV) { glad_glCoverStrokePathInstancedNV = &S_glCoverStrokePathInstancedNV; ++count; }
        if (!glad_glGetPathParameterivNV) { glad_glGetPathParameterivNV = &S_glGetPathParameterivNV; ++count; }
        if (!glad_glGetPathParameterfvNV) { glad_glGetPathParameterfvNV = &S_glGetPathParameterfvNV; ++count; }
        if (!glad_glGetPathCommandsNV) { glad_glGetPathCommandsNV = &S_glGetPathCommandsNV; ++count; }
        if (!glad_glGetPathCoordsNV) { glad_glGetPathCoordsNV = &S_glGetPathCoordsNV; ++count; }
        if (!glad_glGetPathDashArrayNV) { glad_glGetPathDashArrayNV = &S_glGetPathDashArrayNV; ++count; }
        if (!glad_glGetPathMetricsNV) { glad_glGetPathMetricsNV = &S_glGetPathMetricsNV; ++count; }
        if (!glad_glGetPathMetricRangeNV) { glad_glGetPathMetricRangeNV = &S_glGetPathMetricRangeNV; ++count; }
        if (!glad_glGetPathSpacingNV) { glad_glGetPathSpacingNV = &S_glGetPathSpacingNV; ++count; }
        if (!glad_glIsPointInFillPathNV) { glad_glIsPointInFillPathNV = &S_glIsPointInFillPathNV; ++count; }
        if (!glad_glIsPointInStrokePathNV) { glad_glIsPointInStrokePathNV = &S_glIsPointInStrokePathNV; ++count; }
        if (!glad_glGetPathLengthNV) { glad_glGetPathLengthNV = &S_glGetPathLengthNV; ++count; }
        if (!glad_glPointAlongPathNV) { glad_glPointAlongPathNV = &S_glPointAlongPathNV; ++count; }
        if (!glad_glMatrixLoad3x2fNV) { glad_glMatrixLoad3x2fNV = &S_glMatrixLoad3x2fNV; ++count; }
        if (!glad_glMatrixLoad3x3fNV) { glad_glMatrixLoad3x3fNV = &S_glMatrixLoad3x3fNV; ++count; }
        if (!glad_glMatrixLoadTranspose3x3fNV) { glad_glMatrixLoadTranspose3x3fNV = &S_glMatrixLoadTranspose3x3fNV; ++count; }
        if (!glad_glMatrixMult3x2fNV) { glad_glMatrixMult3x2fNV = &S_glMatrixMult3x2fNV; ++count; }
        if (!glad_glMatrixMult3x3fNV) { glad_glMatrixMult3x3fNV = &S_glMatrixMult3x3fNV; ++count; }
        if (!glad_glMatrixMultTranspose3x3fNV) { glad_glMatrixMultTranspose3x3fNV = &S_glMatrixMultTranspose3x3fNV; ++count; }
        if (!glad_glStencilThenCoverFillPathNV) { glad_glStencilThenCoverFillPathNV = &S_glStencilThenCoverFillPathNV; ++count; }
        if (!glad_glStencilThenCoverStrokePathNV) { glad_glStencilThenCoverStrokePathNV = &S_glStencilThenCoverStrokePathNV; ++count; }
        if (!glad_glStencilThenCoverFillPathInstancedNV) { glad_glStencilThenCoverFillPathInstancedNV = &S_glStencilThenCoverFillPathInstancedNV; ++count; }
        if (!glad_glStencilThenCoverStrokePathInstancedNV) { glad_glStencilThenCoverStrokePathInstancedNV = &S_glStencilThenCoverStrokePathInstancedNV; ++count; }
        if (!glad_glPathGlyphIndexRangeNV) { glad_glPathGlyphIndexRangeNV = &S_glPathGlyphIndexRangeNV; ++count; }
        if (!glad_glPathGlyphIndexArrayNV) { glad_glPathGlyphIndexArrayNV = &S_glPathGlyphIndexArrayNV; ++count; }
        if (!glad_glPathMemoryGlyphIndexArrayNV) { glad_glPathMemoryGlyphIndexArrayNV = &S_glPathMemoryGlyphIndexArrayNV; ++count; }
        if (!glad_glProgramPathFragmentInputGenNV) { glad_glProgramPathFragmentInputGenNV = &S_glProgramPathFragmentInputGenNV; ++count; }
        if (!glad_glGetProgramResourcefvNV) { glad_glGetProgramResourcefvNV = &S_glGetProgramResourcefvNV; ++count; }
        if (!glad_glPathColorGenNV) { glad_glPathColorGenNV = &S_glPathColorGenNV; ++count; }
        if (!glad_glPathTexGenNV) { glad_glPathTexGenNV = &S_glPathTexGenNV; ++count; }
        if (!glad_glPathFogGenNV) { glad_glPathFogGenNV = &S_glPathFogGenNV; ++count; }
        if (!glad_glGetPathColorGenivNV) { glad_glGetPathColorGenivNV = &S_glGetPathColorGenivNV; ++count; }
        if (!glad_glGetPathColorGenfvNV) { glad_glGetPathColorGenfvNV = &S_glGetPathColorGenfvNV; ++count; }
        if (!glad_glGetPathTexGenivNV) { glad_glGetPathTexGenivNV = &S_glGetPathTexGenivNV; ++count; }
        if (!glad_glGetPathTexGenfvNV) { glad_glGetPathTexGenfvNV = &S_glGetPathTexGenfvNV; ++count; }
        if (!glad_glPixelDataRangeNV) { glad_glPixelDataRangeNV = &S_glPixelDataRangeNV; ++count; }
        if (!glad_glFlushPixelDataRangeNV) { glad_glFlushPixelDataRangeNV = &S_glFlushPixelDataRangeNV; ++count; }
        if (!glad_glPointParameteriNV) { glad_glPointParameteriNV = &S_glPointParameteriNV; ++count; }
        if (!glad_glPointParameterivNV) { glad_glPointParameterivNV = &S_glPointParameterivNV; ++count; }
        if (!glad_glPresentFrameKeyedNV) { glad_glPresentFrameKeyedNV = &S_glPresentFrameKeyedNV; ++count; }
        if (!glad_glPresentFrameDualFillNV) { glad_glPresentFrameDualFillNV = &S_glPresentFrameDualFillNV; ++count; }
        if (!glad_glGetVideoivNV) { glad_glGetVideoivNV = &S_glGetVideoivNV; ++count; }
        if (!glad_glGetVideouivNV) { glad_glGetVideouivNV = &S_glGetVideouivNV; ++count; }
        if (!glad_glGetVideoi64vNV) { glad_glGetVideoi64vNV = &S_glGetVideoi64vNV; ++count; }
        if (!glad_glGetVideoui64vNV) { glad_glGetVideoui64vNV = &S_glGetVideoui64vNV; ++count; }
        if (!glad_glPrimitiveRestartNV) { glad_glPrimitiveRestartNV = &S_glPrimitiveRestartNV; ++count; }
        if (!glad_glPrimitiveRestartIndexNV) { glad_glPrimitiveRestartIndexNV = &S_glPrimitiveRestartIndexNV; ++count; }
        if (!glad_glQueryResourceNV) { glad_glQueryResourceNV = &S_glQueryResourceNV; ++count; }
        if (!glad_glGenQueryResourceTagNV) { glad_glGenQueryResourceTagNV = &S_glGenQueryResourceTagNV; ++count; }
        if (!glad_glDeleteQueryResourceTagNV) { glad_glDeleteQueryResourceTagNV = &S_glDeleteQueryResourceTagNV; ++count; }
        if (!glad_glQueryResourceTagNV) { glad_glQueryResourceTagNV = &S_glQueryResourceTagNV; ++count; }
        if (!glad_glCombinerParameterfvNV) { glad_glCombinerParameterfvNV = &S_glCombinerParameterfvNV; ++count; }
        if (!glad_glCombinerParameterfNV) { glad_glCombinerParameterfNV = &S_glCombinerParameterfNV; ++count; }
        if (!glad_glCombinerParameterivNV) { glad_glCombinerParameterivNV = &S_glCombinerParameterivNV; ++count; }
        if (!glad_glCombinerParameteriNV) { glad_glCombinerParameteriNV = &S_glCombinerParameteriNV; ++count; }
        if (!glad_glCombinerInputNV) { glad_glCombinerInputNV = &S_glCombinerInputNV; ++count; }
        if (!glad_glCombinerOutputNV) { glad_glCombinerOutputNV = &S_glCombinerOutputNV; ++count; }
        if (!glad_glFinalCombinerInputNV) { glad_glFinalCombinerInputNV = &S_glFinalCombinerInputNV; ++count; }
        if (!glad_glGetCombinerInputParameterfvNV) { glad_glGetCombinerInputParameterfvNV = &S_glGetCombinerInputParameterfvNV; ++count; }
        if (!glad_glGetCombinerInputParameterivNV) { glad_glGetCombinerInputParameterivNV = &S_glGetCombinerInputParameterivNV; ++count; }
        if (!glad_glGetCombinerOutputParameterfvNV) { glad_glGetCombinerOutputParameterfvNV = &S_glGetCombinerOutputParameterfvNV; ++count; }
        if (!glad_glGetCombinerOutputParameterivNV) { glad_glGetCombinerOutputParameterivNV = &S_glGetCombinerOutputParameterivNV; ++count; }
        if (!glad_glGetFinalCombinerInputParameterfvNV) { glad_glGetFinalCombinerInputParameterfvNV = &S_glGetFinalCombinerInputParameterfvNV; ++count; }
        if (!glad_glGetFinalCombinerInputParameterivNV) { glad_glGetFinalCombinerInputParameterivNV = &S_glGetFinalCombinerInputParameterivNV; ++count; }
        if (!glad_glCombinerStageParameterfvNV) { glad_glCombinerStageParameterfvNV = &S_glCombinerStageParameterfvNV; ++count; }
        if (!glad_glGetCombinerStageParameterfvNV) { glad_glGetCombinerStageParameterfvNV = &S_glGetCombinerStageParameterfvNV; ++count; }
        if (!glad_glFramebufferSampleLocationsfvNV) { glad_glFramebufferSampleLocationsfvNV = &S_glFramebufferSampleLocationsfvNV; ++count; }
        if (!glad_glNamedFramebufferSampleLocationsfvNV) { glad_glNamedFramebufferSampleLocationsfvNV = &S_glNamedFramebufferSampleLocationsfvNV; ++count; }
        if (!glad_glResolveDepthValuesNV) { glad_glResolveDepthValuesNV = &S_glResolveDepthValuesNV; ++count; }
        if (!glad_glScissorExclusiveNV) { glad_glScissorExclusiveNV = &S_glScissorExclusiveNV; ++count; }
        if (!glad_glScissorExclusiveArrayvNV) { glad_glScissorExclusiveArrayvNV = &S_glScissorExclusiveArrayvNV; ++count; }
        if (!glad_glMakeBufferResidentNV) { glad_glMakeBufferResidentNV = &S_glMakeBufferResidentNV; ++count; }
        if (!glad_glMakeBufferNonResidentNV) { glad_glMakeBufferNonResidentNV = &S_glMakeBufferNonResidentNV; ++count; }
        if (!glad_glIsBufferResidentNV) { glad_glIsBufferResidentNV = &S_glIsBufferResidentNV; ++count; }
        if (!glad_glMakeNamedBufferResidentNV) { glad_glMakeNamedBufferResidentNV = &S_glMakeNamedBufferResidentNV; ++count; }
        if (!glad_glMakeNamedBufferNonResidentNV) { glad_glMakeNamedBufferNonResidentNV = &S_glMakeNamedBufferNonResidentNV; ++count; }
        if (!glad_glIsNamedBufferResidentNV) { glad_glIsNamedBufferResidentNV = &S_glIsNamedBufferResidentNV; ++count; }
        if (!glad_glGetBufferParameterui64vNV) { glad_glGetBufferParameterui64vNV = &S_glGetBufferParameterui64vNV; ++count; }
        if (!glad_glGetNamedBufferParameterui64vNV) { glad_glGetNamedBufferParameterui64vNV = &S_glGetNamedBufferParameterui64vNV; ++count; }
        if (!glad_glGetIntegerui64vNV) { glad_glGetIntegerui64vNV = &S_glGetIntegerui64vNV; ++count; }
        if (!glad_glUniformui64NV) { glad_glUniformui64NV = &S_glUniformui64NV; ++count; }
        if (!glad_glUniformui64vNV) { glad_glUniformui64vNV = &S_glUniformui64vNV; ++count; }
        if (!glad_glProgramUniformui64NV) { glad_glProgramUniformui64NV = &S_glProgramUniformui64NV; ++count; }
        if (!glad_glProgramUniformui64vNV) { glad_glProgramUniformui64vNV = &S_glProgramUniformui64vNV; ++count; }
        if (!glad_glBindShadingRateImageNV) { glad_glBindShadingRateImageNV = &S_glBindShadingRateImageNV; ++count; }
        if (!glad_glGetShadingRateImagePaletteNV) { glad_glGetShadingRateImagePaletteNV = &S_glGetShadingRateImagePaletteNV; ++count; }
        if (!glad_glGetShadingRateSampleLocationivNV) { glad_glGetShadingRateSampleLocationivNV = &S_glGetShadingRateSampleLocationivNV; ++count; }
        if (!glad_glShadingRateImageBarrierNV) { glad_glShadingRateImageBarrierNV = &S_glShadingRateImageBarrierNV; ++count; }
        if (!glad_glShadingRateImagePaletteNV) { glad_glShadingRateImagePaletteNV = &S_glShadingRateImagePaletteNV; ++count; }
        if (!glad_glShadingRateSampleOrderNV) { glad_glShadingRateSampleOrderNV = &S_glShadingRateSampleOrderNV; ++count; }
        if (!glad_glShadingRateSampleOrderCustomNV) { glad_glShadingRateSampleOrderCustomNV = &S_glShadingRateSampleOrderCustomNV; ++count; }
        if (!glad_glTextureBarrierNV) { glad_glTextureBarrierNV = &S_glTextureBarrierNV; ++count; }
        if (!glad_glTexImage2DMultisampleCoverageNV) { glad_glTexImage2DMultisampleCoverageNV = &S_glTexImage2DMultisampleCoverageNV; ++count; }
        if (!glad_glTexImage3DMultisampleCoverageNV) { glad_glTexImage3DMultisampleCoverageNV = &S_glTexImage3DMultisampleCoverageNV; ++count; }
        if (!glad_glTextureImage2DMultisampleNV) { glad_glTextureImage2DMultisampleNV = &S_glTextureImage2DMultisampleNV; ++count; }
        if (!glad_glTextureImage3DMultisampleNV) { glad_glTextureImage3DMultisampleNV = &S_glTextureImage3DMultisampleNV; ++count; }
        if (!glad_glTextureImage2DMultisampleCoverageNV) { glad_glTextureImage2DMultisampleCoverageNV = &S_glTextureImage2DMultisampleCoverageNV; ++count; }
        if (!glad_glTextureImage3DMultisampleCoverageNV) { glad_glTextureImage3DMultisampleCoverageNV = &S_glTextureImage3DMultisampleCoverageNV; ++count; }
        if (!glad_glCreateSemaphoresNV) { glad_glCreateSemaphoresNV = &S_glCreateSemaphoresNV; ++count; }
        if (!glad_glSemaphoreParameterivNV) { glad_glSemaphoreParameterivNV = &S_glSemaphoreParameterivNV; ++count; }
        if (!glad_glGetSemaphoreParameterivNV) { glad_glGetSemaphoreParameterivNV = &S_glGetSemaphoreParameterivNV; ++count; }
        if (!glad_glBeginTransformFeedbackNV) { glad_glBeginTransformFeedbackNV = &S_glBeginTransformFeedbackNV; ++count; }
        if (!glad_glEndTransformFeedbackNV) { glad_glEndTransformFeedbackNV = &S_glEndTransformFeedbackNV; ++count; }
        if (!glad_glTransformFeedbackAttribsNV) { glad_glTransformFeedbackAttribsNV = &S_glTransformFeedbackAttribsNV; ++count; }
        if (!glad_glBindBufferRangeNV) { glad_glBindBufferRangeNV = &S_glBindBufferRangeNV; ++count; }
        if (!glad_glBindBufferOffsetNV) { glad_glBindBufferOffsetNV = &S_glBindBufferOffsetNV; ++count; }
        if (!glad_glBindBufferBaseNV) { glad_glBindBufferBaseNV = &S_glBindBufferBaseNV; ++count; }
        if (!glad_glTransformFeedbackVaryingsNV) { glad_glTransformFeedbackVaryingsNV = &S_glTransformFeedbackVaryingsNV; ++count; }
        if (!glad_glActiveVaryingNV) { glad_glActiveVaryingNV = &S_glActiveVaryingNV; ++count; }
        if (!glad_glGetVaryingLocationNV) { glad_glGetVaryingLocationNV = &S_glGetVaryingLocationNV; ++count; }
        if (!glad_glGetActiveVaryingNV) { glad_glGetActiveVaryingNV = &S_glGetActiveVaryingNV; ++count; }
        if (!glad_glGetTransformFeedbackVaryingNV) { glad_glGetTransformFeedbackVaryingNV = &S_glGetTransformFeedbackVaryingNV; ++count; }
        if (!glad_glTransformFeedbackStreamAttribsNV) { glad_glTransformFeedbackStreamAttribsNV = &S_glTransformFeedbackStreamAttribsNV; ++count; }
        if (!glad_glBindTransformFeedbackNV) { glad_glBindTransformFeedbackNV = &S_glBindTransformFeedbackNV; ++count; }
        if (!glad_glDeleteTransformFeedbacksNV) { glad_glDeleteTransformFeedbacksNV = &S_glDeleteTransformFeedbacksNV; ++count; }
        if (!glad_glGenTransformFeedbacksNV) { glad_glGenTransformFeedbacksNV = &S_glGenTransformFeedbacksNV; ++count; }
        if (!glad_glIsTransformFeedbackNV) { glad_glIsTransformFeedbackNV = &S_glIsTransformFeedbackNV; ++count; }
        if (!glad_glPauseTransformFeedbackNV) { glad_glPauseTransformFeedbackNV = &S_glPauseTransformFeedbackNV; ++count; }
        if (!glad_glResumeTransformFeedbackNV) { glad_glResumeTransformFeedbackNV = &S_glResumeTransformFeedbackNV; ++count; }
        if (!glad_glDrawTransformFeedbackNV) { glad_glDrawTransformFeedbackNV = &S_glDrawTransformFeedbackNV; ++count; }
        if (!glad_glVDPAUInitNV) { glad_glVDPAUInitNV = &S_glVDPAUInitNV; ++count; }
        if (!glad_glVDPAUFiniNV) { glad_glVDPAUFiniNV = &S_glVDPAUFiniNV; ++count; }
        if (!glad_glVDPAURegisterVideoSurfaceNV) { glad_glVDPAURegisterVideoSurfaceNV = &S_glVDPAURegisterVideoSurfaceNV; ++count; }
        if (!glad_glVDPAURegisterOutputSurfaceNV) { glad_glVDPAURegisterOutputSurfaceNV = &S_glVDPAURegisterOutputSurfaceNV; ++count; }
        if (!glad_glVDPAUIsSurfaceNV) { glad_glVDPAUIsSurfaceNV = &S_glVDPAUIsSurfaceNV; ++count; }
        if (!glad_glVDPAUUnregisterSurfaceNV) { glad_glVDPAUUnregisterSurfaceNV = &S_glVDPAUUnregisterSurfaceNV; ++count; }
        if (!glad_glVDPAUGetSurfaceivNV) { glad_glVDPAUGetSurfaceivNV = &S_glVDPAUGetSurfaceivNV; ++count; }
        if (!glad_glVDPAUSurfaceAccessNV) { glad_glVDPAUSurfaceAccessNV = &S_glVDPAUSurfaceAccessNV; ++count; }
        if (!glad_glVDPAUMapSurfacesNV) { glad_glVDPAUMapSurfacesNV = &S_glVDPAUMapSurfacesNV; ++count; }
        if (!glad_glVDPAUUnmapSurfacesNV) { glad_glVDPAUUnmapSurfacesNV = &S_glVDPAUUnmapSurfacesNV; ++count; }
        if (!glad_glVDPAURegisterVideoSurfaceWithPictureStructureNV) { glad_glVDPAURegisterVideoSurfaceWithPictureStructureNV = &S_glVDPAURegisterVideoSurfaceWithPictureStructureNV; ++count; }
        if (!glad_glFlushVertexArrayRangeNV) { glad_glFlushVertexArrayRangeNV = &S_glFlushVertexArrayRangeNV; ++count; }
        if (!glad_glVertexArrayRangeNV) { glad_glVertexArrayRangeNV = &S_glVertexArrayRangeNV; ++count; }
        if (!glad_glVertexAttribL1i64NV) { glad_glVertexAttribL1i64NV = &S_glVertexAttribL1i64NV; ++count; }
        if (!glad_glVertexAttribL2i64NV) { glad_glVertexAttribL2i64NV = &S_glVertexAttribL2i64NV; ++count; }
        if (!glad_glVertexAttribL3i64NV) { glad_glVertexAttribL3i64NV = &S_glVertexAttribL3i64NV; ++count; }
        if (!glad_glVertexAttribL4i64NV) { glad_glVertexAttribL4i64NV = &S_glVertexAttribL4i64NV; ++count; }
        if (!glad_glVertexAttribL1i64vNV) { glad_glVertexAttribL1i64vNV = &S_glVertexAttribL1i64vNV; ++count; }
        if (!glad_glVertexAttribL2i64vNV) { glad_glVertexAttribL2i64vNV = &S_glVertexAttribL2i64vNV; ++count; }
        if (!glad_glVertexAttribL3i64vNV) { glad_glVertexAttribL3i64vNV = &S_glVertexAttribL3i64vNV; ++count; }
        if (!glad_glVertexAttribL4i64vNV) { glad_glVertexAttribL4i64vNV = &S_glVertexAttribL4i64vNV; ++count; }
        if (!glad_glVertexAttribL1ui64NV) { glad_glVertexAttribL1ui64NV = &S_glVertexAttribL1ui64NV; ++count; }
        if (!glad_glVertexAttribL2ui64NV) { glad_glVertexAttribL2ui64NV = &S_glVertexAttribL2ui64NV; ++count; }
        if (!glad_glVertexAttribL3ui64NV) { glad_glVertexAttribL3ui64NV = &S_glVertexAttribL3ui64NV; ++count; }
        if (!glad_glVertexAttribL4ui64NV) { glad_glVertexAttribL4ui64NV = &S_glVertexAttribL4ui64NV; ++count; }
        if (!glad_glVertexAttribL1ui64vNV) { glad_glVertexAttribL1ui64vNV = &S_glVertexAttribL1ui64vNV; ++count; }
        if (!glad_glVertexAttribL2ui64vNV) { glad_glVertexAttribL2ui64vNV = &S_glVertexAttribL2ui64vNV; ++count; }
        if (!glad_glVertexAttribL3ui64vNV) { glad_glVertexAttribL3ui64vNV = &S_glVertexAttribL3ui64vNV; ++count; }
        if (!glad_glVertexAttribL4ui64vNV) { glad_glVertexAttribL4ui64vNV = &S_glVertexAttribL4ui64vNV; ++count; }
        if (!glad_glGetVertexAttribLi64vNV) { glad_glGetVertexAttribLi64vNV = &S_glGetVertexAttribLi64vNV; ++count; }
        if (!glad_glGetVertexAttribLui64vNV) { glad_glGetVertexAttribLui64vNV = &S_glGetVertexAttribLui64vNV; ++count; }
        if (!glad_glVertexAttribLFormatNV) { glad_glVertexAttribLFormatNV = &S_glVertexAttribLFormatNV; ++count; }
        if (!glad_glBufferAddressRangeNV) { glad_glBufferAddressRangeNV = &S_glBufferAddressRangeNV; ++count; }
        if (!glad_glVertexFormatNV) { glad_glVertexFormatNV = &S_glVertexFormatNV; ++count; }
        if (!glad_glNormalFormatNV) { glad_glNormalFormatNV = &S_glNormalFormatNV; ++count; }
        if (!glad_glColorFormatNV) { glad_glColorFormatNV = &S_glColorFormatNV; ++count; }
        if (!glad_glIndexFormatNV) { glad_glIndexFormatNV = &S_glIndexFormatNV; ++count; }
        if (!glad_glTexCoordFormatNV) { glad_glTexCoordFormatNV = &S_glTexCoordFormatNV; ++count; }
        if (!glad_glEdgeFlagFormatNV) { glad_glEdgeFlagFormatNV = &S_glEdgeFlagFormatNV; ++count; }
        if (!glad_glSecondaryColorFormatNV) { glad_glSecondaryColorFormatNV = &S_glSecondaryColorFormatNV; ++count; }
        if (!glad_glFogCoordFormatNV) { glad_glFogCoordFormatNV = &S_glFogCoordFormatNV; ++count; }
        if (!glad_glVertexAttribFormatNV) { glad_glVertexAttribFormatNV = &S_glVertexAttribFormatNV; ++count; }
        if (!glad_glVertexAttribIFormatNV) { glad_glVertexAttribIFormatNV = &S_glVertexAttribIFormatNV; ++count; }
        if (!glad_glGetIntegerui64i_vNV) { glad_glGetIntegerui64i_vNV = &S_glGetIntegerui64i_vNV; ++count; }
        if (!glad_glAreProgramsResidentNV) { glad_glAreProgramsResidentNV = &S_glAreProgramsResidentNV; ++count; }
        if (!glad_glBindProgramNV) { glad_glBindProgramNV = &S_glBindProgramNV; ++count; }
        if (!glad_glDeleteProgramsNV) { glad_glDeleteProgramsNV = &S_glDeleteProgramsNV; ++count; }
        if (!glad_glExecuteProgramNV) { glad_glExecuteProgramNV = &S_glExecuteProgramNV; ++count; }
        if (!glad_glGenProgramsNV) { glad_glGenProgramsNV = &S_glGenProgramsNV; ++count; }
        if (!glad_glGetProgramParameterdvNV) { glad_glGetProgramParameterdvNV = &S_glGetProgramParameterdvNV; ++count; }
        if (!glad_glGetProgramParameterfvNV) { glad_glGetProgramParameterfvNV = &S_glGetProgramParameterfvNV; ++count; }
        if (!glad_glGetProgramivNV) { glad_glGetProgramivNV = &S_glGetProgramivNV; ++count; }
        if (!glad_glGetProgramStringNV) { glad_glGetProgramStringNV = &S_glGetProgramStringNV; ++count; }
        if (!glad_glGetTrackMatrixivNV) { glad_glGetTrackMatrixivNV = &S_glGetTrackMatrixivNV; ++count; }
        if (!glad_glGetVertexAttribdvNV) { glad_glGetVertexAttribdvNV = &S_glGetVertexAttribdvNV; ++count; }
        if (!glad_glGetVertexAttribfvNV) { glad_glGetVertexAttribfvNV = &S_glGetVertexAttribfvNV; ++count; }
        if (!glad_glGetVertexAttribivNV) { glad_glGetVertexAttribivNV = &S_glGetVertexAttribivNV; ++count; }
        if (!glad_glGetVertexAttribPointervNV) { glad_glGetVertexAttribPointervNV = &S_glGetVertexAttribPointervNV; ++count; }
        if (!glad_glIsProgramNV) { glad_glIsProgramNV = &S_glIsProgramNV; ++count; }
        if (!glad_glLoadProgramNV) { glad_glLoadProgramNV = &S_glLoadProgramNV; ++count; }
        if (!glad_glProgramParameter4dNV) { glad_glProgramParameter4dNV = &S_glProgramParameter4dNV; ++count; }
        if (!glad_glProgramParameter4dvNV) { glad_glProgramParameter4dvNV = &S_glProgramParameter4dvNV; ++count; }
        if (!glad_glProgramParameter4fNV) { glad_glProgramParameter4fNV = &S_glProgramParameter4fNV; ++count; }
        if (!glad_glProgramParameter4fvNV) { glad_glProgramParameter4fvNV = &S_glProgramParameter4fvNV; ++count; }
        if (!glad_glProgramParameters4dvNV) { glad_glProgramParameters4dvNV = &S_glProgramParameters4dvNV; ++count; }
        if (!glad_glProgramParameters4fvNV) { glad_glProgramParameters4fvNV = &S_glProgramParameters4fvNV; ++count; }
        if (!glad_glRequestResidentProgramsNV) { glad_glRequestResidentProgramsNV = &S_glRequestResidentProgramsNV; ++count; }
        if (!glad_glTrackMatrixNV) { glad_glTrackMatrixNV = &S_glTrackMatrixNV; ++count; }
        if (!glad_glVertexAttribPointerNV) { glad_glVertexAttribPointerNV = &S_glVertexAttribPointerNV; ++count; }
        if (!glad_glVertexAttrib1dNV) { glad_glVertexAttrib1dNV = &S_glVertexAttrib1dNV; ++count; }
        if (!glad_glVertexAttrib1dvNV) { glad_glVertexAttrib1dvNV = &S_glVertexAttrib1dvNV; ++count; }
        if (!glad_glVertexAttrib1fNV) { glad_glVertexAttrib1fNV = &S_glVertexAttrib1fNV; ++count; }
        if (!glad_glVertexAttrib1fvNV) { glad_glVertexAttrib1fvNV = &S_glVertexAttrib1fvNV; ++count; }
        if (!glad_glVertexAttrib1sNV) { glad_glVertexAttrib1sNV = &S_glVertexAttrib1sNV; ++count; }
        if (!glad_glVertexAttrib1svNV) { glad_glVertexAttrib1svNV = &S_glVertexAttrib1svNV; ++count; }
        if (!glad_glVertexAttrib2dNV) { glad_glVertexAttrib2dNV = &S_glVertexAttrib2dNV; ++count; }
        if (!glad_glVertexAttrib2dvNV) { glad_glVertexAttrib2dvNV = &S_glVertexAttrib2dvNV; ++count; }
        if (!glad_glVertexAttrib2fNV) { glad_glVertexAttrib2fNV = &S_glVertexAttrib2fNV; ++count; }
        if (!glad_glVertexAttrib2fvNV) { glad_glVertexAttrib2fvNV = &S_glVertexAttrib2fvNV; ++count; }
        if (!glad_glVertexAttrib2sNV) { glad_glVertexAttrib2sNV = &S_glVertexAttrib2sNV; ++count; }
        if (!glad_glVertexAttrib2svNV) { glad_glVertexAttrib2svNV = &S_glVertexAttrib2svNV; ++count; }
        if (!glad_glVertexAttrib3dNV) { glad_glVertexAttrib3dNV = &S_glVertexAttrib3dNV; ++count; }
        if (!glad_glVertexAttrib3dvNV) { glad_glVertexAttrib3dvNV = &S_glVertexAttrib3dvNV; ++count; }
        if (!glad_glVertexAttrib3fNV) { glad_glVertexAttrib3fNV = &S_glVertexAttrib3fNV; ++count; }
        if (!glad_glVertexAttrib3fvNV) { glad_glVertexAttrib3fvNV = &S_glVertexAttrib3fvNV; ++count; }
        if (!glad_glVertexAttrib3sNV) { glad_glVertexAttrib3sNV = &S_glVertexAttrib3sNV; ++count; }
        if (!glad_glVertexAttrib3svNV) { glad_glVertexAttrib3svNV = &S_glVertexAttrib3svNV; ++count; }
        if (!glad_glVertexAttrib4dNV) { glad_glVertexAttrib4dNV = &S_glVertexAttrib4dNV; ++count; }
        if (!glad_glVertexAttrib4dvNV) { glad_glVertexAttrib4dvNV = &S_glVertexAttrib4dvNV; ++count; }
        if (!glad_glVertexAttrib4fNV) { glad_glVertexAttrib4fNV = &S_glVertexAttrib4fNV; ++count; }
        if (!glad_glVertexAttrib4fvNV) { glad_glVertexAttrib4fvNV = &S_glVertexAttrib4fvNV; ++count; }
        if (!glad_glVertexAttrib4sNV) { glad_glVertexAttrib4sNV = &S_glVertexAttrib4sNV; ++count; }
        if (!glad_glVertexAttrib4svNV) { glad_glVertexAttrib4svNV = &S_glVertexAttrib4svNV; ++count; }
        if (!glad_glVertexAttrib4ubNV) { glad_glVertexAttrib4ubNV = &S_glVertexAttrib4ubNV; ++count; }
        if (!glad_glVertexAttrib4ubvNV) { glad_glVertexAttrib4ubvNV = &S_glVertexAttrib4ubvNV; ++count; }
        if (!glad_glVertexAttribs1dvNV) { glad_glVertexAttribs1dvNV = &S_glVertexAttribs1dvNV; ++count; }
        if (!glad_glVertexAttribs1fvNV) { glad_glVertexAttribs1fvNV = &S_glVertexAttribs1fvNV; ++count; }
        if (!glad_glVertexAttribs1svNV) { glad_glVertexAttribs1svNV = &S_glVertexAttribs1svNV; ++count; }
        if (!glad_glVertexAttribs2dvNV) { glad_glVertexAttribs2dvNV = &S_glVertexAttribs2dvNV; ++count; }
        if (!glad_glVertexAttribs2fvNV) { glad_glVertexAttribs2fvNV = &S_glVertexAttribs2fvNV; ++count; }
        if (!glad_glVertexAttribs2svNV) { glad_glVertexAttribs2svNV = &S_glVertexAttribs2svNV; ++count; }
        if (!glad_glVertexAttribs3dvNV) { glad_glVertexAttribs3dvNV = &S_glVertexAttribs3dvNV; ++count; }
        if (!glad_glVertexAttribs3fvNV) { glad_glVertexAttribs3fvNV = &S_glVertexAttribs3fvNV; ++count; }
        if (!glad_glVertexAttribs3svNV) { glad_glVertexAttribs3svNV = &S_glVertexAttribs3svNV; ++count; }
        if (!glad_glVertexAttribs4dvNV) { glad_glVertexAttribs4dvNV = &S_glVertexAttribs4dvNV; ++count; }
        if (!glad_glVertexAttribs4fvNV) { glad_glVertexAttribs4fvNV = &S_glVertexAttribs4fvNV; ++count; }
        if (!glad_glVertexAttribs4svNV) { glad_glVertexAttribs4svNV = &S_glVertexAttribs4svNV; ++count; }
        if (!glad_glVertexAttribs4ubvNV) { glad_glVertexAttribs4ubvNV = &S_glVertexAttribs4ubvNV; ++count; }
        if (!glad_glBeginVideoCaptureNV) { glad_glBeginVideoCaptureNV = &S_glBeginVideoCaptureNV; ++count; }
        if (!glad_glBindVideoCaptureStreamBufferNV) { glad_glBindVideoCaptureStreamBufferNV = &S_glBindVideoCaptureStreamBufferNV; ++count; }
        if (!glad_glBindVideoCaptureStreamTextureNV) { glad_glBindVideoCaptureStreamTextureNV = &S_glBindVideoCaptureStreamTextureNV; ++count; }
        if (!glad_glEndVideoCaptureNV) { glad_glEndVideoCaptureNV = &S_glEndVideoCaptureNV; ++count; }
        if (!glad_glGetVideoCaptureivNV) { glad_glGetVideoCaptureivNV = &S_glGetVideoCaptureivNV; ++count; }
        if (!glad_glGetVideoCaptureStreamivNV) { glad_glGetVideoCaptureStreamivNV = &S_glGetVideoCaptureStreamivNV; ++count; }
        if (!glad_glGetVideoCaptureStreamfvNV) { glad_glGetVideoCaptureStreamfvNV = &S_glGetVideoCaptureStreamfvNV; ++count; }
        if (!glad_glGetVideoCaptureStreamdvNV) { glad_glGetVideoCaptureStreamdvNV = &S_glGetVideoCaptureStreamdvNV; ++count; }
        if (!glad_glVideoCaptureNV) { glad_glVideoCaptureNV = &S_glVideoCaptureNV; ++count; }
        if (!glad_glVideoCaptureStreamParameterivNV) { glad_glVideoCaptureStreamParameterivNV = &S_glVideoCaptureStreamParameterivNV; ++count; }
        if (!glad_glVideoCaptureStreamParameterfvNV) { glad_glVideoCaptureStreamParameterfvNV = &S_glVideoCaptureStreamParameterfvNV; ++count; }
        if (!glad_glVideoCaptureStreamParameterdvNV) { glad_glVideoCaptureStreamParameterdvNV = &S_glVideoCaptureStreamParameterdvNV; ++count; }
        if (!glad_glViewportSwizzleNV) { glad_glViewportSwizzleNV = &S_glViewportSwizzleNV; ++count; }
        if (!glad_glMultiTexCoord1bOES) { glad_glMultiTexCoord1bOES = &S_glMultiTexCoord1bOES; ++count; }
        if (!glad_glMultiTexCoord1bvOES) { glad_glMultiTexCoord1bvOES = &S_glMultiTexCoord1bvOES; ++count; }
        if (!glad_glMultiTexCoord2bOES) { glad_glMultiTexCoord2bOES = &S_glMultiTexCoord2bOES; ++count; }
        if (!glad_glMultiTexCoord2bvOES) { glad_glMultiTexCoord2bvOES = &S_glMultiTexCoord2bvOES; ++count; }
        if (!glad_glMultiTexCoord3bOES) { glad_glMultiTexCoord3bOES = &S_glMultiTexCoord3bOES; ++count; }
        if (!glad_glMultiTexCoord3bvOES) { glad_glMultiTexCoord3bvOES = &S_glMultiTexCoord3bvOES; ++count; }
        if (!glad_glMultiTexCoord4bOES) { glad_glMultiTexCoord4bOES = &S_glMultiTexCoord4bOES; ++count; }
        if (!glad_glMultiTexCoord4bvOES) { glad_glMultiTexCoord4bvOES = &S_glMultiTexCoord4bvOES; ++count; }
        if (!glad_glTexCoord1bOES) { glad_glTexCoord1bOES = &S_glTexCoord1bOES; ++count; }
        if (!glad_glTexCoord1bvOES) { glad_glTexCoord1bvOES = &S_glTexCoord1bvOES; ++count; }
        if (!glad_glTexCoord2bOES) { glad_glTexCoord2bOES = &S_glTexCoord2bOES; ++count; }
        if (!glad_glTexCoord2bvOES) { glad_glTexCoord2bvOES = &S_glTexCoord2bvOES; ++count; }
        if (!glad_glTexCoord3bOES) { glad_glTexCoord3bOES = &S_glTexCoord3bOES; ++count; }
        if (!glad_glTexCoord3bvOES) { glad_glTexCoord3bvOES = &S_glTexCoord3bvOES; ++count; }
        if (!glad_glTexCoord4bOES) { glad_glTexCoord4bOES = &S_glTexCoord4bOES; ++count; }
        if (!glad_glTexCoord4bvOES) { glad_glTexCoord4bvOES = &S_glTexCoord4bvOES; ++count; }
        if (!glad_glVertex2bOES) { glad_glVertex2bOES = &S_glVertex2bOES; ++count; }
        if (!glad_glVertex2bvOES) { glad_glVertex2bvOES = &S_glVertex2bvOES; ++count; }
        if (!glad_glVertex3bOES) { glad_glVertex3bOES = &S_glVertex3bOES; ++count; }
        if (!glad_glVertex3bvOES) { glad_glVertex3bvOES = &S_glVertex3bvOES; ++count; }
        if (!glad_glVertex4bOES) { glad_glVertex4bOES = &S_glVertex4bOES; ++count; }
        if (!glad_glVertex4bvOES) { glad_glVertex4bvOES = &S_glVertex4bvOES; ++count; }
        if (!glad_glAlphaFuncxOES) { glad_glAlphaFuncxOES = &S_glAlphaFuncxOES; ++count; }
        if (!glad_glClearColorxOES) { glad_glClearColorxOES = &S_glClearColorxOES; ++count; }
        if (!glad_glClearDepthxOES) { glad_glClearDepthxOES = &S_glClearDepthxOES; ++count; }
        if (!glad_glClipPlanexOES) { glad_glClipPlanexOES = &S_glClipPlanexOES; ++count; }
        if (!glad_glColor4xOES) { glad_glColor4xOES = &S_glColor4xOES; ++count; }
        if (!glad_glDepthRangexOES) { glad_glDepthRangexOES = &S_glDepthRangexOES; ++count; }
        if (!glad_glFogxOES) { glad_glFogxOES = &S_glFogxOES; ++count; }
        if (!glad_glFogxvOES) { glad_glFogxvOES = &S_glFogxvOES; ++count; }
        if (!glad_glFrustumxOES) { glad_glFrustumxOES = &S_glFrustumxOES; ++count; }
        if (!glad_glGetClipPlanexOES) { glad_glGetClipPlanexOES = &S_glGetClipPlanexOES; ++count; }
        if (!glad_glGetFixedvOES) { glad_glGetFixedvOES = &S_glGetFixedvOES; ++count; }
        if (!glad_glGetTexEnvxvOES) { glad_glGetTexEnvxvOES = &S_glGetTexEnvxvOES; ++count; }
        if (!glad_glGetTexParameterxvOES) { glad_glGetTexParameterxvOES = &S_glGetTexParameterxvOES; ++count; }
        if (!glad_glLightModelxOES) { glad_glLightModelxOES = &S_glLightModelxOES; ++count; }
        if (!glad_glLightModelxvOES) { glad_glLightModelxvOES = &S_glLightModelxvOES; ++count; }
        if (!glad_glLightxOES) { glad_glLightxOES = &S_glLightxOES; ++count; }
        if (!glad_glLightxvOES) { glad_glLightxvOES = &S_glLightxvOES; ++count; }
        if (!glad_glLineWidthxOES) { glad_glLineWidthxOES = &S_glLineWidthxOES; ++count; }
        if (!glad_glLoadMatrixxOES) { glad_glLoadMatrixxOES = &S_glLoadMatrixxOES; ++count; }
        if (!glad_glMaterialxOES) { glad_glMaterialxOES = &S_glMaterialxOES; ++count; }
        if (!glad_glMaterialxvOES) { glad_glMaterialxvOES = &S_glMaterialxvOES; ++count; }
        if (!glad_glMultMatrixxOES) { glad_glMultMatrixxOES = &S_glMultMatrixxOES; ++count; }
        if (!glad_glMultiTexCoord4xOES) { glad_glMultiTexCoord4xOES = &S_glMultiTexCoord4xOES; ++count; }
        if (!glad_glNormal3xOES) { glad_glNormal3xOES = &S_glNormal3xOES; ++count; }
        if (!glad_glOrthoxOES) { glad_glOrthoxOES = &S_glOrthoxOES; ++count; }
        if (!glad_glPointParameterxvOES) { glad_glPointParameterxvOES = &S_glPointParameterxvOES; ++count; }
        if (!glad_glPointSizexOES) { glad_glPointSizexOES = &S_glPointSizexOES; ++count; }
        if (!glad_glPolygonOffsetxOES) { glad_glPolygonOffsetxOES = &S_glPolygonOffsetxOES; ++count; }
        if (!glad_glRotatexOES) { glad_glRotatexOES = &S_glRotatexOES; ++count; }
        if (!glad_glScalexOES) { glad_glScalexOES = &S_glScalexOES; ++count; }
        if (!glad_glTexEnvxOES) { glad_glTexEnvxOES = &S_glTexEnvxOES; ++count; }
        if (!glad_glTexEnvxvOES) { glad_glTexEnvxvOES = &S_glTexEnvxvOES; ++count; }
        if (!glad_glTexParameterxOES) { glad_glTexParameterxOES = &S_glTexParameterxOES; ++count; }
        if (!glad_glTexParameterxvOES) { glad_glTexParameterxvOES = &S_glTexParameterxvOES; ++count; }
        if (!glad_glTranslatexOES) { glad_glTranslatexOES = &S_glTranslatexOES; ++count; }
        if (!glad_glGetLightxvOES) { glad_glGetLightxvOES = &S_glGetLightxvOES; ++count; }
        if (!glad_glGetMaterialxvOES) { glad_glGetMaterialxvOES = &S_glGetMaterialxvOES; ++count; }
        if (!glad_glPointParameterxOES) { glad_glPointParameterxOES = &S_glPointParameterxOES; ++count; }
        if (!glad_glSampleCoveragexOES) { glad_glSampleCoveragexOES = &S_glSampleCoveragexOES; ++count; }
        if (!glad_glAccumxOES) { glad_glAccumxOES = &S_glAccumxOES; ++count; }
        if (!glad_glBitmapxOES) { glad_glBitmapxOES = &S_glBitmapxOES; ++count; }
        if (!glad_glBlendColorxOES) { glad_glBlendColorxOES = &S_glBlendColorxOES; ++count; }
        if (!glad_glClearAccumxOES) { glad_glClearAccumxOES = &S_glClearAccumxOES; ++count; }
        if (!glad_glColor3xOES) { glad_glColor3xOES = &S_glColor3xOES; ++count; }
        if (!glad_glColor3xvOES) { glad_glColor3xvOES = &S_glColor3xvOES; ++count; }
        if (!glad_glColor4xvOES) { glad_glColor4xvOES = &S_glColor4xvOES; ++count; }
        if (!glad_glConvolutionParameterxOES) { glad_glConvolutionParameterxOES = &S_glConvolutionParameterxOES; ++count; }
        if (!glad_glConvolutionParameterxvOES) { glad_glConvolutionParameterxvOES = &S_glConvolutionParameterxvOES; ++count; }
        if (!glad_glEvalCoord1xOES) { glad_glEvalCoord1xOES = &S_glEvalCoord1xOES; ++count; }
        if (!glad_glEvalCoord1xvOES) { glad_glEvalCoord1xvOES = &S_glEvalCoord1xvOES; ++count; }
        if (!glad_glEvalCoord2xOES) { glad_glEvalCoord2xOES = &S_glEvalCoord2xOES; ++count; }
        if (!glad_glEvalCoord2xvOES) { glad_glEvalCoord2xvOES = &S_glEvalCoord2xvOES; ++count; }
        if (!glad_glFeedbackBufferxOES) { glad_glFeedbackBufferxOES = &S_glFeedbackBufferxOES; ++count; }
        if (!glad_glGetConvolutionParameterxvOES) { glad_glGetConvolutionParameterxvOES = &S_glGetConvolutionParameterxvOES; ++count; }
        if (!glad_glGetHistogramParameterxvOES) { glad_glGetHistogramParameterxvOES = &S_glGetHistogramParameterxvOES; ++count; }
        if (!glad_glGetLightxOES) { glad_glGetLightxOES = &S_glGetLightxOES; ++count; }
        if (!glad_glGetMapxvOES) { glad_glGetMapxvOES = &S_glGetMapxvOES; ++count; }
        if (!glad_glGetMaterialxOES) { glad_glGetMaterialxOES = &S_glGetMaterialxOES; ++count; }
        if (!glad_glGetPixelMapxv) { glad_glGetPixelMapxv = &S_glGetPixelMapxv; ++count; }
        if (!glad_glGetTexGenxvOES) { glad_glGetTexGenxvOES = &S_glGetTexGenxvOES; ++count; }
        if (!glad_glGetTexLevelParameterxvOES) { glad_glGetTexLevelParameterxvOES = &S_glGetTexLevelParameterxvOES; ++count; }
        if (!glad_glIndexxOES) { glad_glIndexxOES = &S_glIndexxOES; ++count; }
        if (!glad_glIndexxvOES) { glad_glIndexxvOES = &S_glIndexxvOES; ++count; }
        if (!glad_glLoadTransposeMatrixxOES) { glad_glLoadTransposeMatrixxOES = &S_glLoadTransposeMatrixxOES; ++count; }
        if (!glad_glMap1xOES) { glad_glMap1xOES = &S_glMap1xOES; ++count; }
        if (!glad_glMap2xOES) { glad_glMap2xOES = &S_glMap2xOES; ++count; }
        if (!glad_glMapGrid1xOES) { glad_glMapGrid1xOES = &S_glMapGrid1xOES; ++count; }
        if (!glad_glMapGrid2xOES) { glad_glMapGrid2xOES = &S_glMapGrid2xOES; ++count; }
        if (!glad_glMultTransposeMatrixxOES) { glad_glMultTransposeMatrixxOES = &S_glMultTransposeMatrixxOES; ++count; }
        if (!glad_glMultiTexCoord1xOES) { glad_glMultiTexCoord1xOES = &S_glMultiTexCoord1xOES; ++count; }
        if (!glad_glMultiTexCoord1xvOES) { glad_glMultiTexCoord1xvOES = &S_glMultiTexCoord1xvOES; ++count; }
        if (!glad_glMultiTexCoord2xOES) { glad_glMultiTexCoord2xOES = &S_glMultiTexCoord2xOES; ++count; }
        if (!glad_glMultiTexCoord2xvOES) { glad_glMultiTexCoord2xvOES = &S_glMultiTexCoord2xvOES; ++count; }
        if (!glad_glMultiTexCoord3xOES) { glad_glMultiTexCoord3xOES = &S_glMultiTexCoord3xOES; ++count; }
        if (!glad_glMultiTexCoord3xvOES) { glad_glMultiTexCoord3xvOES = &S_glMultiTexCoord3xvOES; ++count; }
        if (!glad_glMultiTexCoord4xvOES) { glad_glMultiTexCoord4xvOES = &S_glMultiTexCoord4xvOES; ++count; }
        if (!glad_glNormal3xvOES) { glad_glNormal3xvOES = &S_glNormal3xvOES; ++count; }
        if (!glad_glPassThroughxOES) { glad_glPassThroughxOES = &S_glPassThroughxOES; ++count; }
        if (!glad_glPixelMapx) { glad_glPixelMapx = &S_glPixelMapx; ++count; }
        if (!glad_glPixelStorex) { glad_glPixelStorex = &S_glPixelStorex; ++count; }
        if (!glad_glPixelTransferxOES) { glad_glPixelTransferxOES = &S_glPixelTransferxOES; ++count; }
        if (!glad_glPixelZoomxOES) { glad_glPixelZoomxOES = &S_glPixelZoomxOES; ++count; }
        if (!glad_glPrioritizeTexturesxOES) { glad_glPrioritizeTexturesxOES = &S_glPrioritizeTexturesxOES; ++count; }
        if (!glad_glRasterPos2xOES) { glad_glRasterPos2xOES = &S_glRasterPos2xOES; ++count; }
        if (!glad_glRasterPos2xvOES) { glad_glRasterPos2xvOES = &S_glRasterPos2xvOES; ++count; }
        if (!glad_glRasterPos3xOES) { glad_glRasterPos3xOES = &S_glRasterPos3xOES; ++count; }
        if (!glad_glRasterPos3xvOES) { glad_glRasterPos3xvOES = &S_glRasterPos3xvOES; ++count; }
        if (!glad_glRasterPos4xOES) { glad_glRasterPos4xOES = &S_glRasterPos4xOES; ++count; }
        if (!glad_glRasterPos4xvOES) { glad_glRasterPos4xvOES = &S_glRasterPos4xvOES; ++count; }
        if (!glad_glRectxOES) { glad_glRectxOES = &S_glRectxOES; ++count; }
        if (!glad_glRectxvOES) { glad_glRectxvOES = &S_glRectxvOES; ++count; }
        if (!glad_glTexCoord1xOES) { glad_glTexCoord1xOES = &S_glTexCoord1xOES; ++count; }
        if (!glad_glTexCoord1xvOES) { glad_glTexCoord1xvOES = &S_glTexCoord1xvOES; ++count; }
        if (!glad_glTexCoord2xOES) { glad_glTexCoord2xOES = &S_glTexCoord2xOES; ++count; }
        if (!glad_glTexCoord2xvOES) { glad_glTexCoord2xvOES = &S_glTexCoord2xvOES; ++count; }
        if (!glad_glTexCoord3xOES) { glad_glTexCoord3xOES = &S_glTexCoord3xOES; ++count; }
        if (!glad_glTexCoord3xvOES) { glad_glTexCoord3xvOES = &S_glTexCoord3xvOES; ++count; }
        if (!glad_glTexCoord4xOES) { glad_glTexCoord4xOES = &S_glTexCoord4xOES; ++count; }
        if (!glad_glTexCoord4xvOES) { glad_glTexCoord4xvOES = &S_glTexCoord4xvOES; ++count; }
        if (!glad_glTexGenxOES) { glad_glTexGenxOES = &S_glTexGenxOES; ++count; }
        if (!glad_glTexGenxvOES) { glad_glTexGenxvOES = &S_glTexGenxvOES; ++count; }
        if (!glad_glVertex2xOES) { glad_glVertex2xOES = &S_glVertex2xOES; ++count; }
        if (!glad_glVertex2xvOES) { glad_glVertex2xvOES = &S_glVertex2xvOES; ++count; }
        if (!glad_glVertex3xOES) { glad_glVertex3xOES = &S_glVertex3xOES; ++count; }
        if (!glad_glVertex3xvOES) { glad_glVertex3xvOES = &S_glVertex3xvOES; ++count; }
        if (!glad_glVertex4xOES) { glad_glVertex4xOES = &S_glVertex4xOES; ++count; }
        if (!glad_glVertex4xvOES) { glad_glVertex4xvOES = &S_glVertex4xvOES; ++count; }
        if (!glad_glQueryMatrixxOES) { glad_glQueryMatrixxOES = &S_glQueryMatrixxOES; ++count; }
        if (!glad_glClearDepthfOES) { glad_glClearDepthfOES = &S_glClearDepthfOES; ++count; }
        if (!glad_glClipPlanefOES) { glad_glClipPlanefOES = &S_glClipPlanefOES; ++count; }
        if (!glad_glDepthRangefOES) { glad_glDepthRangefOES = &S_glDepthRangefOES; ++count; }
        if (!glad_glFrustumfOES) { glad_glFrustumfOES = &S_glFrustumfOES; ++count; }
        if (!glad_glGetClipPlanefOES) { glad_glGetClipPlanefOES = &S_glGetClipPlanefOES; ++count; }
        if (!glad_glOrthofOES) { glad_glOrthofOES = &S_glOrthofOES; ++count; }
        if (!glad_glFramebufferTextureMultiviewOVR) { glad_glFramebufferTextureMultiviewOVR = &S_glFramebufferTextureMultiviewOVR; ++count; }
        if (!glad_glNamedFramebufferTextureMultiviewOVR) { glad_glNamedFramebufferTextureMultiviewOVR = &S_glNamedFramebufferTextureMultiviewOVR; ++count; }
        if (!glad_glHintPGI) { glad_glHintPGI = &S_glHintPGI; ++count; }
        if (!glad_glDetailTexFuncSGIS) { glad_glDetailTexFuncSGIS = &S_glDetailTexFuncSGIS; ++count; }
        if (!glad_glGetDetailTexFuncSGIS) { glad_glGetDetailTexFuncSGIS = &S_glGetDetailTexFuncSGIS; ++count; }
        if (!glad_glFogFuncSGIS) { glad_glFogFuncSGIS = &S_glFogFuncSGIS; ++count; }
        if (!glad_glGetFogFuncSGIS) { glad_glGetFogFuncSGIS = &S_glGetFogFuncSGIS; ++count; }
        if (!glad_glSampleMaskSGIS) { glad_glSampleMaskSGIS = &S_glSampleMaskSGIS; ++count; }
        if (!glad_glSamplePatternSGIS) { glad_glSamplePatternSGIS = &S_glSamplePatternSGIS; ++count; }
        if (!glad_glPixelTexGenParameteriSGIS) { glad_glPixelTexGenParameteriSGIS = &S_glPixelTexGenParameteriSGIS; ++count; }
        if (!glad_glPixelTexGenParameterivSGIS) { glad_glPixelTexGenParameterivSGIS = &S_glPixelTexGenParameterivSGIS; ++count; }
        if (!glad_glPixelTexGenParameterfSGIS) { glad_glPixelTexGenParameterfSGIS = &S_glPixelTexGenParameterfSGIS; ++count; }
        if (!glad_glPixelTexGenParameterfvSGIS) { glad_glPixelTexGenParameterfvSGIS = &S_glPixelTexGenParameterfvSGIS; ++count; }
        if (!glad_glGetPixelTexGenParameterivSGIS) { glad_glGetPixelTexGenParameterivSGIS = &S_glGetPixelTexGenParameterivSGIS; ++count; }
        if (!glad_glGetPixelTexGenParameterfvSGIS) { glad_glGetPixelTexGenParameterfvSGIS = &S_glGetPixelTexGenParameterfvSGIS; ++count; }
        if (!glad_glPointParameterfSGIS) { glad_glPointParameterfSGIS = &S_glPointParameterfSGIS; ++count; }
        if (!glad_glPointParameterfvSGIS) { glad_glPointParameterfvSGIS = &S_glPointParameterfvSGIS; ++count; }
        if (!glad_glSharpenTexFuncSGIS) { glad_glSharpenTexFuncSGIS = &S_glSharpenTexFuncSGIS; ++count; }
        if (!glad_glGetSharpenTexFuncSGIS) { glad_glGetSharpenTexFuncSGIS = &S_glGetSharpenTexFuncSGIS; ++count; }
        if (!glad_glTexImage4DSGIS) { glad_glTexImage4DSGIS = &S_glTexImage4DSGIS; ++count; }
        if (!glad_glTexSubImage4DSGIS) { glad_glTexSubImage4DSGIS = &S_glTexSubImage4DSGIS; ++count; }
        if (!glad_glTextureColorMaskSGIS) { glad_glTextureColorMaskSGIS = &S_glTextureColorMaskSGIS; ++count; }
        if (!glad_glGetTexFilterFuncSGIS) { glad_glGetTexFilterFuncSGIS = &S_glGetTexFilterFuncSGIS; ++count; }
        if (!glad_glTexFilterFuncSGIS) { glad_glTexFilterFuncSGIS = &S_glTexFilterFuncSGIS; ++count; }
        if (!glad_glAsyncMarkerSGIX) { glad_glAsyncMarkerSGIX = &S_glAsyncMarkerSGIX; ++count; }
        if (!glad_glFinishAsyncSGIX) { glad_glFinishAsyncSGIX = &S_glFinishAsyncSGIX; ++count; }
        if (!glad_glPollAsyncSGIX) { glad_glPollAsyncSGIX = &S_glPollAsyncSGIX; ++count; }
        if (!glad_glGenAsyncMarkersSGIX) { glad_glGenAsyncMarkersSGIX = &S_glGenAsyncMarkersSGIX; ++count; }
        if (!glad_glDeleteAsyncMarkersSGIX) { glad_glDeleteAsyncMarkersSGIX = &S_glDeleteAsyncMarkersSGIX; ++count; }
        if (!glad_glIsAsyncMarkerSGIX) { glad_glIsAsyncMarkerSGIX = &S_glIsAsyncMarkerSGIX; ++count; }
        if (!glad_glFlushRasterSGIX) { glad_glFlushRasterSGIX = &S_glFlushRasterSGIX; ++count; }
        if (!glad_glFragmentColorMaterialSGIX) { glad_glFragmentColorMaterialSGIX = &S_glFragmentColorMaterialSGIX; ++count; }
        if (!glad_glFragmentLightfSGIX) { glad_glFragmentLightfSGIX = &S_glFragmentLightfSGIX; ++count; }
        if (!glad_glFragmentLightfvSGIX) { glad_glFragmentLightfvSGIX = &S_glFragmentLightfvSGIX; ++count; }
        if (!glad_glFragmentLightiSGIX) { glad_glFragmentLightiSGIX = &S_glFragmentLightiSGIX; ++count; }
        if (!glad_glFragmentLightivSGIX) { glad_glFragmentLightivSGIX = &S_glFragmentLightivSGIX; ++count; }
        if (!glad_glFragmentLightModelfSGIX) { glad_glFragmentLightModelfSGIX = &S_glFragmentLightModelfSGIX; ++count; }
        if (!glad_glFragmentLightModelfvSGIX) { glad_glFragmentLightModelfvSGIX = &S_glFragmentLightModelfvSGIX; ++count; }
        if (!glad_glFragmentLightModeliSGIX) { glad_glFragmentLightModeliSGIX = &S_glFragmentLightModeliSGIX; ++count; }
        if (!glad_glFragmentLightModelivSGIX) { glad_glFragmentLightModelivSGIX = &S_glFragmentLightModelivSGIX; ++count; }
        if (!glad_glFragmentMaterialfSGIX) { glad_glFragmentMaterialfSGIX = &S_glFragmentMaterialfSGIX; ++count; }
        if (!glad_glFragmentMaterialfvSGIX) { glad_glFragmentMaterialfvSGIX = &S_glFragmentMaterialfvSGIX; ++count; }
        if (!glad_glFragmentMaterialiSGIX) { glad_glFragmentMaterialiSGIX = &S_glFragmentMaterialiSGIX; ++count; }
        if (!glad_glFragmentMaterialivSGIX) { glad_glFragmentMaterialivSGIX = &S_glFragmentMaterialivSGIX; ++count; }
        if (!glad_glGetFragmentLightfvSGIX) { glad_glGetFragmentLightfvSGIX = &S_glGetFragmentLightfvSGIX; ++count; }
        if (!glad_glGetFragmentLightivSGIX) { glad_glGetFragmentLightivSGIX = &S_glGetFragmentLightivSGIX; ++count; }
        if (!glad_glGetFragmentMaterialfvSGIX) { glad_glGetFragmentMaterialfvSGIX = &S_glGetFragmentMaterialfvSGIX; ++count; }
        if (!glad_glGetFragmentMaterialivSGIX) { glad_glGetFragmentMaterialivSGIX = &S_glGetFragmentMaterialivSGIX; ++count; }
        if (!glad_glLightEnviSGIX) { glad_glLightEnviSGIX = &S_glLightEnviSGIX; ++count; }
        if (!glad_glFrameZoomSGIX) { glad_glFrameZoomSGIX = &S_glFrameZoomSGIX; ++count; }
        if (!glad_glIglooInterfaceSGIX) { glad_glIglooInterfaceSGIX = &S_glIglooInterfaceSGIX; ++count; }
        if (!glad_glGetInstrumentsSGIX) { glad_glGetInstrumentsSGIX = &S_glGetInstrumentsSGIX; ++count; }
        if (!glad_glInstrumentsBufferSGIX) { glad_glInstrumentsBufferSGIX = &S_glInstrumentsBufferSGIX; ++count; }
        if (!glad_glPollInstrumentsSGIX) { glad_glPollInstrumentsSGIX = &S_glPollInstrumentsSGIX; ++count; }
        if (!glad_glReadInstrumentsSGIX) { glad_glReadInstrumentsSGIX = &S_glReadInstrumentsSGIX; ++count; }
        if (!glad_glStartInstrumentsSGIX) { glad_glStartInstrumentsSGIX = &S_glStartInstrumentsSGIX; ++count; }
        if (!glad_glStopInstrumentsSGIX) { glad_glStopInstrumentsSGIX = &S_glStopInstrumentsSGIX; ++count; }
        if (!glad_glGetListParameterfvSGIX) { glad_glGetListParameterfvSGIX = &S_glGetListParameterfvSGIX; ++count; }
        if (!glad_glGetListParameterivSGIX) { glad_glGetListParameterivSGIX = &S_glGetListParameterivSGIX; ++count; }
        if (!glad_glListParameterfSGIX) { glad_glListParameterfSGIX = &S_glListParameterfSGIX; ++count; }
        if (!glad_glListParameterfvSGIX) { glad_glListParameterfvSGIX = &S_glListParameterfvSGIX; ++count; }
        if (!glad_glListParameteriSGIX) { glad_glListParameteriSGIX = &S_glListParameteriSGIX; ++count; }
        if (!glad_glListParameterivSGIX) { glad_glListParameterivSGIX = &S_glListParameterivSGIX; ++count; }
        if (!glad_glPixelTexGenSGIX) { glad_glPixelTexGenSGIX = &S_glPixelTexGenSGIX; ++count; }
        if (!glad_glDeformationMap3dSGIX) { glad_glDeformationMap3dSGIX = &S_glDeformationMap3dSGIX; ++count; }
        if (!glad_glDeformationMap3fSGIX) { glad_glDeformationMap3fSGIX = &S_glDeformationMap3fSGIX; ++count; }
        if (!glad_glDeformSGIX) { glad_glDeformSGIX = &S_glDeformSGIX; ++count; }
        if (!glad_glLoadIdentityDeformationMapSGIX) { glad_glLoadIdentityDeformationMapSGIX = &S_glLoadIdentityDeformationMapSGIX; ++count; }
        if (!glad_glReferencePlaneSGIX) { glad_glReferencePlaneSGIX = &S_glReferencePlaneSGIX; ++count; }
        if (!glad_glSpriteParameterfSGIX) { glad_glSpriteParameterfSGIX = &S_glSpriteParameterfSGIX; ++count; }
        if (!glad_glSpriteParameterfvSGIX) { glad_glSpriteParameterfvSGIX = &S_glSpriteParameterfvSGIX; ++count; }
        if (!glad_glSpriteParameteriSGIX) { glad_glSpriteParameteriSGIX = &S_glSpriteParameteriSGIX; ++count; }
        if (!glad_glSpriteParameterivSGIX) { glad_glSpriteParameterivSGIX = &S_glSpriteParameterivSGIX; ++count; }
        if (!glad_glTagSampleBufferSGIX) { glad_glTagSampleBufferSGIX = &S_glTagSampleBufferSGIX; ++count; }
        if (!glad_glColorTableSGI) { glad_glColorTableSGI = &S_glColorTableSGI; ++count; }
        if (!glad_glColorTableParameterfvSGI) { glad_glColorTableParameterfvSGI = &S_glColorTableParameterfvSGI; ++count; }
        if (!glad_glColorTableParameterivSGI) { glad_glColorTableParameterivSGI = &S_glColorTableParameterivSGI; ++count; }
        if (!glad_glCopyColorTableSGI) { glad_glCopyColorTableSGI = &S_glCopyColorTableSGI; ++count; }
        if (!glad_glGetColorTableSGI) { glad_glGetColorTableSGI = &S_glGetColorTableSGI; ++count; }
        if (!glad_glGetColorTableParameterfvSGI) { glad_glGetColorTableParameterfvSGI = &S_glGetColorTableParameterfvSGI; ++count; }
        if (!glad_glGetColorTableParameterivSGI) { glad_glGetColorTableParameterivSGI = &S_glGetColorTableParameterivSGI; ++count; }
        if (!glad_glFinishTextureSUNX) { glad_glFinishTextureSUNX = &S_glFinishTextureSUNX; ++count; }
        if (!glad_glGlobalAlphaFactorbSUN) { glad_glGlobalAlphaFactorbSUN = &S_glGlobalAlphaFactorbSUN; ++count; }
        if (!glad_glGlobalAlphaFactorsSUN) { glad_glGlobalAlphaFactorsSUN = &S_glGlobalAlphaFactorsSUN; ++count; }
        if (!glad_glGlobalAlphaFactoriSUN) { glad_glGlobalAlphaFactoriSUN = &S_glGlobalAlphaFactoriSUN; ++count; }
        if (!glad_glGlobalAlphaFactorfSUN) { glad_glGlobalAlphaFactorfSUN = &S_glGlobalAlphaFactorfSUN; ++count; }
        if (!glad_glGlobalAlphaFactordSUN) { glad_glGlobalAlphaFactordSUN = &S_glGlobalAlphaFactordSUN; ++count; }
        if (!glad_glGlobalAlphaFactorubSUN) { glad_glGlobalAlphaFactorubSUN = &S_glGlobalAlphaFactorubSUN; ++count; }
        if (!glad_glGlobalAlphaFactorusSUN) { glad_glGlobalAlphaFactorusSUN = &S_glGlobalAlphaFactorusSUN; ++count; }
        if (!glad_glGlobalAlphaFactoruiSUN) { glad_glGlobalAlphaFactoruiSUN = &S_glGlobalAlphaFactoruiSUN; ++count; }
        if (!glad_glDrawMeshArraysSUN) { glad_glDrawMeshArraysSUN = &S_glDrawMeshArraysSUN; ++count; }
        if (!glad_glReplacementCodeuiSUN) { glad_glReplacementCodeuiSUN = &S_glReplacementCodeuiSUN; ++count; }
        if (!glad_glReplacementCodeusSUN) { glad_glReplacementCodeusSUN = &S_glReplacementCodeusSUN; ++count; }
        if (!glad_glReplacementCodeubSUN) { glad_glReplacementCodeubSUN = &S_glReplacementCodeubSUN; ++count; }
        if (!glad_glReplacementCodeuivSUN) { glad_glReplacementCodeuivSUN = &S_glReplacementCodeuivSUN; ++count; }
        if (!glad_glReplacementCodeusvSUN) { glad_glReplacementCodeusvSUN = &S_glReplacementCodeusvSUN; ++count; }
        if (!glad_glReplacementCodeubvSUN) { glad_glReplacementCodeubvSUN = &S_glReplacementCodeubvSUN; ++count; }
        if (!glad_glReplacementCodePointerSUN) { glad_glReplacementCodePointerSUN = &S_glReplacementCodePointerSUN; ++count; }
        if (!glad_glColor4ubVertex2fSUN) { glad_glColor4ubVertex2fSUN = &S_glColor4ubVertex2fSUN; ++count; }
        if (!glad_glColor4ubVertex2fvSUN) { glad_glColor4ubVertex2fvSUN = &S_glColor4ubVertex2fvSUN; ++count; }
        if (!glad_glColor4ubVertex3fSUN) { glad_glColor4ubVertex3fSUN = &S_glColor4ubVertex3fSUN; ++count; }
        if (!glad_glColor4ubVertex3fvSUN) { glad_glColor4ubVertex3fvSUN = &S_glColor4ubVertex3fvSUN; ++count; }
        if (!glad_glColor3fVertex3fSUN) { glad_glColor3fVertex3fSUN = &S_glColor3fVertex3fSUN; ++count; }
        if (!glad_glColor3fVertex3fvSUN) { glad_glColor3fVertex3fvSUN = &S_glColor3fVertex3fvSUN; ++count; }
        if (!glad_glNormal3fVertex3fSUN) { glad_glNormal3fVertex3fSUN = &S_glNormal3fVertex3fSUN; ++count; }
        if (!glad_glNormal3fVertex3fvSUN) { glad_glNormal3fVertex3fvSUN = &S_glNormal3fVertex3fvSUN; ++count; }
        if (!glad_glColor4fNormal3fVertex3fSUN) { glad_glColor4fNormal3fVertex3fSUN = &S_glColor4fNormal3fVertex3fSUN; ++count; }
        if (!glad_glColor4fNormal3fVertex3fvSUN) { glad_glColor4fNormal3fVertex3fvSUN = &S_glColor4fNormal3fVertex3fvSUN; ++count; }
        if (!glad_glTexCoord2fVertex3fSUN) { glad_glTexCoord2fVertex3fSUN = &S_glTexCoord2fVertex3fSUN; ++count; }
        if (!glad_glTexCoord2fVertex3fvSUN) { glad_glTexCoord2fVertex3fvSUN = &S_glTexCoord2fVertex3fvSUN; ++count; }
        if (!glad_glTexCoord4fVertex4fSUN) { glad_glTexCoord4fVertex4fSUN = &S_glTexCoord4fVertex4fSUN; ++count; }
        if (!glad_glTexCoord4fVertex4fvSUN) { glad_glTexCoord4fVertex4fvSUN = &S_glTexCoord4fVertex4fvSUN; ++count; }
        if (!glad_glTexCoord2fColor4ubVertex3fSUN) { glad_glTexCoord2fColor4ubVertex3fSUN = &S_glTexCoord2fColor4ubVertex3fSUN; ++count; }
        if (!glad_glTexCoord2fColor4ubVertex3fvSUN) { glad_glTexCoord2fColor4ubVertex3fvSUN = &S_glTexCoord2fColor4ubVertex3fvSUN; ++count; }
        if (!glad_glTexCoord2fColor3fVertex3fSUN) { glad_glTexCoord2fColor3fVertex3fSUN = &S_glTexCoord2fColor3fVertex3fSUN; ++count; }
        if (!glad_glTexCoord2fColor3fVertex3fvSUN) { glad_glTexCoord2fColor3fVertex3fvSUN = &S_glTexCoord2fColor3fVertex3fvSUN; ++count; }
        if (!glad_glTexCoord2fNormal3fVertex3fSUN) { glad_glTexCoord2fNormal3fVertex3fSUN = &S_glTexCoord2fNormal3fVertex3fSUN; ++count; }
        if (!glad_glTexCoord2fNormal3fVertex3fvSUN) { glad_glTexCoord2fNormal3fVertex3fvSUN = &S_glTexCoord2fNormal3fVertex3fvSUN; ++count; }
        if (!glad_glTexCoord2fColor4fNormal3fVertex3fSUN) { glad_glTexCoord2fColor4fNormal3fVertex3fSUN = &S_glTexCoord2fColor4fNormal3fVertex3fSUN; ++count; }
        if (!glad_glTexCoord2fColor4fNormal3fVertex3fvSUN) { glad_glTexCoord2fColor4fNormal3fVertex3fvSUN = &S_glTexCoord2fColor4fNormal3fVertex3fvSUN; ++count; }
        if (!glad_glTexCoord4fColor4fNormal3fVertex4fSUN) { glad_glTexCoord4fColor4fNormal3fVertex4fSUN = &S_glTexCoord4fColor4fNormal3fVertex4fSUN; ++count; }
        if (!glad_glTexCoord4fColor4fNormal3fVertex4fvSUN) { glad_glTexCoord4fColor4fNormal3fVertex4fvSUN = &S_glTexCoord4fColor4fNormal3fVertex4fvSUN; ++count; }
        if (!glad_glReplacementCodeuiVertex3fSUN) { glad_glReplacementCodeuiVertex3fSUN = &S_glReplacementCodeuiVertex3fSUN; ++count; }
        if (!glad_glReplacementCodeuiVertex3fvSUN) { glad_glReplacementCodeuiVertex3fvSUN = &S_glReplacementCodeuiVertex3fvSUN; ++count; }
        if (!glad_glReplacementCodeuiColor4ubVertex3fSUN) { glad_glReplacementCodeuiColor4ubVertex3fSUN = &S_glReplacementCodeuiColor4ubVertex3fSUN; ++count; }
        if (!glad_glReplacementCodeuiColor4ubVertex3fvSUN) { glad_glReplacementCodeuiColor4ubVertex3fvSUN = &S_glReplacementCodeuiColor4ubVertex3fvSUN; ++count; }
        if (!glad_glReplacementCodeuiColor3fVertex3fSUN) { glad_glReplacementCodeuiColor3fVertex3fSUN = &S_glReplacementCodeuiColor3fVertex3fSUN; ++count; }
        if (!glad_glReplacementCodeuiColor3fVertex3fvSUN) { glad_glReplacementCodeuiColor3fVertex3fvSUN = &S_glReplacementCodeuiColor3fVertex3fvSUN; ++count; }
        if (!glad_glReplacementCodeuiNormal3fVertex3fSUN) { glad_glReplacementCodeuiNormal3fVertex3fSUN = &S_glReplacementCodeuiNormal3fVertex3fSUN; ++count; }
        if (!glad_glReplacementCodeuiNormal3fVertex3fvSUN) { glad_glReplacementCodeuiNormal3fVertex3fvSUN = &S_glReplacementCodeuiNormal3fVertex3fvSUN; ++count; }
        if (!glad_glReplacementCodeuiColor4fNormal3fVertex3fSUN) { glad_glReplacementCodeuiColor4fNormal3fVertex3fSUN = &S_glReplacementCodeuiColor4fNormal3fVertex3fSUN; ++count; }
        if (!glad_glReplacementCodeuiColor4fNormal3fVertex3fvSUN) { glad_glReplacementCodeuiColor4fNormal3fVertex3fvSUN = &S_glReplacementCodeuiColor4fNormal3fVertex3fvSUN; ++count; }
        if (!glad_glReplacementCodeuiTexCoord2fVertex3fSUN) { glad_glReplacementCodeuiTexCoord2fVertex3fSUN = &S_glReplacementCodeuiTexCoord2fVertex3fSUN; ++count; }
        if (!glad_glReplacementCodeuiTexCoord2fVertex3fvSUN) { glad_glReplacementCodeuiTexCoord2fVertex3fvSUN = &S_glReplacementCodeuiTexCoord2fVertex3fvSUN; ++count; }
        if (!glad_glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN) { glad_glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN = &S_glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN; ++count; }
        if (!glad_glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN) { glad_glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN = &S_glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN; ++count; }
        if (!glad_glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN) { glad_glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN = &S_glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN; ++count; }
        if (!glad_glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN) { glad_glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN = &S_glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN; ++count; }
        return count;
    }
}
#else
namespace Conqueror::GLProfile
{
    int InstallMissingStubs() { return 0; }
}
#endif
