#!/usr/bin/env python3
"""Run the production Settings screen input/save logic against controlled I/O."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "sharkdiv/utils.cpp").read_text()
start = source.index("namespace AppSettingsUI {")
end = source.index("{", start) + 1
depth = 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1

fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
enum class Theme {Dark,Light,Hacker,Shark};
constexpr int kThemeCount=4, ACCENT_PRESET_COUNT=7;
constexpr int BTN_UP=0, BTN_DOWN=1, BTN_LEFT=2, BTN_RIGHT=3, BTN_SELECT=4;
constexpr int TL_DATUM=0,TR_DATUM=1,ML_DATUM=2,MR_DATUM=3,TFT_BLACK=0,UI_DIM_TEXT=1;
struct AppSettings {
 uint8_t brightness=200,accentColor=0;
 Theme theme=Theme::Shark;
 bool neopixelEnabled=false,detectionSound=true,autoWifiScan=true,autoBleScan=true;
} config;
struct {uint16_t bg=0,icon=0xffff,text=0xffff,line=1;} UI;
struct MockTft {
 int width() const {return 240;}
 void fillScreen(uint16_t) {}
 void setTextSize(int) {}
 void setTextFont(int) {}
 void setTextDatum(int) {}
 void setTextColor(uint16_t,uint16_t) {}
 void fillRect(int,int,int,int,uint16_t) {}
 void drawFastHLine(int,int,int,uint16_t) {}
 void drawString(const char*,int,int,int) {}
} tft;
bool keys[5]={},touch=false,feature_exit_requested=false,navEnabled=false;
bool nvsSuccess=true,sdSuccess=true,audioAvailable=true,audioEnabled=true;
int touchX=0,touchY=0,nvsWrites=0,sdWrites=0,previewCalls=0,backlight=0;
float currentBatteryVoltage=4.1f;
AppSettings& settings(){return config;}
void applyThemeToPalette(Theme) {}
void drawStatusBar(float,bool) {}
const char* accentPresetName(uint8_t){return "Orange";}
void setTouchNavLabels(const char*,const char*,const char*,const char*,const char*) {}
void redrawTouchButtonBar() {}
void setTouchButtonInputEnabled(bool value){navEnabled=value;}
void setBrightness(uint8_t value){backlight=value;}
void delay(unsigned) {}
bool isButtonPressed(int pin){return keys[pin];}
bool readTouchXY(int& x,int& y){x=touchX;y=touchY;return touch;}
bool settingsSave(){++sdWrites;return sdSuccess;}
bool settingsSetDetectionSound(bool value){++nvsWrites;config.detectionSound=value;return nvsSuccess;}
namespace DetectionAlerts {
bool available(){return audioAvailable;}
void setEnabled(bool value){audioEnabled=value;}
bool preview(){++previewCalls;return audioEnabled&&audioAvailable;}
}
'''
fixture += source[start:end]
fixture += r'''
void resetFixture(bool arm=true) {
 config=AppSettings{};
 for(bool& key:keys)key=false;
 touch=false; feature_exit_requested=false; nvsSuccess=sdSuccess=true;
 audioAvailable=audioEnabled=true; nvsWrites=sdWrites=previewCalls=0;
 AppSettingsUI::setup();
 if(arm)AppSettingsUI::loop(); // observe all opening inputs released
}
void pulse(int pin) {
 keys[pin]=true; AppSettingsUI::loop();
 keys[pin]=false; AppSettingsUI::loop();
}
int main() {
 // Holding the physical opening Select key must not immediately save/exit.
 resetFixture(false);keys[BTN_SELECT]=true;
 for(int i=0;i<30;++i)AppSettingsUI::loop();
 assert(!feature_exit_requested && AppSettingsUI::opening && sdWrites==0);
 keys[BTN_SELECT]=false;AppSettingsUI::loop();
 assert(!AppSettingsUI::opening);
 keys[BTN_SELECT]=true;AppSettingsUI::loop();
 assert(feature_exit_requested && !navEnabled);

 // The touch that opened Settings is also ignored until released.
 resetFixture(false);touch=true;touchX=40;touchY=46+5*29+5;
 for(int i=0;i<30;++i)AppSettingsUI::loop();
 assert(config.detectionSound && nvsWrites==0 && !feature_exit_requested);
 touch=false;AppSettingsUI::loop();
 touch=true;AppSettingsUI::loop();
 assert(!config.detectionSound && !audioEnabled && nvsWrites==1);
 for(int i=0;i<100;++i)AppSettingsUI::loop();
 assert(nvsWrites==1); // held row never toggles twice
 touch=false;AppSettingsUI::loop();touch=true;AppSettingsUI::loop();
 assert(config.detectionSound && audioEnabled && nvsWrites==2);

 // Clamp before conversion to uint8_t, preventing wrap or a black screen.
 resetFixture();config.brightness=250;pulse(BTN_RIGHT);
 assert(config.brightness==255 && backlight==255);
 pulse(BTN_RIGHT);assert(config.brightness==255);
 config.brightness=17;pulse(BTN_LEFT);
 assert(config.brightness==16 && backlight==16);
 pulse(BTN_LEFT);assert(config.brightness==16);

 // Failed local sound persistence is visible; the live sound toggle still works.
 resetFixture();nvsSuccess=false;AppSettingsUI::selected=5;pulse(BTN_RIGHT);
 assert(!audioEnabled && !config.detectionSound && nvsWrites==1);
 assert(strstr(AppSettingsUI::message,"device save failed"));
 assert(AppSettingsUI::dirty && !feature_exit_requested);

 // Failed SD saving keeps the screen open and does not retry while held.
 resetFixture();pulse(BTN_RIGHT);sdSuccess=false;
 keys[BTN_SELECT]=true;AppSettingsUI::loop();
 assert(sdWrites==1 && !feature_exit_requested && navEnabled);
 assert(strstr(AppSettingsUI::message,"SD save failed"));
 for(int i=0;i<100;++i)AppSettingsUI::loop();assert(sdWrites==1);
 keys[BTN_SELECT]=false;AppSettingsUI::loop();sdSuccess=true;
 keys[BTN_SELECT]=true;AppSettingsUI::loop();
 assert(sdWrites==2 && feature_exit_requested && !AppSettingsUI::dirty);

 // Back remains available if storage cannot save.
 resetFixture();pulse(BTN_RIGHT);sdSuccess=false;
 touch=true;touchX=220;touchY=30;AppSettingsUI::loop();
 assert(feature_exit_requested && !navEnabled && sdWrites==0);
 puts("Settings opening press, held touch, brightness bounds and save failures passed");
}
'''
with tempfile.TemporaryDirectory(prefix="sharkdiv-settings-test-") as temp:
    cpp = Path(temp) / "test.cpp"
    binary = Path(temp) / "test"
    cpp.write_text(fixture)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
