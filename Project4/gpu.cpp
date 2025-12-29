#include "shared.hpp"
#include <gl/GL.h>

// Link OpenGL
#pragma comment(lib, "opengl32.lib")

void InitGpuInfo() {
    WmiQuery wmi;
    if (!wmi.Init()) return;
    IEnumWbemClassObject* pEnum = wmi.Exec(L"SELECT Name FROM Win32_VideoController");
    if (pEnum) {
        IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
        pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
        if (uRet) {
            VARIANT v; pObj->Get(L"Name", 0, &v, 0, 0);
            if (v.vt == VT_BSTR) {
                std::lock_guard<std::mutex> lock(g_StatsMutex);
                g_GpuName = v.bstrVal;
            }
            VariantClear(&v); pObj->Release();
        }
        pEnum->Release();
    }
}

// OpenGL Stress Worker
void GpuStressWorker() {
    // Create invisible window for GL context
    WNDCLASSW wc = { 0 }; wc.lpfnWndProc = DefWindowProc; wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GLStress";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"GLStress", L"", WS_POPUP, 0, 0, 100, 100, NULL, NULL, GetModuleHandle(NULL), NULL);
    HDC hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = { sizeof(PIXELFORMATDESCRIPTOR), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32, 0,0,0,0,0,0, 0,0,0,0,0,0,0, 24, 8, 0, PFD_MAIN_PLANE, 0, 0, 0, 0 };
    int format = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, format, &pfd);
    HGLRC hglrc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, hglrc);

    // Heavy math in immediate mode for broad compatibility (no shader loader needed for demo)
    // Draws thousands of transparent overlapping quads to stress ROPs and fillrate
    while (g_GpuStress && g_AppRunning) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glBegin(GL_QUADS);
        for (int i = 0; i < 4000; i++) {
            float r = (float)(rand() % 100) / 100.0f;
            float g = (float)(rand() % 100) / 100.0f;
            float b = (float)(rand() % 100) / 100.0f;
            glColor4f(r, g, b, 0.1f);

            float x = (float)(rand() % 200) / 100.0f - 1.0f;
            float y = (float)(rand() % 200) / 100.0f - 1.0f;
            glVertex2f(x, y);
            glVertex2f(x + 0.5f, y);
            glVertex2f(x + 0.5f, y + 0.5f);
            glVertex2f(x, y + 0.5f);
        }
        glEnd();

        // Force GPU to finish queue
        glFinish();
        SwapBuffers(hdc);
    }

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hglrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
}

void StartGpuStress() {
    g_GpuStress = true;
    std::thread(GpuStressWorker).detach();
}