#!/usr/bin/env python3
"""Exercise production receiver event gates and Radar scan polling without radios."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

def function(path, signature):
    source = (root / 'sharkdiv' / path).read_text()
    start = source.index(signature)
    first = source.index('{', start)
    depth, end = 1, first + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include "DetectionAlerts.h"
#include "DetectorSupport.h"
#include "RemoteIdParser.h"
uint32_t clockMs = 5000;
uint32_t millis() { return clockMs; }
int lockDepth = 0;
#define portENTER_CRITICAL(lock) do { (void)(lock); ++lockDepth; } while(0)
#define portEXIT_CRITICAL(lock) do { (void)(lock); --lockDepth; } while(0)
namespace DetectionAlerts {
int calls = 0; Source lastSource; uint32_t lastIdentity;
uint32_t hash(const uint8_t* p, size_t n) {
 uint32_t h=2166136261UL;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619UL;return h;
}
void notify(Source source, uint32_t identity) {
 assert(lockDepth==0);++calls;lastSource=source;lastIdentity=identity;
}
void reset(){calls=0;}
}
namespace flk { constexpr int kRssiPossible=-95; }
'''
wifi = r'''
namespace Wifi {
struct Hit {char mac[18],ssid[33];int8_t rssi;bool bySsid,byOui,byProbe,byFingerprint;uint32_t lastMs;};
constexpr int kMaxHits=12;Hit s_hits[kMaxHits];int s_hitCount=0,s_lock=0;
bool s_matchSound=true,s_acceptProbes=false;
'''
wifi += function('flock_wifi.cpp', 'void setMatchSound(')
wifi += function('flock_wifi.cpp', 'static void upsert(')
wifi += r'''
void test(){
 using namespace DetectionAlerts;
 uint8_t mac[6]={0xb4,0x1e,0x52,0,0,1};reset();
 upsert(mac,"Flock",-80,true,false,false,false);assert(calls==1&&lastSource==Source::Flock);
 setMatchSound(false);clockMs++;upsert(mac,"Flock",-70,true,false,false,false);
 assert(calls==1&&s_hits[0].lastMs==clockMs); // mute does not stop reception
 setMatchSound(true);upsert(mac,"",-60,false,true,true,false);assert(calls==1); // stopped callback
 s_acceptProbes=true;upsert(mac,"",-96,false,true,true,false);assert(calls==1); // below threshold
 upsert(mac,"",-80,false,true,true,false);assert(calls==2);
}
}
'''
ble = r'''
namespace Ble {
struct Hit {char name[24],mac[18];int8_t rssi;uint8_t evidence;uint32_t lastMs;};
constexpr int kMaxHits=8;Hit s_hits[kMaxHits];int s_count=0,s_lock=0;
bool s_matchSound=true,s_accept=false;
namespace FlockSignatures {constexpr uint8_t ByService=4;}
'''
ble += function('flock_ble.cpp', 'void setMatchSound(')
ble += function('flock_ble.cpp', 'static void upsert(')
ble += r'''
void test(){
 using namespace DetectionAlerts;reset();
 upsert("Raven","01:02:03:04:05:06",-50,1);assert(calls==0);
 s_accept=true;upsert("Raven","01:02:03:04:05:06",-50,1);
 assert(calls==1&&lastSource==Source::Flock);
 setMatchSound(false);upsert("Raven","01:02:03:04:05:06",-40,1);
 assert(calls==1&&s_hits[0].rssi==-40);
 setMatchSound(true);upsert("Raven","01:02:03:04:05:06",-96,1);assert(calls==1);
}
}
'''
detector = r'''
namespace Detector {
struct Sig{const char* detName;};
DetectorSupport::Table s_table;int s_detectorLock=0;const Sig* s_active=nullptr;
'''
detector += function('detectors.cpp', 'static bool cameraMode(')
detector += function('detectors.cpp', 'static bool gearMode(')
detector += function('detectors.cpp', 'static void upsert(')
detector += r'''
void test(){
 using namespace DetectionAlerts;reset();
 const Sig camera={"WiFi Cameras"},gear={"LE Gear"},other={"Flipper Zero"};
 const char* mac="01:02:03:04:05:06";
 upsert(nullptr,"",mac,-50,0,1);assert(calls==0);
 s_active=&camera;upsert(&other,"",mac,-50,0,1);assert(calls==0);
 upsert(&camera,"",mac,-50,0,DetectorSupport::Advertisement);
 assert(calls==1&&lastSource==Source::BLE); // Inspector must not claim a camera
 upsert(&camera,"",mac,-50,6,DetectorSupport::Vendor);assert(lastSource==Source::Camera);
 s_active=&gear;upsert(&gear,"",mac,-50,6,DetectorSupport::Vendor);assert(lastSource==Source::Gear);
 s_active=&other;upsert(&other,"",mac,-50,6,DetectorSupport::Name);assert(lastSource==Source::WiFi);
 upsert(&other,"",mac,-50,0,DetectorSupport::Name);assert(lastSource==Source::BLE);
}
}
'''
rid = r'''
namespace Rid {
struct RidHit{uint8_t mac[6],channel;int8_t rssi;RemoteId::Observation value;uint32_t seenMs,idMs,locationMs;};
constexpr int kRidMax=12;RidHit s_ridHits[kRidMax];int s_ridCount=0,s_ridLock=0;
bool s_ridReceiving=false;
'''
rid += function('detectors.cpp', 'static void ridRecord(')
rid += r'''
void test(){
 using namespace DetectionAlerts;reset();uint8_t mac[6]={1,2,3,4,5,6};RemoteId::Observation value;
 value.hasId=true;strcpy(value.id,"DRONE-1");
 ridRecord(mac,0,-60,value);assert(calls==0);
 s_ridReceiving=true;ridRecord(mac,0,-60,value);assert(calls==1&&lastSource==Source::Drone);
 assert(s_ridCount==1&&!strcmp(s_ridHits[0].value.id,"DRONE-1"));
 s_ridReceiving=false;ridRecord(mac,0,-50,value);assert(calls==1);
}
}
'''
radar = r'''
namespace Radar {
using String=std::string;
constexpr int WIFI_SCAN_RUNNING=-1,ESP_ERR_TIMEOUT=0x107;
struct Radio{
 int result=-1,starts=0,deletes=0;uint8_t macs[60][6]={};
 int scanComplete(){return result;}
 void scanDelete(){++deletes;}
 int scanNetworks(bool async,bool hidden,bool passive,int dwell){
   assert(async&&hidden&&passive&&dwell==180);++starts;return result;
 }
 const uint8_t* BSSID(int i){return macs[i];}
 int RSSI(int i){return -40-i;}
 String SSID(int i){return i?"other":"network";}
} WiFi;
int stopped=0;void esp_wifi_scan_stop(){++stopped;}
uint32_t s_lastScan=0,s_scanStarted=0;
bool s_scanRunning=false,s_ready=true;int s_found=0,s_error=0;
struct RadarHit{int8_t rssi;uint16_t angle;};RadarHit s_hits[48];int s_hitCount=0;
String s_bestName;int s_bestRssi=-128;
'''
radar += function('SharkTools.cpp', 'static uint16_t keyHash(')
radar += function('SharkTools.cpp', 'static void pollScan(')
radar += r'''
void test(){
 using namespace DetectionAlerts;reset();clockMs=10000;
 for(int i=0;i<60;i++)WiFi.macs[i][5]=i;
 pollScan();assert(s_scanRunning&&WiFi.starts==1&&calls==0);
 clockMs+=200;pollScan();assert(WiFi.starts==1&&calls==0); // ongoing scan never restarted
 WiFi.result=2;pollScan();assert(!s_scanRunning&&s_found==2&&s_hitCount==2&&calls==2);
 assert(lastSource==Source::WiFi&&s_bestName=="network");
 clockMs+=100;pollScan();assert(calls==2&&WiFi.starts==1); // cached plot stays silent
 clockMs+=2500;WiFi.result=-1;pollScan();assert(s_scanRunning&&s_hitCount==2);
 clockMs+=6500;pollScan();assert(!s_scanRunning&&s_error==ESP_ERR_TIMEOUT&&stopped==1&&calls==2);
 pollScan();assert(WiFi.starts==2); // failed scans have retry backoff
 clockMs+=2500;WiFi.result=-2;pollScan();assert(s_error==-2&&!s_scanRunning&&calls==2);
 clockMs+=2500;WiFi.result=60;pollScan();assert(s_scanRunning);
 pollScan();assert(s_found==60&&s_hitCount==48&&calls==62); // bounded plot, all fresh events
 clockMs=UINT32_MAX-100;s_lastScan=clockMs-2500;WiFi.result=-1;pollScan();
 clockMs=6500;pollScan();assert(!s_scanRunning&&stopped==2); // deadline handles rollover
}
}
'''
monitor = r'''
namespace Monitor {
using wifi_promiscuous_pkt_type_t=int;
struct wifi_promiscuous_filter_t{int filter_mask;};
constexpr int WIFI_PROMIS_FILTER_MASK_MGMT=1,WIFI_SECOND_CHAN_NONE=0,ESP_OK=0;
int failAt=-1,stops=0;bool enabled=false,attached=false,channelSet=false;
struct DetectorWifiDriver{};
namespace PassiveWifiLifecycle {template<class T>bool start(T&){return failAt!=0;}}
bool stopDetectorWifi(){++stops;enabled=attached=false;return true;}
int esp_wifi_set_promiscuous_filter(wifi_promiscuous_filter_t*){return failAt==1?-1:0;}
int esp_wifi_set_promiscuous_rx_cb(void (*)(void*,int)){
 if(failAt==2)return -1;attached=true;return 0;
}
int esp_wifi_set_channel(int channel,int){assert(channel==6);if(failAt==3)return -1;channelSet=true;return 0;}
int esp_wifi_set_promiscuous(bool value){assert(attached&&channelSet&&value);enabled=value;return failAt==4?-1:0;}
'''
monitor += function('detectors.cpp', 'static bool startPassiveMonitor(')
monitor += r'''
void callback(void*,int){}
void test(){
 for(failAt=0;failAt<=4;++failAt){
  stops=0;enabled=attached=channelSet=false;
  assert(!startPassiveMonitor(&callback));assert(stops==1&&!enabled&&!attached);
 }
 failAt=-1;stops=0;enabled=attached=channelSet=false;
 assert(startPassiveMonitor(&callback));assert(stops==0&&enabled&&attached&&channelSet);
}
}
'''
main = r'''
int main(){Wifi::test();Ble::test();Detector::test();Rid::test();Radar::test();Monitor::test();
 assert(lockDepth==0);puts("Receiver notification and passive Radar tests passed");}
'''
with tempfile.TemporaryDirectory(prefix='receiver-alerts-') as directory:
    cpp = Path(directory) / 'receivers.cpp'
    binary = Path(directory) / 'receivers'
    cpp.write_text(fixture + wifi + ble + detector + rid + radar + monitor + main)
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
        '-fno-omit-frame-pointer', '-I' + str(root / 'sharkdiv'), str(cpp), '-o', str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True)
