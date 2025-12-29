#pragma once

// ==========================================================
//  LIBRE HARDWARE MONITOR - SUPPORTED SUPER I/O CHIPS
// ==========================================================

// --- ITE IT87xx Series (Gigabyte, Some ASUS, Some ASRock) ---
#define CHIP_IT8613E  0x8613
#define CHIP_IT8620E  0x8620
#define CHIP_IT8625E  0x8625
#define CHIP_IT8628E  0x8628 // Common AM4
#define CHIP_IT8631E  0x8631
#define CHIP_IT8637E  0x8637
#define CHIP_IT8655E  0x8655
#define CHIP_IT8665E  0x8665
#define CHIP_IT8686E  0x8686 // Ryzen 3000/5000
#define CHIP_IT8688E  0x8688 // Ryzen 5000/B550
#define CHIP_IT8689E  0x8689
#define CHIP_IT8695E  0x8695
#define CHIP_IT8705F  0x8705
#define CHIP_IT8712F  0x8712
#define CHIP_IT8716F  0x8716
#define CHIP_IT8718F  0x8718
#define CHIP_IT8720F  0x8720
#define CHIP_IT8721F  0x8721
#define CHIP_IT8726F  0x8726
#define CHIP_IT8728F  0x8728
#define CHIP_IT8733E  0x8733
#define CHIP_IT8771E  0x8771
#define CHIP_IT8772E  0x8772
#define CHIP_IT8792E  0x8792

// --- Nuvoton NCTxx Series (ASUS, ASRock, MSI) ---
#define CHIP_NCT6102D 0xC450
#define CHIP_NCT6106D 0xC450
#define CHIP_NCT6683D 0xC730
#define CHIP_NCT6686D 0xD440
#define CHIP_NCT6687D 0xD590 // MSI Z490+
#define CHIP_NCT6771F 0xB470
#define CHIP_NCT6772F 0xB470
#define CHIP_NCT6775F 0xB470
#define CHIP_NCT6776F 0xC330
#define CHIP_NCT6779D 0xC560
#define CHIP_NCT6791D 0xC800
#define CHIP_NCT6792D 0xC910
#define CHIP_NCT6793D 0xD120
#define CHIP_NCT6795D 0xD350
#define CHIP_NCT6796D 0xD420 // ASUS Common
#define CHIP_NCT6797D 0xD450
#define CHIP_NCT6798D 0xD420
#define CHIP_NCT6799D 0xD800

// --- Fintek F718xx Series (Older MSI) ---
#define CHIP_F71808E  0x0901
#define CHIP_F71858   0x0507
#define CHIP_F71862   0x0601
#define CHIP_F71869   0x0814
#define CHIP_F71882   0x0541
#define CHIP_F71889   0x0723

// --- Microchip (OEM) ---
#define CHIP_SCH3112  0x7C
#define CHIP_SCH3114  0x7D
#define CHIP_SCH3116  0x7F