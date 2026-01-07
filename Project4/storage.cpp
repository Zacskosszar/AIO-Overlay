#include "shared.hpp"
#include <pdh.h>

// --- DEFINITIONS ---
std::vector<std::wstring> g_DriveInfo;

PDH_HQUERY g_PdhQuery = NULL;
PDH_HCOUNTER g_Read = NULL, g_Write = NULL;

void InitDiskPdh() {
    PdhOpenQuery(NULL, 0, &g_PdhQuery);
    PdhAddEnglishCounter(g_PdhQuery, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &g_Read);
    PdhAddEnglishCounter(g_PdhQuery, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &g_Write);
    PdhCollectQueryData(g_PdhQuery);
}

void UpdateDiskIo() {
    if (!g_PdhQuery) return;
    PdhCollectQueryData(g_PdhQuery);
}

void CheckStorage() {
    while (g_AppRunning) {
        DWORD mask = GetLogicalDrives();
        std::vector<std::wstring> drvs;
        for (wchar_t c = 'A'; c <= 'Z'; c++) {
            if (mask & 1) {
                std::wstring r = L" :\\"; r[0] = c;
                drvs.push_back(r);
            }
            mask >>= 1;
        }
        { std::lock_guard<std::mutex> l(g_StatsMutex); g_DriveInfo = drvs; }
        Sleep(5000);
    }
}