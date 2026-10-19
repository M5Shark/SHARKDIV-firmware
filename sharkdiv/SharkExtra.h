#pragma once

// SharkExtra — fresh implementations of tool categories popularised by other
// multi-tool firmwares (Bruce et al.), written for the ESP32-DIV feature
// contract. No third-party firmware code is used; the QR encoder is the
// separately-licensed MIT qrcodegen library (see CREDITS.md).
//   QrTool            render text/URLs as scannable QR codes on screen
//   ChannelAnalyzer   per-channel 2.4 GHz airtime/utilisation meter

namespace QrTool {
  void qrSetup();
  void qrLoop();
  void exit();
}

namespace ChannelAnalyzer {
  void chanSetup();
  void chanLoop();
  void exit();
}
