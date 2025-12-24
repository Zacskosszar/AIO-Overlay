#include "shared.h"
#include <comdef.h>
#include <Wbemidl.h>

std::wstring g_MoboName = L"Mobo: Detecting...";
std::wstring g_BiosWmi = L"BIOS: Detecting...";
int g_GlobalThreads = 0;
int g_ContextSwitches = 0;

int GetIntFromVariant(VARIANT* v) {
    if (v->vt == VT_I4) return v->intVal;
    if (v->vt == VT_UI4) return (int)v->uintVal;
    if (v->vt == VT_BSTR) return _wtoi(v->bstrVal);
    return 0;
}

std::wstring GetStrFromVariant(VARIANT* v) {
    if (v->vt == VT_BSTR) return std::wstring(v->bstrVal);
    return L"Unknown";
}

void MonitorSystem() {
    // Separate WMI instance for CIMV2 (Different from WMI root in cpu.cpp)
    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    IWbemLocator *pLoc = NULL;
    CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID *)&pLoc);
    IWbemServices *pSvc = NULL;
    
    if (pLoc) {
        pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
        if (pSvc) {
            CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

            IEnumWbemClassObject* pEnum = NULL; IWbemClassObject *pObj = NULL; ULONG uRet = 0;

            // Mobo
            pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT Product FROM Win32_BaseBoard"), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);
            if (pEnum) {
                pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
                if (uRet) {
                    VARIANT v; pObj->Get(L"Product", 0, &v, 0, 0);
                    { std::lock_guard<std::mutex> l(g_StatsMutex); g_MoboName = GetStrFromVariant(&v); }
                    VariantClear(&v); pObj->Release();
                }
                pEnum->Release();
            }

            // BIOS
            pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT SMBIOSBIOSVersion FROM Win32_BIOS"), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);
            if (pEnum) {
                pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
                if (uRet) {
                    VARIANT v; pObj->Get(L"SMBIOSBIOSVersion", 0, &v, 0, 0);
                    { std::lock_guard<std::mutex> l(g_StatsMutex); g_BiosWmi = GetStrFromVariant(&v); }
                    VariantClear(&v); pObj->Release();
                }
                pEnum->Release();
            }

            while (g_AppRunning) {
                pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT Threads, ContextSwitchesPerSec FROM Win32_PerfFormattedData_PerfOS_System"), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);
                if (pEnum) {
                    pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
                    if (uRet) {
                        VARIANT v; 
                        pObj->Get(L"Threads", 0, &v, 0, 0); int t = GetIntFromVariant(&v); VariantClear(&v);
                        pObj->Get(L"ContextSwitchesPerSec", 0, &v, 0, 0); int c = GetIntFromVariant(&v); VariantClear(&v);

                        { std::lock_guard<std::mutex> l(g_StatsMutex); g_GlobalThreads = t; g_ContextSwitches = c; }
                        pObj->Release();
                    }
                    pEnum->Release();
                }
                Sleep(500);
            }
            pSvc->Release();
        }
        pLoc->Release();
    }
    CoUninitialize();
}