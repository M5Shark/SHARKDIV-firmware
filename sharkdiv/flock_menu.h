#pragma once
#include <stdint.h>
// flock_menu.h — the Flock Scanner app (registered via UserApps).
namespace flock_menu {
// mode: 0 = full cycle, 1 = Wi-Fi only, 2 = BLE only
void openAt(uint8_t tab, uint8_t mode);
void appSetup();
void appLoop();
void exit();
}  // namespace flock_menu
