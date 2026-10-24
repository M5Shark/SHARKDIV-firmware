#!/usr/bin/env python3
"""Run the production Flock phase engine with deterministic receiver fixtures."""
from pathlib import Path
import os, shlex, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
source = (root/'sharkdiv/flock_menu.cpp').read_text()
def function(name):
    start = source.index('static void '+name+'(')
    first = source.index('{', start); depth = 1; end = first+1
    while depth:
        depth += (source[end]=='{') - (source[end]=='}'); end += 1
    return source[start:end]
fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
enum Phase : uint8_t { P_WIFI, P_PROBE, P_BLE, P_SENSORS };
static Phase s_phase;
static bool s_phaseEntered, s_phaseOk;
static uint32_t s_phaseStart, clockMs;
static uint8_t s_mode;
static bool s_settings[5];
static int s_nrfAct, s_lteStr, scored;
uint32_t millis() { return clockMs; }
namespace flk { constexpr uint32_t kWifiWindowMs=4000, kProbeWindowMs=3000, kBleWindowMs=4000; }
namespace flock_wifi {
int starts, stops, probes, polls; bool complete, success=true, hop;
bool startScan(){++starts;return success;}
bool pollScan(int* n){*n=0;return complete;}
void stop(){++stops;}
bool startProbeWindow(bool value){++probes;hop=value;return success;}
void stopProbeWindow(){}
void pollProbes(){++polls;}
}
namespace flock_ble {
int starts, stops;bool active;
bool startWindow(){++starts;active=true;return true;}
void stopWindow(){++stops;active=false;}
bool running(){return active;}
void poll(){}
}
namespace flock_sensors {int nrf,sub;int sampleNrf(){return ++nrf;}int sampleLte(){return ++sub;} }
void scoreAndAlert(){++scored;}
'''
checks=r'''
void reset(uint8_t mode) {
 s_phaseEntered=false;s_phaseOk=false;s_phaseStart=0;clockMs=5000;
 s_mode=mode;for(auto& enabled:s_settings)enabled=true;
 flock_wifi::starts=flock_wifi::stops=flock_wifi::probes=flock_wifi::polls=0;
 flock_wifi::complete=false;flock_wifi::success=true;
 flock_ble::starts=flock_ble::stops=0;flock_ble::active=false;
 flock_sensors::nrf=flock_sensors::sub=scored=0;
 selectPhase(mode==2?P_BLE:P_WIFI);
}
int main(){
 reset(0);runPhase();assert(flock_wifi::starts==1); // late first tick starts
 clockMs+=500;runPhase();assert(flock_wifi::starts==1); // no restart loop
 flock_wifi::complete=true;runPhase();assert(s_phase==P_PROBE);
 runPhase();assert(flock_wifi::probes==1&&flock_wifi::polls==1);
 clockMs+=3000;runPhase();assert(s_phase==P_BLE&&flock_wifi::polls==2);
 runPhase();assert(flock_ble::starts==1);
 clockMs+=4000;runPhase();assert(s_phase==P_SENSORS);
 runPhase();assert(scored==1&&flock_sensors::nrf==1&&flock_sensors::sub==1);
 assert(s_phase==P_WIFI&&!s_phaseEntered);
 reset(2);runPhase();clockMs+=4000;runPhase();
 assert(scored==1&&s_phase==P_BLE&&!s_phaseEntered);
 assert(flock_wifi::starts==0&&flock_wifi::probes==0&&flock_sensors::nrf==0);
 runPhase();assert(flock_ble::starts==2);
 reset(1);runPhase();clockMs+=8000;runPhase();
 assert(s_phase==P_PROBE&&flock_wifi::stops==1); // hung AP scan bounded
 s_settings[0]=false;runPhase();assert(!flock_wifi::hop);
 clockMs+=3000;runPhase();assert(scored==1&&s_phase==P_WIFI);
 assert(flock_ble::starts==0&&flock_sensors::nrf==0);
 reset(0);flock_wifi::success=false;runPhase();assert(s_phase==P_PROBE);
 runPhase();assert(s_phase==P_BLE); // errors do not wedge the cycle
 puts("Flock phase tests passed");
}
'''
with tempfile.TemporaryDirectory(prefix='flock-ui-') as directory:
    cpp=Path(directory)/'phases.cpp';binary=Path(directory)/'phases'
    cpp.write_text(fixture+'\n'.join(function(n) for n in ['selectPhase','finishCycle','afterWifi','runPhase'])+checks)
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

# The logging gate must not relabel retained hits at a newly acquired GPS fix.
log_source = (root/'sharkdiv/flock_log.cpp').read_text()
start = log_source.index('bool canAlert(')
end = log_source.index('bool exportDeFlock()', start)
gate = log_source[start:end]
log_fixture = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
uint32_t nowMs=1000;
uint32_t millis(){return nowMs;}
namespace flk {constexpr uint32_t kRealertMs=30000;}
struct AlertT {char mac[18]; uint32_t ms, observedMs;};
AlertT s_alerts[16]; int s_alertCount=0;
"""
log_checks = r"""
int main(){
 const char* mac="00:11:22:33:44:55";
 assert(canAlert(mac,1000));assert(!canAlert(mac,1000));
 nowMs=2000;assert(!canAlert(mac,2000));
 nowMs=32000;assert(!canAlert(mac,1000)); // stale cache isn't new evidence
 assert(canAlert(mac,31900));assert(!canAlert(mac,31900));
 nowMs=65000;assert(!canAlert("aa:bb:cc:dd:ee:ff",62000));
 s_alertCount=0;nowMs=UINT32_MAX-100;
 assert(canAlert(mac,nowMs));
 nowMs=30000;assert(canAlert(mac,29999)); // elapsed time wraps safely
 puts("Flock logging freshness tests passed");
}
"""
with tempfile.TemporaryDirectory(prefix='flock-log-') as directory:
    cpp=Path(directory)/'gate.cpp'; binary=Path(directory)/'gate'
    cpp.write_text(log_fixture+gate+log_checks)
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
