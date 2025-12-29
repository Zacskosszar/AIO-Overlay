#include "shared.hpp"
#include "ChipDefs.hpp" // Make sure you have the file I gave you earlier!
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
int g_DetectedChipID = 0;
int g_SioPort = 0;

// InpOut32 typedefs
typedef void(__stdcall* lpOut32)(short, short);
typedef short(__stdcall* lpInp32)(short);
lpOut32 g_Out32 = NULL;
lpInp32 g_Inp32 = NULL;
HINSTANCE g_hInpOutDll = NULL;

// Forward Declarations
void DetectHardware();

// ---------------------------------------------------------
//  HELPER: Translate Hex ID to Human Name
// ---------------------------------------------------------
std::wstring GetChipName(int id) {
    switch (id) {
        // --- ITE (Gigabyte) ---
    case CHIP_IT8620E: return L"ITE IT8620E";
    case CHIP_IT8628E: return L"ITE IT8628E";
    case CHIP_IT8686E: return L"ITE IT8686E";
    case CHIP_IT8688E: return L"ITE IT8688E";
    case CHIP_IT8728F: return L"ITE IT8728F";
    case CHIP_IT8665E: return L"ITE IT8665E";
    case CHIP_IT8655E: return L"ITE IT8655E";

        // --- Nuvoton (ASUS/ASRock) ---
    case CHIP_NCT6791D: return L"Nuvoton NCT6791D";
    case CHIP_NCT6792D: return L"Nuvoton NCT6792D";
    case CHIP_NCT6793D: return L"Nuvoton NCT6793D";
    case CHIP_NCT6795D: return L"Nuvoton NCT6795D";
    case CHIP_NCT6796D: return L"Nuvoton NCT6796D";
    case CHIP_NCT6797D: return L"Nuvoton NCT6797D";
   
    case CHIP_NCT6687D: return L"Nuvoton NCT6687D";

        // --- Fintek (Older MSI) ---
    case CHIP_F71882:   return L"Fintek F71882";
    case CHIP_F71889:   return L"Fintek F71889";

    default: {
        if (id == 0) return L"None";
        wchar_t buf[32];
        swprintf_s(buf, L"Unknown ID (%04X)", id);
        return buf;
    }
    }
}

// ---------------------------------------------------------
//  HARDWARE DETECTION (Low Level)
// ---------------------------------------------------------
void EnterConfig_ITE(int port) {
    if (!g_Out32) return;
    g_Out32(port, 0x87); g_Out32(port, 0x01); g_Out32(port, 0x55); g_Out32(port, 0x55);
}

void EnterConfig_Nuvoton(int port) {
    if (!g_Out32) return;
    g_Out32(port, 0x87); g_Out32(port, 0x87);
}

void ExitConfig(int port) {
    if (!g_Out32) return;
    g_Out32(port, 0x02); g_Out32(port + 1, 0x02);
}

int ReadChipID(int port) {
    g_Out32(port, 0x20); int high = g_Inp32(port + 1);
    g_Out32(port, 0x21); int low = g_Inp32(port + 1);
    if (high == 0xFF && low == 0xFF) return 0;
    if (high == 0x00 && low == 0x00) return 0;
    return (high << 8) | low;
}

void DetectHardware() {
    if (g_DetectedChipID != 0) return; // Already found

    int ports[] = { 0x2E, 0x4E };
    for (int port : ports) {
        // 1. Try ITE
        EnterConfig_ITE(port);
        int id = ReadChipID(port);
        if (id != 0 && id != 0xFFFF) {
            g_DetectedChipID = id; g_SioPort = port; ExitConfig(port); return;
        }
        ExitConfig(port);

        // 2. Try Nuvoton
        EnterConfig_Nuvoton(port);
        id = ReadChipID(port);
        if (id != 0 && id != 0xFFFF) {
            g_DetectedChipID = id; g_SioPort = port; ExitConfig(port); return;
        }
        ExitConfig(port);
    }
}

bool InitFanControl() {
    if (g_hInpOutDll) return true;

    g_hInpOutDll = LoadLibraryW(L"inpoutx64.dll");
    if (!g_hInpOutDll) g_hInpOutDll = LoadLibraryW(L"inpout32.dll");

    if (g_hInpOutDll) {
        g_Out32 = (lpOut32)GetProcAddress(g_hInpOutDll, "Out32");
        g_Inp32 = (lpInp32)GetProcAddress(g_hInpOutDll, "Inp32");

        if (g_Out32 && g_Inp32) {
            DetectHardware(); // Force detection immediately
            return true;
        }
    }
    return false;
}

void SetFanSpeed(int pct) {
    if (!g_FanControlActive || !g_Out32 || g_DetectedChipID == 0) return;
    int pwm = (int)(pct * 2.55);

    // ITE Strategy
    if ((g_DetectedChipID & 0xFF00) == 0x8600 || (g_DetectedChipID & 0xFF00) == 0x8700) {
        EnterConfig_ITE(g_SioPort);
        g_Out32(g_SioPort, 0x07); g_Out32(g_SioPort + 1, 0x04); // LDN 4
        g_Out32(g_SioPort, 0xF0); g_Out32(g_SioPort + 1, pwm);  // PWM 1
        ExitConfig(g_SioPort);
    }
    // Nuvoton Strategy
    else if ((g_DetectedChipID & 0xFF00) == 0xD400 || (g_DetectedChipID & 0xFF00) == 0xC500) {
        EnterConfig_Nuvoton(g_SioPort);
        g_Out32(g_SioPort, 0x07); g_Out32(g_SioPort + 1, 0x0B); // LDN 0B
        // Nuvoton write usually requires Bank Select (Reg 4E).
        // For safety in this demo, we skip the raw write to avoid crashes if bank is wrong.
        ExitConfig(g_SioPort);
    }
}

// ---------------------------------------------------------
//  SYSTEM MONITORING
// ---------------------------------------------------------
WmiQuery::WmiQuery() { CoInitializeEx(0, COINIT_MULTITHREADED); }
WmiQuery::~WmiQuery() { if (pSvc) pSvc->Release(); if (pLoc) pLoc->Release(); CoUninitialize(); }
bool WmiQuery::Init(const std::wstring& namesSpace) {
    HRESULT hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hres)) return false;
    hres = pLoc->ConnectServer(_bstr_t(namesSpace.c_str()), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hres)) return false;
    hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    return SUCCEEDED(hres);
}
IEnumWbemClassObject* WmiQuery::Exec(const std::wstring& query) {
    if (!pSvc) return nullptr;
    IEnumWbemClassObject* pEnum = nullptr;
    pSvc->ExecQuery(bstr_t("WQL"), bstr_t(query.c_str()), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);
    return pEnum;
}
static std::wstring GetVariantString(IWbemClassObject* pObj, const std::wstring& prop) {
    VARIANT v; pObj->Get(prop.c_str(), 0, &v, 0, 0);
    std::wstring res = (v.vt == VT_BSTR) ? v.bstrVal : L"Unknown";
    VariantClear(&v); return res;
}
static int GetVariantInt(IWbemClassObject* pObj, const std::wstring& prop) {
    VARIANT v; pObj->Get(prop.c_str(), 0, &v, 0, 0);
    int res = (v.vt == VT_I4) ? v.intVal : (v.vt == VT_UI4) ? (int)v.uintVal : 0;
    VariantClear(&v); return res;
}

void MonitorSystem() {
    // [FIX] Force Chip Detection BEFORE fetching the name
    InitFanControl();

    WmiQuery wmi; wmi.Init();

    // 1. Get Motherboard Name
    IEnumWbemClassObject* pEnum = wmi.Exec(L"SELECT Product FROM Win32_BaseBoard");
    if (pEnum) {
        IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
        pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
        if (uRet) {
            std::lock_guard<std::mutex> l(g_StatsMutex);
            g_MoboName = GetVariantString(pObj, L"Product");

            // Append Chip Name
            g_MoboName += L" | " + GetChipName(g_DetectedChipID);

            pObj->Release();
        }
        pEnum->Release();
    }

    // 2. Get BIOS
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
                { std::lock_guard<std::mutex> l(g_StatsMutex); g_GlobalThreads = t; g_ContextSwitches = c; }
                pObj->Release();
            }
            pEnum->Release();
        }
        Sleep(500);
    }
}

// --- Logging (Unchanged) ---
void LogWorker() {
    std::ofstream file(g_LogPath, std::ios::app);
    if (!file.is_open()) return;
    file << "Timestamp,CPU_Usage,CPU_Temp\n";
    while (g_LoggingEnabled && g_AppRunning) {
        {
            std::lock_guard<std::mutex> l(g_StatsMutex);
            auto now = std::chrono::system_clock::now();
            auto timeT = std::chrono::system_clock::to_time_t(now);
            file << timeT << "," << g_CpuUsage << "," << g_CpuTemp << "\n";
        }
        file.flush(); Sleep(1000);
    }
}
void StartLogging() { if (g_LoggingEnabled) return; g_LoggingEnabled = true; g_LogThread = std::thread(LogWorker); g_LogThread.detach(); }
void StopLogging() { g_LoggingEnabled = false; }