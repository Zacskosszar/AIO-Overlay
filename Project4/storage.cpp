#include "shared.hpp"
#include <winioctl.h>

#pragma pack(push, 1)
struct SMART_ATTRIBUTE { BYTE Id; WORD Status; BYTE Value; BYTE Worst; BYTE Raw[6]; };
struct SMART_DATA { WORD Version; SMART_ATTRIBUTE Attributes[30]; BYTE Padding[100]; };

struct My_SENDCMDINPARAMS {
    DWORD cBufferSize; IDEREGS irDriveRegs; BYTE bDriveNumber; BYTE bReserved[3]; DWORD dwReserved[4]; BYTE bBuffer[1];
};
struct My_SENDCMDOUTPARAMS {
    DWORD cBufferSize; DRIVERSTATUS DriverStatus; BYTE bBuffer[1];
};
#pragma pack(pop)

#define SMART_RCV_DRIVE_DATA 0x0007c088
#define SMART_CMD 0xB0
#define READ_ATTRIBUTES 0xD0

void CheckStorage() {
    std::vector<std::wstring> drives;
    for (int i = 0; i < 4; ++i) {
        std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(i);
        HANDLE hDevice = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDevice != INVALID_HANDLE_VALUE) {
            My_SENDCMDINPARAMS in = { 0 };
            DWORD br = 0;
            in.cBufferSize = sizeof(My_SENDCMDOUTPARAMS) + 512 - 1;
            in.bDriveNumber = i;
            in.irDriveRegs.bFeaturesReg = READ_ATTRIBUTES;
            in.irDriveRegs.bSectorCountReg = 1;
            in.irDriveRegs.bSectorNumberReg = 1;
            in.irDriveRegs.bCylLowReg = SMART_CMD;
            in.irDriveRegs.bCylHighReg = SMART_CMD >> 8;
            in.irDriveRegs.bCommandReg = SMART_CMD;

            BYTE buffer[sizeof(My_SENDCMDOUTPARAMS) + 512];
            if (DeviceIoControl(hDevice, SMART_RCV_DRIVE_DATA, &in, sizeof(My_SENDCMDINPARAMS) - 1, buffer, sizeof(buffer), &br, NULL)) {
                My_SENDCMDOUTPARAMS* pOut = (My_SENDCMDOUTPARAMS*)buffer;
                SMART_DATA* smart = (SMART_DATA*)pOut->bBuffer;
                int temp = 0; int hours = 0; bool foundTemp = false;
                for (int k = 0; k < 30; ++k) {
                    if (smart->Attributes[k].Id == 194) { temp = smart->Attributes[k].Raw[0]; foundTemp = true; }
                    if (smart->Attributes[k].Id == 9) hours = *(int*)smart->Attributes[k].Raw;
                }
                std::wstring status = L"Disk " + std::to_wstring(i) + L": " + (foundTemp ? std::to_wstring(temp) + L"C" : L"?C") + L" (" + std::to_wstring(hours) + L"h)";
                drives.push_back(status);
            }
            else {
                drives.push_back(L"Disk " + std::to_wstring(i) + L": Active (No SMART)");
            }
            CloseHandle(hDevice);
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_StatsMutex);
        g_DriveInfo = drives;
        if (g_DriveInfo.empty()) g_DriveInfo.push_back(L"No Drives Found (Run as Admin)");
    }
}