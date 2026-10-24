#!/usr/bin/env python3
"""Execute the production bitmap renderer and two-stage boot using a mock TFT."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'sharkdiv/BootAnimation.cpp').read_text().replace('#include "config.h"', '')
ui = (root / 'sharkdiv/SharkUI.cpp').read_text()
start = ui.index('void playSharkBoot(')
first = ui.index('{', start)
depth, end = 1, first + 1
while depth:
    depth += (ui[end] == '{') - (ui[end] == '}')
    end += 1
logo = ui[start:end]

fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include "BootAnimation.h"
uint32_t nowMs = 0, frameCost = 50;
std::vector<uint32_t> waits;
std::vector<std::string> events;
uint32_t millis() { return nowMs; }
struct Display {
 int w=240,h=320;bool active=false,window=false;
 unsigned begins=0,ends=0,pixels=0,rows=0;
 uint8_t expected[BootAnimation::BufferBytes];
 int width(){return w;}int height(){return h;}
 void startWrite(){
  assert(!active);active=true;window=false;pixels=rows=0;
  BootAnimation::renderFrame(begins,expected);++begins;events.push_back("frame");
 }
 void setAddrWindow(int x,int y,int width,int height){
  assert(active&&!window&&x==0&&y==0&&width==240&&height==320);window=true;
 }
 void pushColors(uint16_t* row,unsigned count,bool swap){
  assert(active&&window&&count==240&&swap&&pixels+count<=240*320);
  for(unsigned i=0;i<count;++i){
   unsigned logicalX=((pixels+i)%240)/2,logicalY=((pixels+i)/240)/2;
   bool white=expected[logicalY*15+logicalX/8]&(0x80>>(logicalX&7));
   assert(row[i]==(white?0xffff:0));
  }
  pixels+=count;++rows;
 }
 void endWrite(){
  assert(active&&window&&pixels==240*320&&rows==320);
  active=false;++ends;nowMs+=frameCost;events.push_back("release");
 }
 void fillScreen(int){assert(!active);events.push_back("clear");}
 void drawBitmap(int,int,const uint8_t*,int,int,int){assert(!active);events.push_back("logo");}
 void setTextSize(int){}void setFreeFont(const int*){}void setTextDatum(int){}
 void setTextColor(int){}void setTextFont(int){}
 void drawString(const char* text,int,int){events.push_back(text);}
} tft;
void delay(uint32_t ms){assert(!tft.active&&ms>0);waits.push_back(ms);nowMs+=ms;events.push_back("delay");}
struct Palette{int bg=0,text=0xffff;} UI;
struct Settings{int theme=0;} appSettings;
Settings& settings(){return appSettings;}
void applyThemeToPalette(int){}
constexpr int SHARK_LOGO_W=150,SHARK_LOGO_H=140,UI_DIM_TEXT=1;
constexpr int C_BASELINE=0,TC_DATUM=1,TL_DATUM=2,FreeSansBold12pt7b=1;
const uint8_t shark_logo_bitmap[]={0};
#define ESP32DIV_NAME "name"
#define ESP32DIV_VERSION "version"
void reset(uint32_t time=0,uint32_t cost=50){
 tft=Display{};nowMs=time;frameCost=cost;waits.clear();events.clear();
}
'''
checks = r'''
int main(){
 using namespace BootAnimation;
 static_assert(BufferBytes==2400,"Portrait bitmap must stay small");
 static_assert(sizeof(skull_boot_frames)==3600,"Packed poses should remain in flash");
 struct Guarded{uint8_t before[16],image[BufferBytes],after[16];} frame;
 std::set<std::string> distinct;
 for(unsigned index=0;index<256;++index){
  memset(&frame,0xa5,sizeof(frame));renderFrame(uint8_t(index),frame.image);
  for(uint8_t b:frame.before)assert(b==0xa5);
  for(uint8_t b:frame.after)assert(b==0xa5);
  uint8_t again[BufferBytes];memset(again,0x55,sizeof(again));renderFrame(uint8_t(index),again);
  assert(!memcmp(frame.image,again,BufferBytes)); // deterministic, clears old pixels
  unsigned lit=0;
  for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){
   bool white=frame.image[y*15+x/8]&(0x80>>(x&7));lit+=white;
   if(y<35||y>=125)assert(!white);
  }
  if(index>=FrameCount-1)assert(lit==0); // black transition + invalid frame indices
  else {assert(lit>0&&lit<Width*Height/2);distinct.insert(std::string((char*)frame.image,BufferBytes));}
 }
 assert(distinct.size()>=8);renderFrame(0,nullptr);
 reset();play();
 assert(tft.begins==FrameCount&&tft.ends==FrameCount&&waits.size()==FrameCount);
 assert(nowMs==FrameCount*FrameMs);for(uint32_t ms:waits)assert(ms==40);
 reset(0,FrameMs+10);play();
 assert(waits.size()==FrameCount);for(uint32_t ms:waits)assert(ms==1); // late frames still yield
 reset(UINT32_MAX-20,70);uint32_t started=nowMs;play();
 assert(uint32_t(nowMs-started)==FrameCount*FrameMs);for(uint32_t ms:waits)assert(ms==20);
 reset();tft.w=320;tft.h=240;play();assert(!tft.begins&&waits.empty());
 reset();playSharkBoot(2000);
#if DIV_VM_BUILD
 assert(tft.begins==0&&waits.size()==1);
#else
 assert(tft.begins==FrameCount&&tft.ends==FrameCount&&waits.size()==FrameCount+1);
#endif
 assert(waits.back()==2000);
 auto clear=events.end(),bitmap=events.end(),name=events.end(),version=events.end();
 for(auto i=events.begin();i!=events.end();++i){
  if(*i=="clear")clear=i;else if(*i=="logo")bitmap=i;else if(*i=="name")name=i;else if(*i=="version")version=i;
 }
 assert(clear<bitmap&&bitmap<name&&name<version&&version+1==events.end()-1&&events.back()=="delay");
 if(tft.begins)assert(clear>events.begin()&&*(clear-1)=="delay"); // entire intro completes before logo clear
 puts("Boot animation: guarded frames, exact pixel stream, bounded timing and skull/logo order passed");
}
'''
with tempfile.TemporaryDirectory(prefix='boot-animation-') as directory:
    folder = Path(directory)
    (folder / 'Arduino.h').write_text('#pragma once\n#include <stdint.h>\n#define PROGMEM\n#define pgm_read_byte(p) (*(const uint8_t*)(p))\n')
    cpp = folder / 'boot.cpp'
    cpp.write_text(fixture + source + logo + checks)
    for vm in [0, 1]:
        binary = folder / ('boot-' + str(vm))
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=c++11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer', '-DDIV_VM_BUILD=' + str(vm),
            '-I' + str(folder), '-I' + str(root / 'sharkdiv'), str(cpp), '-o', str(binary)
        ], check=True)
        subprocess.run([str(binary)], check=True)
