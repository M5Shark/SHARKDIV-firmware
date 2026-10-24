#!/usr/bin/env python3
"""Run the production audio task with a deterministic PWM/task fixture."""
from pathlib import Path
import os, shlex, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'sharkdiv/DetectionAlerts.cpp').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
fixture = r'''
#include "DetectionAlerts.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <functional>
#define DIV_VM_BUILD 0
#define PWM_CHANNEL 0
#define BUZZER_PIN 45
#define BACKLIGHT_PIN 46
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) == 45)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
using portMUX_TYPE = int;
using TaskHandle_t = void*;
bool feature_active = true;
uint32_t nowMs = 0;
uint32_t millis() {return nowMs;}
struct Stop {};
struct Tone {uint32_t ms, hz;};
std::vector<Tone> tones;
uint32_t frequencyNow[4] = {5000, 0, 0, 0}, duty[4] = {128, 0, 0, 0};
std::function<void()> tick;
void vTaskDelay(uint32_t value) {nowMs += value; tick(); if (nowMs >= 3000) throw Stop();}
int xTaskCreate(void(*)(void*),const char*,int,void*,int,TaskHandle_t* task) {*task=(void*)1;return pdPASS;}
uint32_t ledcSetup(int ch,uint32_t hz,int) {assert(ch==2);frequencyNow[ch]=hz;return hz;}
void ledcAttachPin(int pin,int ch) {assert(pin==45&&ch==2);}
void ledcDetachPin(int pin) {assert(pin==45);}
void ledcWrite(int ch,uint32_t value) {assert(ch==2);duty[ch]=value;}
void ledcWriteTone(int ch,uint32_t hz) {assert(ch==2);frequencyNow[ch]=hz;duty[ch]=512;tones.push_back({nowMs,hz});}
uint32_t ledcReadFreq(int ch) {return frequencyNow[ch];}
struct SerialFixture {void printf(const char*, ...) {}} Serial;
'''
checks = r'''
int main() {
 using namespace DetectionAlerts;
 begin(true);
 assert(available()&&enabled());
 tick=[] {
   if(nowMs==100) notify(Source::WiFi,42);
   if(nowMs>100&&nowMs<1100) notify(Source::WiFi,42);
   if(nowMs==180) assert(duty[2]==0); // single pulse finishes while scan continues
   if(nowMs==1100) {setEnabled(false);notify(Source::BLE,1);assert(!preview());}
   if(nowMs==1200) {setEnabled(true);notify(Source::GPS,1);}
   if(nowMs==1230) setEnabled(false);
   if(nowMs==1250) assert(duty[2]==0); // mute cancels a running pulse
   if(nowMs==1400) {setEnabled(true);assert(preview());}
   if(nowMs==1480) assert(duty[2]==0); // deliberate silent gap in double chirp
   if(nowMs==1630) assert(duty[2]==0);
   if(nowMs==2000) {notify(Source::Drone,1);}
   if(nowMs==2030) feature_active=false;
   if(nowMs==2050) assert(duty[2]==0); // exiting a tool stops an alert
   if(nowMs==2200) notify(Source::WiFi,2); // background results silent
   if(nowMs==2400) assert(preview()); // explicit test works on menu
 };
 try {audioTask(nullptr);} catch(Stop&) {}
 assert(tones.size()==7); // WiFi, GPS, 2 test, drone first pulse only, 2 test
 assert(tones[0].ms==100&&tones[1].ms==1200);
 assert(frequencyNow[0]==5000&&duty[0]==128); // backlight never reconfigured
 assert(duty[2]==0);
 puts("Production audio task: finite tones, mute, exit, background suppression and PWM isolation passed");
}
'''
with tempfile.TemporaryDirectory(prefix='sharkdiv-audio-') as directory:
    cpp=Path(directory)/'audio.cpp';binary=Path(directory)/'audio'
    cpp.write_text(fixture+source+checks)
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'sharkdiv'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
