#!/usr/bin/env python3
"""Execute production receiver guards with host fakes and ASan/UBSan.

The fakes preserve the two ownership behaviors that caused real regressions:
NimBLE results borrow scanner-owned pointers; Arduino consumes IDF scan records.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    level = 1
    pos = brace + 1
    while level:
        level += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos]


def run(name, body):
    with tempfile.TemporaryDirectory(prefix='sharkdiv-receiver-') as tmp:
        src = Path(tmp) / (name + '.cpp')
        binary = Path(tmp) / name
        src.write_text(body)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-g', '-O1',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(src), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0:halt_on_error=1',
                            'UBSAN_OPTIONS': 'halt_on_error=1'})


gps = (ROOT / 'sharkdiv/gps.cpp').read_text()
run('gps_fix', r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <string>
using std::isfinite;
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
uint32_t clockMs = 10;
uint32_t millis() { return clockMs; }
namespace DetectionAlerts { enum class Source { GPS }; int calls = 0;
void notify(Source, uint32_t) { ++calls; } }
bool s_alertHadFix=false, rmcNavValid=false;
uint32_t s_alertLastFixSentenceMs=0, lastNavMs=0;
uint8_t fixQuality=0, satsUsedGga=0;
double navLat=NAN, navLon=NAN;
float hdopLive=-1, navAltM=NAN;
void formatUtcFromField(const char*) {}
void formatDateFromRmc(const char*) {}
bool handleGsvSentence(char*) { return false; }
bool handleGsaSentence(char*) { return false; }
''' + '\n'.join(function(gps, sig) for sig in (
    'static bool validNmeaChecksum(', 'static void observeGpsFix(',
    'void stripChecksum(', 'int splitCommaFields(', 'double dmToDeg(',
    'bool handleGgaSentence(', 'bool handleRmcSentence(', 'void dispatchNmeaLine('
)) + r'''
std::string sentence(const char* body) {
  uint8_t sum=0; for (const char* p=body; *p; ++p) sum ^= *p;
  char tail[4]; snprintf(tail,sizeof(tail),"*%02X",sum);
  return "$"+std::string(body)+tail;
}
void deliver(std::string s) { dispatchNmeaLine(&s[0]); }
int main() {
  const char* valid="GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
  auto s=sentence(valid); assert(validNmeaChecksum(s.c_str()));
  assert(validNmeaChecksum("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
  for (size_t i=0;i<s.size();++i) assert(!validNmeaChecksum(s.substr(0,i).c_str()));
  assert(!validNmeaChecksum(nullptr));
  assert(!validNmeaChecksum("$GPGGA,1*ZZ"));
  assert(!validNmeaChecksum((s+"EXTRA").c_str()));
  auto corrupt=s; corrupt[8]='9'; deliver(corrupt);
  assert(DetectionAlerts::calls==0 && std::isnan(navLat));
  deliver(s); assert(DetectionAlerts::calls==1);
  assert(std::abs(navLat-48.1173)<0.000001);
  for(int i=0;i<10;++i) {clockMs+=1000; deliver(s);}
  assert(DetectionAlerts::calls==1);
  deliver(sentence("GPGGA,123519,4807.038,N,01131.000,E,0,00,0.9,545.4,M,46.9,M,,"));
  deliver(s); assert(DetectionAlerts::calls==2);
  clockMs+=6000; deliver(s); assert(DetectionAlerts::calls==3);
  // Garbage coordinates previously became zero through strtod; range overflow
  // and simulated/dead-reckoning fixes must also remain silent.
  deliver(sentence("GPGGA,123519,garbage,N,01131.000,E,1,08,0.9,1,M,1,M,,"));
  deliver(sentence("GPGGA,123519,9160.000,N,18100.000,E,1,08,0.9,1,M,1,M,,"));
  deliver(sentence("GPGGA,123519,4807.038,N,01131.000,E,8,08,0.9,1,M,1,M,,"));
  assert(DetectionAlerts::calls==3);
  for(const char* bad:{"nan","inf","-0000.00","4807.0junk","4860.00","9000.01","4807..0"})
    assert(std::isnan(dmToDeg(bad,'N')));
  assert(dmToDeg("9000.00",'S')==-90);
  assert(dmToDeg("18000.00",'W')==-180);
  deliver(sentence("GPRMC,123519,V,4807.038,N,01131.000,E,0,0,230394,,,A"));
  assert(DetectionAlerts::calls==3);
  deliver(sentence("GPRMC,123519,A,4807.038,N,01131.000,E,0,0,230394,,,S"));
  assert(DetectionAlerts::calls==3);
  deliver(sentence("GPRMC,123519,A,4807.038,N,01131.000,E,0,0,230394,,,A"));
  assert(DetectionAlerts::calls==4);
  s_alertHadFix=true; s_alertLastFixSentenceMs=0xFFFFFFF0u; clockMs=20;
  observeGpsFix(true); assert(DetectionAlerts::calls==4);
  puts("GPS receiver: checksums, malformed/range inputs, true fix edges and clock wrap passed");
}
''')

ble = (ROOT / 'sharkdiv/bluetooth.cpp').read_text().split('namespace BleScan {', 1)[1]
run('ble_ownership', r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
struct BLEScanResults { std::vector<int*> pointers; int getCount() {return pointers.size();} };
BLEScanResults bleResults;
bool bgHasResults=true, bleInitDone=false;
int bgLastCount=1, s_scanAlerts=0;
int bgBleScanMutex=1, lockDepth=0;
const int portMAX_DELAY=0;
void xSemaphoreTake(int,int){++lockDepth;}
void xSemaphoreGive(int){assert(lockDepth==1);--lockDepth;}
struct Log {void println(const char*) {}} Serial;
bool ready=true, host=true;
bool ensureBleStackReady(){return ready;}
bool runBleHostOperation(void (*fn)(void*),void* ctx){if(!host)return false;fn(ctx);return true;}
struct BLEScan {
 bool stopOk=true, active=true, duplicates=false; int clears=0,maxResults=0,interval=0,window=0;
 void* callbacks=nullptr;
 bool stop(){return stopOk;}
 void clearResults(){++clears;}
 void setAdvertisedDeviceCallbacks(void* cb,bool){callbacks=cb;}
 void setMaxResults(int n){maxResults=n;}
 void setActiveScan(bool x){active=x;}
 void setInterval(int x){interval=x;}
 void setWindow(int x){window=x;}
 void setDuplicateFilter(bool x){duplicates=x;}
} scanner;
BLEScan* bleScan=nullptr;
namespace BLEDevice {BLEScan* getScan(){return &scanner;}}
''' + '\n'.join(function(ble, sig) for sig in (
    'static void invalidateBleCache(', 'static bool ensureBleInit(',
    'void waitForBackgroundScanner('
)) + r'''
int main(){
  // Simulate a completed background scan, handoff, then scanner-owned deletion.
  int* device=new int(42); bleResults.pointers.push_back(device);
  waitForBackgroundScanner(); delete device;
  assert(!bgHasResults && bgLastCount==0 && bleResults.getCount()==0 && lockDepth==0);
  assert(ensureBleInit()); assert(scanner.clears==1 && scanner.maxResults==64);
  assert(!scanner.active && scanner.duplicates && scanner.callbacks==&s_scanAlerts);
  assert(scanner.interval==100 && scanner.window==80);
  scanner.stopOk=false; const int clears=scanner.clears;
  assert(!ensureBleInit() && scanner.clears==clears);
  ready=false; assert(!ensureBleInit());
  ready=true; host=false; assert(!ensureBleInit());
  bgBleScanMutex=0; bgHasResults=true; bgLastCount=1;
  bleResults.pointers.push_back(nullptr); waitForBackgroundScanner();
  assert(!bgHasResults && bgLastCount==0 && bleResults.getCount()==0);
  puts("BLE receiver: borrowed-result handoff, bounded reconfiguration and failures passed");
}
''')

wifi = (ROOT / 'sharkdiv/wifi.cpp').read_text().split('namespace WpsScanner {', 1)[1]
run('wps_results', r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
struct wifi_ap_record_t {uint8_t ssid[33],bssid[6]; int rssi,primary,authmode; bool wps;};
struct WpsAp {char ssid[33]; uint8_t bssid[6]; int rssi,channel,authmode;};
constexpr int MAX_WPS_APS=3, WIFI_STA=1;
WpsAp s_aps[MAX_WPS_APS];
bool s_scanning=false; int s_count=0,s_currentIndex=0,s_currentPage=0,s_lastRenderedPage=0;
void displayScanning(){} void updateNavLabels(){} void pauseBackgroundRadioTasks(){}
void delay(int){} void drawScanScreen(bool){} uint32_t wifiStaScanMsPerChannel(){return 300;}
void truncCopy(char* d,size_t n,const char* s,int){snprintf(d,n,"%s",s);}
int compareWpsAp(const void* a,const void* b){return ((const WpsAp*)b)->rssi-((const WpsAp*)a)->rssi;}
struct WifiMock {
 std::vector<wifi_ap_record_t> observed,cache; bool erased=false; int scanResult=0;
 void mode(int){} void disconnect(bool off,bool erase){assert(!off);erased=erase;}
 void scanDelete(){cache.clear();}
 int scanNetworks(bool,bool,bool,uint32_t){cache=observed; return scanResult;}
 void* getScanInfoByIndex(int i){return i>=0 && i<(int)cache.size()? &cache[i]:nullptr;}
} WiFi;
namespace DetectionAlerts {enum class Source {WiFi}; int calls=0;
uint32_t hash(const uint8_t*,size_t){return 1;} void notify(Source,uint32_t){++calls;}}
''' + function(wifi, 'static void runScan() {') + r'''
int main(){
  wifi_ap_record_t a{}; memcpy(a.ssid,"normal",7); a.rssi=-40; a.wps=false;
  wifi_ap_record_t b{}; memcpy(b.ssid,"wps",4); b.rssi=-60; b.wps=true; b.primary=6;
  WiFi.observed={a,b}; WiFi.scanResult=2;
  runScan(); assert(s_count==1 && strcmp(s_aps[0].ssid,"wps")==0);
  assert(s_aps[0].channel==6 && DetectionAlerts::calls==1 && !WiFi.erased);
  WiFi.observed={a}; WiFi.scanResult=1;
  runScan(); assert(s_count==0 && DetectionAlerts::calls==1);
  WiFi.observed.clear(); WiFi.scanResult=-2;
  runScan(); assert(s_count==0 && DetectionAlerts::calls==1 && !s_scanning);
  puts("WPS receiver: retained Arduino results, non-WPS filtering and failed scans passed");
}
''')
