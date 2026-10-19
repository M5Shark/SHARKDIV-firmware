#pragma once

// UserApps — the SHARKDIV app registry. A new app is ONE .cpp file with a
// namespace (setup/loop/exit, standard feature contract) plus ONE entry in
// the SHARKDIV_APP_LIST below. The Tools menu picks apps up automatically:
// no menu arrays, icons or dispatch chains to edit by hand.

#include <Arduino.h>

typedef void (*UserAppFn)();

struct UserApp {
  const char* name;
  UserAppFn setup;
  UserAppFn loop;
  UserAppFn exitFn;
};

// Register apps here (the MCP app builder appends to this list):
#define SHARKDIV_APP_LIST \
  SHARKDIV_APP("Demo App", DemoApp::appSetup, DemoApp::appLoop, DemoApp::exit) \

#define SHARKDIV_APP(name, setupFn, loopFn, exitFn) \
  { name, setupFn, loopFn, exitFn },

namespace DemoApp {
  void appSetup();
  void appLoop();
  void exit();
}
// === generated app declarations (app builder writes below this line) ===
namespace SignalLoggerApp {
  void appSetup();
  void appLoop();
  void exit();
}

// Count apps without the trailing comma trick: list with commas added per
// entry; USER_APP_COUNT derives from the array in UserApps.cpp.

// Accessors (defined in UserApps.cpp).
const UserApp* userApps();
int userAppCount();
