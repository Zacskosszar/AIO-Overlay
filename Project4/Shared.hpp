#pragma once
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <wbemidl.h>
#include <comdef.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "wbemuuid.lib") 

// App State
extern std::atomic<bool> g_AppRunning;
extern std::mutex g_StatsMutex;
extern std::mutex g_IoMutex;

// Stress & Bench
extern std::atomic<bool> g_CpuStress;
extern std::atomic<bool> g_GpuStress;
extern std::atomic<bool> g_RamStress;
extern std::atomic<int> g_BenchScore;
extern std::atomic<bool> g_BenchRunning;
extern std::atomic<int> g_BenchProgress;
extern std::wstring g_BenchMode;
extern std::atomic<int> g_GpuScore;
extern std::atomic<bool> g_GpuBenchRunning;

// Hardware Stats
extern int g_CpuUsage;
extern std::vector<int> g_CoreLoad;
extern int g_CpuTemp;
extern std::wstring g_CpuName;

extern int g_RamLoad;
extern std::wstring g_RamText;

extern std::wstring g_GpuName;
extern unsigned long long g_GpuVramUsed;
extern unsigned long long g_GpuVramTotal;

extern std::wstring g_MoboName;
extern std::wstring g_BiosWmi;
extern std::wstring g_UpgradePath;
extern std::wstring g_BiosAnalysis;
extern std::wstring g_AgesaVersion;

extern int g_GlobalThreads;
extern int g_ContextSwitches;

extern std::vector<std::wstring> g_DriveInfo;

// Motherboard Sensors
extern int g_DetectedChipID;
extern int g_DebugID;
extern float g_Volt12V;
extern float g_Volt5V;
extern float g_VoltVCore;
extern float g_VoltDram;
extern float g_VoltSoC;
extern int g_TempVRM;
extern int g_TempPCH;
extern int g_TempSocket;
extern int g_TempSystem;

// Fan
extern int g_FanSpeedPct;
extern int g_FanRPM;
extern bool g_FanControlActive;

// Battery
extern bool g_HasBattery;
extern int g_BatteryPct;
extern std::wstring g_BatteryTime;

// Config
extern bool g_LoggingEnabled;
extern std::wstring g_LogPath;

// Functions
void StartBenchmark(bool multiCore);
void StartGpuBenchmark();
void StartCpuStress();
void StartGpuStress();
void StartRamStress();
void MonitorCpu();
void MonitorSystem();
void InitGpuInfo();
void UpdateGpuVram();
void GetDetailedRamInfo();
void InitDiskPdh();
void UpdateDiskIo();
void CheckStorage();
void UpdateBattery();

// SIO
bool InitFanControl();
void SetFanSpeed(int pct);

class WmiQuery {
public:
    WmiQuery();
    ~WmiQuery();
    bool Init(const std::wstring& namesSpace = L"ROOT\\CIMV2");
    IEnumWbemClassObject* Exec(const std::wstring& query);
private:
    IWbemLocator* pLoc = nullptr;
    IWbemServices* pSvc = nullptr;
};