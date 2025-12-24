#include "shared.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <immintrin.h>
#include <fstream>
#include <filesystem>
#include <comdef.h>
#include <Wbemidl.h>

namespace fs = std::filesystem;

// BIOS Pattern Logic (Same as before)
struct BytePattern { std::string name; std::vector<int> bytes; int offset; };
std::vector<BytePattern> g_Patterns = {
    {"Vermeer (5xxx)", {0x38,0x00,0xFF,0xFF,0xFF,0xFF,0x00}, -2},
    {"Raphael (7xxx)", {0x01,0x10,0x01,0x81,0x00,0x00,0x00,0x00,0x00,-1,0x4C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 8},
    {"Granite (9xxx)", {0x01,0x10,0x01,0x81,0x00,0x00,0x00,0x00,0x00,-1,0x62,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 8}
};
std::vector<int> g_AgesaHead = { 0x3D,0x9B,0x25,0x70,0x41,0x47,0x45,0x53,0x41 };

size_t FindPattern(const std::vector<unsigned char>& d, const std::vector<int>& p) {
    if (d.size() < p.size()) return std::string::npos;
    for (size_t i = 0; i <= d.size() - p.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < p.size(); ++j) {
            if (p[j] != -1 && d[i + j] != p[j]) { match = false; break; }
        }
        if (match) return i;
    }
    return std::string::npos;
}

void ScanBios() {
    std::wstring fileP = L"";
    for (const auto& e : fs::directory_iterator(".")) {
        auto ext = e.path().extension();
        if (ext == ".bin" || ext == ".rom" || ext == ".cap") { fileP = e.path().wstring(); break; }
    }
    if (fileP.empty()) return;

    std::ifstream f(fileP, std::ios::binary | std::ios::ate);
    if (!f) return;
    std::streamsize sz = f.tellg();
    f.seekg(0);
    std::vector<unsigned char> buf(sz);
    f.read((char*)buf.data(), sz);

    std::wstringstream rpt;
    rpt << L"BIOS: " << fs::path(fileP).filename().wstring() << L"\n";

    size_t ag = FindPattern(buf, g_AgesaHead);
    if (ag != std::string::npos && ag + 20 < buf.size()) {
        char* str = (char*)&buf[ag + 13];
        if (strlen(str) > 3) g_AgesaVersion = std::wstring(str, str + strlen(str));
    }

    for (auto& pat : g_Patterns) {
        size_t idx = FindPattern(buf, pat.bytes);
        if (idx != std::string::npos) {
            int verOffset = idx + pat.offset;
            if (verOffset + 2 < buf.size()) {
                rpt << std::wstring(pat.name.begin(), pat.name.end()) << L" Supported\n";
                if (pat.name.find("Granite") != std::string::npos) g_UpgradePath = L"Max: Ryzen 9000";
                else if (pat.name.find("Raphael") != std::string::npos && g_UpgradePath.find(L"9000") == std::string::npos) g_UpgradePath = L"Max: Ryzen 7000";
            }
        }
    }
    g_BiosAnalysis = rpt.str();
}

void CpuStressTask(int) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    __m256 a = _mm256_set1_ps(1.1f), b = _mm256_set1_ps(2.2f);
    while (g_CpuStress) { for (int i = 0; i < 1000; i++) a = _mm256_add_ps(a, b); }
}

void StartCpuStress() {
    g_CpuStress = true;
    for (int i = 0; i < std::thread::hardware_concurrency(); i++) std::thread(CpuStressTask, i).detach();
}

// Helper for WMI Temp
int GetWmiTemp(IWbemServices* pSvc) {
    IEnumWbemClassObject* pEnum = NULL;
    // Try standard ThermalZone (works on Laptops/OEMs)
    HRESULT hres = pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);
    if (FAILED(hres) || !pEnum) return 0;

    IWbemClassObject* pObj = NULL; ULONG uRet = 0;
    pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
    int kelvin = 0;
    if (uRet) {
        VARIANT v; pObj->Get(L"CurrentTemperature", 0, &v, 0, 0);
        if (v.vt == VT_I4 || v.vt == VT_UI4) kelvin = v.intVal;
        VariantClear(&v); pObj->Release();
    }
    pEnum->Release();

    if (kelvin > 2732) return (kelvin - 2732) / 10; // 0.1K units
    return 0;
}

void MonitorCpu() {
    ScanBios();

    // CPU Name
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == 0) {
        wchar_t buf[256]; DWORD sz = sizeof(buf);
        RegQueryValueExW(hKey, L"ProcessorNameString", NULL, NULL, (LPBYTE)buf, &sz);
        g_CpuName = buf;
        RegCloseKey(hKey);
    }

    // Setup WMI for Temp
    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    IWbemServices* pSvc = NULL;
    IWbemLocator* pLoc = NULL;
    bool wmiOk = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc))) {
        if (SUCCEEDED(pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), NULL, NULL, 0, NULL, 0, 0, &pSvc))) {
            CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
            wmiOk = true;
        }
    }

    // Setup PDH (Total + Per Core)
    PDH_HQUERY q; PdhOpenQueryW(NULL, 0, &q);
    PDH_HCOUNTER cTotal;
    PdhAddEnglishCounterW(q, L"\\Processor(_Total)\\% Processor Time", 0, &cTotal);

    SYSTEM_INFO sys; GetSystemInfo(&sys);
    int coreCount = sys.dwNumberOfProcessors;
    std::vector<PDH_HCOUNTER> cCores(coreCount);

    for (int i = 0; i < coreCount; i++) {
        std::wstring path = L"\\Processor(" + std::to_wstring(i) + L")\\% Processor Time";
        PdhAddEnglishCounterW(q, path.c_str(), 0, &cCores[i]);
    }

    PdhCollectQueryData(q);

    while (g_AppRunning) {
        PdhCollectQueryData(q);

        // Total
        PDH_FMT_COUNTERVALUE cv;
        PdhGetFormattedCounterValue(cTotal, PDH_FMT_LONG, NULL, &cv);
        int total = (int)cv.longValue;

        // Per Core
        std::vector<int> loads;
        for (int i = 0; i < coreCount; i++) {
            PdhGetFormattedCounterValue(cCores[i], PDH_FMT_LONG, NULL, &cv);
            loads.push_back((int)cv.longValue);
        }

        int temp = wmiOk ? GetWmiTemp(pSvc) : 0;

        {
            std::lock_guard<std::mutex> l(g_StatsMutex);
            g_CpuUsage = total;
            g_CoreLoad = loads; // Sync vector
            g_CpuTemp = temp;
        }
        Sleep(500);
    }
    if (wmiOk) { pSvc->Release(); pLoc->Release(); }
}