#include "shared.h"
#include <fstream>
#include <chrono>

std::wstring g_MoboName = L"Mobo: Detecting...";
std::wstring g_BiosWmi = L"BIOS: Detecting...";
int g_GlobalThreads = 0;
int g_ContextSwitches = 0;

// Logging globals
bool g_LoggingEnabled = false;
std::wstring g_LogPath = L"stats_log.csv";
std::thread g_LogThread;

// Fan globals
int g_FanSpeedPct = 50;
bool g_FanControlActive = false;

// InpOut32 typedefs
typedef void(__stdcall* lpOut32)(short, short);
typedef short(__stdcall* lpInp32)(short);
lpOut32 g_Out32 = NULL;
lpInp32 g_Inp32 = NULL;
HINSTANCE g_hInpOutDll = NULL;

// --- Fan Control (InpOut32 Wrapper) ---
bool InitFanControl() {
    if (g_hInpOutDll) return true;

    // Try to load 64-bit DLL first, then 32-bit
    g_hInpOutDll = LoadLibraryW(L"inpoutx64.dll");
    if (!g_hInpOutDll) g_hInpOutDll = LoadLibraryW(L"inpout32.dll");

    if (g_hInpOutDll) {
        g_Out32 = (lpOut32)GetProcAddress(g_hInpOutDll, "Out32");
        g_Inp32 = (lpInp32)GetProcAddress(g_hInpOutDll, "Inp32");
        return (g_Out32 && g_Inp32);
    }
    return false;
}

// Wrapper for EC write (Typical IT87/Nuvoton logic)
// WARNING: OFFSETS ARE SPECIFIC TO MOTHERBOARD. 
// THIS IS A GENERIC IMPLEMENTATION PATTERN.
void WriteEc(short reg, short val) {
    if (!g_Out32 || !g_Inp32) return;
    // Enter EC Config Mode (Example: 0x2E/0x2F or 0x4E/0x4F)
    g_Out32(0x2E, 0x87);
    g_Out32(0x2E, 0x01);
    g_Out32(0x2E, 0x55);
    g_Out32(0x2E, 0x55);

    // Select Logical Device (Fan Controller often LDN=0x04)
    g_Out32(0x2E, 0x07);
    g_Out32(0x2F, 0x04);

    // Write Value
    g_Out32(0x2E, reg);
    g_Out32(0x2F, val);

    // Exit Config
    g_Out32(0x2E, 0x02);
    g_Out32(0x2F, 0x02);
}

void SetFanSpeed(int pct) {
    if (!g_FanControlActive) return;
    // Example: Writing PWM value to a common register (0xF0 is just a placeholder)
    // Real implementation requires specific datasheet (e.g., IT8686E, NCT6793D)
    int pwm = (int)(pct * 2.55); // 0-255
    WriteEc(0xF0, pwm);
}

// --- Logging System ---
void LogWorker() {
    std::ofstream file(g_LogPath, std::ios::app);
    if (!file.is_open()) return;

    file << "Timestamp,CPU_Usage,CPU_Temp,RAM_Load,GPU_Name\n";

    while (g_LoggingEnabled && g_AppRunning) {
        {
            std::lock_guard<std::mutex> l(g_StatsMutex);
            auto now = std::chrono::system_clock::now();
            auto timeT = std::chrono::system_clock::to_time_t(now);
            file << timeT << "," << g_CpuUsage << "," << g_CpuTemp << "," << g_RamLoad << ",";
            // Simple CSV safe string
            std::string gpu(g_GpuName.begin(), g_GpuName.end());
            file << gpu << "\n";
        }
        file.flush();
        Sleep(1000);
    }
}

void StartLogging() {
    if (g_LoggingEnabled) return;
    g_LoggingEnabled = true;
    g_LogThread = std::thread(LogWorker);
    g_LogThread.detach();
}

void StopLogging() {
    g_LoggingEnabled = false;
}

// --- WMI & System Monitor ---
WmiQuery::WmiQuery() {
    CoInitializeEx(0, COINIT_MULTITHREADED);
}

WmiQuery::~WmiQuery() {
    if (pSvc) pSvc->Release();
    if (pLoc) pLoc->Release();
    CoUninitialize();
}

bool WmiQuery::Init(const std::wstring& namesSpace) {
    HRESULT hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hres)) return false;
    hres = pLoc->ConnectServer(_bstr_t(namesSpace.c_str()), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hres)) return false;
    hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (FAILED(hres)) return false;
    initialized = true;
    return true;
}

IEnumWbemClassObject* WmiQuery::Exec(const std::wstring& query) {
    if (!initialized) return nullptr;
    IEnumWbemClassObject* pEnumerator = nullptr;
    pSvc->ExecQuery(bstr_t("WQL"), bstr_t(query.c_str()), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
    return pEnumerator;
}

static std::wstring GetVariantString(IWbemClassObject* pObj, const std::wstring& prop) {
    VARIANT v; pObj->Get(prop.c_str(), 0, &v, 0, 0);
    std::wstring res = (v.vt == VT_BSTR) ? v.bstrVal : L"Unknown";
    VariantClear(&v); return res;
}

static int GetVariantInt(IWbemClassObject* pObj, const std::wstring& prop) {
    VARIANT v; pObj->Get(prop.c_str(), 0, &v, 0, 0);
    int res = 0;
    if (v.vt == VT_I4) res = v.intVal;
    else if (v.vt == VT_UI4) res = (int)v.uintVal;
    else if (v.vt == VT_BSTR) res = _wtoi(v.bstrVal);
    VariantClear(&v); return res;
}

void MonitorSystem() {
    WmiQuery wmi;
    if (!wmi.Init()) return;

    IEnumWbemClassObject* pEnum = wmi.Exec(L"SELECT Product FROM Win32_BaseBoard");
    if (pEnum) {
        IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
        pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
        if (uRet) {
            std::lock_guard<std::mutex> l(g_StatsMutex);
            g_MoboName = GetVariantString(pObj, L"Product");
            pObj->Release();
        }
        pEnum->Release();
    }

    pEnum = wmi.Exec(L"SELECT SMBIOSBIOSVersion FROM Win32_BIOS");
    if (pEnum) {
        IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
        pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
        if (uRet) {
            std::lock_guard<std::mutex> l(g_StatsMutex);
            g_BiosWmi = GetVariantString(pObj, L"SMBIOSBIOSVersion");
            pObj->Release();
        }
        pEnum->Release();
    }

    while (g_AppRunning) {
        pEnum = wmi.Exec(L"SELECT Threads, ContextSwitchesPerSec FROM Win32_PerfFormattedData_PerfOS_System");
        if (pEnum) {
            IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
            pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
            if (uRet) {
                int t = GetVariantInt(pObj, L"Threads");
                int c = GetVariantInt(pObj, L"ContextSwitchesPerSec");
                {
                    std::lock_guard<std::mutex> l(g_StatsMutex);
                    g_GlobalThreads = t; g_ContextSwitches = c;
                }
                pObj->Release();
            }
            pEnum->Release();
        }
        Sleep(500);
    }
}