#pragma once

#include <imgui.h>
#include <glm/glm.hpp>

#include <string>

#include "Particles/ParticleSystem.h"

// Conqueror editör widget kütüphanesi: tüm panellerde tutarlı, hizalı
// "etiket | değer" özellik satırları, bölüm başlıkları, bilgi kutuları.
namespace Conqueror::Editor::UI
{
    // Renk paleti (tema ile uyumlu vurgu renkleri)
    namespace Colors
    {
        constexpr ImVec4 Accent        = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        constexpr ImVec4 AccentDim     = ImVec4(0.20f, 0.45f, 0.78f, 1.00f);
        constexpr ImVec4 Success       = ImVec4(0.30f, 0.80f, 0.45f, 1.00f);
        constexpr ImVec4 Warning       = ImVec4(1.00f, 0.75f, 0.25f, 1.00f);
        constexpr ImVec4 Error         = ImVec4(1.00f, 0.38f, 0.38f, 1.00f);
        constexpr ImVec4 TextDim       = ImVec4(0.62f, 0.64f, 0.68f, 1.00f);
        constexpr ImVec4 AxisX         = ImVec4(0.89f, 0.30f, 0.30f, 1.00f);
        constexpr ImVec4 AxisY         = ImVec4(0.42f, 0.80f, 0.32f, 1.00f);
        constexpr ImVec4 AxisZ         = ImVec4(0.30f, 0.52f, 0.95f, 1.00f);
    }

    enum class InfoType { Info, Warning, Error, Success };

    // Etiket sütun genişliği (pencere genişliğine oranla, sınırlı)
    float LabelWidth();
    void SetLabelRatio(float ratio);

    // ---- Bölümler
    bool BeginSection(const char* title, bool defaultOpen = true, const char* icon = nullptr);
    void EndSection();
    void SubHeader(const char* text);
    void Separator(float spacing = 4.0f);

    // ---- Satır temel yapı taşları
    void BeginProperty(const char* label, const char* tooltip = nullptr);
    void EndProperty();

    // ---- Özellik satırları (değişince true döner)
    bool Checkbox(const char* label, bool& value, const char* tooltip = nullptr);
    bool DragFloat(const char* label, float& value, float speed = 0.01f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", const char* tooltip = nullptr);
    bool SliderFloat(const char* label, float& value, float min, float max, const char* format = "%.2f", const char* tooltip = nullptr);
    bool DragInt(const char* label, int& value, float speed = 1.0f, int min = 0, int max = 0, const char* tooltip = nullptr);
    bool SliderInt(const char* label, int& value, int min, int max, const char* tooltip = nullptr);
    bool Combo(const char* label, int& index, const char* const items[], int count, const char* tooltip = nullptr);
    bool Color3(const char* label, glm::vec3& color, const char* tooltip = nullptr);
    bool Color4(const char* label, glm::vec4& color, const char* tooltip = nullptr);
    bool Vec2(const char* label, glm::vec2& value, float speed = 0.05f, const char* tooltip = nullptr);
    bool Vec3(const char* label, glm::vec3& value, float speed = 0.05f, float resetValue = 0.0f, const char* tooltip = nullptr);
    bool InputText(const char* label, std::string& value, const char* tooltip = nullptr);
    void Text(const char* label, const char* fmt, ...) IM_FMTARGS(2);

    // Enum yardımcıları
    template<typename E>
    bool ComboEnum(const char* label, E& value, const char* const items[], int count, const char* tooltip = nullptr)
    {
        int idx = (int)value;
        if (Combo(label, idx, items, count, tooltip))
        {
            value = (E)idx;
            return true;
        }
        return false;
    }

    // ---- Diğer
    void InfoBox(const char* text, InfoType type = InfoType::Info);
    void HelpMarker(const char* text);
    bool ButtonFullWidth(const char* label, float height = 0.0f);
    bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0));
    bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0));
    bool ToggleButton(const char* label, bool& value, const ImVec2& size = ImVec2(0, 0));
    void StatusDot(const ImVec4& color);
    // Arka planlı küçük etiket (rozet)
    void Badge(const char* text, const ImVec4& color);
    // Yatay yüzde çubuğu (ör. cascade önizlemesi)
    void ProgressBarLabeled(float fraction, const char* overlay, const ImVec4& color);

    // Renk geçişi düzenleyici: tıkla = anahtar ekle, sürükle = taşı, sağ tık = sil,
    // çift tık = renk seç. Değişince true.
    bool GradientEditor(const char* label, Conqueror::ColorGradient& gradient);
    // 0..1 zaman eğrisi düzenleyici (grafik): çift tık = nokta ekle, sağ tık = sil
    bool CurveEditor(const char* label, Conqueror::FloatCurve& curve, float minValue = 0.0f, float maxValue = 1.0f, float height = 90.0f);
}
