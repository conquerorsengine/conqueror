#pragma once

#include "Core/Layer.h"
#include "Core/Events/Event.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"

struct ImFont;

namespace Conqueror
{
    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        virtual ~ImGuiLayer() = default;

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnEvent(Event& e) override;

        void Begin();
        void End();

        void BlockEvents(bool block) { m_BlockEvents = block; }
        
        void SetDarkThemeColors();

        enum class FontRole { Regular = 0, Bold, Large, Mono, Count };
        // Editör fontları (Font Awesome ikonları Regular/Bold/Large'a birleştirilmiştir)
        static ::ImFont* GetFont(FontRole role);
        // Pencere bu kareyi göstermesin mi (bir kez okunur). ImGui ilk karesinde paneller henüz görünmez (yalnız menü çubuğu, gerisi
        // siyah): o kare ekrana verilmez, pencere ilk kez tam yerleşimle görünür (238)
        static bool ConsumeSkipPresent();
        // Bu kare ekrana gönderilmez (editör açılırken düzen oturana kadar)
        static void RequestSkipPresent();
        // Açılış penceresi (270): sonraki Begin'de ImGui ekran boyutu bu olur (ana pencere yerine); EndInto kareyi o pencereye
        // (ana bağlamla paylaşımlı OpenGL bağlamı) çizer ve gösterir, sonra ana bağlama döner
        static void OverrideNextDisplaySize(float width, float height, float fbScaleX, float fbScaleY);
        void EndInto(void* nativeWindow, float clearR, float clearG, float clearB, float clearA);

    private:
        void LoadFonts();
        static ::ImFont* s_Fonts[(int)FontRole::Count];

        bool m_BlockEvents = true;
        float m_Time = 0.0f;
    };
}
