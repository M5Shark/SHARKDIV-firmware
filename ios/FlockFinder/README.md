# FlockFinder — iOS companion for SHARKDIV

SwiftUI app (iOS 16+). Connects to the SHARKDIV Flock Scanner over BLE and
shows live Flock-camera detections from the passive radio engine.

## Pairing

1. On SHARKDIV: **Flock → Flock Settings → iOS app link (BLE) = ON**
   (starts the GATT server; the device advertises as "SHARKDIV")
2. Open FlockFinder on the iPhone → it scans and connects
3. Detections stream in as JSON notifications

## BLE contract

| Item | Value |
|---|---|
| Service | `4fafc201-1fb5-459e-8fcc-c5c9c331914b` |
| Detections (notify) | `beb5483e-36e1-4688-b7f5-ea07361b26a8` |
| Stream (notify) | `beb5483e-36e1-4688-b7f5-ea07361b26aa` |

Detection JSON: `{"type":"wifi","mac":"e4:aa:ea:12:34:56","rssi":-62,"confidence":"CONFIRMED","ts":123}`

## Build

Open `FlockFinder.xcodeproj` in Xcode (or create a new iOS App project and
drop in the three .swift files), set your team, run on a **physical iPhone**
(the simulator has no Bluetooth). Add `NSBluetoothAlwaysUsageDescription`
to Info.plist: "FlockFinder connects to your SHARKDIV detector over
Bluetooth to receive passive camera-detection alerts."
