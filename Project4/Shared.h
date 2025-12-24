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

// --- LIBRARY LINKS ---
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "wbemuuid.lib") 

// --- GLOBAL DECLARATIONS (Externs) ---
// These are "Promises" that the variables exist in main.cpp
extern std::atomic<bool> g_AppRunning;
extern std::atomic<bool> g_CpuStress;
extern std::atomic<bool> g_GpuStress;
extern std::atomic<bool> g_RamStress;
extern std::mutex g_StatsMutex;

// Data Variables
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

// --- FUNCTION PROTOTYPES ---
void StartCpuStress();
void MonitorCpu();
void StartGpuStress();
void InitGpuInfo();
void StartRamStress();
void CheckStorage();
void MonitorSystem();