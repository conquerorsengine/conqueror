#pragma once

namespace Conqueror
{
    class GraphicsContext
    {
    public:
        virtual ~GraphicsContext() = default;

        // false: arka uç bu sistemde başlatılamadı (pencere OpenGL ile yeniden kurulur)
        virtual bool Init() = 0;
        virtual void SwapBuffers() = 0;
        virtual void SetVSync(bool enabled) { (void)enabled; }
        virtual const char* GetName() const = 0;

        static GraphicsContext* Create(void* window);
    };
}
