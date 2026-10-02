#include "OffscreenGL.h"
#include <juce_opengl/juce_opengl.h>

#if JUCE_MAC
 #include <OpenGL/OpenGL.h>

struct OffscreenGL::Impl
{
    CGLContextObj ctx = nullptr;
};

OffscreenGL::OffscreenGL() : impl (std::make_unique<Impl>()) {}
OffscreenGL::~OffscreenGL() { destroy(); }

bool OffscreenGL::create (juce::String& error)
{
    destroy();
    const CGLPixelFormatAttribute attrs[] = {
        kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute) kCGLOGLPVersion_3_2_Core,
        kCGLPFAAccelerated,
        kCGLPFAAllowOfflineRenderers,
        (CGLPixelFormatAttribute) 0
    };
    CGLPixelFormatObj pf = nullptr;
    GLint count = 0;
    if (CGLChoosePixelFormat (attrs, &pf, &count) != kCGLNoError || pf == nullptr)
    {
        error = "CGLChoosePixelFormat failed";
        return false;
    }
    const auto r = CGLCreateContext (pf, nullptr, &impl->ctx);
    CGLDestroyPixelFormat (pf);
    if (r != kCGLNoError || impl->ctx == nullptr)
    {
        error = "CGLCreateContext failed";
        impl->ctx = nullptr;
        return false;
    }
    CGLSetCurrentContext (impl->ctx);
    juce::gl::loadFunctions();
    return true;
}

void OffscreenGL::destroy()
{
    if (impl->ctx != nullptr)
    {
        if (CGLGetCurrentContext() == impl->ctx)
            CGLSetCurrentContext (nullptr);
        CGLDestroyContext (impl->ctx);
        impl->ctx = nullptr;
    }
}

bool OffscreenGL::isValid() const { return impl->ctx != nullptr; }
void OffscreenGL::pump() {}

#elif JUCE_WINDOWS
 #include <windows.h>

namespace
{
    using CreateContextAttribs = HGLRC (WINAPI*) (HDC, HGLRC, const int*);
    constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB    = 0x2091;
    constexpr int WGL_CONTEXT_MINOR_VERSION_ARB    = 0x2092;
    constexpr int WGL_CONTEXT_PROFILE_MASK_ARB     = 0x9126;
    constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x0001;

    LRESULT CALLBACK wndProc (HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW (h, m, w, l); }
    const wchar_t* className = L"SpecShaderOffscreenGL";
}

struct OffscreenGL::Impl
{
    HWND wnd = nullptr;
    HDC dc = nullptr;
    HGLRC rc = nullptr;
};

OffscreenGL::OffscreenGL() : impl (std::make_unique<Impl>()) {}
OffscreenGL::~OffscreenGL() { destroy(); }

bool OffscreenGL::create (juce::String& error)
{
    destroy();
    auto inst = GetModuleHandleW (nullptr);
    WNDCLASSW wc {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = className;
    RegisterClassW (&wc);   // 2 回目以降は失敗するが問題ない

    impl->wnd = CreateWindowExW (0, className, L"", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr, nullptr, inst, nullptr);
    if (impl->wnd == nullptr) { error = "CreateWindow failed"; return false; }
    impl->dc = GetDC (impl->wnd);

    PIXELFORMATDESCRIPTOR pfd {};
    pfd.nSize = sizeof (pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int fmt = ChoosePixelFormat (impl->dc, &pfd);
    if (fmt == 0 || ! SetPixelFormat (impl->dc, fmt, &pfd)) { error = "SetPixelFormat failed"; destroy(); return false; }

    // 一時的な古いコンテキストで wglCreateContextAttribsARB を取ってから Core を作る
    HGLRC legacy = wglCreateContext (impl->dc);
    if (legacy == nullptr) { error = "wglCreateContext failed"; destroy(); return false; }
    wglMakeCurrent (impl->dc, legacy);
    auto createAttribs = (CreateContextAttribs) wglGetProcAddress ("wglCreateContextAttribsARB");
    if (createAttribs != nullptr)
    {
        const int attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
        impl->rc = createAttribs (impl->dc, nullptr, attribs);
    }
    wglMakeCurrent (nullptr, nullptr);
    wglDeleteContext (legacy);
    if (impl->rc == nullptr) { error = "OpenGL 3.3 core context is not available"; destroy(); return false; }

    wglMakeCurrent (impl->dc, impl->rc);
    juce::gl::loadFunctions();
    return true;
}

void OffscreenGL::destroy()
{
    if (impl->rc != nullptr)
    {
        if (wglGetCurrentContext() == impl->rc)
            wglMakeCurrent (nullptr, nullptr);
        wglDeleteContext (impl->rc);
        impl->rc = nullptr;
    }
    if (impl->dc != nullptr) { ReleaseDC (impl->wnd, impl->dc); impl->dc = nullptr; }
    if (impl->wnd != nullptr) { DestroyWindow (impl->wnd); impl->wnd = nullptr; }
}

bool OffscreenGL::isValid() const { return impl->rc != nullptr; }

void OffscreenGL::pump()
{
    MSG msg;
    while (impl->wnd != nullptr && PeekMessageW (&msg, impl->wnd, 0, 0, PM_REMOVE))
        DispatchMessageW (&msg);
}

#else

struct OffscreenGL::Impl {};
OffscreenGL::OffscreenGL() : impl (std::make_unique<Impl>()) {}
OffscreenGL::~OffscreenGL() = default;
bool OffscreenGL::create (juce::String& error) { error = "This platform is not supported"; return false; }
void OffscreenGL::destroy() {}
bool OffscreenGL::isValid() const { return false; }
void OffscreenGL::pump() {}

#endif
