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

// Globals
extern std::atomic<bool> g_AppRunning;
extern std::atomic<bool> g_CpuStress;
extern std::atomic<bool> g_GpuStress;
extern std::atomic<bool> g_RamStress;
extern std::mutex g_StatsMutex;

// Stats
extern int g_CpuUsage;
extern std::vector<int> g_CoreLoad;
extern int g_CpuTemp;
extern int g_RamLoad;
extern std::wstring g_RamText;
extern std::wstring g_CpuName;
extern std::wstring g_GpuName;
extern std::wstring g_MoboName;
extern std::wstring g_BiosWmi;
extern std::wstring g_BiosAnalysis;
extern std::wstring g_AgesaVersion;
extern std::wstring g_UpgradePath;
extern int g_GlobalThreads;
extern int g_ContextSwitches;
extern std::vector<std::wstring> g_DriveInfo;

// Logging & Fan
extern bool g_LoggingEnabled;
extern std::wstring g_LogPath;
extern int g_FanSpeedPct;
extern bool g_FanControlActive;

// Functions
void StartCpuStress();
void MonitorCpu();
void StartGpuStress(); // Now OpenGL based
void InitGpuInfo();
void StartRamStress();
void CheckStorage();
void MonitorSystem();

// Logging
void StartLogging();
void StopLogging();

// Fan Control (InpOut32)
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
    bool initialized = false;
};