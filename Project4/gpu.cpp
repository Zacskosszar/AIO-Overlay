#include "shared.h"
#include <wbemidl.h>
#include <comdef.h>

#pragma comment(lib, "wbemuuid.lib")

// WMI Helper
std::wstring GetGpuWmi(const std::wstring& prop) {
    std::wstring result = L"Unknown";
    HRESULT hres;
    IWbemLocator* pLoc = NULL;
    IWbemServices* pSvc = NULL;
    CoInitializeEx(0, COINIT_MULTITHREADED);
    CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (!pLoc) return result;

    pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (pSvc) {
        CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
        IEnumWbemClassObject* pEnumerator = NULL;
        pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT Name FROM Win32_VideoController"), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnumerator);
        if (pEnumerator) {
            IWbemClassObject* pclsObj = NULL;
            ULONG uReturn = 0;
            pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
            if (uReturn != 0) {
                VARIANT vtProp;
                pclsObj->Get(prop.c_str(), 0, &vtProp, 0, 0);
                if (vtProp.vt == VT_BSTR) result = vtProp.bstrVal;
                VariantClear(&vtProp);
                pclsObj->Release();
            }
            pEnumerator->Release();
        }
        pSvc->Release();
    }
    pLoc->Release();
    return result;
}

void InitGpuInfo() {
    std::wstring name = GetGpuWmi(L"Name");
    std::lock_guard<std::mutex> lock(g_StatsMutex);
    g_GpuName = name;
}

void StartGpuStress() {
    // Basic busy loop for GPU thread simulation
    while (g_GpuStress && g_AppRunning) {
        Sleep(10);
    }
}