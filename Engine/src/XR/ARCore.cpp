#include "ARCore.h"

#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"

#include <cstdlib>
#include <cstring>

#if defined(__ANDROID__)
    #include "Core/Application.h"
    #include "Platform/Android/AndroidPlatform.h"

    #include <glad/glad.h>
    #include <GLFW/glfw3.h>
    #include <android_native_app_glue.h>
    #include <dlfcn.h>
    #include <jni.h>

    #include <algorithm>
    #include <chrono>
    #include <cmath>
#endif

namespace Conqueror::ARCore
{
    namespace
    {
        std::string s_State = "Unavailable";
        std::string s_Error;
        std::vector<Plane> s_Planes;
    }

    bool WantedAtStartup()
    {
#if defined(__ANDROID__)
        if (const char* env = std::getenv("CQ_ARCORE"))
            return env[0] == '1';
        return Project::GetActive() && Project::GetActive()->GetConfig().XR.ARCore;
#else
        return false;
#endif
    }

    const std::string& GetTrackingState() { return s_State; }
    const std::string& GetError() { return s_Error; }
    const std::vector<Plane>& GetPlanes() { return s_Planes; }

#if !defined(__ANDROID__)
    bool Start() { s_State = "Unavailable"; s_Error = "ARCore runs on Android phones and tablets"; return false; }
    void Stop() {}
    void Update() {}
    bool IsRunning() { return false; }
    bool CameraPose(glm::vec3&, glm::quat&) { return false; }
    float CameraFovY() { return 0.0f; }
    bool Raycast(float, float, HitResult&) { return false; }
    uint32_t BackgroundTexture() { return 0; }
    uint64_t FramesUpdated() { return 0; }
#else
    namespace
    {
        // ------------------------------------------------------------ ARCore C API (birlikte çalışma için kendi bildirimlerimiz;
        // Google'ın başlığı depoda değil). Türler opak, enumlar int32_t (arcore_c_api.h AR_DEFINE_ENUM)
        struct ArSession; struct ArConfig; struct ArFrame; struct ArCamera; struct ArPose; struct ArTrackable; struct ArTrackableList;
        struct ArHitResult; struct ArHitResultList;
        using ArStatus = int32_t;
        constexpr int32_t AR_SUCCESS = 0, AR_ERROR_SESSION_PAUSED = -3, AR_ERROR_CAMERA_PERMISSION_NOT_GRANTED = -9;
        constexpr int32_t AR_AVAILABILITY_UNKNOWN_CHECKING = 1, AR_AVAILABILITY_SUPPORTED_NOT_INSTALLED = 201,
                          AR_AVAILABILITY_SUPPORTED_APK_TOO_OLD = 202, AR_AVAILABILITY_SUPPORTED_INSTALLED = 203;
        constexpr int32_t AR_INSTALL_STATUS_INSTALLED = 0;
        constexpr int32_t AR_PLANE_FINDING_MODE_HORIZONTAL_AND_VERTICAL = 3, AR_FOCUS_MODE_AUTO = 1, AR_LIGHT_ESTIMATION_MODE_DISABLED = 0;
        constexpr int32_t AR_TRACKING_STATE_TRACKING = 0, AR_TRACKING_STATE_PAUSED = 1;
        constexpr int32_t AR_TRACKABLE_PLANE = 0x41520101;
        constexpr int32_t AR_COORDINATES_2D_TEXTURE_NORMALIZED = 1, AR_COORDINATES_2D_OPENGL_NORMALIZED_DEVICE_COORDINATES = 6;
        constexpr GLenum kTextureExternalOES = 0x8D65;

        #define CQ_AR_FUNCTIONS(X) \
            X(void, ArCoreApk_checkAvailability, (void*, void*, int32_t*)) \
            X(ArStatus, ArCoreApk_requestInstall, (void*, void*, int32_t, int32_t*)) \
            X(ArStatus, ArSession_create, (void*, void*, ArSession**)) \
            X(void, ArSession_destroy, (ArSession*)) \
            X(void, ArConfig_create, (const ArSession*, ArConfig**)) \
            X(void, ArConfig_destroy, (ArConfig*)) \
            X(void, ArConfig_setPlaneFindingMode, (const ArSession*, ArConfig*, int32_t)) \
            X(void, ArConfig_setLightEstimationMode, (const ArSession*, ArConfig*, int32_t)) \
            X(void, ArConfig_setFocusMode, (const ArSession*, ArConfig*, int32_t)) \
            X(ArStatus, ArSession_configure, (ArSession*, const ArConfig*)) \
            X(ArStatus, ArSession_resume, (ArSession*)) \
            X(ArStatus, ArSession_pause, (ArSession*)) \
            X(void, ArSession_setCameraTextureName, (ArSession*, uint32_t)) \
            X(void, ArSession_setDisplayGeometry, (ArSession*, int32_t, int32_t, int32_t)) \
            X(ArStatus, ArSession_update, (ArSession*, ArFrame*)) \
            X(void, ArFrame_create, (const ArSession*, ArFrame**)) \
            X(void, ArFrame_destroy, (ArFrame*)) \
            X(void, ArFrame_acquireCamera, (const ArSession*, const ArFrame*, ArCamera**)) \
            X(void, ArCamera_release, (ArCamera*)) \
            X(void, ArCamera_getTrackingState, (const ArSession*, const ArCamera*, int32_t*)) \
            X(void, ArCamera_getDisplayOrientedPose, (const ArSession*, const ArCamera*, ArPose*)) \
            X(void, ArCamera_getProjectionMatrix, (const ArSession*, const ArCamera*, float, float, float*)) \
            X(void, ArFrame_transformCoordinates2d, (const ArSession*, const ArFrame*, int32_t, int32_t, const float*, int32_t, float*)) \
            X(void, ArTrackableList_create, (const ArSession*, ArTrackableList**)) \
            X(void, ArTrackableList_destroy, (ArTrackableList*)) \
            X(void, ArSession_getAllTrackables, (const ArSession*, int32_t, ArTrackableList*)) \
            X(void, ArTrackableList_getSize, (const ArSession*, const ArTrackableList*, int32_t*)) \
            X(void, ArTrackableList_acquireItem, (const ArSession*, const ArTrackableList*, int32_t, ArTrackable**)) \
            X(void, ArTrackable_release, (ArTrackable*)) \
            X(void, ArTrackable_getTrackingState, (const ArSession*, const ArTrackable*, int32_t*)) \
            X(void, ArTrackable_getType, (const ArSession*, const ArTrackable*, int32_t*)) \
            X(void, ArPlane_acquireSubsumedBy, (const ArSession*, const ArTrackable*, ArTrackable**)) \
            X(void, ArPlane_getType, (const ArSession*, const ArTrackable*, int32_t*)) \
            X(void, ArPlane_getCenterPose, (const ArSession*, const ArTrackable*, ArPose*)) \
            X(void, ArPlane_getExtentX, (const ArSession*, const ArTrackable*, float*)) \
            X(void, ArPlane_getExtentZ, (const ArSession*, const ArTrackable*, float*)) \
            X(void, ArPlane_getPolygonSize, (const ArSession*, const ArTrackable*, int32_t*)) \
            X(void, ArPlane_getPolygon, (const ArSession*, const ArTrackable*, float*)) \
            X(void, ArPlane_isPoseInPolygon, (const ArSession*, const ArTrackable*, const ArPose*, int32_t*)) \
            X(void, ArPose_create, (const ArSession*, const float*, ArPose**)) \
            X(void, ArPose_destroy, (ArPose*)) \
            X(void, ArPose_getPoseRaw, (const ArSession*, const ArPose*, float*)) \
            X(void, ArFrame_hitTest, (const ArSession*, const ArFrame*, float, float, ArHitResultList*)) \
            X(void, ArHitResultList_create, (const ArSession*, ArHitResultList**)) \
            X(void, ArHitResultList_destroy, (ArHitResultList*)) \
            X(void, ArHitResultList_getSize, (const ArSession*, const ArHitResultList*, int32_t*)) \
            X(void, ArHitResultList_getItem, (const ArSession*, const ArHitResultList*, int32_t, ArHitResult*)) \
            X(void, ArHitResult_create, (const ArSession*, ArHitResult**)) \
            X(void, ArHitResult_destroy, (ArHitResult*)) \
            X(void, ArHitResult_getHitPose, (const ArSession*, const ArHitResult*, ArPose*)) \
            X(void, ArHitResult_acquireTrackable, (const ArSession*, const ArHitResult*, ArTrackable**))
        struct Api
        {
            #define CQ_AR_MEMBER(ret, name, args) ret (*name) args = nullptr;
            CQ_AR_FUNCTIONS(CQ_AR_MEMBER)
            #undef CQ_AR_MEMBER
        } s_Api;
        void* s_Lib = nullptr;

        ArSession* s_Session = nullptr;
        ArFrame* s_Frame = nullptr;
        ArPose* s_Pose = nullptr;
        bool s_Running = false;
        bool s_Requested = false;            // Start çağrıldı (izin / kurulum bekleniyor olabilir)
        bool s_PermissionAsked = false, s_InstallAsked = false;
        std::chrono::steady_clock::time_point s_NextTry{};
        uint64_t s_Frames = 0;

        // Kamera: dış (OES) doku -> ekran oranında RGBA8 doku
        GLuint s_CameraTexture = 0, s_ColorTexture = 0, s_Fbo = 0, s_Program = 0, s_Vao = 0;
        GLint s_UVLocation = -1;
        int s_TexW = 0, s_TexH = 0, s_DisplayW = 0, s_DisplayH = 0, s_Rotation = -1;
        bool s_HasImage = false;
        glm::vec3 s_CamPos{ 0.0f };
        glm::quat s_CamRot{ 1.0f, 0.0f, 0.0f, 0.0f };
        bool s_CamValid = false;
        float s_FovY = 60.0f;

        android_app* App() { return Android::GetApp(); }

        JNIEnv* Env()
        {
            android_app* app = App();
            if (!app || !app->activity || !app->activity->vm)
                return nullptr;
            JNIEnv* env = nullptr;
            if (app->activity->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
                app->activity->vm->AttachCurrentThread(&env, nullptr);
            return env;
        }

        void ClearJniException(JNIEnv* env)
        {
            if (env && env->ExceptionCheck())
            {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }

        // Kamera izni (Android 6+ çalışma zamanı izni): yoksa bir kez istenir, sonra her saniye yeniden bakılır
        bool HasCameraPermission(JNIEnv* env)
        {
            jobject activity = App()->activity->clazz;
            jclass cls = env->GetObjectClass(activity);
            jmethodID check = env->GetMethodID(cls, "checkSelfPermission", "(Ljava/lang/String;)I");
            jstring perm = env->NewStringUTF("android.permission.CAMERA");
            const jint granted = check ? env->CallIntMethod(activity, check, perm) : -1;
            ClearJniException(env);
            if (granted != 0 && !s_PermissionAsked)
            {
                s_PermissionAsked = true;
                jmethodID request = env->GetMethodID(cls, "requestPermissions", "([Ljava/lang/String;I)V");
                jobjectArray arr = env->NewObjectArray(1, env->FindClass("java/lang/String"), perm);
                if (request)
                    env->CallVoidMethod(activity, request, arr, (jint)1);
                ClearJniException(env);
                env->DeleteLocalRef(arr);
            }
            env->DeleteLocalRef(perm);
            env->DeleteLocalRef(cls);
            return granted == 0;
        }

        // Ekran dönüşü (Display.getRotation): kamera görüntüsü ve izdüşüm buna göre döner
        int DisplayRotation(JNIEnv* env)
        {
            jobject activity = App()->activity->clazz;
            jclass cls = env->GetObjectClass(activity);
            jmethodID getWm = env->GetMethodID(cls, "getWindowManager", "()Landroid/view/WindowManager;");
            jobject wm = getWm ? env->CallObjectMethod(activity, getWm) : nullptr;
            int rotation = 0;
            if (wm)
            {
                jclass wmCls = env->GetObjectClass(wm);
                jmethodID getDisplay = env->GetMethodID(wmCls, "getDefaultDisplay", "()Landroid/view/Display;");
                jobject display = getDisplay ? env->CallObjectMethod(wm, getDisplay) : nullptr;
                if (display)
                {
                    jclass dCls = env->GetObjectClass(display);
                    jmethodID getRotation = env->GetMethodID(dCls, "getRotation", "()I");
                    rotation = getRotation ? env->CallIntMethod(display, getRotation) : 0;
                    env->DeleteLocalRef(dCls);
                    env->DeleteLocalRef(display);
                }
                env->DeleteLocalRef(wmCls);
                env->DeleteLocalRef(wm);
            }
            ClearJniException(env);
            env->DeleteLocalRef(cls);
            return rotation;
        }

        bool LoadLibrary()
        {
            if (s_Lib)
                return true;
            s_Lib = dlopen("libarcore_sdk_c.so", RTLD_NOW | RTLD_LOCAL);
            if (!s_Lib)
            {
                s_Error = "ARCore SDK library not in the APK (build with ARCore enabled)";
                return false;
            }
            bool ok = true;
            #define CQ_AR_LOAD(ret, name, args) \
                s_Api.name = reinterpret_cast<ret (*) args>(dlsym(s_Lib, #name)); \
                if (!s_Api.name) { ok = false; s_Error = std::string("ARCore SDK has no ") + #name; }
            CQ_AR_FUNCTIONS(CQ_AR_LOAD)
            #undef CQ_AR_LOAD
            return ok;
        }

        GLuint Compile(GLenum type, const char* source)
        {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[512] = "";
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                CQ_CORE_ERROR("[ARCore] camera shader: {0}", log);
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        }

        // Dış doku OES uzantısı istediği için motorun gölgelendirici hattı (SPIR-V) yerine doğrudan GLSL ES
        bool CreateGpuObjects()
        {
            if (s_Program)
                return true;
            const char* vs = "#version 300 es\n"
                             "uniform vec2 u_UV[4];\n"
                             "out vec2 v_UV;\n"
                             "void main() {\n"
                             "    vec2 p = vec2(float(gl_VertexID & 1) * 2.0 - 1.0, float(gl_VertexID >> 1) * 2.0 - 1.0);\n"
                             "    v_UV = u_UV[gl_VertexID];\n"
                             "    gl_Position = vec4(p, 0.0, 1.0);\n"
                             "}\n";
            const char* fs = "#version 300 es\n"
                             "#extension GL_OES_EGL_image_external_essl3 : require\n"
                             "precision mediump float;\n"
                             "uniform samplerExternalOES u_Camera;\n"
                             "in vec2 v_UV;\n"
                             "out vec4 o_Color;\n"
                             "void main() { o_Color = vec4(texture(u_Camera, v_UV).rgb, 1.0); }\n";
            GLuint v = Compile(GL_VERTEX_SHADER, vs), f = Compile(GL_FRAGMENT_SHADER, fs);
            if (!v || !f)
                return false;
            s_Program = glCreateProgram();
            glAttachShader(s_Program, v);
            glAttachShader(s_Program, f);
            glLinkProgram(s_Program);
            glDeleteShader(v);
            glDeleteShader(f);
            GLint linked = 0;
            glGetProgramiv(s_Program, GL_LINK_STATUS, &linked);
            if (!linked)
            {
                glDeleteProgram(s_Program);
                s_Program = 0;
                return false;
            }
            s_UVLocation = glGetUniformLocation(s_Program, "u_UV");
            glGenTextures(1, &s_CameraTexture);
            glBindTexture(kTextureExternalOES, s_CameraTexture);
            glTexParameteri(kTextureExternalOES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(kTextureExternalOES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(kTextureExternalOES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(kTextureExternalOES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(kTextureExternalOES, 0);
            glGenVertexArrays(1, &s_Vao);
            return true;
        }

        void EnsureColorTarget(int w, int h)
        {
            if (s_ColorTexture && s_TexW == w && s_TexH == h)
                return;
            if (s_Fbo) glDeleteFramebuffers(1, &s_Fbo);
            if (s_ColorTexture) glDeleteTextures(1, &s_ColorTexture);
            glGenTextures(1, &s_ColorTexture);
            glBindTexture(GL_TEXTURE_2D, s_ColorTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1, &s_Fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, s_Fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_ColorTexture, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            s_TexW = w;
            s_TexH = h;
        }

        // Oturum: kullanılabilirlik, gerekirse ARCore kurulumu, kamera izni, oluşturma ve yapılandırma
        bool TryCreateSession()
        {
            JNIEnv* env = Env();
            if (!env)
            {
                s_Error = "no Java VM";
                return false;
            }
            void* activity = App()->activity->clazz;
            int32_t availability = AR_AVAILABILITY_UNKNOWN_CHECKING;
            s_Api.ArCoreApk_checkAvailability(env, activity, &availability);
            if (availability == AR_AVAILABILITY_UNKNOWN_CHECKING)
            {
                s_State = "Initializing";
                return false;
            }
            if (availability == AR_AVAILABILITY_SUPPORTED_NOT_INSTALLED || availability == AR_AVAILABILITY_SUPPORTED_APK_TOO_OLD)
            {
                // Google Play Services for AR kurulumu (bir kez kullanıcıya sorulur)
                int32_t install = -1;
                s_Api.ArCoreApk_requestInstall(env, activity, s_InstallAsked ? 0 : 1, &install);
                ClearJniException(env);
                s_InstallAsked = true;
                if (install != AR_INSTALL_STATUS_INSTALLED)
                {
                    s_State = "Installing";
                    return false;
                }
            }
            else if (availability != AR_AVAILABILITY_SUPPORTED_INSTALLED)
            {
                s_State = "Unavailable";
                s_Error = "this device does not support ARCore (availability " + std::to_string(availability) + ")";
                return false;
            }
            if (!HasCameraPermission(env))
            {
                s_State = "Waiting for camera permission";
                return false;
            }
            const ArStatus created = s_Api.ArSession_create(env, activity, &s_Session);
            ClearJniException(env);
            if (created != AR_SUCCESS || !s_Session)
            {
                s_Session = nullptr;
                if (created == AR_ERROR_CAMERA_PERMISSION_NOT_GRANTED)
                {
                    s_State = "Waiting for camera permission";
                    return false;
                }
                // Ölümcül hata (kamera yok, cihaz desteklenmiyor ...): yeniden denenmez (ARCore yarım kalan oturumdan sonra
                // tekrar denenince süreci düşürebiliyor)
                s_Error = "ArSession_create failed (" + std::to_string(created) + ")";
                s_State = "Unavailable";
                s_Requested = false;
                return false;
            }
            ArConfig* config = nullptr;
            s_Api.ArConfig_create(s_Session, &config);
            s_Api.ArConfig_setPlaneFindingMode(s_Session, config, AR_PLANE_FINDING_MODE_HORIZONTAL_AND_VERTICAL);
            s_Api.ArConfig_setLightEstimationMode(s_Session, config, AR_LIGHT_ESTIMATION_MODE_DISABLED);
            s_Api.ArConfig_setFocusMode(s_Session, config, AR_FOCUS_MODE_AUTO);
            const ArStatus configured = s_Api.ArSession_configure(s_Session, config);
            s_Api.ArConfig_destroy(config);
            if (configured != AR_SUCCESS)
            {
                s_Error = "ArSession_configure failed (" + std::to_string(configured) + ")";
                return false;
            }
            if (!CreateGpuObjects())
            {
                s_Error = "camera shader failed";
                return false;
            }
            s_Api.ArSession_setCameraTextureName(s_Session, s_CameraTexture);
            s_Api.ArFrame_create(s_Session, &s_Frame);
            const float identity[7] = { 0, 0, 0, 1, 0, 0, 0 };
            s_Api.ArPose_create(s_Session, identity, &s_Pose);
            const ArStatus resumed = s_Api.ArSession_resume(s_Session);
            if (resumed != AR_SUCCESS)
            {
                s_Error = "ArSession_resume failed (" + std::to_string(resumed) + ")";
                return false;
            }
            s_Running = true;
            s_State = "Initializing";
            s_Error.clear();
            CQ_CORE_INFO("[ARCore] Session running");
            return true;
        }

        void ReadPose(const float raw[7], glm::vec3& position, glm::quat& rotation)
        {
            rotation = glm::quat(raw[3], raw[0], raw[1], raw[2]);
            position = glm::vec3(raw[4], raw[5], raw[6]);
        }

        void UpdatePlanes()
        {
            s_Planes.clear();
            ArTrackableList* list = nullptr;
            s_Api.ArTrackableList_create(s_Session, &list);
            s_Api.ArSession_getAllTrackables(s_Session, AR_TRACKABLE_PLANE, list);
            int32_t count = 0;
            s_Api.ArTrackableList_getSize(s_Session, list, &count);
            for (int32_t i = 0; i < count; ++i)
            {
                ArTrackable* t = nullptr;
                s_Api.ArTrackableList_acquireItem(s_Session, list, i, &t);
                if (!t)
                    continue;
                int32_t state = -1;
                s_Api.ArTrackable_getTrackingState(s_Session, t, &state);
                ArTrackable* subsumedBy = nullptr;
                s_Api.ArPlane_acquireSubsumedBy(s_Session, t, &subsumedBy);
                if (subsumedBy)
                    s_Api.ArTrackable_release(subsumedBy);
                if (state == AR_TRACKING_STATE_TRACKING && !subsumedBy)
                {
                    Plane p;
                    p.Id = reinterpret_cast<uintptr_t>(t);   // ARCore aynı izlenebilir için aynı tutamacı verir
                    float raw[7];
                    s_Api.ArPlane_getCenterPose(s_Session, t, s_Pose);
                    s_Api.ArPose_getPoseRaw(s_Session, s_Pose, raw);
                    ReadPose(raw, p.Position, p.Rotation);
                    s_Api.ArPlane_getExtentX(s_Session, t, &p.ExtentX);
                    s_Api.ArPlane_getExtentZ(s_Session, t, &p.ExtentZ);
                    s_Api.ArPlane_getType(s_Session, t, &p.Type);
                    int32_t n = 0;
                    s_Api.ArPlane_getPolygonSize(s_Session, t, &n);
                    std::vector<float> xz((size_t)std::max(n, 0));
                    if (n > 0)
                        s_Api.ArPlane_getPolygon(s_Session, t, xz.data());
                    for (int32_t k = 0; k + 1 < n; k += 2)
                        p.Polygon.emplace_back(xz[(size_t)k], xz[(size_t)k + 1]);
                    s_Planes.push_back(std::move(p));
                }
                s_Api.ArTrackable_release(t);
            }
            s_Api.ArTrackableList_destroy(list);
        }

        // Kamera görüntüsü -> ekran boyutunda RGBA8 (ekran dönüşü ve kırpma ARCore'un dönüşümüyle)
        void CopyCameraImage(int w, int h)
        {
            const float ndc[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
            float uv[8];
            s_Api.ArFrame_transformCoordinates2d(s_Session, s_Frame, AR_COORDINATES_2D_OPENGL_NORMALIZED_DEVICE_COORDINATES, 4, ndc,
                                                 AR_COORDINATES_2D_TEXTURE_NORMALIZED, uv);
            EnsureColorTarget(w, h);
            GLint prevFbo = 0, prevViewport[4], prevProgram = 0, prevVao = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
            glGetIntegerv(GL_VIEWPORT, prevViewport);
            glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
            const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE),
                            scissor = glIsEnabled(GL_SCISSOR_TEST);
            glBindFramebuffer(GL_FRAMEBUFFER, s_Fbo);
            glViewport(0, 0, w, h);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glDisable(GL_SCISSOR_TEST);
            glUseProgram(s_Program);
            glUniform2fv(s_UVLocation, 4, uv);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(kTextureExternalOES, s_CameraTexture);
            glUniform1i(glGetUniformLocation(s_Program, "u_Camera"), 0);
            glBindVertexArray(s_Vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindTexture(kTextureExternalOES, 0);
            glBindVertexArray((GLuint)prevVao);
            glUseProgram((GLuint)prevProgram);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
            glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
            if (depth) glEnable(GL_DEPTH_TEST);
            if (blend) glEnable(GL_BLEND);
            if (cull) glEnable(GL_CULL_FACE);
            if (scissor) glEnable(GL_SCISSOR_TEST);
            s_HasImage = true;
        }
    }

    bool Start()
    {
        Stop();
        s_Requested = true;
        s_State = "Initializing";
        s_Error.clear();
        if (!LoadLibrary())
        {
            s_State = "Unavailable";
            CQ_CORE_WARN("[ARCore] {0}", s_Error);
            s_Requested = false;
            return false;
        }
        s_NextTry = std::chrono::steady_clock::now();
        return true;   // oturum Update'te kurulur (kullanılabilirlik / izin / kurulum birkaç kare sürebilir)
    }

    void Stop()
    {
        if (s_Session)
        {
            s_Api.ArSession_pause(s_Session);
            if (s_Pose) s_Api.ArPose_destroy(s_Pose);
            if (s_Frame) s_Api.ArFrame_destroy(s_Frame);
            s_Api.ArSession_destroy(s_Session);
        }
        s_Session = nullptr;
        s_Frame = nullptr;
        s_Pose = nullptr;
        s_Running = false;
        s_Requested = false;
        s_HasImage = false;
        s_CamValid = false;
        s_Planes.clear();
        s_Rotation = -1;
        s_State = "Unavailable";
    }

    void Update()
    {
        if (!s_Requested)
            return;
        if (!s_Running)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now < s_NextTry)
                return;
            s_NextTry = now + std::chrono::milliseconds(500);
            if (!TryCreateSession())
            {
                if (!s_Error.empty())
                    CQ_CORE_WARN("[ARCore] {0}", s_Error);
                if (s_Session && !s_Running)
                {
                    s_Api.ArSession_destroy(s_Session);
                    s_Session = nullptr;
                }
                return;
            }
        }
        // Ekran boyutu / dönüşü değiştiyse ARCore'a bildir
        int w = 0, h = 0;
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow()), &w, &h);
        if (w <= 0 || h <= 0)
            return;
        JNIEnv* env = Env();
        const int rotation = env ? DisplayRotation(env) : 0;
        if (w != s_DisplayW || h != s_DisplayH || rotation != s_Rotation)
        {
            s_DisplayW = w;
            s_DisplayH = h;
            s_Rotation = rotation;
            s_Api.ArSession_setDisplayGeometry(s_Session, rotation, w, h);
        }
        const ArStatus st = s_Api.ArSession_update(s_Session, s_Frame);
        if (st == AR_ERROR_SESSION_PAUSED)
        {
            s_Api.ArSession_resume(s_Session);
            return;
        }
        if (st != AR_SUCCESS)
            return;
        ++s_Frames;
        CopyCameraImage(w, h);
        ArCamera* camera = nullptr;
        s_Api.ArFrame_acquireCamera(s_Session, s_Frame, &camera);
        int32_t tracking = -1;
        s_Api.ArCamera_getTrackingState(s_Session, camera, &tracking);
        if (tracking == AR_TRACKING_STATE_TRACKING)
        {
            float raw[7];
            s_Api.ArCamera_getDisplayOrientedPose(s_Session, camera, s_Pose);
            s_Api.ArPose_getPoseRaw(s_Session, s_Pose, raw);
            ReadPose(raw, s_CamPos, s_CamRot);
            s_CamValid = true;
            float proj[16];
            s_Api.ArCamera_getProjectionMatrix(s_Session, camera, 0.05f, 100.0f, proj);
            if (std::abs(proj[5]) > 1e-4f)
                s_FovY = glm::degrees(2.0f * std::atan(1.0f / proj[5]));
            s_State = "Tracking";
        }
        else
            s_State = tracking == AR_TRACKING_STATE_PAUSED ? (s_CamValid ? "Limited" : "Initializing") : "Limited";
        s_Api.ArCamera_release(camera);
        UpdatePlanes();
    }

    bool IsRunning() { return s_Running; }

    bool CameraPose(glm::vec3& position, glm::quat& rotation)
    {
        if (!s_CamValid)
            return false;
        position = s_CamPos;
        rotation = s_CamRot;
        return true;
    }

    float CameraFovY() { return s_CamValid ? s_FovY : 0.0f; }

    bool Raycast(float pixelX, float pixelY, HitResult& out)
    {
        if (!s_Running || !s_Frame)
            return false;
        ArHitResultList* list = nullptr;
        s_Api.ArHitResultList_create(s_Session, &list);
        s_Api.ArFrame_hitTest(s_Session, s_Frame, pixelX, pixelY, list);
        int32_t count = 0;
        s_Api.ArHitResultList_getSize(s_Session, list, &count);
        bool found = false;
        ArHitResult* hit = nullptr;
        s_Api.ArHitResult_create(s_Session, &hit);
        for (int32_t i = 0; i < count && !found; ++i)
        {
            s_Api.ArHitResultList_getItem(s_Session, list, i, hit);
            ArTrackable* t = nullptr;
            s_Api.ArHitResult_acquireTrackable(s_Session, hit, &t);
            if (!t)
                continue;
            int32_t type = 0;
            s_Api.ArTrackable_getType(s_Session, t, &type);
            if (type == AR_TRACKABLE_PLANE)
            {
                s_Api.ArHitResult_getHitPose(s_Session, hit, s_Pose);
                int32_t inside = 0;
                s_Api.ArPlane_isPoseInPolygon(s_Session, t, s_Pose, &inside);
                if (inside)
                {
                    float raw[7];
                    s_Api.ArPose_getPoseRaw(s_Session, s_Pose, raw);
                    glm::quat q;
                    ReadPose(raw, out.Point, q);
                    out.Normal = q * glm::vec3(0.0f, 1.0f, 0.0f);
                    out.PlaneId = reinterpret_cast<uintptr_t>(t);
                    out.Distance = s_CamValid ? glm::distance(out.Point, s_CamPos) : 0.0f;
                    found = true;
                }
            }
            s_Api.ArTrackable_release(t);
        }
        s_Api.ArHitResult_destroy(hit);
        s_Api.ArHitResultList_destroy(list);
        return found;
    }

    uint32_t BackgroundTexture() { return s_Running && s_HasImage ? s_ColorTexture : 0; }
    uint64_t FramesUpdated() { return s_Frames; }
#endif
}
