#include "shared.hpp"
#include "ChipDefs.hpp"
#include <fstream>
#include <chrono>
#include <thread>
#include <iomanip>

// Global Definitions
std::wstring g_MoboName = L"Detecting...";
std::wstring g_BiosWmi = L"...";
int g_GlobalThreads = 0;
int g_ContextSwitches = 0;

std::wstring g_UpgradePath = L"";
std::wstring g_BiosAnalysis = L"";
std::wstring g_AgesaVersion = L"";

bool g_HasBattery = false;
int g_BatteryPct = 0;
std::wstring g_BatteryTime = L"";

// Detailed Sensors
int g_DetectedChipID = 0;
int g_DebugID = 0;
float g_Volt12V = 0.0f;
float g_Volt5V = 0.0f;
float g_VoltVCore = 0.0f;
float g_VoltDram = 0.0f;
float g_VoltSoC = 0.0f;
int g_TempVRM = 0;
int g_TempPCH = 0;
int g_TempSocket = 0;
int g_TempSystem = 0;
int g_FanRPM = 0;

// Fan Control State
int g_FanSpeedPct = 50;
int g_AppliedFanSpeed = -1;
bool g_FanControlActive = false;
int g_SioPort = 0;
int g_SioBaseAddr = 0;

// Logging
bool g_LoggingEnabled = false;
std::wstring g_LogPath = L"stats_log.csv";
std::thread g_LogThread;

// InpOut32 Driver Pointers
typedef void(__stdcall* lpOut32)(short, short);
typedef short(__stdcall* lpInp32)(short);
lpOut32 g_Out32 = NULL;
lpInp32 g_Inp32 = NULL;
HINSTANCE g_hInpOutDll = NULL;

std::mutex g_IoMutex;

// ---------------------------------------------------------
//  NCT6687D EC ACCESS
// ---------------------------------------------------------
int ReadNct6687_EC(int baseAddr, int logicalAddress) {
    if (!g_Out32 || !g_Inp32 || baseAddr == 0) return 0;
    std::lock_guard<std::mutex> lock(g_IoMutex);

    int pagePort = baseAddr + 0x04;
    int indexPort = baseAddr + 0x05;
    int dataPort = baseAddr + 0x06;

    // Spin wait for access
    int timeout = 1000;
    while ((g_Inp32(pagePort) & 0xFF) != 0xFF && timeout > 0) {
        for (int i = 0; i < 100; i++) _mm_pause();
        timeout--;
    }
    if (timeout <= 0) g_Out32(pagePort, 0xFF); // Force

    g_Out32(pagePort, (logicalAddress >> 8) & 0xFF);
    g_Out32(indexPort, logicalAddress & 0xFF);
    int result = g_Inp32(dataPort) & 0xFF;
    g_Out32(pagePort, 0xFF);

    return result;
}

void WriteNct6687_EC(int baseAddr, int logicalAddress, int value) {
    if (!g_Out32 || !g_Inp32 || baseAddr == 0) return;
    std::lock_guard<std::mutex> lock(g_IoMutex);

    int pagePort = baseAddr + 0x04;
    int indexPort = baseAddr + 0x05;
    int dataPort = baseAddr + 0x06;

    int timeout = 1000;
    while ((g_Inp32(pagePort) & 0xFF) != 0xFF && timeout > 0) {
        for (int i = 0; i < 100; i++) _mm_pause();
        timeout--;
    }
    if (timeout <= 0) g_Out32(pagePort, 0xFF);

    g_Out32(pagePort, (logicalAddress >> 8) & 0xFF);
    g_Out32(indexPort, logicalAddress & 0xFF);
    g_Out32(dataPort, value & 0xFF);
    g_Out32(pagePort, 0xFF);
}

float ReadNct6687_Temp(int baseAddr, int reg) {
    int val = ReadNct6687_EC(baseAddr, reg);
    int frac = ReadNct6687_EC(baseAddr, reg + 1);
    return (float)val + ((frac & 0x80) ? 0.5f : 0.0f);
}

float ReadNct6687_Voltage(int baseAddr, int reg, float multiplier) {
    int high = ReadNct6687_EC(baseAddr, reg);
    int low = ReadNct6687_EC(baseAddr, reg + 1);
    float val = 0.001f * ((high << 4) | (low >> 4));
    return val * multiplier;
}

int ReadNct6687_Fan(int baseAddr, int reg) {
    int high = ReadNct6687_EC(baseAddr, reg);
    int low = ReadNct6687_EC(baseAddr, reg + 1);
    return (high << 8) | low;
}

// ---------------------------------------------------------
//  FAN CONTROL WRITING
// ---------------------------------------------------------
void ApplyFanSpeedInternal(int pct) {
    if (g_SioBaseAddr == 0) return;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    int pwm = (int)(pct * 2.55f);

    // Fans 0-5
    for (int i = 0; i < 6; i++) {
        int cmdReg = 0xA28 + i;
        int modeReg = 0xA00;
        int reqReg = 0xA01;

        WriteNct6687_EC(g_SioBaseAddr, reqReg, 0x80); // Request
        Sleep(20);

        int curMode = ReadNct6687_EC(g_SioBaseAddr, modeReg);
        if ((curMode & (1 << i)) == 0) {
            WriteNct6687_EC(g_SioBaseAddr, modeReg, curMode | (1 << i)); // Manual Mode
        }

        WriteNct6687_EC(g_SioBaseAddr, cmdReg, pwm); // PWM
        WriteNct6687_EC(g_SioBaseAddr, reqReg, 0x40); // Commit
        Sleep(20);
    }
}

void SetFanSpeed(int pct) {
    g_FanSpeedPct = pct;
}

// ---------------------------------------------------------
//  HARDWARE DETECTION (WITH UNLOCK FIX)
// ---------------------------------------------------------
void DetectHardware() {
    if (g_DetectedChipID != 0) return;

    std::lock_guard<std::mutex> lock(g_IoMutex);

    int ports[] = { 0x4E, 0x2E };
    for (int port : ports) {
        if (g_Out32) {
            g_Out32(port, 0x87); g_Out32(port, 0x87); // Enter Config

            g_Out32(port, 0x20); int high = g_Inp32(port + 1);
            g_Out32(port, 0x21); int low = g_Inp32(port + 1);
            int id = (high << 8) | low;

            if (id != 0 && id != 0xFFFF) g_DebugID = id;

            if (id == CHIP_NCT6687D || id == CHIP_NCT6687D_R) {
                g_DetectedChipID = id;
                g_SioPort = port;

                // --- KEY FIX: UNLOCK IO SPACE ---
                // Write to 0x28 to clear bit 4 (Lock Bit)
                g_Out32(port, 0x28);
                int options = g_Inp32(port + 1);
                if ((options & 0x10) > 0) {
                    g_Out32(port + 1, options & ~0x10);
                }

                // Select HWM (0x0B)
                g_Out32(port, 0x07); g_Out32(port + 1, 0x0B);

                // Force Active
                g_Out32(port, 0x30);
                if ((g_Inp32(port + 1) & 0x01) == 0) g_Out32(port + 1, 0x01);

                // Get Base Address
                g_Out32(port, 0x60); int h = g_Inp32(port + 1);
                g_Out32(port, 0x61); int l = g_Inp32(port + 1);
                g_SioBaseAddr = ((h << 8) | l) & 0xFFF8;

                g_Out32(port, 0xAA); // Exit Config
                return;
            }
            g_Out32(port, 0xAA); // Exit Config
        }
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
            DetectHardware();
            return true;
        }
    }
    return false;
}

void UpdateBattery() {
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        std::lock_guard<std::mutex> l(g_StatsMutex);
        g_HasBattery = (sps.BatteryFlag != 128 && sps.BatteryFlag != 255);
        if (g_HasBattery) {
            g_BatteryPct = sps.BatteryLifePercent;
            if (sps.BatteryLifeTime != -1 && sps.ACLineStatus == 0) {
                wchar_t buf[32]; swprintf_s(buf, L"%dh %02dm", sps.BatteryLifeTime / 3600, (sps.BatteryLifeTime % 3600) / 60);
                g_BatteryTime = buf;
            }
            else {
                g_BatteryTime = (sps.ACLineStatus == 1) ? L"Charging" : L"...";
            }
        }
    }
}

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
    InitFanControl();
    WmiQuery wmi; wmi.Init();

    IEnumWbemClassObject* pEnum = wmi.Exec(L"SELECT Product FROM Win32_BaseBoard");
    if (pEnum) {
        IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
        pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
        if (uRet) {
            std::lock_guard<std::mutex> l(g_StatsMutex);
            g_MoboName = GetVariantString(pObj, L"Product");
            if (g_DetectedChipID != 0) {
                std::wstringstream ss;
                ss << L" (NCT" << std::hex << (g_DetectedChipID >> 4) << L" @ " << g_SioBaseAddr << L")";
                g_MoboName += ss.str();
            }
            else {
                std::wstringstream ss; ss << L" (Scanning... ID:" << std::hex << g_DebugID << L")";
                g_MoboName += ss.str();
            }
            pObj->Release();
        }
        pEnum->Release();
    }

    while (g_AppRunning) {
        if ((g_DetectedChipID == CHIP_NCT6687D || g_DetectedChipID == CHIP_NCT6687D_R) && g_SioBaseAddr > 0) {
            float tCpu = ReadNct6687_Temp(g_SioBaseAddr, 0x100);
            float tSys = ReadNct6687_Temp(g_SioBaseAddr, 0x102);
            float tMos = ReadNct6687_Temp(g_SioBaseAddr, 0x104);
            float tPch = ReadNct6687_Temp(g_SioBaseAddr, 0x106);
            float tSoc = ReadNct6687_Temp(g_SioBaseAddr, 0x108);

            float v12 = ReadNct6687_Voltage(g_SioBaseAddr, 0x120, 12.0f);
            float v5 = ReadNct6687_Voltage(g_SioBaseAddr, 0x122, 5.0f);
            float vCore = ReadNct6687_Voltage(g_SioBaseAddr, 0x124, 1.0f);
            float vDram = ReadNct6687_Voltage(g_SioBaseAddr, 0x128, 2.0f);
            float vSoc = ReadNct6687_Voltage(g_SioBaseAddr, 0x12C, 1.0f);

            int rpm = ReadNct6687_Fan(g_SioBaseAddr, 0x140);

            {
                std::lock_guard<std::mutex> l(g_StatsMutex);
                if (tCpu > 0 && tCpu < 115) g_CpuTemp = (int)tCpu;
                g_TempSystem = (int)tSys;
                g_TempVRM = (int)tMos;
                g_TempPCH = (int)tPch;
                g_TempSocket = (int)tSoc;

                g_Volt12V = v12;
                g_Volt5V = v5;
                g_VoltVCore = vCore;
                g_VoltDram = vDram;
                g_VoltSoC = vSoc;
                g_FanRPM = rpm;
            }

            // Fan Control (Background Write)
            if (g_FanSpeedPct != g_AppliedFanSpeed) {
                ApplyFanSpeedInternal(g_FanSpeedPct);
                g_AppliedFanSpeed = g_FanSpeedPct;
            }
        }

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