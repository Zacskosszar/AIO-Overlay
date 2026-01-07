#include "shared.hpp"

// --- DEFINITIONS ---
int g_RamLoad = 0;
std::wstring g_RamText = L"RAM";

void GetDetailedRamInfo() {
    WmiQuery wmi; wmi.Init();
    IEnumWbemClassObject* pEnum = wmi.Exec(L"SELECT Capacity, Speed FROM Win32_PhysicalMemory");
    if (pEnum) pEnum->Release();
}

void StartRamStress() {
    g_RamStress = true;
    std::thread([]() {
        std::vector<int*> ptrs;
        while (g_RamStress && g_AppRunning) {
            try { ptrs.push_back(new int[1000000]); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
            catch (...) { break; }
        }
        for (auto p : ptrs) delete[] p;
        g_RamStress = false;
        }).detach();
}