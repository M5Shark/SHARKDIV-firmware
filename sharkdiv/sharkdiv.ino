#include <Arduino.h>
#include "esp_log.h"

// DIV_VM_BUILD: set to 1 (via compiler flag -DDIV_VM_BUILD=1 in build-firmware.sh)
// when running the firmware in the QEMU virtual device (~/ESP32-DIV-VM). Skips
// hardware QEMU does not emulate: NeoPixel RMT and LEDC setup, whose register
// reads divide by zero under QEMU. Defaults to 0 for real hardware builds.
#ifndef DIV_VM_BUILD
#define DIV_VM_BUILD 0
#endif

#include <PCF8574.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "SettingsStore.h"
#include "Touchscreen.h"
#include "config.h"
#include "SharkUI.h"
#include "SharkTools.h"
#include "SharkGamesDiv.h"
#include "UserApps.h"
#include "SharkExtra.h"
#include "ducky.h"
#include "icon.h"
#include "ir.h"
#include "gps.h"
#include "rfid.h"
#include "shared.h"
#include "utils.h"
#include "flock_hunter.h"
#include <Adafruit_NeoPixel.h>

#if !BOARD_HAS_ESP32S3
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#endif

extern bool has_ip5306;

Adafruit_NeoPixel strip(1, RGB_DATA, NEO_GRB + NEO_KHZ800);

TFT_eSPI tft = TFT_eSPI();

PCF8574 pcf(PCF8574_I2C_ADDR);

void setBrightness(uint8_t value) {
  ledcWrite(PWM_CHANNEL, value);
}bool feature_exit_requested = false;

const int NUM_MENU_ITEMS = 8;
const char *menu_items[NUM_MENU_ITEMS] = {
    "WiFi",
    "2.4GHz",
    "More",
    "Settings",
    "Bluetooth",
    "SubGHz",
    "Tools",
    "About"};

const unsigned char *bitmap_icons[NUM_MENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_jammer,
    bitmap_icon_dialog,
    bitmap_icon_setting,
    bitmap_icon_spoofer,
    bitmap_icon_analyzer,
    bitmap_icon_stat,
    bitmap_icon_question};

int current_menu_index = 0;
bool is_main_menu = false;

const int NUM_SUBMENU_ITEMS = 12;
const char *submenu_items[NUM_SUBMENU_ITEMS] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Probe Request Flood",
    "Deauth Detector",
    "WiFi Scanner",
    "Captive Portal",
    "Hidden SSID Revealer",
    "WPS Scanner",
    "ARP Scanner",
    "Karma Attack",
    "Back to Main Menu"};

// WiFi submenu is split across two pages (features after Hidden SSID on page 2).
// Bottom row: icon | Main Menu                 Next/Prev Page | icon
static constexpr int WIFI_PAGE0_FEATURES = 8;
static constexpr int WIFI_PAGE1_FEATURES = 6;
static int wifi_submenu_page = 0;

const char *wifi_page0_items[WIFI_PAGE0_FEATURES] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Probe Request Flood",
    "Deauth Detector",
    "WiFi Scanner",
    "Captive Portal",
    "Hidden SSID Revealer"};

const char *wifi_page1_items[WIFI_PAGE1_FEATURES] = {
    "WPS Scanner",
    "ARP Scanner",
    "Karma Attack",
    "Flock Hunter",
    "WiFi Radar",
    "SSID Rotator"};

// Bluetooth submenu uses the same paged footer layout as WiFi.
static constexpr int BT_PAGE0_FEATURES = 7;
static constexpr int BT_PAGE1_FEATURES = 3;
static int bluetooth_submenu_page = 0;

const char *bluetooth_page0_items[BT_PAGE0_FEATURES] = {
    "BLE Spoofer",
    "Sour Apple",
    "AirTag Spoofer",
    "AirTag Sniffer",
    "Sniffer",
    "BLE Scanner",
    "BLE Rubber Ducky"};

const char *bluetooth_page1_items[BT_PAGE1_FEATURES] = {
    "Skimmer Detect",
    "BLE Spam",
    "BLE Link Test"};

static FeatureUI::Button s_pagedFooterBtns[2];
static int s_pagedFooterFocus = -1;  // 0=back, 1=page btn, -1=none

const int nrf_NUM_SUBMENU_ITEMS = 8;
const char *nrf_submenu_items[nrf_NUM_SUBMENU_ITEMS] = {
    "Scanner",
    "Proto Kill",
    "ESB Sniffer",
    "ESB Replay",
    "MouseJack Scan",
    "MouseJack Inject",
    "Src Finder",
    "Back to Main Menu"};

const int subghz_NUM_SUBMENU_ITEMS = 6;
const char *subghz_submenu_items[subghz_NUM_SUBMENU_ITEMS] = {
    "Replay Attack",
    "De Bruijn / Brute",
    "Jamming Detector",
    "Saved Profile",
    "SDR Spectrum",
    "Back to Main Menu"};

constexpr int TOOLS_BASE_ITEMS = 9;
constexpr int TOOLS_MAX_USER_APPS = 4;
const int tools_NUM_SUBMENU_ITEMS = TOOLS_BASE_ITEMS + TOOLS_MAX_USER_APPS;
const char *tools_submenu_items[tools_NUM_SUBMENU_ITEMS] = {
    "Serial Monitor",
    "Update Firmware",
    "Touch Calibrate",
    "SD File Manager",
    "Snake",
    "Reaction Test",
    "QR Code",
    "Ch Analyzer",
    "", "", "", "",   // user apps, filled at boot
    "Back to Main Menu"};

static constexpr uint8_t OTHER_LAYER_HOME = 0;
static constexpr uint8_t OTHER_LAYER_IR   = 1;
static constexpr uint8_t OTHER_LAYER_RFID = 2;
static constexpr uint8_t OTHER_LAYER_GPS  = 3;

const int other_NUM_SUBMENU_ITEMS = 4;
static constexpr int OTHER_GRID_COLS = 2;
const char *other_submenu_items[other_NUM_SUBMENU_ITEMS] = {
    "IR Remote",
    "RFID/NFC",
    "GPS",
    "Main Menu"};

const int rfid_NUM_SUBMENU_ITEMS = 9;
const char *rfid_submenu_items[rfid_NUM_SUBMENU_ITEMS] = {
    "Card Reader",
    "Card Clone",
    "Erase",
    "Dump",
    "Decode Access",
    "Jam Reader",
    "Tag Disrupt",
    "Disrupt Emulate",
    "Back to Main Menu"};

const int gps_NUM_SUBMENU_ITEMS = 5;
const char *gps_submenu_items[gps_NUM_SUBMENU_ITEMS] = {
    "Wardriver",
    "Satellite Scanner",
    "GPS Guard",
    "Coordinates",
    "Back to Main Menu"};

const int ir_NUM_SUBMENU_ITEMS = 6;
const char *ir_submenu_items[ir_NUM_SUBMENU_ITEMS] = {
    "Record",
    "Saved Profile",
    "Universal Controller",
    "TV Power",
    "IR Repeater",
    "Back to Main Menu"};

const int about_NUM_SUBMENU_ITEMS = 1;
const char *about_submenu_items[about_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

const int setting_NUM_SUBMENU_ITEMS = 1;
const char *setting_submenu_items[setting_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

int current_submenu_index = 0;
bool in_sub_menu = false;
int last_submenu_index = -1;
bool submenu_initialized = false;
uint8_t other_layer = OTHER_LAYER_HOME;
int last_other_menu_index = -1;
bool other_menu_grid_initialized = false;

const char **active_submenu_items = nullptr;
int active_submenu_size = 0;

const unsigned char *wifi_submenu_icons[NUM_SUBMENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_antenna,
    bitmap_icon_wifi_jammer,
    bitmap_icon_Skull_3,
    bitmap_icon_eye2,
    bitmap_icon_jammer,
    bitmap_icon_bash,
    bitmap_icon_eye_blind,
    bitmap_icon_key,
    bitmap_icon_list,
    bitmap_icon_devil,
    bitmap_icon_go_back
};

const unsigned char *wifi_page0_icons[WIFI_PAGE0_FEATURES] = {
    bitmap_icon_wifi,
    bitmap_icon_antenna,
    bitmap_icon_wifi_jammer,
    bitmap_icon_Skull_3,
    bitmap_icon_eye2,
    bitmap_icon_jammer,
    bitmap_icon_bash,
    bitmap_icon_eye_blind
};

const unsigned char *wifi_page1_icons[WIFI_PAGE1_FEATURES] = {
    bitmap_icon_key,
    bitmap_icon_list,
    bitmap_icon_devil,
    bitmap_icon_eye2,
    bitmap_icon_stat,
    bitmap_icon_spoofer
};

const unsigned char *bluetooth_page0_icons[BT_PAGE0_FEATURES] = {
    bitmap_icon_spoofer,
    bitmap_icon_apple,
    bitmap_icon_tags,
    bitmap_icon_magnifying_glass,
    bitmap_icon_analyzer,
    bitmap_icon_graph,
    bitmap_icon_rubber_ducky
};

const unsigned char *bluetooth_page1_icons[BT_PAGE1_FEATURES] = {
    bitmap_icon_Wireless_4,
    bitmap_icon_apple,
    bitmap_icon_graph
};

const unsigned char *nrf_submenu_icons[nrf_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_scanner,
    bitmap_icon_kill,
    bitmap_icon_analyzer,
    bitmap_icon_follow,
    bitmap_icon_magnifying_glass,
    bitmap_icon_key,
    bitmap_icon_stat,
    bitmap_icon_go_back
};

const unsigned char *subghz_submenu_icons[subghz_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_antenna,
    bitmap_icon_graph_self_loop,
    bitmap_icon_Voice_Id,
    bitmap_icon_list,
    bitmap_icon_analyzer,
    bitmap_icon_go_back
};

const unsigned char *tools_submenu_icons[tools_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_bash,
    bitmap_icon_follow,
    bitmap_icon_undo,
    bitmap_icon_sdcard,
    bitmap_icon_stat,
    bitmap_icon_dialog,
    bitmap_icon_list,
    bitmap_icon_analyzer,
    nullptr, nullptr, nullptr, nullptr,   // user apps
    bitmap_icon_go_back
};

const unsigned char *other_submenu_icons[other_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_led,
    bitmap_icon_rfid_chip,
    bitmap_icon_satellite,
    bitmap_icon_go_back
};

const unsigned char *rfid_submenu_icons[rfid_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_magnifying_glass,
    bitmap_icon_follow,
    bitmap_icon_recycle,
    bitmap_icon_dot_matrix,
    bitmap_icon_key,
    bitmap_icon_kill,
    bitmap_icon_flash,
    bitmap_icon_devil,
    bitmap_icon_go_back
};

const unsigned char *gps_submenu_icons[gps_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_satellite,
    bitmap_icon_satellite_dish,
    bitmap_icon_go_back,
    bitmap_icon_stat,
    bitmap_icon_list
};

const unsigned char *ir_submenu_icons[ir_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_led,
    bitmap_icon_list,
    bitmap_icon_remote_control,
    bitmap_icon_dialog,
    bitmap_icon_stat,
    bitmap_icon_go_back
};

const unsigned char *about_submenu_icons[about_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char *setting_submenu_icons[setting_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char **active_submenu_icons = nullptr;

static int wifiFeatureCount() {
    return (wifi_submenu_page == 0) ? WIFI_PAGE0_FEATURES : WIFI_PAGE1_FEATURES;
}

static int bluetoothFeatureCount() {
    return (bluetooth_submenu_page == 0) ? BT_PAGE0_FEATURES : BT_PAGE1_FEATURES;
}

static int pagedFeatureCount() {
    if (current_menu_index == 4) {
        return bluetoothFeatureCount();
    }
    return wifiFeatureCount();
}

static int* pagedSubmenuPage() {
    return (current_menu_index == 4) ? &bluetooth_submenu_page : &wifi_submenu_page;
}

// Bottom row: [icon | Main Menu] ........ [Next/Prev Page | icon]
static int pagedBackBtnIndex() {
    return pagedFeatureCount();
}

static int pagedPageBtnIndex() {
    return pagedFeatureCount() + 1;
}

static int pagedNavRowY() {
    return tft.height() - 30;
}

static const char* pagedPageBtnLabel() {
    return (*pagedSubmenuPage() == 0) ? "Next Page" : "Prev Page";
}

static const unsigned char* pagedPageBtnIcon() {
    return (*pagedSubmenuPage() == 0) ? bitmap_icon_navigate_right : bitmap_icon_navigate_left;
}

static void layoutPagedFooterButtons() {
    const int y = pagedNavRowY();
    const int mid = tft.width() / 2;
    s_pagedFooterBtns[0] = {
        0, (int16_t)y, (int16_t)mid, 28,
        "Main Menu", FeatureUI::ButtonStyle::Secondary, false};
    s_pagedFooterBtns[1] = {
        (int16_t)mid, (int16_t)y, (int16_t)(tft.width() - mid), 28,
        pagedPageBtnLabel(), FeatureUI::ButtonStyle::Secondary, false};
}

static void drawPagedFooterButtons() {
    layoutPagedFooterButtons();
    const int y = pagedNavRowY();
    const int rowH = 28;
    const int iconSize = 16;
    tft.fillRect(0, y, tft.width(), rowH, UI_BG);

    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextSize(1);
    // Font 2 is ~16px; center icon + text on the same midline within the row.
    const int textH = 16;
    const int iconY = y + (rowH - iconSize) / 2;
    const int textY = y + (rowH - textH) / 2;

    {
        const uint16_t color = (s_pagedFooterFocus == 0) ? UI_ICON : UI_TEXT;
        tft.setTextColor(color, UI_BG);
        tft.drawBitmap(10, iconY, bitmap_icon_go_back, iconSize, iconSize, color);
        tft.setCursor(30, textY);
        tft.print("Main Menu");
    }

    {
        const uint16_t color = (s_pagedFooterFocus == 1) ? UI_ICON : UI_TEXT;
        const char* label = pagedPageBtnLabel();
        const int gap = 4;
        const int textW = tft.textWidth(label);
        // Right-aligned group: [label][gap][icon] — same vertical midline.
        const int iconX = tft.width() - 10 - iconSize;
        const int textX = iconX - gap - textW;
        tft.setTextColor(color, UI_BG);
        tft.setCursor(textX, textY);
        tft.print(label);
        tft.drawBitmap(iconX, iconY, pagedPageBtnIcon(), iconSize, iconSize, color);
    }
}

static void applyWifiSubmenuPage() {
    if (wifi_submenu_page == 0) {
        active_submenu_items = wifi_page0_items;
        active_submenu_icons = wifi_page0_icons;
    } else {
        active_submenu_items = wifi_page1_items;
        active_submenu_icons = wifi_page1_icons;
    }
    active_submenu_size = wifiFeatureCount() + 2;
    if (current_submenu_index >= active_submenu_size) {
        current_submenu_index = 0;
    }
    s_pagedFooterFocus = -1;
    last_submenu_index = -1;
    submenu_initialized = false;
}

static void applyBluetoothSubmenuPage() {
    if (bluetooth_submenu_page == 0) {
        active_submenu_items = bluetooth_page0_items;
        active_submenu_icons = bluetooth_page0_icons;
    } else {
        active_submenu_items = bluetooth_page1_items;
        active_submenu_icons = bluetooth_page1_icons;
    }
    active_submenu_size = bluetoothFeatureCount() + 2;
    if (current_submenu_index >= active_submenu_size) {
        current_submenu_index = 0;
    }
    s_pagedFooterFocus = -1;
    last_submenu_index = -1;
    submenu_initialized = false;
}

void updateActiveSubmenu() {
    switch (current_menu_index) {
        case 0:
            wifi_submenu_page = 0;
            current_submenu_index = 0;
            applyWifiSubmenuPage();
            break;
        case 1:
            active_submenu_items = nrf_submenu_items;
            active_submenu_size = nrf_NUM_SUBMENU_ITEMS;
            active_submenu_icons = nrf_submenu_icons;
            break;
        case 2:
            if (other_layer == OTHER_LAYER_HOME) {
                active_submenu_items = other_submenu_items;
                active_submenu_size = other_NUM_SUBMENU_ITEMS;
                active_submenu_icons = other_submenu_icons;
            } else if (other_layer == OTHER_LAYER_IR) {
                active_submenu_items = ir_submenu_items;
                active_submenu_size = ir_NUM_SUBMENU_ITEMS;
                active_submenu_icons = ir_submenu_icons;
            } else if (other_layer == OTHER_LAYER_RFID) {
                active_submenu_items = rfid_submenu_items;
                active_submenu_size = rfid_NUM_SUBMENU_ITEMS;
                active_submenu_icons = rfid_submenu_icons;
            } else if (other_layer == OTHER_LAYER_GPS) {
                active_submenu_items = gps_submenu_items;
                active_submenu_size = gps_NUM_SUBMENU_ITEMS;
                active_submenu_icons = gps_submenu_icons;
            } else {
                active_submenu_items = other_submenu_items;
                active_submenu_size = other_NUM_SUBMENU_ITEMS;
                active_submenu_icons = other_submenu_icons;
            }
            break;
        case 3:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
        case 4:
            bluetooth_submenu_page = 0;
            current_submenu_index = 0;
            applyBluetoothSubmenuPage();
            break;
        case 5:
            active_submenu_items = subghz_submenu_items;
            active_submenu_size = subghz_NUM_SUBMENU_ITEMS;
            active_submenu_icons = subghz_submenu_icons;
            break;
        case 6:
            active_submenu_items = tools_submenu_items;
            active_submenu_size = TOOLS_BASE_ITEMS + userAppCount();
            active_submenu_icons = tools_submenu_icons;
            break;
        case 7:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;

        default:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
    }
}

static bool touchButtonInputEnabled = false;
static bool touchButtonCueDrawn = false;
static bool s_touchNavLabelsConfigured = false;
static bool s_touchNavHeld[5] = {false, false, false, false, false};
#if HAS_PCF8574_BUTTONS
static bool s_pcfButtonLastState[8] = {true, true, true, true, true, true, true, true};
#endif
static FeatureUI::Button s_touchNavBtns[5];
static const char* s_touchNavLabels[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
static constexpr int16_t TOUCH_NAV_BAR_H = (FeatureUI::FOOTER_H * 4) / 5;  // 20% shorter than footer

static int touchNavPinForIndex(int idx) {
  switch (idx) {
    case 0: return BTN_LEFT;
    case 1: return BTN_DOWN;
    case 2: return BTN_SELECT;
    case 3: return BTN_UP;
    case 4: return BTN_RIGHT;
    default: return -1;
  }
}

void setTouchButtonInputEnabled(bool enabled) {
  if (touchButtonInputEnabled != enabled) {
    touchButtonCueDrawn = false;
    if (!enabled) {
      for (int i = 0; i < 5; ++i) {
        s_touchNavLabels[i] = nullptr;
      }
      s_touchNavLabelsConfigured = false;
      for (int i = 0; i < 5; ++i) {
        s_touchNavHeld[i] = false;
      }
    }
  }
  touchButtonInputEnabled = enabled;
#if TOUCH_BUTTON_CUE_ENABLED
  if (enabled && feature_active) {
    drawTouchNavBar();
    touchButtonCueDrawn = true;
  }
#endif
}

bool featureHasTouchNavBar() {
#if TOUCH_BUTTON_CUE_ENABLED
  return touchButtonInputEnabled && feature_active;
#else
  return false;
#endif
}

void setTouchNavLabels(const char* left, const char* down, const char* center,
                       const char* up, const char* right) {
  s_touchNavLabels[0] = left;
  s_touchNavLabels[1] = down;
  s_touchNavLabels[2] = center;
  s_touchNavLabels[3] = up;
  s_touchNavLabels[4] = right;
  s_touchNavLabelsConfigured = true;
  invalidateTouchButtonCue();
}

void invalidateTouchButtonCue() {
  touchButtonCueDrawn = false;
}

void resetTouchNavHeldState() {
  for (int i = 0; i < 5; ++i) {
    s_touchNavHeld[i] = false;
  }
}

void redrawTouchButtonBar() {
  invalidateTouchButtonCue();
  drawTouchButtonCue();
}

static void layoutTouchNavBtns() {
  const int barY = tft.height() - TOUCH_NAV_BAR_H;
  const int barH = TOUCH_NAV_BAR_H;
  const int totalW = tft.width();
  const int cellW = totalW / 5;

  for (int i = 0; i < 5; ++i) {
    const int x = i * cellW;
    const int w = (i == 4) ? (totalW - x) : cellW;
    s_touchNavBtns[i] = {
      (int16_t)x, (int16_t)barY, (int16_t)w, (int16_t)barH,
      nullptr, FeatureUI::ButtonStyle::Secondary, false};
  }
}

static String fitTouchNavLabel(const char* label, int maxWidth) {
  if (!label || !label[0]) {
    return String();
  }
  String out = label;
  if (tft.textWidth(out) <= maxWidth) {
    return out;
  }
  while (out.length() > 1 && tft.textWidth(out + "...") > maxWidth) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

static void drawTouchNavBar() {
  static const unsigned char* kIcons[5] = {
    bitmap_icon_LEFT,
    bitmap_icon_DOWN,
    bitmap_icon_go_back,
    bitmap_icon_UP,
    bitmap_icon_RIGHT,
  };
  constexpr int kIconSize = 16;

  const int barY = tft.height() - TOUCH_NAV_BAR_H;
  const int barH = TOUCH_NAV_BAR_H;
  const int barW = tft.width();

  layoutTouchNavBtns();

  tft.fillRect(0, barY, barW, barH, UI_FG);
  tft.drawFastHLine(0, barY, barW, UI_LINE);

  for (int i = 0; i < 5; ++i) {
    const auto& b = s_touchNavBtns[i];
    if (i > 0) {
      tft.drawFastVLine(b.x, barY + 3, barH - 6, UI_LINE);
    }

    if (s_touchNavLabels[i] && s_touchNavLabels[i][0]) {
      tft.setTextDatum(MC_DATUM);
      const uint16_t txtColor = (i == 2) ? UI_ICON : UI_TEXT;
      tft.setTextColor(txtColor, UI_FG);
      const String fit = fitTouchNavLabel(s_touchNavLabels[i], b.w - 8);
      tft.drawString(fit, b.x + b.w / 2, b.y + b.h / 2, 1);
    } else {
      const int ix = b.x + (b.w - kIconSize) / 2;
      const int iy = b.y + (b.h - kIconSize) / 2;
      const bool inactiveSlot = s_touchNavLabelsConfigured && !s_touchNavLabels[i];
      const unsigned char* icon = inactiveSlot ? bitmap_icon_dots : kIcons[i];
      const uint16_t iconColor = inactiveSlot ? LIGHT_GRAY : ((i == 2) ? UI_ICON : UI_TEXT);
      tft.drawBitmap(ix, iy, icon, kIconSize, kIconSize, iconColor);
    }
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
}

void maintainTouchNavBar() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (!touchButtonInputEnabled || !feature_active || touchButtonCueDrawn) {
    return;
  }
  drawTouchNavBar();
  touchButtonCueDrawn = true;
#endif
}

void featureClearContent(uint16_t color) {
  const int bottom = touchNavContentBottomY();
  if (bottom > 0) {
    tft.fillRect(0, 0, tft.width(), bottom, color);
  } else {
    tft.fillScreen(color);
  }
}

int16_t touchNavReservedHeight() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (touchButtonInputEnabled && feature_active) {
    return TOUCH_NAV_BAR_H;
  }
#endif
  return 0;
}

int16_t touchNavContentBottomY() {
  return (int16_t)(tft.height() - touchNavReservedHeight());
}

void drawTouchButtonCue() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (!touchButtonInputEnabled || !feature_active) {
    return;
  }
  drawTouchNavBar();
  touchButtonCueDrawn = true;
#endif
}

static int touchNavIndexForPin(int buttonPin) {
  for (int i = 0; i < 5; ++i) {
    if (touchNavPinForIndex(i) == buttonPin) {
      return i;
    }
  }
  return -1;
}

static bool isTouchNavSlotDown(int idx) {
  if (idx < 0 || !feature_active || !touchButtonInputEnabled) {
    return false;
  }

  int x = 0;
  int y = 0;
  if (!readTouchXYDismiss(x, y)) {
    return false;
  }

  layoutTouchNavBtns();

  const int stripTop = tft.height() - TOUCH_NAV_BAR_H;
  if (y < stripTop) {
    return false;
  }

  return FeatureUI::hit(s_touchNavBtns, 5, x, y) == idx;
}

bool isPhysicalButtonPressed(int buttonPin) {
#if HAS_PCF8574_BUTTONS
  if (getPcf8574Address() != 0) {
    return !pcf.digitalRead(buttonPin);
  }
#endif
  return false;
}

static uint32_t s_navTapLatchMs[5] = {0};
static bool     s_navTapLatchNew[5] = {false};
static uint16_t s_navTapLatchDur[5] = {300, 300, 300, 300, 300};

bool isTouchNavButtonPressed(int buttonPin) {
  const int idx = touchNavIndexForPin(buttonPin);
  if (idx < 0) {
    return false;
  }
  if (isTouchNavSlotDown(idx)) {
    s_navTapLatchMs[idx] = millis();
    s_navTapLatchNew[idx] = true;
    s_navTapLatchDur[idx] = 300;
    return true;
  }
  // A short tap between two loop iterations still counts as a press.
  // Window shorter than the menus' 200 ms poll gap: exactly one step per
  // injected tap, regardless of which reader polls first.
  return s_navTapLatchMs[idx] != 0 &&
         millis() - s_navTapLatchMs[idx] < s_navTapLatchDur[idx];
}

#if DIV_VM_BUILD
// ---- Virtual buttons over Serial (QEMU has no touch/PCF8574 inputs) ----
// Send from the serial console: w=UP  s=DOWN  a=LEFT  d=RIGHT  k (or Enter)=SELECT.
// Each key press is consumed once (one-shot), like a short physical press.
static uint8_t vmSerialButtons = 0;
static void vmPollSerialButtons() {
  while (Serial.available()) {
    const int ic = Serial.read();
    if (ic < 0) break;
    Serial.printf("[vm:key] 0x%02x\n", ic);
    switch (ic) {
      case 'w': case 'W': vmSerialButtons |= 0x01; break;
      case 's': case 'S': vmSerialButtons |= 0x02; break;
      case 'a': case 'A': vmSerialButtons |= 0x04; break;
      case 'd': case 'D': vmSerialButtons |= 0x08; break;
      case 'k': case 'K': vmSerialButtons |= 0x10; break;  // SELECT (Enter not
                                                           // mapped: nc/terminals
                                                           // append it line-wise)
      default: break;
    }
  }
}
static bool vmButtonPressed(int buttonPin) {
  uint8_t bit = 0;
  if (buttonPin == BTN_UP) bit = 0x01;
  else if (buttonPin == BTN_DOWN) bit = 0x02;
  else if (buttonPin == BTN_LEFT) bit = 0x04;
  else if (buttonPin == BTN_RIGHT) bit = 0x08;
  else if (buttonPin == BTN_SELECT) bit = 0x10;
  else return false;
  if (vmSerialButtons & bit) {
    vmSerialButtons &= ~bit;
    return true;
  }
  return false;
}
#endif

#if DIV_VM_BUILD
// Screen-stream transport: the panel emulator (VM_Panel.cpp) sends its
// binary frame packets over the console UART. QEMU's extra UARTs proved
// unreliable, while UART0 has been robust in every configuration.
extern "C" void tft_vm_emit(const uint8_t *p, uint32_t n) {
  Serial.write(p, n);
}
#endif

bool isButtonPressed(int buttonPin) {
#if DIV_VM_BUILD
  if (vmButtonPressed(buttonPin)) {
    return true;
  }
#endif
  if (isPhysicalButtonPressed(buttonPin)) {
    return true;
  }
  return isTouchNavButtonPressed(buttonPin);
}

bool isTouchNavButtonPressedEdge(int buttonPin) {
  if (!feature_active || !touchButtonInputEnabled) {
    return false;
  }

  const int navIdx = touchNavIndexForPin(buttonPin);
  if (navIdx < 0) {
    return false;
  }

  const bool down = isTouchNavSlotDown(navIdx);
  if (down) {
    s_navTapLatchMs[navIdx] = millis();
    s_navTapLatchNew[navIdx] = true;
  }
  const bool edge = down && !s_touchNavHeld[navIdx];
  s_touchNavHeld[navIdx] = down;
  if (edge) {
    return true;
  }
  // Deliver a captured tap exactly once, even if the finger is already up.
  if (s_navTapLatchNew[navIdx] && s_navTapLatchMs[navIdx] != 0 &&
      millis() - s_navTapLatchMs[navIdx] < 350) {
    s_navTapLatchNew[navIdx] = false;
    return true;
  }
  return false;
}

bool isButtonPressedEdge(int buttonPin) {
#if DIV_VM_BUILD
  if (vmButtonPressed(buttonPin)) {
    return true;
  }
#endif
#if HAS_PCF8574_BUTTONS
  if (getPcf8574Address() != 0) {
    const int idx = buttonPin % 8;
    const bool cur = pcf.digitalRead(buttonPin);
    const bool edge = !cur && s_pcfButtonLastState[idx];
    s_pcfButtonLastState[idx] = cur;
    if (edge) {
      return true;
    }
  }
#endif

  return isTouchNavButtonPressedEdge(buttonPin);
}

bool featureExitButtonPressed() {
  // A quick tap can fall between two loop iterations of a slow feature, so
  // any SELECT hit is latched for a short window and replayed on the next
  // check.
  static uint32_t s_exitLatchMs = 0;
#if DIV_VM_BUILD
  if (vmButtonPressed(BTN_SELECT)) {
    s_exitLatchMs = millis();
    return true;
  }
#endif
  if (isPhysicalButtonPressed(BTN_SELECT) || isTouchNavButtonPressed(BTN_SELECT)) {
    s_exitLatchMs = millis();
    return true;
  }
  return s_exitLatchMs != 0 && millis() - s_exitLatchMs < 350;
}


// Ensures every feature has the touch nav bar with a working Exit, even the
// ones that predate the bar (Flock Hunter, RFID, GPS, ...). Features that
// install their own labels afterwards simply override these.
static void featureTouchBarInit() {
  setTouchButtonInputEnabled(true);
  // Only label the bar when the feature did not install its own.
  if (!s_touchNavLabelsConfigured) {
    setTouchNavLabels(nullptr, nullptr, "Exit", nullptr, nullptr);
  }
  redrawTouchButtonBar();
}

static void showFeatureUnavailable(const char* featureName, const char* requirement) {
  feature_active = false;
  feature_exit_requested = false;
  showNotification(featureName, requirement);
  delay(250);
}

static void runBleDuckyFeature() {
#if FEATURE_BLE_DUCKY
  current_submenu_index = 5;
  in_sub_menu = true;
  feature_active = true;
  feature_exit_requested = false;
  Ducky::enter();
  while (current_submenu_index == 5 && !feature_exit_requested) {
      current_submenu_index = 5;
      in_sub_menu = true;
      Ducky::loop();
  }

  Ducky::exit();
  if (feature_exit_requested) {
      in_sub_menu = true;
      is_main_menu = false;
      submenu_initialized = false;
      feature_active = false;
      feature_exit_requested = false;
      displaySubmenu();
      delay(200);
  }
#else
  showFeatureUnavailable("BLE Rubber Ducky", "This feature requires ESP32-S3.");
#endif
}

// VM note: initialized in setup() — calling readBatteryVoltage() (float math)
// from a global constructor panics under QEMU's lazy FPU enable, and is
// redundant anyway since setup() assigns it before first use.
float currentBatteryVoltage = 0.0f;
unsigned long last_interaction_time = 0;

int last_menu_index = -1;
bool menu_initialized = false;

const int COLUMN_WIDTH = 120;
const int X_OFFSET_LEFT = 10;
const int X_OFFSET_RIGHT = X_OFFSET_LEFT + COLUMN_WIDTH;
const int Y_START = 30;
const int Y_SPACING = 75;

void displayOtherMenuGrid();
void displayPagedSubmenu();

// Last submenu item ("Back to Main Menu") is pinned to the bottom of the screen.
static int submenuItemY(int index) {
    if (active_submenu_size > 0 && index == active_submenu_size - 1) {
        return tft.height() - 30;
    }
    return 30 + index * 30;
}

void displaySubmenu() {
    setTouchButtonInputEnabled(false);

    if (current_menu_index == 2 && other_layer == OTHER_LAYER_HOME) {
        displayOtherMenuGrid();
        return;
    }

    if (current_menu_index == 0 || current_menu_index == 4) {
        displayPagedSubmenu();
        return;
    }

    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(2);
    tft.setTextSize(1);

    if (!submenu_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < active_submenu_size; i++) {
            const int yPos = submenuItemY(i);
            const bool isBack = (i == active_submenu_size - 1);

            tft.setTextColor(UI_TEXT, UI_BG);
            if (active_submenu_icons[i]) {
                tft.drawBitmap(10, yPos, active_submenu_icons[i], 16, 16, UI_TEXT);
            }
            tft.setCursor(30, yPos);
            if (!isBack) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[i]);
        }

        submenu_initialized = true;
        last_submenu_index = -1;
    }

    if (last_submenu_index != current_submenu_index) {
        if (last_submenu_index >= 0) {
            const int prev_yPos = submenuItemY(last_submenu_index);
            const bool prevBack = (last_submenu_index == active_submenu_size - 1);

            tft.fillRect(0, prev_yPos, tft.width(), 28, UI_BG);
            drawSharkRowRail(prev_yPos, 28, false);
            tft.setTextColor(UI_TEXT, UI_BG);
            tft.drawBitmap(10, prev_yPos, active_submenu_icons[last_submenu_index], 16, 16, UI_TEXT);
            tft.setCursor(30, prev_yPos);
            tft.print(active_submenu_items[last_submenu_index]);
        }

        const int new_yPos = submenuItemY(current_submenu_index);
        const bool newBack = (current_submenu_index == active_submenu_size - 1);

        tft.fillRect(0, new_yPos, tft.width(), 28, UI_BG);
        drawSharkRowRail(new_yPos, 28, true);
        tft.setTextColor(UI_ICON, UI_BG);
        tft.drawBitmap(10, new_yPos, active_submenu_icons[current_submenu_index], 16, 16, UI_ICON);
        tft.setCursor(30, new_yPos);
        tft.print("> ");
        tft.print(active_submenu_items[current_submenu_index]);

        last_submenu_index = current_submenu_index;
#if DIV_VM_BUILD
        Serial.printf("[vm:submenu] sel=%d %s\n",
                      current_submenu_index,
                      (current_submenu_index >= 0 &&
                       current_submenu_index < active_submenu_size)
                          ? active_submenu_items[current_submenu_index]
                          : "(footer)");
#endif
    }

    drawStatusBar(currentBatteryVoltage, true);
}

void displayPagedSubmenu() {
    menu_initialized = false;
    last_menu_index = -1;

    const int featureCount = pagedFeatureCount();
    tft.setTextFont(2);
    tft.setTextSize(1);

    if (!submenu_initialized) {
        tft.fillScreen(UI_BG);
        for (int i = 0; i < featureCount; i++) {
            const int yPos = 30 + i * 30;
            tft.setTextColor(UI_TEXT, UI_BG);
            if (active_submenu_icons[i]) {
                tft.drawBitmap(10, yPos, active_submenu_icons[i], 16, 16, UI_TEXT);
            }
            tft.setCursor(30, yPos);
            tft.print("| ");
            tft.print(active_submenu_items[i]);
        }
        drawPagedFooterButtons();
        submenu_initialized = true;
        last_submenu_index = -1;
        s_pagedFooterFocus = -1;
    }

    if (last_submenu_index != current_submenu_index) {
        if (last_submenu_index >= 0 && last_submenu_index < featureCount) {
            const int prev_yPos = 30 + last_submenu_index * 30;
            drawSharkRowRail(prev_yPos, 28, false);
            tft.fillRect(30, prev_yPos, tft.width() - 30, 24, UI_BG);
            tft.setTextColor(UI_TEXT, UI_BG);
            tft.drawBitmap(10, prev_yPos, active_submenu_icons[last_submenu_index], 16, 16, UI_TEXT);
            tft.setCursor(30, prev_yPos);
            tft.print(active_submenu_items[last_submenu_index]);
        }

        if (current_submenu_index >= 0 && current_submenu_index < featureCount) {
            const int new_yPos = 30 + current_submenu_index * 30;
            drawSharkRowRail(new_yPos, 28, true);
            tft.fillRect(30, new_yPos, tft.width() - 30, 24, UI_BG);
            tft.setTextColor(UI_ICON, UI_BG);
            tft.drawBitmap(10, new_yPos, active_submenu_icons[current_submenu_index], 16, 16, UI_ICON);
            tft.setCursor(30, new_yPos);
            tft.print("> ");
            tft.print(active_submenu_items[current_submenu_index]);
            s_pagedFooterFocus = -1;
        } else if (current_submenu_index == pagedBackBtnIndex()) {
            s_pagedFooterFocus = 0;
        } else if (current_submenu_index == pagedPageBtnIndex()) {
            s_pagedFooterFocus = 1;
        } else {
            s_pagedFooterFocus = -1;
        }

        drawPagedFooterButtons();
        last_submenu_index = current_submenu_index;
#if DIV_VM_BUILD
        Serial.printf("[vm:submenu] sel=%d %s\n",
                      current_submenu_index,
                      (current_submenu_index >= 0 &&
                       current_submenu_index < active_submenu_size)
                          ? active_submenu_items[current_submenu_index]
                          : "(footer)");
#endif
    }

    drawStatusBar(currentBatteryVoltage, true);
}

void displayOtherMenuGrid() {
    applyThemeToPalette(settings().theme);

    submenu_initialized = false;
    last_submenu_index = -1;
    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(2);

    if (!other_menu_grid_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            drawSharkTile(x_position, y_position, 100, 60, i == current_submenu_index);
            tft.drawBitmap(x_position + 42, y_position + 10, other_submenu_icons[i], 16, 16, UI_ICON);

            tft.setTextColor(UI_TEXT, UI_FG);
            int textWidth = tft.textWidth(other_submenu_items[i]);
            int textX = x_position + (100 - textWidth) / 2;
            int textY = y_position + 30;
            tft.setCursor(textX, textY);
            tft.print(other_submenu_items[i]);
        }

        other_menu_grid_initialized = true;
        last_other_menu_index = -1;
    }

    if (last_other_menu_index != current_submenu_index) {
        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_other_menu_index) {
                drawSharkTile(x_position, y_position, 100, 60, false);
                tft.setTextColor(UI_TEXT, UI_FG);
                tft.drawBitmap(x_position + 42, y_position + 10,
                               other_submenu_icons[last_other_menu_index], 16, 16, UI_ICON);
                int textWidth = tft.textWidth(other_submenu_items[last_other_menu_index]);
                int textX = x_position + (100 - textWidth) / 2;
                int textY = y_position + 30;
                tft.setCursor(textX, textY);
                tft.print(other_submenu_items[last_other_menu_index]);
            }
        }

        int column = current_submenu_index % OTHER_GRID_COLS;
        int row = current_submenu_index / OTHER_GRID_COLS;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        drawSharkTile(x_position, y_position, 100, 60, true);

        tft.setTextColor(UI_ICON, UI_FG);
        tft.drawBitmap(x_position + 42, y_position + 10, other_submenu_icons[current_submenu_index],
                       16, 16, SELECTED_ICON_COLOR);
        int textWidth = tft.textWidth(other_submenu_items[current_submenu_index]);
        int textX = x_position + (100 - textWidth) / 2;
        int textY = y_position + 30;
        tft.setCursor(textX, textY);
        tft.print(other_submenu_items[current_submenu_index]);

        last_other_menu_index = current_submenu_index;
    }

    drawStatusBar(currentBatteryVoltage, true);
}

/** Main menu "Other" tile (index 2): triple preview icons (LED / satellite / dots). */
static constexpr int MAIN_MENU_OTHER_IDX = 2;
static constexpr int MAIN_MENU_OTHER_ICON_GAP = 4;

static void drawMainMenuOtherTripleIcons(int x_position, int y_position, uint16_t iconColor) {
    const int tripleW = 16 * 3 + MAIN_MENU_OTHER_ICON_GAP * 2;
    int ix = x_position + (100 - tripleW) / 2;
    const int iy = y_position + 10;
    tft.drawBitmap(ix, iy, bitmap_icon_led, 16, 16, iconColor);
    tft.drawBitmap(ix + 16 + MAIN_MENU_OTHER_ICON_GAP, iy, bitmap_icon_satellite, 16, 16, iconColor);
    tft.drawBitmap(ix + 32 + MAIN_MENU_OTHER_ICON_GAP * 2, iy, bitmap_icon_down_dots, 16, 16, iconColor);
}

void displayMenu() {

  setTouchButtonInputEnabled(false);
  applyThemeToPalette(settings().theme);

const uint16_t icon_colors[NUM_MENU_ITEMS] = {
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON
};

    submenu_initialized = false;
    last_submenu_index = -1;
    other_menu_grid_initialized = false;
    last_other_menu_index = -1;
    tft.setTextFont(2);

    if (!menu_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            drawSharkTile(x_position, y_position, 100, 60, i == current_menu_index);
            if (i == MAIN_MENU_OTHER_IDX) {
                drawMainMenuOtherTripleIcons(x_position, y_position, icon_colors[i]);
            } else {
                tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[i], 16, 16, icon_colors[i]);
            }

            tft.setTextColor(UI_TEXT, UI_FG);
            int textWidth = tft.textWidth(menu_items[i]);
            int textX = x_position + (100 - textWidth) / 2;
            int textY = y_position + 30;
            tft.setCursor(textX, textY);
            tft.print(menu_items[i]);
        }
        menu_initialized = true;
        last_menu_index = -1;
    }

    if (last_menu_index != current_menu_index) {
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_menu_index) {
                drawSharkTile(x_position, y_position, 100, 60, false);
                tft.setTextColor(UI_TEXT, UI_FG);
                if (last_menu_index == MAIN_MENU_OTHER_IDX) {
                    drawMainMenuOtherTripleIcons(x_position, y_position, icon_colors[last_menu_index]);
                } else {
                    tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[last_menu_index], 16, 16, icon_colors[last_menu_index]);
                }
                int textWidth = tft.textWidth(menu_items[last_menu_index]);
                int textX = x_position + (100 - textWidth) / 2;
                int textY = y_position + 30;
                tft.setCursor(textX, textY);
                tft.print(menu_items[last_menu_index]);
            }
        }

        int column = current_menu_index / 4;
        int row = current_menu_index % 4;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        drawSharkTile(x_position, y_position, 100, 60, true);

        tft.setTextColor(UI_ICON, UI_FG);
        if (current_menu_index == MAIN_MENU_OTHER_IDX) {
            drawMainMenuOtherTripleIcons(x_position, y_position, SELECTED_ICON_COLOR);
        } else {
            tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[current_menu_index], 16, 16, SELECTED_ICON_COLOR);
        }
        int textWidth = tft.textWidth(menu_items[current_menu_index]);
        int textX = x_position + (100 - textWidth) / 2;
        int textY = y_position + 30;
        tft.setCursor(textX, textY);
        tft.print(menu_items[current_menu_index]);

        last_menu_index = current_menu_index;
#if DIV_VM_BUILD
        Serial.println("[vm:menu] ---- main menu ----");
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
          Serial.printf("[vm:menu] %c %d. %s\n",
                        i == current_menu_index ? '>' : ' ', i, menu_items[i]);
        }
#endif
    }
    drawStatusBar(currentBatteryVoltage, true);
}

void handleWiFiSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(70);

        // Footer: Next / Prev
        if (current_submenu_index == pagedPageBtnIndex()) {
            wifi_submenu_page = (wifi_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyWifiSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        // Footer: Back to Main Menu
        if (current_submenu_index == pagedBackBtnIndex()) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            wifi_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            PacketMonitor::ptmSetup();
            featureTouchBarInit();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                PacketMonitor::ptmLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BeaconSpammer::beaconSpamSetup();
            featureTouchBarInit();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BeaconSpammer::beaconSpamLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Deauther::deautherSetup();
            featureTouchBarInit();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                Deauther::deautherLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProbeRequestFlood::probeRequestFloodSetup();
            featureTouchBarInit();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                ProbeRequestFlood::probeRequestFloodLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            DeauthDetect::deauthdetectSetup();
            featureTouchBarInit();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                DeauthDetect::deauthdetectLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WifiScan::wifiscanSetup();
            featureTouchBarInit();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                WifiScan::wifiscanLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 0 && current_submenu_index == 6) {
            current_submenu_index = 6;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            CaptivePortal::cportalSetup();
            featureTouchBarInit();
            while (current_submenu_index == 6 && !feature_exit_requested) {
                current_submenu_index = 6;
                in_sub_menu = true;
                CaptivePortal::cportalLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 0 && current_submenu_index == 7) {
            current_submenu_index = 7;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            HiddenSsidReveal::hiddenSsidSetup();
            featureTouchBarInit();
            while (current_submenu_index == 7 && !feature_exit_requested) {
                current_submenu_index = 7;
                in_sub_menu = true;
                HiddenSsidReveal::hiddenSsidLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WpsScanner::wpsScannerSetup();
            featureTouchBarInit();
            while (wifi_submenu_page == 1 && current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                WpsScanner::wpsScannerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ArpScanner::arpScannerSetup();
            featureTouchBarInit();
            while (wifi_submenu_page == 1 && current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                ArpScanner::arpScannerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            KarmaAttack::karmaSetup();
            featureTouchBarInit();
            while (wifi_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                KarmaAttack::karmaLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            FlockHunter::flockSetup();
            featureTouchBarInit();
            while (wifi_submenu_page == 1 && current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                FlockHunter::flockLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    FlockHunter::flockStop();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                FlockHunter::flockStop();
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }        if (wifi_submenu_page == 1 && current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WifiRadar::wifiRadarSetup();
            featureTouchBarInit();
            while (wifi_submenu_page == 1 && current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                WifiRadar::wifiRadarLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    WifiRadar::exit();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                WifiRadar::exit();
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }        if (wifi_submenu_page == 1 && current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SsidRotator::rotatorSetup();
            featureTouchBarInit();
            while (wifi_submenu_page == 1 && current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                SsidRotator::rotatorLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    SsidRotator::exit();
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                SsidRotator::exit();
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        layoutPagedFooterButtons();
        const int footerHit = FeatureUI::hit(s_pagedFooterBtns, 2, x, y);
        if (footerHit == 0) {
            // Left: Main Menu
            current_submenu_index = pagedBackBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            wifi_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }
        if (footerHit == 1) {
            // Right: Next / Prev page
            current_submenu_index = pagedPageBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            wifi_submenu_page = (wifi_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyWifiSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        const int featureCount = wifiFeatureCount();
        for (int i = 0; i < featureCount; i++) {
            int yPos = 30 + i * 30;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 220;
            int button_y2 = yPos + 30;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (wifi_submenu_page == 0 && current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    PacketMonitor::ptmSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        PacketMonitor::ptmLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BeaconSpammer::beaconSpamSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BeaconSpammer::beaconSpamLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Deauther::deautherSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        Deauther::deautherLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ProbeRequestFlood::probeRequestFloodSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        ProbeRequestFlood::probeRequestFloodLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    DeauthDetect::deauthdetectSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        DeauthDetect::deauthdetectLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WifiScan::wifiscanSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        WifiScan::wifiscanLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 6) {
                    current_submenu_index = 6;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    CaptivePortal::cportalSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 6 && !feature_exit_requested) {
                        current_submenu_index = 6;
                        in_sub_menu = true;
                        CaptivePortal::cportalLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 7) {
                    current_submenu_index = 7;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    HiddenSsidReveal::hiddenSsidSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 7 && !feature_exit_requested) {
                        current_submenu_index = 7;
                        in_sub_menu = true;
                        HiddenSsidReveal::hiddenSsidLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WpsScanner::wpsScannerSetup();
                    featureTouchBarInit();
                    while (wifi_submenu_page == 1 && current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        WpsScanner::wpsScannerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ArpScanner::arpScannerSetup();
                    featureTouchBarInit();
                    while (wifi_submenu_page == 1 && current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        ArpScanner::arpScannerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    KarmaAttack::karmaSetup();
                    featureTouchBarInit();
                    while (wifi_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        KarmaAttack::karmaLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    FlockHunter::flockSetup();
                    featureTouchBarInit();
                    while (wifi_submenu_page == 1 && current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        FlockHunter::flockLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            FlockHunter::flockStop();
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        FlockHunter::flockStop();
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WifiRadar::wifiRadarSetup();
                    featureTouchBarInit();
                    while (wifi_submenu_page == 1 && current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        WifiRadar::wifiRadarLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                        }
                    }
                    WifiRadar::exit();
                    if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SsidRotator::rotatorSetup();
                    featureTouchBarInit();
                    while (wifi_submenu_page == 1 && current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        SsidRotator::rotatorLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                        }
                    }
                    SsidRotator::exit();
                    if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    }
                }
                break;
            }
        }
    }
}

void handleBluetoothSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(70);

        if (current_submenu_index == pagedPageBtnIndex()) {
            bluetooth_submenu_page = (bluetooth_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyBluetoothSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        if (current_submenu_index == pagedBackBtnIndex()) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            bluetooth_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSpoofer::spooferSetup();
            featureTouchBarInit();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                BleSpoofer::spooferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            BleSpoofer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SourApple::sourappleSetup();
            featureTouchBarInit();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                SourApple::sourappleLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            SourApple::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            AirTagSpoofer::airTagSetup();
            featureTouchBarInit();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                AirTagSpoofer::airTagLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            AirTagSpoofer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            AirTagSniffer::airTagSnifferSetup();
            featureTouchBarInit();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                AirTagSniffer::airTagSnifferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            AirTagSniffer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSniffer::blesnifferSetup();
            featureTouchBarInit();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                BleSniffer::blesnifferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            BleSniffer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleScan::bleScanSetup();
            featureTouchBarInit();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                BleScan::bleScanLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            BleScan::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (bluetooth_submenu_page == 0 && current_submenu_index == 6) {
            runBleDuckyFeature();
        }

        if (bluetooth_submenu_page == 1 && current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSkimmer::bleSkimmerSetup();
            featureTouchBarInit();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                BleSkimmer::bleSkimmerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            BleSkimmer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (bluetooth_submenu_page == 1 && current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSpam::bleSpamSetup();
            featureTouchBarInit();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BleSpam::bleSpamLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            BleSpam::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (bluetooth_submenu_page == 1 && current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            LinkTest::linkTestSetup();
            featureTouchBarInit();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                LinkTest::linkTestLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
            while (isButtonPressed(BTN_SELECT)) {
            }
            break;
                }
            }
            LinkTest::exit();
            if (feature_exit_requested) {
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        layoutPagedFooterButtons();
        const int footerHit = FeatureUI::hit(s_pagedFooterBtns, 2, x, y);
        if (footerHit == 0) {
            current_submenu_index = pagedBackBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            bluetooth_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }
        if (footerHit == 1) {
            current_submenu_index = pagedPageBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            bluetooth_submenu_page = (bluetooth_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyBluetoothSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        const int featureCount = bluetoothFeatureCount();
        for (int i = 0; i < featureCount; i++) {
            int yPos = 30 + i * 30;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 220;
            int button_y2 = yPos + 30;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (bluetooth_submenu_page == 0 && current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSpoofer::spooferSetup();
                    featureTouchBarInit();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        BleSpoofer::spooferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleSpoofer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SourApple::sourappleSetup();
                    featureTouchBarInit();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        SourApple::sourappleLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    SourApple::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    AirTagSpoofer::airTagSetup();
                    featureTouchBarInit();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        AirTagSpoofer::airTagLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    AirTagSpoofer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    AirTagSniffer::airTagSnifferSetup();
                    featureTouchBarInit();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        AirTagSniffer::airTagSnifferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    AirTagSniffer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSniffer::blesnifferSetup();
                    featureTouchBarInit();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        BleSniffer::blesnifferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleSniffer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleScan::bleScanSetup();
                    featureTouchBarInit();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        BleScan::bleScanLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleScan::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 6) {
                    runBleDuckyFeature();
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSkimmer::bleSkimmerSetup();
                    featureTouchBarInit();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        BleSkimmer::bleSkimmerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleSkimmer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSpam::bleSpamSetup();
                    featureTouchBarInit();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BleSpam::bleSpamLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                        }
                    }
                    BleSpam::exit();
                    if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    }
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    LinkTest::linkTestSetup();
                    featureTouchBarInit();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        LinkTest::linkTestLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                        }
                    }
                    LinkTest::exit();
                    if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    }
                }
                break;
            }
        }
    }
}

void handleNRFSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 6) {
            current_submenu_index = 6;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            InterferenceFinder::srcFinderSetup();
            featureTouchBarInit();
            while (current_submenu_index == 6 && !feature_exit_requested) {
                current_submenu_index = 6;
                in_sub_menu = true;
                InterferenceFinder::srcFinderLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
            while (isButtonPressed(BTN_SELECT)) {
            }
            break;
                }
            }
            InterferenceFinder::exit();
            if (feature_exit_requested) {
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
            }
        }
        if (current_submenu_index == 7) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Scanner::scannerSetup();
            featureTouchBarInit();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                Scanner::scannerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            Scanner::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProtoKill::prokillSetup();
            featureTouchBarInit();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                ProtoKill::prokillLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            ProtoKill::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            EsbSniffer::esbSnifferSetup();
            featureTouchBarInit();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                EsbSniffer::esbSnifferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            EsbSniffer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            EsbReplay::esbReplaySetup();
            featureTouchBarInit();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                EsbReplay::esbReplayLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            EsbReplay::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            MouseJack::mouseJackSetup();
            featureTouchBarInit();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                MouseJack::mouseJackLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            MouseJack::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            MouseJackInject::mouseJackInjectSetup();
            featureTouchBarInit();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                MouseJackInject::mouseJackInjectLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            MouseJackInject::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = submenuItemY(i);

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 220;
            int button_y2 = yPos + 28;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 7) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else if (current_submenu_index == 6) {
                    current_submenu_index = 6;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    InterferenceFinder::srcFinderSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 6 && !feature_exit_requested) {
                        current_submenu_index = 6;
                        in_sub_menu = true;
                        InterferenceFinder::srcFinderLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                        }
                    }
                    InterferenceFinder::exit();
                    if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    }
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Scanner::scannerSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        Scanner::scannerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    Scanner::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ProtoKill::prokillSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        ProtoKill::prokillLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    ProtoKill::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    EsbSniffer::esbSnifferSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        EsbSniffer::esbSnifferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    EsbSniffer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    EsbReplay::esbReplaySetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        EsbReplay::esbReplayLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    EsbReplay::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    MouseJack::mouseJackSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        MouseJack::mouseJackLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    MouseJack::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    MouseJackInject::mouseJackInjectSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        MouseJackInject::mouseJackInjectLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    MouseJackInject::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

void handleSubGHzSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 5) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            replayat::ReplayAttackSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                replayat::ReplayAttackLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SubBrute::subBruteSetup();
            featureTouchBarInit();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                SubBrute::subBruteLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            jammingdetector::Setup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                jammingdetector::Loop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SavedProfile::saveSetup();
            featureTouchBarInit();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                SavedProfile::saveLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            RfSpectrum::rfSpectrumSetup();
            featureTouchBarInit();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                RfSpectrum::rfSpectrumLoop();
                if (featureExitButtonPressed() || isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            RfSpectrum::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = submenuItemY(i);

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 220;
            int button_y2 = yPos + 28;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 5) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                }
                else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    RfSpectrum::rfSpectrumSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        RfSpectrum::rfSpectrumLoop();
                        if (featureExitButtonPressed() || isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    RfSpectrum::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    replayat::ReplayAttackSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        replayat::ReplayAttackLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }  else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SubBrute::subBruteSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        SubBrute::subBruteLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    jammingdetector::Setup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        jammingdetector::Loop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SavedProfile::saveSetup();
                    featureTouchBarInit();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        SavedProfile::saveLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

constexpr int TOOLS_IDX_TERMINAL = 0;
constexpr int TOOLS_IDX_UPDATE   = 1;
constexpr int TOOLS_IDX_TOUCH    = 2;
constexpr int TOOLS_IDX_SD_FILES = 3;
constexpr int TOOLS_IDX_SNAKE    = 4;
constexpr int TOOLS_IDX_REACTION = 5;
constexpr int TOOLS_IDX_QR       = 6;
constexpr int TOOLS_IDX_CHANALYZE = 7;
constexpr int TOOLS_IDX_SETTINGS = -1;
constexpr int TOOLS_IDX_BACK     = 8;

static void runToolsFeatureExitCleanup() {
    in_sub_menu = true;
    is_main_menu = false;
    submenu_initialized = false;
    feature_active = false;
    feature_exit_requested = false;
    setTouchButtonInputEnabled(false);
    setTouchNavLabels(nullptr, nullptr, nullptr, nullptr, nullptr);
    resetTouchNavHeldState();
    displaySubmenu();
    delay(200);
    while (isButtonPressed(BTN_SELECT)) {
    }
}

static void runToolsFeature(int idx, void (*setupFn)(), void (*loopFn)()) {
    const bool useTouchNav = (idx != TOOLS_IDX_TOUCH);
    current_submenu_index = idx;
    in_sub_menu = true;
    feature_active = true;
    feature_exit_requested = false;
    if (useTouchNav) {
        setTouchButtonInputEnabled(true);
    }
    setupFn();
    while (current_submenu_index == idx && !feature_exit_requested) {
        current_submenu_index = idx;
        in_sub_menu = true;
        loopFn();
        if (feature_exit_requested) {
            break;
        }
        if (!useTouchNav && isButtonPressed(BTN_SELECT)) {
            break;
        }
    }
    runToolsFeatureExitCleanup();
}

static void launchToolsFeature(int idx) {
    switch (idx) {
        case TOOLS_IDX_TERMINAL:
            runToolsFeature(idx, Terminal::terminalSetup, Terminal::terminalLoop);
            break;
        case TOOLS_IDX_UPDATE:
            runToolsFeature(idx, FirmwareUpdate::updateSetup, FirmwareUpdate::updateLoop);
            break;
        case TOOLS_IDX_TOUCH:
            runToolsFeature(idx, TouchCalib::setup, TouchCalib::loop);
            break;
        case TOOLS_IDX_SD_FILES:
            runToolsFeature(idx, SdFileManager::setup, SdFileManager::loop);
            break;
        case TOOLS_IDX_SNAKE:
            runToolsFeature(idx, SnakeGame::snakeSetup, SnakeGame::snakeLoop);
            break;
        case TOOLS_IDX_REACTION:
            runToolsFeature(idx, ReactionGame::reactionSetup, ReactionGame::reactionLoop);
            break;
        case TOOLS_IDX_QR:
            runToolsFeature(idx, QrTool::qrSetup, QrTool::qrLoop);
            break;
        case TOOLS_IDX_CHANALYZE:
            runToolsFeature(idx, ChannelAnalyzer::chanSetup, ChannelAnalyzer::chanLoop);
            break;
        default: {
            const int appIdx = idx - (TOOLS_IDX_CHANALYZE + 1);
            if (appIdx >= 0 && appIdx < userAppCount()) {
                runToolsFeature(idx, userApps()[appIdx].setup, userApps()[appIdx].loop);
            }
            break;
        }
    }
}

void handleToolsSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == TOOLS_IDX_BACK + userAppCount()) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }

        launchToolsFeature(current_submenu_index);
        return;
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) {
            return;
        }

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = submenuItemY(i);

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 220;
            int button_y2 = yPos + 28;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == TOOLS_IDX_BACK + userAppCount()) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else {
                    launchToolsFeature(current_submenu_index);
                }
                break;
            }
        }
    }
}

static void otherDismissPlaceholder() {
    delay(25);
    while (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
        delay(5);
    }
    while (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT)) {
        int x = 0, y = 0;
        if (!readTouchXYDismiss(x, y) && !readTouchXY(x, y)) {
            delay(12);
            continue;
        }
        if (isNotificationVisible()) {
            NotificationAction a = notificationHandleTouch(x, y);
            if (a != NotificationAction::None) {
                break;
            }
            hideNotification();
        }
        break;
    }
    if (in_sub_menu) {
        submenu_initialized = false;
        if (current_menu_index == 2 && other_layer == OTHER_LAYER_HOME) {
            other_menu_grid_initialized = false;
            last_other_menu_index = -1;
        }
        displaySubmenu();
    }
}

static void otherRfidReturnGuard() {
    delay(120);
    for (int i = 0; i < 120; i++) {
        if (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT) &&
            !isButtonPressed(BTN_RIGHT) && !isButtonPressed(BTN_UP) &&
            !isButtonPressed(BTN_DOWN) && !isTouchDownDismiss()) {
            break;
        }
        delay(5);
    }
    delay(120);
}

static void otherRfidPlaceholderAction(int idx) {
    feature_active = true;
    if (!RfidNfc::begin()) {
        showNotification("RFID/NFC", "PN532 not found. Check SPI wiring/pins.");
        otherDismissPlaceholder();
        feature_active = false;
        return;
    }
    feature_exit_requested = false;
    setTouchButtonInputEnabled(true);
    for (;;) {
        RfidNfc::clearSessionRetry();
        switch (idx) {
            case 0:
                RfidNfc::sessionCardReader();
                break;
            case 1:
                RfidNfc::sessionClone();
                break;
            case 2:
                RfidNfc::sessionErase();
                break;
            case 3:
                RfidNfc::sessionDump();
                break;
            case 4:
                RfidNfc::sessionDecodeAccess();
                break;
            case 5:
                RfidNfc::sessionJamReader();
                break;
            case 6:
                RfidNfc::sessionTagDisrupt();
                break;
            case 7:
                RfidNfc::sessionDisruptEmulate();
                break;
            default:
                feature_active = false;
                restoreSdAfterSharedSpi();
                return;
        }
        if (feature_exit_requested || !RfidNfc::consumeSessionRetry()) {
            break;
        }
        feature_exit_requested = false;
    }
    restoreSdAfterSharedSpi();
    otherRfidReturnGuard();
    submenu_initialized = false;
    displaySubmenu();
    feature_active = false;
}

static void otherGpsPlaceholderAction(int idx) {
    feature_active = true;
    if (idx == 0) {
        feature_exit_requested = false;
        setTouchButtonInputEnabled(true);
        for (;;) {
            GpsWardriver::clearSessionRetry();
            GpsWardriver::session();
            if (feature_exit_requested || !GpsWardriver::consumeSessionRetry()) {
                break;
            }
            feature_exit_requested = false;
        }
        setTouchButtonInputEnabled(false);
    } else {
        switch (idx) {
            case 1:
                GpsSatelliteScanner::session();
                break;
            case 2:
                GpsGuard::session();
                break;
            case 3:
                GpsCoord::session();
                break;
            default:
                feature_active = false;
                return;
        }
    }
    otherRfidReturnGuard();
    submenu_initialized = false;
    displaySubmenu();
    feature_active = false;
}

void handleOtherSubmenuButtons() {
    if (other_layer == OTHER_LAYER_HOME) {
        const int og_rows =
            (other_NUM_SUBMENU_ITEMS + OTHER_GRID_COLS - 1) / OTHER_GRID_COLS;

        if (isButtonPressed(BTN_UP)) {
            int row = current_submenu_index / OTHER_GRID_COLS;
            if (row > 0) {
                current_submenu_index -= OTHER_GRID_COLS;
            } else {
                current_submenu_index += OTHER_GRID_COLS * (og_rows - 1);
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN)) {
            int row = current_submenu_index / OTHER_GRID_COLS;
            if (row < og_rows - 1) {
                current_submenu_index += OTHER_GRID_COLS;
            } else {
                current_submenu_index -= OTHER_GRID_COLS * (og_rows - 1);
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_LEFT)) {
            int col = current_submenu_index % OTHER_GRID_COLS;
            if (col > 0) {
                current_submenu_index--;
            } else {
                current_submenu_index++;
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_RIGHT)) {
            int col = current_submenu_index % OTHER_GRID_COLS;
            if (col < OTHER_GRID_COLS - 1) {
                current_submenu_index++;
            } else {
                current_submenu_index--;
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }
    } else {
        if (isButtonPressed(BTN_UP)) {
            current_submenu_index =
                (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN)) {
            current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (other_layer == OTHER_LAYER_HOME) {
            if (current_submenu_index == other_NUM_SUBMENU_ITEMS - 1) {
                in_sub_menu = false;
                feature_active = false;
                feature_exit_requested = false;
                displayMenu();
                handleButtons();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                other_layer = OTHER_LAYER_IR;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 1) {
                other_layer = OTHER_LAYER_RFID;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 2) {
                other_layer = OTHER_LAYER_GPS;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            }
        } else if (other_layer == OTHER_LAYER_IR) {
            if (current_submenu_index == ir_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                feature_active = false;
                feature_exit_requested = false;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                current_submenu_index = 0;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRRemoteFeature::setup();
                while (current_submenu_index == 0 && !feature_exit_requested) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    IRRemoteFeature::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 1) {
                current_submenu_index = 1;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRSavedProfile::setup();
                while (current_submenu_index == 1 && !feature_exit_requested) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    IRSavedProfile::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 2) {
                current_submenu_index = 2;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRUniversalController::setup();
                while (current_submenu_index == 2 && !feature_exit_requested) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    IRUniversalController::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            }
            else if (current_submenu_index == 3) {
                current_submenu_index = 3;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                TvPowerOff::tvPowerSetup();
                featureTouchBarInit();
                while (current_submenu_index == 3 && !feature_exit_requested) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    TvPowerOff::tvPowerLoop();
                    if (featureExitButtonPressed() || isButtonPressed(BTN_SELECT)) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
                while (isButtonPressed(BTN_SELECT)) {
                }
                break;
                    }
                }
                TvPowerOff::exit();
                if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
                }
            }
            else if (current_submenu_index == 4) {
                current_submenu_index = 4;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IrRepeater::repeaterSetup();
                featureTouchBarInit();
                while (current_submenu_index == 4 && !feature_exit_requested) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    IrRepeater::repeaterLoop();
                    if (featureExitButtonPressed() || isButtonPressed(BTN_SELECT)) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
                while (isButtonPressed(BTN_SELECT)) {
                }
                break;
                    }
                }
                IrRepeater::exit();
                if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
                }
            }

        } else if (other_layer == OTHER_LAYER_RFID) {
            if (current_submenu_index == rfid_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherRfidPlaceholderAction(current_submenu_index);
            }
        } else if (other_layer == OTHER_LAYER_GPS) {
            if (current_submenu_index == gps_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherGpsPlaceholderAction(current_submenu_index);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        int touched_slot = -1;
        if (other_layer == OTHER_LAYER_HOME) {
            for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
                int column = i % OTHER_GRID_COLS;
                int row = i / OTHER_GRID_COLS;
                int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
                int y_position = Y_START + row * Y_SPACING;
                int button_x1 = x_position;
                int button_y1 = y_position;
                int button_x2 = x_position + 100;
                int button_y2 = y_position + 60;
                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    touched_slot = i;
                    break;
                }
            }
        } else {
            for (int i = 0; i < active_submenu_size; i++) {
                int yPos = submenuItemY(i);

                int button_x1 = 10;
                int button_y1 = yPos;
                int button_x2 = 220;
                int button_y2 = yPos + 28;

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    touched_slot = i;
                    break;
                }
            }
        }

        if (touched_slot < 0) {
            return;
        }

        current_submenu_index = touched_slot;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);

        if (other_layer == OTHER_LAYER_HOME) {
            if (current_submenu_index == other_NUM_SUBMENU_ITEMS - 1) {
                in_sub_menu = false;
                feature_active = false;
                feature_exit_requested = false;
                displayMenu();
                handleButtons();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                other_layer = OTHER_LAYER_IR;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 1) {
                other_layer = OTHER_LAYER_RFID;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 2) {
                other_layer = OTHER_LAYER_GPS;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            }
        } else if (other_layer == OTHER_LAYER_IR) {
            if (current_submenu_index == ir_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                feature_active = false;
                feature_exit_requested = false;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                current_submenu_index = 0;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRRemoteFeature::setup();
                while (current_submenu_index == 0 && !feature_exit_requested) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    IRRemoteFeature::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 1) {
                current_submenu_index = 1;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRSavedProfile::setup();
                while (current_submenu_index == 1 && !feature_exit_requested) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    IRSavedProfile::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 2) {
                current_submenu_index = 2;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRUniversalController::setup();
                while (current_submenu_index == 2 && !feature_exit_requested) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    IRUniversalController::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            }
            else if (current_submenu_index == 3) {
                current_submenu_index = 3;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                TvPowerOff::tvPowerSetup();
                featureTouchBarInit();
                while (current_submenu_index == 3 && !feature_exit_requested) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    TvPowerOff::tvPowerLoop();
                    if (featureExitButtonPressed() || isButtonPressed(BTN_SELECT)) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
                while (isButtonPressed(BTN_SELECT)) {
                }
                break;
                    }
                }
                TvPowerOff::exit();
                if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
                }
            }
            else if (current_submenu_index == 4) {
                current_submenu_index = 4;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IrRepeater::repeaterSetup();
                featureTouchBarInit();
                while (current_submenu_index == 4 && !feature_exit_requested) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    IrRepeater::repeaterLoop();
                    if (featureExitButtonPressed() || isButtonPressed(BTN_SELECT)) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
                while (isButtonPressed(BTN_SELECT)) {
                }
                break;
                    }
                }
                IrRepeater::exit();
                if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
                }
            }

        } else if (other_layer == OTHER_LAYER_RFID) {
            if (current_submenu_index == rfid_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherRfidPlaceholderAction(current_submenu_index);
            }
        } else if (other_layer == OTHER_LAYER_GPS) {
            if (current_submenu_index == gps_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherGpsPlaceholderAction(current_submenu_index);
            }
        }
    }
}

void handleAboutPage() {
  feature_active = true;
  feature_exit_requested = false;

  tft.fillScreen(UI_BG);
  currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);

  tft.setTextFont(2);
  tft.setTextColor(UI_ICON, UI_BG);
  tft.setCursor(16, 40);
  tftPrintObf(OBF_PN, sizeof(OBF_PN));

  tft.setTextFont(1);
  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(16, 60);
  tft.print("by ");
  tftPrintObf(OBF_DN, sizeof(OBF_DN));
  tft.print(" - ");
  tft.print(ESP32DIV_VERSION);

  tft.drawFastHLine(12, 78, 216, UI_LINE);

  const int xLabel = 16;
  const int xValue = 80;
  int y = 96;
  const int step = 22;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print("Board");
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(xValue, y);
  tft.print(ESP32DIV_BOARD_NAME);
  y += step;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print("Mail");
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(xValue, y);
  tftPrintObf(OBF_EM, sizeof(OBF_EM));
  y += step;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print("GitHub");
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(xValue, y);
  tftPrintObf(OBF_GH, sizeof(OBF_GH));
  y += step;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print("Web");
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(xValue, y);
  tftPrintObf(OBF_WB, sizeof(OBF_WB));

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(16, 300);
  tft.print("SELECT / tap to go back");

  while (!feature_exit_requested) {
    if (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
      last_interaction_time = millis();
      feature_exit_requested = true;
      delay(200);
      break;
    }

    int x, ty;
    if (readTouchXY(x, ty)) {
      last_interaction_time = millis();
      feature_exit_requested = true;
      delay(200);
      break;
    }

    delay(20);
  }

  feature_active = false;
  feature_exit_requested = false;
  in_sub_menu = false;
  submenu_initialized = false;

  menu_initialized = false;
  last_menu_index = -1;
  is_main_menu = false;
  displayMenu();
}

void handleSettingsSubmenuButtons() {

  feature_active = true;
  feature_exit_requested = false;

  AppSettingsUI::setup();
  while (!feature_exit_requested) {
    AppSettingsUI::loop();
  }

  feature_active = false;
  feature_exit_requested = false;

  in_sub_menu = false;
  submenu_initialized = false;

  menu_initialized = false;
  last_menu_index = -1;
  is_main_menu = false;
  displayMenu();
}

void handleButtons() {
    if (in_sub_menu) {
        switch (current_menu_index) {

            case 0: handleWiFiSubmenuButtons(); break;
            case 1: handleNRFSubmenuButtons(); break;
            case 2: handleOtherSubmenuButtons(); break;
            case 3: /* Settings: full-screen AppSettings, not list submenu */ break;
            case 4: handleBluetoothSubmenuButtons(); break;
            case 5: handleSubGHzSubmenuButtons(); break;
            case 6: handleToolsSubmenuButtons(); break;
            default: break;
        }
    } else {

        if (isButtonPressed(BTN_UP) && !is_main_menu) {
            current_menu_index--;
            if (current_menu_index < 0) {
                current_menu_index = NUM_MENU_ITEMS - 1;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN) && !is_main_menu) {
            current_menu_index++;
            if (current_menu_index >= NUM_MENU_ITEMS) {
                current_menu_index = 0;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_LEFT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index >= 4) {
                current_menu_index = row;
            } else if (current_menu_index == 0) {
                current_menu_index = 3;
            } else {
                current_menu_index = row - 1;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_RIGHT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index < 4) {
                current_menu_index = row + 4;
            } else if (current_menu_index == 7) {
                current_menu_index = 0;
            } else {
                current_menu_index = row + 5;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_SELECT)) {
            last_interaction_time = millis();
            delay(200);

            if (current_menu_index == 3) {
                handleSettingsSubmenuButtons();
            } else if (current_menu_index == 7) {
                handleAboutPage();
            } else {
                updateActiveSubmenu();

                if (active_submenu_items && active_submenu_size > 0) {
                    current_submenu_index = 0;
                    if (current_menu_index == 2) {
                        other_layer = OTHER_LAYER_HOME;
                        other_menu_grid_initialized = false;
                        last_other_menu_index = -1;
                    }
                    in_sub_menu = true;
                    submenu_initialized = false;
                    displaySubmenu();
#if DIV_VM_BUILD
                    Serial.printf("[vm] entered '%s' submenu (%d items) — navigate with w/s, select with k\n",
                                  menu_items[current_menu_index], active_submenu_size);
#endif
                }

                if (is_main_menu) {
                    is_main_menu = false;
                    displayMenu();
                } else {
                    is_main_menu = true;
                }
            }
        }

        static unsigned long lastTouchTime = 0;
        const unsigned long touchFeedbackDelay = 100;

        if (!feature_active && (millis() - lastTouchTime >= touchFeedbackDelay)) {
            int x, y;
            if (!readTouchXY(x, y)) { return; }
            delay(10);
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
                int column = i / 4;
                int row = i % 4;
                int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
                int y_position = Y_START + row * Y_SPACING;

                int button_x1 = x_position;
                int button_y1 = y_position;
                int button_x2 = x_position + 100;
                int button_y2 = y_position + 60;

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    current_menu_index = i;
                    last_interaction_time = millis();
                    displayMenu();

                    unsigned long startTime = millis();
                    while (isTouchDownDismiss() && (millis() - startTime < touchFeedbackDelay)) {
                        delay(10);
                    }

                    if (isTouchDownDismiss()) {

                        if (current_menu_index == 3) {
                            handleSettingsSubmenuButtons();
                        } else if (current_menu_index == 7) {
                            handleAboutPage();
                        } else {
                            updateActiveSubmenu();

                            if (active_submenu_items && active_submenu_size > 0) {
                                current_submenu_index = 0;
                                if (current_menu_index == 2) {
                                    other_layer = OTHER_LAYER_HOME;
                                    other_menu_grid_initialized = false;
                                    last_other_menu_index = -1;
                                }
                                in_sub_menu = true;
                                submenu_initialized = false;
                                displaySubmenu();
                            } else {
                                if (is_main_menu) {
                                    is_main_menu = false;
                                    displayMenu();
                                } else {
                                    is_main_menu = true;
                                }
                            }
                        }
                    }
                    delay(200);
                    break;
                }
            }
        }
    }
}

void setup() {
  /*打开RGB-LED*/
#if !DIV_VM_BUILD
  strip.begin();
  strip.setPixelColor(0, strip.Color(0, 50, 30));
  strip.show();
#endif

  Serial.begin(115200);
  delay(50);
#if DIV_VM_BUILD
  // ESP_LOG writes to the UART through the ROM, bypassing the driver — those
  // bytes can land in the middle of the binary screen-stream packets and
  // corrupt them. Silence all ESP logs in VM builds (the sketch's own
  // Serial prints go through the driver and interleave atomically).
  esp_log_level_set("*", ESP_LOG_NONE);
#endif
  Serial.println("[boot] start");

#if !BOARD_HAS_ESP32S3
  // Weak USB / backlight load can brownout classic ESP32 during intro.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif

  tft.init();
  tft.setRotation(TFT_ROTATION);

#if !DIV_VM_BUILD
  // Real ESP32-DIV V2 panel is an inverted (IPS) type: without this the
  // colors show as their complement (black->white, green->magenta).
  // Guarded to hardware only so the QEMU panel emulator is unaffected.
  tft.invertDisplay(true);
#endif

#if !DIV_VM_BUILD
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(BACKLIGHT_PIN, PWM_CHANNEL);
#endif
  setBrightness(80);

  applyThemeToPalette(settings().theme);

  tft.fillScreen(TFT_BLACK);

#if DIV_VM_BUILD
  loading(15, UI_ICON, 0, 0, 2, true);   // shortened boot animation under QEMU
#else
  loading(100, UI_ICON, 0, 0, 2, true);
#endif

  tft.fillScreen(TFT_BLACK);
#if DIV_VM_BUILD
  displayLogo(UI.text, 150);              // shortened logo hold under QEMU
#else
  displayLogo(UI.text, 500);
#endif

  initSDCard();

#if BOARD_HAS_ESP32S3
  settingsLoad();
#else
  // Avoid SD mount via settingsLoad on v1 (same crash as step 3).
  settingsApplyBoardTouchDefaults();
  Serial.println("[boot] settings defaults (v1, SD deferred)");
#endif
  applyThemeToPalette(settings().theme);
  setBrightness(settings().brightness);

#if HAS_PCF8574_BUTTONS
  if (!initPcf8574Buttons()) {
    Serial.println("PCF8574 buttons unavailable");
  }
#else
  Serial.println("PCF8574 buttons disabled for this board");
#endif

#if BOARD_HAS_ESP32S3 && !DIV_VM_BUILD
  ensureBleStackReady();
#elif BOARD_HAS_ESP32S3
  // QEMU has no BT/WiFi controllers — initializing them asserts. Deferred on VM.
  Serial.println("[boot] BLE deferred (VM)");
#else
  // Classic ESP32: defer NimBLE; also skip boot-time WiFi scan task (heap/WDT).
  Serial.println("[boot] BLE/WiFi-bg deferred (v1)");
#endif

#if FEATURE_BLE_DUCKY && !DIV_VM_BUILD
  Ducky::setup();
#endif

#if BOARD_HAS_ESP32S3 && !DIV_VM_BUILD
  WifiScan::startBackgroundScanner();
  BleScan::startBackgroundScanner();
  startStatusBarTask();
#elif BOARD_HAS_ESP32S3
  // No background WiFi/BLE scans on the VM (no radios); status bar updates
  // from loop() instead.
  startStatusBarTask();
#else
  // Keep boot lightweight on ESP32 — status bar updates from loop() instead.
#endif



  menu_initialized = false;
  currentBatteryVoltage = readBatteryVoltage();
  displayMenu();
  drawStatusBar(currentBatteryVoltage, false);

  setupTouchscreen();
    
 /*蜂鸣器提示*/
#if !DIV_VM_BUILD
  ledcSetup(1, 1500, 8);//channel_1，KHz，8bit
  ledcAttachPin(BUZZER_PIN, 1);//BUZZER_PIN,channel_1
#endif
  ledcWrite(1, 125);//50%
  delay(100);
#if !DIV_VM_BUILD
  ledcSetup(1, 2000, 8);//channel_1，KHz，8bit
#endif
  delay(100);
  ledcWrite(1, 0);//OFF

  /*检查IP5306是否存在*/
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(IP5306_ADDR);

  if (Wire.endTransmission() == 0) {
    Serial.println("Detected IP5306");
    has_ip5306 = true;
    // Debug: raw battery-related registers, to calibrate the percentage map.
    static const uint8_t kIp5306Regs[] = {0x70, 0x71, 0x72, 0x77, 0x78, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F};
    for (uint8_t r : kIp5306Regs) {
      Wire.beginTransmission(IP5306_ADDR);
      Wire.write(r);
      if (Wire.endTransmission(false) == 0 && Wire.requestFrom(IP5306_ADDR, 1) == 1) {
        Serial.printf("[ip5306] reg 0x%02X = 0x%02X\n", r, Wire.read());
      } else {
        Serial.printf("[ip5306] reg 0x%02X read failed\n", r);
      }
      delay(4);
    }
  }
  else{
    Serial.println("IP5306 Error");
  }

  last_interaction_time = millis();
  Serial.printf("[boot] ready (theme=%d)\n", (int)settings().theme);
  xTaskCreatePinnedToCore(serialConsoleTask, "srvcmd", 3072, nullptr, 1, nullptr, 0);
  Serial.println("[mcp] remote console ready");

  // Register user apps into the Tools menu (back entry shifts down).
  {
    const int n = userAppCount();
    for (int i = 0; i < n && i < TOOLS_MAX_USER_APPS; i++) {
      tools_submenu_items[TOOLS_BASE_ITEMS - 1 + i] = userApps()[i].name;
      tools_submenu_icons[TOOLS_BASE_ITEMS - 1 + i] = bitmap_icon_dialog;
    }
    tools_submenu_items[TOOLS_BASE_ITEMS - 1 + n] = "Back to Main Menu";
    tools_submenu_icons[TOOLS_BASE_ITEMS - 1 + n] = bitmap_icon_go_back;
  }
#if DIV_VM_BUILD
  Serial.println("[vm] QEMU virtual device — no display/touch/radios are emulated.");
  Serial.println("[vm] Drive the UI from this serial console:");
  Serial.println("[vm]   w=UP  s=DOWN  a=LEFT  d=RIGHT  k or Enter=SELECT");
#endif
}

// ── remote-test console (for the MCP bridge / automated testing) ──
// Single-letter commands over USB serial: u/d/l/r/k = nav-bar tap (uses the
// same latch a real touch sets, so every tool reacts identically),
// s = state report, b = battery, h = help. Own task so it keeps working
// while a feature loop is blocking the main loop.
volatile bool serialConsoleEnabled = true;

static void serialConsoleTask(void*) {
  for (;;) {
    // Menus disable the touch-nav input after a feature exits; keep it
    // alive outside features so injected taps (and the nav bar) always work.
    if (!feature_active && !touchButtonInputEnabled) {
      setTouchButtonInputEnabled(true);
    }
    while (Serial.available()) {
      const char cmd = (char)Serial.read();
      if (!serialConsoleEnabled) continue;   // QR serial-input owns the port
      int idx = -1;
      uint16_t dur = 300;
      switch (cmd) {
        case 'U': dur = 260;  // fall through: deterministic single step
        case 'u': idx = touchNavIndexForPin(BTN_UP); break;
        case 'D': dur = 260;
        case 'd': idx = touchNavIndexForPin(BTN_DOWN); break;
        case 'L': dur = 260;
        case 'l': idx = touchNavIndexForPin(BTN_LEFT); break;
        case 'R': dur = 260;
        case 'r': idx = touchNavIndexForPin(BTN_RIGHT); break;
        case 'K': dur = 260;
        case 'k': idx = touchNavIndexForPin(BTN_SELECT); break;
        case 's':
          Serial.printf("[mcp] v=%s menu=%d(%s) sub=%d(%s) feature=%d theme=%d\n",
                        ESP32DIV_VERSION,
                        current_menu_index,
                        (current_menu_index >= 0 && current_menu_index < NUM_MENU_ITEMS)
                            ? menu_items[current_menu_index] : "?",
                        current_submenu_index,
                        (active_submenu_items && current_submenu_index >= 0 &&
                         current_submenu_index < active_submenu_size)
                            ? active_submenu_items[current_submenu_index] : "-",
                        feature_active ? 1 : 0, (int)settings().theme);
          break;
        case 'b': {
          Wire.beginTransmission(IP5306_ADDR);
          Wire.write(0x78);
          if (Wire.endTransmission(false) == 0 && Wire.requestFrom(IP5306_ADDR, 1) == 1) {
            Serial.printf("[mcp] battery reg78=0x%02X adc=%.2fV\n", Wire.read(),
                          (double)readBatteryVoltage());
          } else {
            Serial.println("[mcp] battery read FAIL");
          }
          break;
        }
        default:
          if (cmd == 'h' || cmd == '\n' || cmd == '\r') {
            Serial.println("[mcp] cmds: u d l r k = tap, s = state, b = battery");
          }
          break;
      }
      if (idx >= 0) {
        s_navTapLatchMs[idx] = millis();
        s_navTapLatchNew[idx] = true;
        s_navTapLatchDur[idx] = dur;
      }
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

void loop() {
  applyThemeToPalette(settings().theme);
#if DIV_VM_BUILD
  vmPollSerialButtons();
#endif
//   Serial.println("[app] 1");
  handleButtons();
//   Serial.println("[app] 2");
  updateStatusBar();
//   Serial.println("[app] 3");
}
