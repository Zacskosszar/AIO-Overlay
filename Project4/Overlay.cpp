#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <pdh.h>
#include <pdhmsg.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <gl/GL.h>
#include <immintrin.h> // AVX
#include <fcntl.h>
#include <io.h>

// --- WMI INCLUDES ---
#include <comdef.h>
#include <Wbemidl.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "wbemuuid.lib") 

// --- GLOBALS ---
HWND hOverlay = NULL;
int SCREEN_W = 0;
int SCREEN_H = 0;
std::atomic<bool> g_CpuStress(false);
std::atomic<bool> g_GpuStress(false);
std::atomic<bool> g_RamStress(false);
std::atomic<bool> g_AppRunning(true);


std::mutex g_StatsMutex;
int g_TotalCpuUsage = 0;
std::vector<int> g_CoreUsages;
int g_RamUsage = 0;
std::wstring g_RamInfo = L"0/0 GB";
int g_GlobalThreadCount = 0;
int g_ContextSwitches = 0;
int g_AppThreadCount = 0;

std::wstring cpuName = L"Unknown CPU";
std::wstring gpuName = L"Unknown GPU";
int cpuClock = 0;
int g_PhysicalCores = 0;
int g_LogicalProcs = 0;

int GetIntFromVariant(VARIANT* v) {
    if (v->vt == VT_I4) return v->intVal;
    if (v->vt == VT_UI4) return (int)v->uintVal;
    if (v->vt == VT_BSTR) return _wtoi(v->bstrVal);
    return 0;
}


int ReadRegInt(const wchar_t* path, const wchar_t* key) {
    HKEY hKey;
    DWORD val = 0;
    DWORD size = sizeof(val);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, key, NULL, NULL, (LPBYTE)&val, &size);
        RegCloseKey(hKey);
    }
    return (int)val;
}

std::wstring ReadRegString(const wchar_t* path, const wchar_t* key) {
    HKEY hKey;
    wchar_t buf[256] = { 0 };
    DWORD size = sizeof(buf);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, key, NULL, NULL, (LPBYTE)buf, &size);
        RegCloseKey(hKey);
    }
    return std::wstring(buf);
}

void CountCores() {
    DWORD len = 0;
    GetLogicalProcessorInformation(NULL, &len);
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(len);
    GetLogicalProcessorInformation(buffer, &len);

    int phys = 0;
    int log = 0;
    DWORD ptr = 0;
    while (ptr < len) {
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION current = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)((char*)buffer + ptr);
        if (current->Relationship == RelationProcessorCore) {
            phys++;
            // Count set bits in mask for logical threads per core
            ULONG_PTR mask = current->ProcessorMask;
            while (mask) {
                if (mask & 1) log++;
                mask >>= 1;
            }
        }
        ptr += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    }
    free(buffer);
    g_PhysicalCores = phys;
    g_LogicalProcs = log;
}

//   THREAD 1: PDH MONITOR (1->500ms)
void MonitorThread() {
    CountCores();

    PDH_HQUERY cpuQuery;
    PdhOpenQueryW(NULL, 0, &cpuQuery);

    PDH_HCOUNTER totalCounter;
    PdhAddEnglishCounterW(cpuQuery, L"\\Processor(_Total)\\% Processor Time", 0, &totalCounter);

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;

    std::vector<PDH_HCOUNTER> coreCounters(numCores);
    for (int i = 0; i < numCores; i++) {
        std::wstring path = L"\\Processor(" + std::to_wstring(i) + L")\\% Processor Time";
        PdhAddEnglishCounterW(cpuQuery, path.c_str(), 0, &coreCounters[i]);
    }

    {
        std::lock_guard<std::mutex> lock(g_StatsMutex);
        g_CoreUsages.resize(numCores, 0);
    }

    PdhCollectQueryData(cpuQuery);

    cpuClock = ReadRegInt(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz");
    cpuName = ReadRegString(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");

    DISPLAY_DEVICEW dd; dd.cb = sizeof(dd);
    if (EnumDisplayDevicesW(NULL, 0, &dd, 0)) gpuName = dd.DeviceString;

    while (g_AppRunning) {
        PdhCollectQueryData(cpuQuery);

        PDH_FMT_COUNTERVALUE cv;
        PdhGetFormattedCounterValue(totalCounter, PDH_FMT_LONG, NULL, &cv);
        long totalVal = cv.longValue;

        std::vector<int> localCores(numCores);
        for (int i = 0; i < numCores; i++) {
            PdhGetFormattedCounterValue(coreCounters[i], PDH_FMT_LONG, NULL, &cv);
            localCores[i] = (int)cv.longValue;
        }

        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        DWORDLONG totalRam = memInfo.ullTotalPhys / (1024 * 1024 * 1024);
        DWORDLONG usedRam = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024 * 1024 * 1024);

        std::wstringstream ss;
        ss << usedRam << L"/" << totalRam + 1 << L" GB";

        {
            std::lock_guard<std::mutex> lock(g_StatsMutex);
            g_TotalCpuUsage = (int)totalVal;
            g_CoreUsages = localCores;
            g_RamUsage = (int)memInfo.dwMemoryLoad;
            g_RamInfo = ss.str();
        }

        Sleep(500);
    }
    PdhCloseQuery(cpuQuery);
}


//   THREAD 2: WMI THREAD
void WmiThread() {
    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) return;

    hres = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    if (FAILED(hres)) { CoUninitialize(); return; }

    IWbemLocator* pLoc = NULL;
    hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hres)) { CoUninitialize(); return; }

    IWbemServices* pSvc = NULL;
    hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hres)) { pLoc->Release(); CoUninitialize(); return; }

    hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (FAILED(hres)) { pSvc->Release(); pLoc->Release(); CoUninitialize(); return; }

    while (g_AppRunning) {
        int threads = 0, ctx = 0, appThreads = 0;
        IEnumWbemClassObject* pEnumerator = NULL;

        hres = pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT Threads, ContextSwitchesPerSec FROM Win32_PerfFormattedData_PerfOS_System"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);

        if (SUCCEEDED(hres) && pEnumerator) {
            IWbemClassObject* pclsObj = NULL;
            ULONG uReturn = 0;
            while (pEnumerator) {
                pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                if (0 == uReturn) break;
                VARIANT vtProp;
                pclsObj->Get(L"Threads", 0, &vtProp, 0, 0); threads = GetIntFromVariant(&vtProp); VariantClear(&vtProp);
                pclsObj->Get(L"ContextSwitchesPerSec", 0, &vtProp, 0, 0); ctx = GetIntFromVariant(&vtProp); VariantClear(&vtProp);
                pclsObj->Release();
            }
            pEnumerator->Release();
        }

        DWORD myPid = GetCurrentProcessId();
        std::wstring query = L"SELECT ThreadCount FROM Win32_Process WHERE ProcessId = " + std::to_wstring(myPid);
        hres = pSvc->ExecQuery(bstr_t("WQL"), bstr_t(query.c_str()), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);

        if (SUCCEEDED(hres) && pEnumerator) {
            IWbemClassObject* pclsObj = NULL;
            ULONG uReturn = 0;
            while (pEnumerator) {
                pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                if (0 == uReturn) break;
                VARIANT vtProp;
                pclsObj->Get(L"ThreadCount", 0, &vtProp, 0, 0); appThreads = GetIntFromVariant(&vtProp); VariantClear(&vtProp);
                pclsObj->Release();
            }
            pEnumerator->Release();
        }

        {
            std::lock_guard<std::mutex> lock(g_StatsMutex);
            g_GlobalThreadCount = threads;
            g_ContextSwitches = ctx;
            g_AppThreadCount = appThreads;
        }
        Sleep(500);
    }
    pSvc->Release(); pLoc->Release(); CoUninitialize();
}

//   CPU AVX2 LOGIC
void CpuThread(int id) {
    DWORD_PTR mask = (1ULL << id);
    SetThreadAffinityMask(GetCurrentThread(), mask);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    __m256 a = _mm256_set1_ps(1.1f); __m256 b = _mm256_set1_ps(2.2f); __m256 c = _mm256_set1_ps(3.3f); __m256 d = _mm256_set1_ps(4.4f);
    while (g_CpuStress) {
        for (int i = 0; i < 1000; i++) {
            a = _mm256_fmadd_ps(a, b, c); b = _mm256_fmadd_ps(b, c, d);
            c = _mm256_fmadd_ps(c, d, a); d = _mm256_fmadd_ps(d, a, b);
        }
    }
}
void StartCpuStress() { g_CpuStress = true; int cores = std::thread::hardware_concurrency(); for (int i = 0; i < cores; i++) std::thread(CpuThread, i).detach(); }

void StartRamStress() {
    g_RamStress = true;
    std::thread([]() {
        MEMORYSTATUSEX statex; statex.dwLength = sizeof(statex); GlobalMemoryStatusEx(&statex);
        size_t safeBytes = (size_t)(statex.ullAvailPhys * 0.90);
        size_t chunkSize = 100 * 1024 * 1024; size_t total = 0;
        std::vector<char*> blocks;
        while (g_RamStress && total < safeBytes) {
            try { char* block = new char[chunkSize]; memset(block, 0xAA, chunkSize); blocks.push_back(block); total += chunkSize; Sleep(20); }
            catch (...) { break; }
        }
        while (g_RamStress) { for (char* b : blocks) { if (!g_RamStress) break; volatile char x = b[0]; b[0] = ~x; } Sleep(10); }
        for (char* b : blocks) delete[] b;
        }).detach();
}

void StartGpuStress() {
    g_GpuStress = true;
    std::thread([]() {
        WNDCLASSW wc = { 0 }; wc.lpfnWndProc = DefWindowProc; wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"GpuStress"; wc.style = CS_OWNDC; RegisterClassW(&wc);
        HWND h = CreateWindowW(L"GpuStress", L"", WS_POPUP, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
        HDC hdc = GetDC(h); PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
        int pf = ChoosePixelFormat(hdc, &pfd); SetPixelFormat(hdc, pf, &pfd);
        HGLRC ctx = wglCreateContext(hdc); wglMakeCurrent(hdc, ctx);
        GLuint list = glGenLists(1); glNewList(list, GL_COMPILE); glBegin(GL_TRIANGLES); glVertex3f(-1, -1, 0); glVertex3f(1, -1, 0); glVertex3f(0, 1, 0); glEnd(); glEndList();
        MSG msg;
        while (g_GpuStress) {
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&msg);
            glClear(GL_COLOR_BUFFER_BIT); for (int i = 0; i < 50; i++) { glLoadIdentity(); glRotatef((float)i, 0, 0, 1); glCallList(list); } SwapBuffers(hdc);
        }
        wglMakeCurrent(NULL, NULL); wglDeleteContext(ctx); DestroyWindow(h);
        }).detach();
}


//   OVERLAY

void DrawLine(Gdiplus::Graphics* g, const std::wstring& text, Gdiplus::Font* font, float x, float y, Gdiplus::Brush* brush) {
    Gdiplus::PointF origin(x, y);
    g->DrawString(text.c_str(), -1, (const Gdiplus::Font*)font, origin, (const Gdiplus::Brush*)brush);
}

void DrawInfo(Gdiplus::Graphics* g, Gdiplus::Font* fHead, Gdiplus::Font* fDet, Gdiplus::Font* fSm, Gdiplus::Brush* bBg, Gdiplus::Brush* bTxt, Gdiplus::Brush* bOn, Gdiplus::Brush* bOff) {
    int localTotalCpu, localRamUsage, localGlobThreads, localCtx, localAppThreads, localPhys, localLog;
    std::wstring localRamInfo;
    std::vector<int> localCoreLoads;

    {
        std::lock_guard<std::mutex> lock(g_StatsMutex);
        localTotalCpu = g_TotalCpuUsage; localRamUsage = g_RamUsage; localRamInfo = g_RamInfo;
        localCoreLoads = g_CoreUsages; localGlobThreads = g_GlobalThreadCount;
        localCtx = g_ContextSwitches; localAppThreads = g_AppThreadCount;
        localPhys = g_PhysicalCores; localLog = g_LogicalProcs;
    }

    int coreRows = (int)ceil(localCoreLoads.size() / 4.0);
    int baseH = 280; int coreH = coreRows * 20;

    g->FillRectangle(bBg, 20, 20, 320, baseH + coreH);

    float y = 30.0f;
    DrawLine(g, L"HARDWARE MONITOR", fHead, 30.0f, y, bTxt); y += 30.0f;

    DrawLine(g, cpuName, fSm, 30.0f, y, bTxt); y += 20.0f;
    std::wstring s = L"Cores: " + std::to_wstring(localPhys) + L" (Phys)  |  Threads: " + std::to_wstring(localLog) + L" (Log)";
    DrawLine(g, s, fSm, 30.0f, y, bTxt); y += 20.0f;

    s = L"Use: " + std::to_wstring(localTotalCpu) + L"%  |  " + std::to_wstring(cpuClock) + L" MHz";
    DrawLine(g, s, fDet, 30.0f, y, g_CpuStress ? bOn : bTxt); y += 25.0f;

    // CHANGED TEXT to 0.5s
    DrawLine(g, L"--- Logical Processor Load (0.5s) ---", fSm, 30.0f, y, bTxt); y += 15.0f;
    int col = 0; float startX = 30.0f;
    for (size_t i = 0; i < localCoreLoads.size(); i++) {
        s = L"C" + std::to_wstring(i) + L": " + std::to_wstring(localCoreLoads[i]) + L"%";
        Gdiplus::Brush* coreBrush = (localCoreLoads[i] > 80) ? bOn : bTxt;
        DrawLine(g, s, fSm, startX + (col * 70.0f), y, coreBrush);
        col++; if (col >= 4) { col = 0; y += 15.0f; }
    }
    if (col != 0) y += 15.0f; y += 10.0f;

    DrawLine(g, g_CpuStress ? L"[2] STRESS: ON" : L"[2] Stress: Off", fDet, 30.0f, y, g_CpuStress ? bOn : bOff); y += 30.0f;
    s = L"RAM: " + localRamInfo + L" (" + std::to_wstring(localRamUsage) + L"%)";
    DrawLine(g, s, fDet, 30.0f, y, g_RamStress ? bOn : bTxt); y += 20.0f;
    DrawLine(g, g_RamStress ? L"[1] STRESS: ON" : L"[1] Stress: Off", fDet, 30.0f, y, g_RamStress ? bOn : bOff); y += 30.0f;
    DrawLine(g, L"GPU: " + gpuName, fSm, 30.0f, y, bTxt); y += 15.0f;
    DrawLine(g, g_GpuStress ? L"[3] STRESS: ON" : L"[3] Stress: Off", fDet, 30.0f, y, g_GpuStress ? bOn : bOff); y += 30.0f;

    DrawLine(g, L"--- System WMI Stats ---", fSm, 30.0f, y, bTxt); y += 15.0f;
    DrawLine(g, L"Global Threads: " + std::to_wstring(localGlobThreads) + L"  |  Ctx/s: " + std::to_wstring(localCtx), fSm, 30.0f, y, bTxt);
}


int main() {
    _setmode(_fileno(stdout), _O_U16TEXT);

    std::thread(MonitorThread).detach();
    std::thread(WmiThread).detach();

    Gdiplus::GdiplusStartupInput gsi; ULONG_PTR tok;
    Gdiplus::GdiplusStartup(&tok, &gsi, NULL);

    WNDCLASSW wc = { 0 }; wc.lpfnWndProc = DefWindowProc; wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"Ovl"; RegisterClassW(&wc);
    SCREEN_W = GetSystemMetrics(SM_CXSCREEN); SCREEN_H = GetSystemMetrics(SM_CYSCREEN);
    hOverlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        L"Ovl", L"", WS_POPUP, 0, 0, SCREEN_W, SCREEN_H, 0, 0, wc.hInstance, 0);
    ShowWindow(hOverlay, SW_SHOWNOACTIVATE);

    HDC hdcScreen = GetDC(NULL); HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBm = CreateCompatibleBitmap(hdcScreen, SCREEN_W, SCREEN_H);
    SelectObject(hdcMem, hBm);
    Gdiplus::Graphics g(hdcMem); g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    Gdiplus::FontFamily sans(L"Segoe UI");
    Gdiplus::Font fHead(&sans, 14, Gdiplus::FontStyleBold);
    Gdiplus::Font fDet(&sans, 11, Gdiplus::FontStyleRegular);
    Gdiplus::Font fSm(&sans, 9, Gdiplus::FontStyleRegular);

    Gdiplus::SolidBrush bBg(Gdiplus::Color(220, 10, 10, 10));
    Gdiplus::SolidBrush bTxt(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush bOn(Gdiplus::Color(255, 255, 50, 50));
    Gdiplus::SolidBrush bOff(Gdiplus::Color(255, 0, 255, 0));

    std::wcout << L"=== OVERLAY ACTIVE ===\n";
    std::wcout << L"Monitor: 500ms (PDH), 500ms (WMI), 16ms (GUI)\n";

    while (g_AppRunning) {
        if (GetAsyncKeyState(VK_END) & 0x8000) { g_AppRunning = false; break; }
        if (GetAsyncKeyState('1') & 0x8000) { g_RamStress = !g_RamStress; if (g_RamStress) StartRamStress(); Sleep(200); }
        if (GetAsyncKeyState('2') & 0x8000) { g_CpuStress = !g_CpuStress; if (g_CpuStress) StartCpuStress(); Sleep(200); }
        if (GetAsyncKeyState('3') & 0x8000) { g_GpuStress = !g_GpuStress; if (g_GpuStress) StartGpuStress(); Sleep(200); }

        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        DrawInfo(&g, &fHead, &fDet, &fSm, &bBg, &bTxt, &bOn, &bOff);

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        POINT ptSrc = { 0,0 }; SIZE sz = { SCREEN_W, SCREEN_H };
        UpdateLayeredWindow(hOverlay, hdcScreen, &ptSrc, &sz, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);
        Sleep(16);
    }
    return 0;
} 