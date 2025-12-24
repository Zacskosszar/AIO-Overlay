#include "shared.h"
#include <gdiplus.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <windowsx.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <regex>
#include <wbemidl.h>
#include <comdef.h> 
#include <pdh.h>
#include <dxgi1_4.h>
#include <powrprof.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

// --- DESIGN CONSTANTS ---
const int UI_WIDTH_NORMAL = 550;
const int UI_WIDTH_MINI = 220;
const int CORNER_RADIUS = 18;
const int BTN_HEIGHT = 30; // Height of stress buttons

// --- CONFIGURATION STRUCT ---
struct AppConfig {
    bool showCpu = true;
    bool showCores = true;
    bool showRam = true;
    bool showRamDetail = true;
    bool showGpu = true;
    bool showVram = true;
    bool showDrives = true;
    bool showDiskIo = true;
    bool showBios = true;
    bool showUptime = true;
    bool showBattery = true;
    bool miniMode = false;
    int refreshRateMs = 30;
    int opacity = 230; // Slightly less transparent for buttons
    int xOffset = 30;
    int yOffset = 30;
};

// --- DEFINITIONS ---
AppConfig g_Cfg;
std::atomic<bool> g_AppRunning(true);
std::atomic<bool> g_CpuStress(false);
std::atomic<bool> g_RamStress(false);
std::atomic<bool> g_GpuStress(false); // Added for button
std::mutex g_StatsMutex;
HWND g_hOverlay = NULL;
HWND g_hSettings = NULL;

// Local Stats
int g_CpuUsage = 0;
int g_CpuTemp = 0;
std::vector<int> g_CoreLoad;
int g_RamLoad = 0;
std::wstring g_RamText = L"Detecting...";
std::wstring g_CpuName = L"Processing...";
std::wstring g_GpuName = L"Processing...";
std::wstring g_BiosDate = L""; 
std::vector<std::wstring> g_RamModules; 
bool g_DualChannelActive = false; 
int g_FPS = 0;

// NEW STATS
unsigned long long g_GpuVramUsed = 0;
unsigned long long g_GpuVramTotal = 0;
double g_DiskReadMB = 0.0;
double g_DiskWriteMB = 0.0;
bool g_HasBattery = false;
int g_BatteryPct = 0;
bool g_BatteryCharging = false;
std::wstring g_BatteryTime = L"";

// Shared Globals
bool g_LoggingEnabled = false;
std::vector<std::wstring> g_DriveInfo; 
std::wstring g_UpgradePath = L"";
std::wstring g_BiosAnalysis = L"";
std::wstring g_AgesaVersion = L"";

// External
extern std::wstring g_MoboName; 
extern std::wstring g_BiosWmi;  
extern int g_GlobalThreads;    
extern int g_ContextSwitches;

// --- FORWARD DECLARATIONS ---
void SaveSettings();
void LoadSettings();
void AppendLog();
void MonitorCpu();    
void MonitorSystem(); 
void InitGpuInfo();   
void StartRamStress();
void StartCpuStress();
void StartGpuStress(); // Ensure this exists in gpu.cpp
void OpenSettingsWindow(HINSTANCE hInst);

// --- CONTROL IDs ---
#define ID_CHK_CPU     101
#define ID_CHK_CORES   102
#define ID_CHK_RAM     103
#define ID_CHK_RAMDTL  104
#define ID_CHK_GPU     105
#define ID_CHK_VRAM    106
#define ID_CHK_DRIVES  107
#define ID_CHK_DISKIO  108
#define ID_CHK_BIOS    109
#define ID_CHK_UPTIME  110
#define ID_CHK_BATTERY 111

// --- WMI HELPER ---
void GetDetailedRamInfo() {
    HRESULT hres;
    IWbemLocator *pLoc = NULL;
    IWbemServices *pSvc = NULL;
    CoInitializeEx(0, COINIT_MULTITHREADED);
    CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID *)&pLoc);
    if (!pLoc) { CoUninitialize(); return; }
    pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (pSvc) {
        CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
        IEnumWbemClassObject* pEnumerator = NULL;
        pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT DeviceLocator, BankLabel, Capacity, Speed, Manufacturer, PartNumber FROM Win32_PhysicalMemory"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
        if (pEnumerator) {
            IWbemClassObject *pclsObj = NULL;
            ULONG uReturn = 0;
            std::vector<std::wstring> modules;
            int stickCount = 0;
            while (pEnumerator) {
                HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                if (0 == uReturn) break;
                stickCount++;
                VARIANT vtCap, vtSpeed, vtMan, vtBank, vtPart, vtLoc; 
                pclsObj->Get(L"Capacity", 0, &vtCap, 0, 0); pclsObj->Get(L"Speed", 0, &vtSpeed, 0, 0);
                pclsObj->Get(L"Manufacturer", 0, &vtMan, 0, 0); pclsObj->Get(L"BankLabel", 0, &vtBank, 0, 0);
                pclsObj->Get(L"PartNumber", 0, &vtPart, 0, 0); pclsObj->Get(L"DeviceLocator", 0, &vtLoc, 0, 0);
                unsigned long long capGB = 0; if (vtCap.vt == VT_BSTR) capGB = _wtoll(vtCap.bstrVal) / (1024*1024*1024);
                std::wstring devLoc = (vtLoc.vt == VT_BSTR) ? vtLoc.bstrVal : L"N/A";
                std::wstring bankLbl = (vtBank.vt == VT_BSTR) ? vtBank.bstrVal : L"";
                int mts = (vtSpeed.vt == VT_I4) ? vtSpeed.intVal : 0;
                int mhz = mts / 2; 
                wchar_t buf[512];
                swprintf_s(buf, L"[%s%s] %s %s %d MT/s", devLoc.c_str(), (bankLbl.empty() ? L"" : (L"/" + bankLbl).c_str()), 
                    (vtMan.vt == VT_BSTR) ? vtMan.bstrVal : L"Unknown", (vtPart.vt == VT_BSTR) ? vtPart.bstrVal : L"", mts);
                modules.push_back(buf);
                pclsObj->Release();
            }
            { std::lock_guard<std::mutex> l(g_StatsMutex); g_RamModules = modules; g_DualChannelActive = (stickCount % 2 == 0 && stickCount > 0); }
            pEnumerator->Release();
        }
        pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT ReleaseDate FROM Win32_BIOS"), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnumerator);
        if (pEnumerator) {
            IWbemClassObject *pclsObj = NULL;
            ULONG uReturn = 0;
            pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
            if (uReturn) {
                VARIANT vtDate; pclsObj->Get(L"ReleaseDate", 0, &vtDate, 0, 0);
                if (vtDate.vt == VT_BSTR) {
                    std::wstring d(vtDate.bstrVal);
                    if (d.length() >= 8) {
                        std::wstring fmt = d.substr(0, 4) + L"-" + d.substr(4, 2) + L"-" + d.substr(6, 2);
                        std::lock_guard<std::mutex> l(g_StatsMutex); g_BiosDate = fmt;
                    }
                }
                pclsObj->Release();
            }
            pEnumerator->Release();
        }
        pSvc->Release();
    }
    pLoc->Release();
    CoUninitialize();
}

// --- PDH/DXGI/BATTERY HELPERS ---
PDH_HQUERY g_PdhQuery = NULL;
PDH_HCOUNTER g_PdhReadCounter = NULL;
PDH_HCOUNTER g_PdhWriteCounter = NULL;

void InitDiskPdh() {
    PdhOpenQuery(NULL, 0, &g_PdhQuery);
    PdhAddEnglishCounter(g_PdhQuery, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &g_PdhReadCounter);
    PdhAddEnglishCounter(g_PdhQuery, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &g_PdhWriteCounter);
    PdhCollectQueryData(g_PdhQuery);
}
void UpdateDiskIo() {
    if (!g_PdhQuery) return;
    PdhCollectQueryData(g_PdhQuery);
    PDH_FMT_COUNTERVALUE r, w;
    PdhGetFormattedCounterValue(g_PdhReadCounter, PDH_FMT_DOUBLE, NULL, &r);
    PdhGetFormattedCounterValue(g_PdhWriteCounter, PDH_FMT_DOUBLE, NULL, &w);
    std::lock_guard<std::mutex> l(g_StatsMutex);
    g_DiskReadMB = r.doubleValue / 1048576.0; g_DiskWriteMB = w.doubleValue / 1048576.0;
}
void UpdateGpuVram() {
    IDXGIFactory* pFactory = NULL; if (CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory) != S_OK) return;
    IDXGIAdapter* pAdapter = NULL; if (pFactory->EnumAdapters(0, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC desc; pAdapter->GetDesc(&desc);
        IDXGIAdapter3* pAdapter3 = NULL; pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&pAdapter3);
        if (pAdapter3) {
            DXGI_QUERY_VIDEO_MEMORY_INFO info;
            if (pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info) == S_OK) {
                std::lock_guard<std::mutex> l(g_StatsMutex); g_GpuVramTotal = info.Budget; g_GpuVramUsed = info.CurrentUsage;
            }
            pAdapter3->Release();
        } else {
            std::lock_guard<std::mutex> l(g_StatsMutex); g_GpuVramTotal = desc.DedicatedVideoMemory;
        }
        pAdapter->Release();
    }
    pFactory->Release();
}
void UpdateBattery() {
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        std::lock_guard<std::mutex> l(g_StatsMutex);
        g_HasBattery = (sps.BatteryFlag != 128 && sps.BatteryFlag != 255);
        if (g_HasBattery) {
            g_BatteryPct = sps.BatteryLifePercent; g_BatteryCharging = (sps.ACLineStatus == 1);
            if (sps.BatteryLifeTime != -1 && !g_BatteryCharging) {
                wchar_t buf[32]; swprintf_s(buf, L"%dh %02dm left", sps.BatteryLifeTime/3600, (sps.BatteryLifeTime%3600)/60); g_BatteryTime = buf;
            } else g_BatteryTime = g_BatteryCharging ? L"Charging" : L"Calculating...";
        }
    }
}

// --- SETTINGS (JSON) ---
void SaveSettings() {
    std::wofstream file(L"settings.json");
    if (file.is_open()) {
        file << L"{\n" << L"  \"showCpu\": " << (g_Cfg.showCpu ? L"true" : L"false") << L",\n"
             << L"  \"showCores\": " << (g_Cfg.showCores ? L"true" : L"false") << L",\n"
             << L"  \"showRam\": " << (g_Cfg.showRam ? L"true" : L"false") << L",\n"
             << L"  \"showRamDetail\": " << (g_Cfg.showRamDetail ? L"true" : L"false") << L",\n"
             << L"  \"showGpu\": " << (g_Cfg.showGpu ? L"true" : L"false") << L",\n"
             << L"  \"showVram\": " << (g_Cfg.showVram ? L"true" : L"false") << L",\n"
             << L"  \"showDrives\": " << (g_Cfg.showDrives ? L"true" : L"false") << L",\n"
             << L"  \"showDiskIo\": " << (g_Cfg.showDiskIo ? L"true" : L"false") << L",\n"
             << L"  \"showBios\": " << (g_Cfg.showBios ? L"true" : L"false") << L",\n"
             << L"  \"showUptime\": " << (g_Cfg.showUptime ? L"true" : L"false") << L",\n"
             << L"  \"showBattery\": " << (g_Cfg.showBattery ? L"true" : L"false") << L",\n"
             << L"  \"miniMode\": " << (g_Cfg.miniMode ? L"true" : L"false") << L"\n" << L"}" << std::endl;
    }
}
void LoadSettings() {
    std::wstring path = L"settings.json"; std::wifstream file(path); if (!file.good()) { SaveSettings(); return; }
    std::wstring line; while (std::getline(file, line)) {
        if (line.find(L"showCpu") != std::wstring::npos) g_Cfg.showCpu = (line.find(L"true") != std::wstring::npos);
        if (line.find(L"showCores") != std::wstring::npos) g_Cfg.showCores = (line.find(L"true") != std::wstring::npos);
        if (line.find(L"showRamDetail") != std::wstring::npos) g_Cfg.showRamDetail = (line.find(L"true") != std::wstring::npos);
        if (line.find(L"showVram") != std::wstring::npos) g_Cfg.showVram = (line.find(L"true") != std::wstring::npos);
        if (line.find(L"showDiskIo") != std::wstring::npos) g_Cfg.showDiskIo = (line.find(L"true") != std::wstring::npos);
        if (line.find(L"showBattery") != std::wstring::npos) g_Cfg.showBattery = (line.find(L"true") != std::wstring::npos);
        if (line.find(L"miniMode") != std::wstring::npos) g_Cfg.miniMode = (line.find(L"true") != std::wstring::npos);
    }
}

// --- SETTINGS WINDOW LOGIC ---
LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateWindowW(L"BUTTON", L"Show CPU & Temp", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 20, 200, 25, hwnd, (HMENU)ID_CHK_CPU, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Show Threads", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 50, 200, 25, hwnd, (HMENU)ID_CHK_CORES, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Show RAM Usage", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 80, 200, 25, hwnd, (HMENU)ID_CHK_RAM, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Show RAM Details", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 110, 200, 25, hwnd, (HMENU)ID_CHK_RAMDTL, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Show GPU Name", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 140, 200, 25, hwnd, (HMENU)ID_CHK_GPU, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Show VRAM", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 170, 200, 25, hwnd, (HMENU)ID_CHK_VRAM, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Show Drives", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 200, 200, 25, hwnd, (HMENU)ID_CHK_DRIVES, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Show Disk Speed", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 230, 200, 25, hwnd, (HMENU)ID_CHK_DISKIO, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Show BIOS Info", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 260, 200, 25, hwnd, (HMENU)ID_CHK_BIOS, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Show Uptime", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 290, 200, 25, hwnd, (HMENU)ID_CHK_UPTIME, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Show Battery", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, 320, 200, 25, hwnd, (HMENU)ID_CHK_BATTERY, NULL, NULL);
        CheckDlgButton(hwnd, ID_CHK_CPU, g_Cfg.showCpu); CheckDlgButton(hwnd, ID_CHK_CORES, g_Cfg.showCores);
        CheckDlgButton(hwnd, ID_CHK_RAM, g_Cfg.showRam); CheckDlgButton(hwnd, ID_CHK_RAMDTL, g_Cfg.showRamDetail);
        CheckDlgButton(hwnd, ID_CHK_GPU, g_Cfg.showGpu); CheckDlgButton(hwnd, ID_CHK_VRAM, g_Cfg.showVram);
        CheckDlgButton(hwnd, ID_CHK_DRIVES, g_Cfg.showDrives); CheckDlgButton(hwnd, ID_CHK_DISKIO, g_Cfg.showDiskIo);
        CheckDlgButton(hwnd, ID_CHK_BIOS, g_Cfg.showBios); CheckDlgButton(hwnd, ID_CHK_UPTIME, g_Cfg.showUptime);
        CheckDlgButton(hwnd, ID_CHK_BATTERY, g_Cfg.showBattery);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wParam); bool val = IsDlgButtonChecked(hwnd, id);
        switch(id) {
            case ID_CHK_CPU: g_Cfg.showCpu = val; break; case ID_CHK_CORES: g_Cfg.showCores = val; break;
            case ID_CHK_RAM: g_Cfg.showRam = val; break; case ID_CHK_RAMDTL: g_Cfg.showRamDetail = val; break;
            case ID_CHK_GPU: g_Cfg.showGpu = val; break; case ID_CHK_VRAM: g_Cfg.showVram = val; break;
            case ID_CHK_DRIVES: g_Cfg.showDrives = val; break; case ID_CHK_DISKIO: g_Cfg.showDiskIo = val; break;
            case ID_CHK_BIOS: g_Cfg.showBios = val; break; case ID_CHK_UPTIME: g_Cfg.showUptime = val; break;
            case ID_CHK_BATTERY: g_Cfg.showBattery = val; break;
        }
        SaveSettings(); return 0;
    }
    case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
void OpenSettingsWindow(HINSTANCE hInst) {
    if (g_hSettings) { ShowWindow(g_hSettings, SW_SHOW); SetForegroundWindow(g_hSettings); return; }
    WNDCLASSW wc = {0}; wc.lpfnWndProc = SettingsWndProc; wc.hInstance = hInst; wc.lpszClassName = L"CfgMenu"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    RegisterClassW(&wc);
    g_hSettings = CreateWindowW(L"CfgMenu", L"Overlay Config", WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, 100, 100, 250, 400, NULL, NULL, hInst, NULL);
    ShowWindow(g_hSettings, SW_SHOW);
}

// --- RENDERING HELPERS ---
void DrawRoundedRect(Gdiplus::Graphics* g, Gdiplus::Brush* fillBrush, Gdiplus::Pen* borderPen, int x, int y, int w, int h, int r) {
    Gdiplus::GraphicsPath path; path.AddArc(x, y, r, r, 180, 90); path.AddArc(x + w - r, y, r, r, 270, 90);
    path.AddArc(x + w - r, y + h - r, r, r, 0, 90); path.AddArc(x, y + h - r, r, r, 90, 90); path.CloseFigure();
    if (fillBrush) g->FillPath(fillBrush, &path); if (borderPen) g->DrawPath(borderPen, &path);
}
void DrawPillBar(Gdiplus::Graphics* g, float x, float y, float w, float h, float pct, Gdiplus::Brush* bFill, Gdiplus::Brush* bBg) {
    DrawRoundedRect(g, bBg, NULL, (int)x, (int)y, (int)w, (int)h, (int)h);
    float fillW = w * pct; if (fillW < h) fillW = h; if (fillW > w) fillW = w;
    DrawRoundedRect(g, bFill, NULL, (int)x, (int)y, (int)fillW, (int)h, (int)h);
}
void DrawStr(Gdiplus::Graphics* g, const WCHAR* s, Gdiplus::Font* f, float x, float y, Gdiplus::Brush* b) {
    g->DrawString(s, -1, f, Gdiplus::PointF(x, y), b);
}
void DrawButton(Gdiplus::Graphics* g, const WCHAR* s, float x, float y, float w, float h, bool active, Gdiplus::Font* f) {
    Gdiplus::SolidBrush bActive(Gdiplus::Color(200, 255, 69, 58)); // Red for Active Stress
    Gdiplus::SolidBrush bIdle(Gdiplus::Color(80, 80, 80, 80));     // Gray for Idle
    Gdiplus::SolidBrush bText(Gdiplus::Color(255, 255, 255, 255));
    DrawRoundedRect(g, active ? &bActive : &bIdle, NULL, (int)x, (int)y, (int)w, (int)h, 10);
    // Center Text
    Gdiplus::RectF rect(x, y + 5, w, h);
    Gdiplus::StringFormat format; format.SetAlignment(Gdiplus::StringAlignmentCenter);
    g->DrawString(s, -1, f, rect, &format, &bText);
}

std::wstring GetUptime() {
    ULONGLONG ticks = GetTickCount64(); ULONGLONG hours = ticks / 3600000; ULONGLONG minutes = (ticks / 60000) % 60;
    wchar_t buf[64]; swprintf_s(buf, L"%llu hours, %llu mins", hours, minutes); return std::wstring(buf);
}
float ParseDriveUsage(const std::wstring& s) {
    try {
        std::wregex num_regex(L"(\\d+)"); auto words_begin = std::wsregex_iterator(s.begin(), s.end(), num_regex);
        long long used = 0, total = 0; int c=0;
        for (auto i = words_begin; i != std::wsregex_iterator(); ++i) { if (c==0) used = std::stoll(i->str()); if (c==1) total = std::stoll(i->str()); c++; }
        if (total > 0) return (float)used / (float)total;
    } catch (...) {} return 0.0f;
}
void AppendLog() {
    if (!g_LoggingEnabled) return;
    std::wofstream logFile; logFile.open(L"flight_recorder.log", std::ios_base::app);
    if (logFile.is_open()) {
        auto now = std::time(nullptr); struct tm timeInfo; localtime_s(&timeInfo, &now);
        logFile << L"[" << std::put_time(&timeInfo, L"%H:%M:%S") << L"] CPU:" << g_CpuUsage << L"%" << std::endl;
    }
}

// --- MAIN UI DRAW ---
void DrawAppleUI(Gdiplus::Graphics* g, int w, int h) {
    Gdiplus::SolidBrush bBg(Gdiplus::Color(g_Cfg.opacity, 20, 20, 22)); 
    Gdiplus::Pen pBorder(Gdiplus::Color(50, 255, 255, 255), 1);
    DrawRoundedRect(g, &bBg, &pBorder, 0, 0, w, h, CORNER_RADIUS);

    Gdiplus::SolidBrush bWhite(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush bGray(Gdiplus::Color(150, 142, 142, 147)); 
    Gdiplus::SolidBrush bBlue(Gdiplus::Color(255, 10, 132, 255));  
    Gdiplus::SolidBrush bRed(Gdiplus::Color(255, 255, 69, 58));
    Gdiplus::SolidBrush bYellow(Gdiplus::Color(255, 255, 204, 0));
    Gdiplus::SolidBrush bGreen(Gdiplus::Color(255, 46, 204, 113)); 
    Gdiplus::SolidBrush bTrack(Gdiplus::Color(50, 255, 255, 255)); 

    Gdiplus::Font fHeader(L"Segoe UI", 11, Gdiplus::FontStyleBold); 
    Gdiplus::Font fBody(L"Segoe UI", 9, Gdiplus::FontStyleRegular);
    Gdiplus::Font fSmall(L"Segoe UI", 8, Gdiplus::FontStyleRegular);
    Gdiplus::Font fIcon(L"Segoe UI Symbol", 12, Gdiplus::FontStyleRegular);

    // Header Traffic Lights
    // Red (Close), Yellow (Mini), Gear (Config)
    DrawRoundedRect(g, &bRed, NULL, 20, 15, 14, 14, 7);    // Close
    DrawRoundedRect(g, &bYellow, NULL, 40, 15, 14, 14, 7); // Mini
    DrawStr(g, L"\u2699", &fIcon, 60, 10, &bGray);         // Gear

    // Mini Mode
    if (g_Cfg.miniMode) {
        std::lock_guard<std::mutex> l(g_StatsMutex);
        wchar_t buf[128]; swprintf_s(buf, L"CPU %d%%", g_CpuUsage);
        DrawStr(g, buf, &fHeader, 90, 12, &bWhite);
        swprintf_s(buf, L"%d\u00B0C", g_CpuTemp); DrawStr(g, buf, &fBody, 160, 14, &bGray);
        return;
    }

    float y = 40.0f; float x = 25.0f; float contentW = w - 50.0f;
    DrawStr(g, L"Control Center", &fHeader, x, 12, &bWhite);
    
    std::lock_guard<std::mutex> l(g_StatsMutex);
    wchar_t buf[512];

    if (g_Cfg.showUptime) {
        swprintf_s(buf, L"Up for %s", GetUptime().c_str()); DrawStr(g, buf, &fSmall, x, y, &bGray); y += 20.0f;
    }

    // CPU + Threads
    if (g_Cfg.showCpu) {
        DrawStr(g, g_CpuName.c_str(), &fBody, x, y, &bWhite); y += 18.0f;
        DrawPillBar(g, x, y, contentW, 8, g_CpuUsage / 100.0f, (g_CpuTemp > 85) ? &bRed : &bBlue, &bTrack); y += 12.0f;
        swprintf_s(buf, L"%d%% Load  \u2022  %d\u00B0C Temp  \u2022  %d Thr  \u2022  %d Ctx/s", g_CpuUsage, g_CpuTemp, g_GlobalThreads, g_ContextSwitches);
        DrawStr(g, buf, &fSmall, x, y, &bGray); y += 20.0f;
    }
    if (g_Cfg.showCores && !g_CoreLoad.empty()) {
        float startX = x; int col = 0; int maxCols = 4;
        for (size_t i = 0; i < g_CoreLoad.size(); i++) {
            float coreX = startX + (col * (contentW / maxCols));
            swprintf_s(buf, L"C%zu", i); DrawStr(g, buf, &fSmall, coreX, y, &bGray);
            float pct = g_CoreLoad[i] / 100.0f;
            DrawPillBar(g, coreX + 25, y + 4, (contentW/maxCols)-35, 4, pct, (pct > 0.8f) ? &bRed : &bBlue, &bTrack);
            col++; if (col >= maxCols) { col = 0; y += 15.0f; }
        }
        if (col != 0) y += 15.0f; y += 5.0f;
    }

    // GPU + VRAM
    if (g_Cfg.showGpu) {
        DrawStr(g, g_GpuName.c_str(), &fBody, x, y, &bWhite); 
        if (g_Cfg.showVram && g_GpuVramTotal > 0) {
            float vramPct = (float)g_GpuVramUsed / (float)g_GpuVramTotal;
            DrawPillBar(g, x + 200, y + 4, contentW - 200, 6, vramPct, &bBlue, &bTrack);
            swprintf_s(buf, L"VRAM: %llu / %llu MB", g_GpuVramUsed / (1024*1024), g_GpuVramTotal / (1024*1024));
            y += 12.0f; DrawStr(g, buf, &fSmall, x, y, &bGray);
        }
        y += 20.0f;
    }

    // Disk I/O
    if (g_Cfg.showDiskIo) {
        swprintf_s(buf, L"Disk Activity: Read %.1f MB/s  \u2022  Write %.1f MB/s", g_DiskReadMB, g_DiskWriteMB);
        DrawStr(g, buf, &fSmall, x, y, (g_DiskWriteMB > 50.0) ? &bRed : &bGray); y += 20.0f;
    }

    // Battery
    if (g_Cfg.showBattery && g_HasBattery) {
        swprintf_s(buf, L"Battery: %d%% (%s)", g_BatteryPct, g_BatteryTime.c_str());
        DrawStr(g, buf, &fBody, x, y, (g_BatteryCharging) ? &bGreen : (g_BatteryPct < 20 ? &bRed : &bWhite));
        DrawPillBar(g, x + 200, y + 4, contentW - 200, 6, g_BatteryPct / 100.0f, (g_BatteryPct < 20) ? &bRed : &bGreen, &bTrack); y += 20.0f;
    }

    // RAM
    if (g_Cfg.showRam) {
        DrawStr(g, L"Memory", &fBody, x, y, &bWhite); y += 18.0f;
        float ramPct = g_RamLoad / 100.0f;
        DrawPillBar(g, x, y, contentW, 8, ramPct, &bBlue, &bTrack);
        y += 12.0f;
        if (g_DualChannelActive) { DrawStr(g, g_RamText.c_str(), &fSmall, x, y, &bGray); DrawStr(g, L"[DUAL CHANNEL]", &fSmall, x + 180, y, &bGreen); }
        else { DrawStr(g, g_RamText.c_str(), &fSmall, x, y, &bGray); }
        y += 15.0f;
        if (g_Cfg.showRamDetail && !g_RamModules.empty()) {
            for (const auto& mod : g_RamModules) { DrawStr(g, mod.c_str(), &fSmall, x + 10, y, &bGray); y += 14.0f; }
            y += 5.0f;
        }
    }

    // Drives
    if (g_Cfg.showDrives) {
        DrawStr(g, L"Storage", &fBody, x, y, &bWhite); y += 18.0f;
        for (const auto& drv : g_DriveInfo) {
            float usage = ParseDriveUsage(drv);
            DrawPillBar(g, x, y, contentW, 6, usage, (usage > 0.9f) ? &bRed : &bBlue, &bTrack);
            y += 8.0f; DrawStr(g, drv.c_str(), &fSmall, x, y, &bGray); y += 18.0f;
        }
    }

    // BIOS
    if (g_Cfg.showBios) {
        DrawStr(g, L"Hardware Identity", &fBody, x, y, &bWhite); y += 18.0f;
        DrawStr(g, g_MoboName.c_str(), &fSmall, x, y, &bGray); y += 14.0f;
        swprintf_s(buf, L"BIOS: %s (%s)", g_BiosWmi.c_str(), g_BiosDate.c_str()); DrawStr(g, buf, &fSmall, x, y, &bGray); y += 20.0f;
    }

    // --- STRESS BUTTONS (Footer) ---
    float btnW = (contentW - 20) / 3;
    float btnY = h - 45;
    DrawButton(g, L"CPU STRESS", x, btnY, btnW, BTN_HEIGHT, g_CpuStress, &fBody);
    DrawButton(g, L"RAM STRESS", x + btnW + 10, btnY, btnW, BTN_HEIGHT, g_RamStress, &fBody);
    DrawButton(g, L"GPU STRESS", x + (btnW * 2) + 20, btnY, btnW, BTN_HEIGHT, g_GpuStress, &fBody);
}

// --- WNDPROC ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam); int y = GET_Y_LPARAM(lParam); 
        int w = g_Cfg.miniMode ? UI_WIDTH_MINI : UI_WIDTH_NORMAL;
        
        // Header Clicks (Traffic Lights)
        if (y < 40) {
            // Close (Red, approx 20-34px)
            if (x >= 20 && x <= 34) { PostQuitMessage(0); return 0; }
            // Mini (Yellow, approx 40-54px)
            if (x >= 40 && x <= 54) { g_Cfg.miniMode = !g_Cfg.miniMode; SaveSettings(); return 0; }
            // Config (Gear, approx 60-80px)
            if (x >= 60 && x <= 80) { OpenSettingsWindow(GetModuleHandle(NULL)); return 0; }
        }

        // Footer Clicks (Stress Buttons) - Only in Normal Mode
        if (!g_Cfg.miniMode) {
            RECT rect; GetClientRect(hwnd, &rect);
            int h = rect.bottom;
            int btnY = h - 45;
            if (y >= btnY && y <= btnY + BTN_HEIGHT) {
                float contentW = w - 50.0f;
                float btnW = (contentW - 20) / 3;
                float startX = 25.0f; // matches UI x
                
                // CPU Button
                if (x >= startX && x <= startX + btnW) { 
                    g_CpuStress = !g_CpuStress; 
                    if(g_CpuStress) StartCpuStress(); 
                    return 0; 
                }
                // RAM Button
                if (x >= startX + btnW + 10 && x <= startX + (btnW * 2) + 10) { 
                    g_RamStress = !g_RamStress; 
                    if(g_RamStress) StartRamStress(); 
                    return 0; 
                }
                // GPU Button
                if (x >= startX + (btnW * 2) + 20 && x <= startX + (btnW * 3) + 20) { 
                    g_GpuStress = !g_GpuStress; 
                    if(g_GpuStress) StartGpuStress(); 
                    return 0; 
                }
            }
        }

        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int main() {
    _setmode(_fileno(stdout), _O_U16TEXT);
    LoadSettings();
    InitDiskPdh(); 

    std::thread(MonitorCpu).detach();
    std::thread(MonitorSystem).detach();
    std::thread([](){ GetDetailedRamInfo(); }).detach();

    Gdiplus::GdiplusStartupInput gsi; ULONG_PTR tok; Gdiplus::GdiplusStartup(&tok, &gsi, NULL);
    int w = UI_WIDTH_NORMAL; int h = 850; int x = GetSystemMetrics(SM_CXSCREEN) - w - g_Cfg.xOffset;
    WNDCLASSW wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandle(NULL); wc.lpszClassName = L"AppleOverlay"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); RegisterClassW(&wc);
    g_hOverlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, L"AppleOverlay", L"", WS_POPUP | WS_VISIBLE, x, g_Cfg.yOffset, w, h, 0, 0, wc.hInstance, 0);
    HDC sc = GetDC(NULL); HDC memDC = CreateCompatibleDC(sc); HBITMAP memBM = CreateCompatibleBitmap(sc, w, h); SelectObject(memDC, memBM);
    Gdiplus::Graphics g(memDC); g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    while (g_AppRunning) {
        MSG msg; while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); if (msg.message == WM_QUIT) g_AppRunning=false; }
        if (GetAsyncKeyState(VK_END) & 0x8000) break;

        MEMORYSTATUSEX m; m.dwLength = sizeof(m); GlobalMemoryStatusEx(&m);
        { 
            std::lock_guard<std::mutex> l(g_StatsMutex); g_RamLoad = m.dwMemoryLoad; 
            double usedGB = (m.ullTotalPhys - m.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
            double totalGB = m.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
            wchar_t buf[64]; swprintf_s(buf, L"%.1f/%.1f GB", usedGB, totalGB); g_RamText = buf;
        }

        UpdateDiskIo(); UpdateGpuVram(); UpdateBattery();

        int curW = g_Cfg.miniMode ? UI_WIDTH_MINI : UI_WIDTH_NORMAL; int curH = g_Cfg.miniMode ? 70 : 850; 
        g.Clear(Gdiplus::Color(0,0,0,0)); DrawAppleUI(&g, curW, curH);
        POINT pSrc = {0,0}; SIZE sSize = {curW, curH}; POINT pPos = {x, g_Cfg.yOffset};
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(g_hOverlay, sc, &pPos, &sSize, memDC, &pSrc, 0, &bf, ULW_ALPHA);
        Sleep(30);
    }
    DeleteObject(memBM); DeleteDC(memDC); ReleaseDC(NULL, sc); Gdiplus::GdiplusShutdown(tok); return 0;
}