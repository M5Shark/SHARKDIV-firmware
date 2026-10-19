#pragma once

// SharkTools — tools ported from the M5SHARK firmware (github.com/M5Shark/
// M5Shark) onto the ESP32-DIV feature contract (Setup/Loop/exit namespaces).
//   WifiRadar    radar sweep of nearby APs, plotted by RSSI (from SharkRadar)
//   SsidRotator  open hotspot cycling joke SSIDs (from SharkPrank)
//   BleSpam      SwiftPair / Samsung EasySetup / Google Fast Pair spam suite
//                (from M5SHARK's Bluetooth Attacks)

namespace WifiRadar {
  void wifiRadarSetup();
  void wifiRadarLoop();
  void exit();
}

namespace SsidRotator {
  void rotatorSetup();
  void rotatorLoop();
  void exit();
}

namespace BleSpam {
  void bleSpamSetup();
  void bleSpamLoop();
  void exit();
}
