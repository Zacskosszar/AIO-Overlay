#include "shared.h"

void StartRamStress() {
    g_RamStress = true;
    std::thread([] {
        while (g_RamStress) {
            std::vector<char*> p;
            while (g_RamStress && p.size() < 200) {
                try {
                    char* b = new char[100 * 1024 * 1024];
                    memset(b, 0, 100 * 1024 * 1024);
                    p.push_back(b);
                    Sleep(50);
                }
                catch (...) { break; }
            }
            Sleep(1000);
            for (auto ptr : p) delete[] ptr;
            p.clear();
        }
        }).detach();
}