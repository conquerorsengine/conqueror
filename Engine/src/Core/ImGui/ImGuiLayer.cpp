#include "ImGuiLayer.h"
#include "Renderer/GraphicsAPIs/OpenGL/GLProfile.h"
#include "Core/Application.h"
#include "Core/Input/Input.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"
#include "Core/Automation/Automation.h"
#include "Core/Utils/FileSystem.h"
#include "Renderer/RHI/RendererAPI.h"

#include <filesystem>

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

namespace Conqueror
{
    ImGuiLayer::ImGuiLayer()
        : Layer("ImGuiLayer")
    {
    }

    void ImGuiLayer::OnAttach()
    {
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // Çoklu viewport ek GL bağlamları ister; çeviri katmanlı arka uçlarda (Vulkan / D3D11) platform pencereleri kapalı
        const bool translated = RendererAPI::GetBackend() != RendererAPI::Backend::OpenGL;
        if (!translated)
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        CQ_CORE_INFO("ImGui: Loading fonts...");
        LoadFonts();

        CQ_CORE_INFO("ImGui: Setting up style...");
        ImGui::StyleColorsDark();
        SetDarkThemeColors();

        // Çoklu viewport'ta platform pencereleri ana pencereyle aynı görünsün
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        CQ_CORE_INFO("ImGui: Initializing platform backends...");

        Application& app = Application::Get();
        GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

        // Setup Platform/Renderer backends
        [[maybe_unused]] bool glfwInitResult = translated ? ImGui_ImplGlfw_InitForOther(window, true) : ImGui_ImplGlfw_InitForOpenGL(window, true);
        CQ_CORE_ASSERT(glfwInitResult, "Failed to initialize ImGui GLFW backend!");
        
        // GLSL sürümü bağlama göre (ES: 300 es, 3.3 core: 330); emüle arka uçlar 4.5 yüzeyi sunar
        [[maybe_unused]] bool openglInitResult = ImGui_ImplOpenGL3_Init(GLProfile::ImGuiGLSLVersion());
        CQ_CORE_ASSERT(openglInitResult, "Failed to initialize ImGui OpenGL3 backend!");

        CQ_CORE_INFO("ImGui: Initialization complete");
    }

    ImFont* ImGuiLayer::s_Fonts[(int)ImGuiLayer::FontRole::Count] = {};

    ImFont* ImGuiLayer::GetFont(FontRole role)
    {
        ImFont* f = s_Fonts[(int)role];
        return f ? f : ImGui::GetFont();
    }

    void ImGuiLayer::LoadFonts()
    {
        ImGuiIO& io = ImGui::GetIO();
        const float baseSize = 16.0f;

        auto resolve = [](const char* relative) -> std::string
        {
            std::string path = FileSystem::ResolveResourcePath(relative);
            std::error_code ec;
            return std::filesystem::exists(path, ec) ? path : std::string();
        };

        const std::string regularPath = resolve("Resources/Fonts/opensans/OpenSans-Regular.ttf");
        const std::string boldPath = resolve("Resources/Fonts/opensans/OpenSans-Bold.ttf");
        const std::string iconPath = resolve("Resources/Fonts/fontawesome/fa-solid-900.ttf");
        const std::string monoPath = resolve("Resources/Fonts/jetbrainsmono/JetBrainsMono-Regular.ttf");

        auto addWithIcons = [&](const std::string& path, float size) -> ImFont*
        {
            ImFont* font = nullptr;
            if (!path.empty())
                font = io.Fonts->AddFontFromFileTTF(path.c_str(), size);
            if (!font)
            {
                ImFontConfig cfg;
                cfg.SizePixels = size;
                font = io.Fonts->AddFontDefault(&cfg);
            }
            if (!iconPath.empty())
            {
                // İkonlar aynı fonta birleştirilir: ICON_FA_* makroları metin içinde kullanılabilir
                ImFontConfig iconCfg;
                iconCfg.MergeMode = true;
                iconCfg.PixelSnapH = true;
                iconCfg.GlyphMinAdvanceX = size * 1.1f;
                iconCfg.GlyphOffset = ImVec2(0.0f, 1.0f);
                io.Fonts->AddFontFromFileTTF(iconPath.c_str(), size * 0.88f, &iconCfg);
            }
            return font;
        };

        s_Fonts[(int)FontRole::Regular] = addWithIcons(regularPath, baseSize);
        s_Fonts[(int)FontRole::Bold] = addWithIcons(boldPath.empty() ? regularPath : boldPath, baseSize);
        s_Fonts[(int)FontRole::Large] = addWithIcons(boldPath.empty() ? regularPath : boldPath, baseSize * 1.35f);
        s_Fonts[(int)FontRole::Mono] = !monoPath.empty() ? io.Fonts->AddFontFromFileTTF(monoPath.c_str(), baseSize * 0.95f) : nullptr;
        if (!s_Fonts[(int)FontRole::Mono])
            s_Fonts[(int)FontRole::Mono] = s_Fonts[(int)FontRole::Regular];

        io.FontDefault = s_Fonts[(int)FontRole::Regular];
        if (regularPath.empty())
            CQ_CORE_WARN("ImGui: OpenSans not found (Resources/Fonts/opensans), using the built-in font");
        if (iconPath.empty())
            CQ_CORE_WARN("ImGui: Font Awesome not found (Resources/Fonts/fontawesome), icons disabled");
    }

    void ImGuiLayer::OnDetach()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiLayer::OnEvent(Event& e)
    {
        if (m_BlockEvents)
        {
            ImGuiIO& io = ImGui::GetIO();
            e.Handled |= e.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
            e.Handled |= e.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
        }
    }

    void ImGuiLayer::Begin()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        Automation::InjectImGuiInput();
        Automation::BeforeImGuiNewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    void ImGuiLayer::End()
    {
        ImGuiIO& io = ImGui::GetIO();
        Application& app = Application::Get();
        io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

        // Rendering
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (Automation::IsActive())
        {
            int fbWidth = 0, fbHeight = 0;
            glfwGetFramebufferSize(static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow()), &fbWidth, &fbHeight);
            Automation::OnFrameRendered((uint32_t)fbWidth, (uint32_t)fbHeight);
        }

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
    }

    void ImGuiLayer::SetDarkThemeColors()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        // Yerleşim
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.CellPadding = ImVec2(6.0f, 3.0f);
        style.ItemSpacing = ImVec2(7.0f, 5.0f);
        style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
        style.IndentSpacing = 16.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;

        // Kenarlıklar ve köşeler
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;
        style.WindowRounding = 4.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 4.0f;
        style.WindowTitleAlign = ImVec2(0.02f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.SeparatorTextBorderSize = 1.0f;
        style.DockingSeparatorSize = 2.0f;

        // Renkler: katmanlı koyu griler + mavi vurgu
        auto& c = style.Colors;
        const ImVec4 bg0 = ImVec4(0.086f, 0.090f, 0.098f, 1.0f);   // en koyu (dock arkası)
        const ImVec4 bg1 = ImVec4(0.110f, 0.115f, 0.125f, 1.0f);   // pencere
        const ImVec4 bg2 = ImVec4(0.145f, 0.150f, 0.163f, 1.0f);   // çerçeve/başlık
        const ImVec4 bg3 = ImVec4(0.190f, 0.197f, 0.214f, 1.0f);   // hover
        const ImVec4 bg4 = ImVec4(0.235f, 0.244f, 0.265f, 1.0f);   // aktif
        const ImVec4 accent = ImVec4(0.259f, 0.533f, 0.961f, 1.0f);
        const ImVec4 accentDim = ImVec4(0.200f, 0.400f, 0.720f, 1.0f);
        const ImVec4 text = ImVec4(0.900f, 0.910f, 0.930f, 1.0f);
        const ImVec4 textDim = ImVec4(0.520f, 0.540f, 0.570f, 1.0f);
        const ImVec4 border = ImVec4(0.050f, 0.052f, 0.058f, 1.0f);

        c[ImGuiCol_Text] = text;
        c[ImGuiCol_TextDisabled] = textDim;
        c[ImGuiCol_WindowBg] = bg1;
        c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_PopupBg] = ImVec4(0.120f, 0.125f, 0.135f, 0.98f);
        c[ImGuiCol_Border] = border;
        c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_FrameBg] = bg2;
        c[ImGuiCol_FrameBgHovered] = bg3;
        c[ImGuiCol_FrameBgActive] = bg4;
        c[ImGuiCol_TitleBg] = bg0;
        c[ImGuiCol_TitleBgActive] = bg0;
        c[ImGuiCol_TitleBgCollapsed] = bg0;
        c[ImGuiCol_MenuBarBg] = bg0;
        c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ScrollbarGrab] = bg3;
        c[ImGuiCol_ScrollbarGrabHovered] = bg4;
        c[ImGuiCol_ScrollbarGrabActive] = accentDim;
        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accentDim;
        c[ImGuiCol_SliderGrabActive] = accent;
        c[ImGuiCol_Button] = bg2;
        c[ImGuiCol_ButtonHovered] = bg3;
        c[ImGuiCol_ButtonActive] = bg4;
        c[ImGuiCol_Header] = bg2;
        c[ImGuiCol_HeaderHovered] = bg3;
        c[ImGuiCol_HeaderActive] = ImVec4(accentDim.x, accentDim.y, accentDim.z, 0.55f);
        c[ImGuiCol_Separator] = ImVec4(0.180f, 0.186f, 0.200f, 1.0f);
        c[ImGuiCol_SeparatorHovered] = accentDim;
        c[ImGuiCol_SeparatorActive] = accent;
        c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ResizeGripHovered] = accentDim;
        c[ImGuiCol_ResizeGripActive] = accent;
        c[ImGuiCol_Tab] = bg0;
        c[ImGuiCol_TabHovered] = bg3;
        c[ImGuiCol_TabSelected] = bg1;
        c[ImGuiCol_TabSelectedOverline] = accent;
        c[ImGuiCol_TabDimmed] = bg0;
        c[ImGuiCol_TabDimmedSelected] = bg1;
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.35f, 0.37f, 0.40f, 1.0f);
        c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.6f);
        c[ImGuiCol_DockingEmptyBg] = bg0;
        c[ImGuiCol_PlotLines] = accent;
        c[ImGuiCol_PlotHistogram] = accent;
        c[ImGuiCol_TableHeaderBg] = bg2;
        c[ImGuiCol_TableBorderStrong] = border;
        c[ImGuiCol_TableBorderLight] = ImVec4(0.16f, 0.165f, 0.18f, 1.0f);
        c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        c[ImGuiCol_DragDropTarget] = accent;
        c[ImGuiCol_NavCursor] = accent;
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    }
}
