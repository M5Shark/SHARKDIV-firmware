# Flock and camera tools — v1.7.30-flock

The previous build could run its scanning animation without collecting usable
results. This version fixes radio startup and scan scheduling, replaces placeholder
signatures, and displays reception counters and errors.

## Where to start

Open **Flock → Flock Scanner** for Wi-Fi and BLE, or choose **WiFi Detect** /
**BLE Detect** for one radio. Let a full scan cycle finish. Wi-Fi AP results,
probe requests and BLE advertisements arrive at different parts of the cycle.
Use **Signal Meter** to inspect a received candidate. **Evidence** explains the
indicators; **Log / Export** shows session records.

**WiFi → Flock Hunter** is a dedicated probe monitor. **More → Detectors →
WiFi Cameras** uses the same built-in camera indicators, plus optional custom
vendor prefixes. No SD signature file is required for the built-in Flock
indicators. **LE Gear** requires a custom `/flock/gear-ouis.txt` list and clearly
shows "No filter configured" when absent; there is no universal radio signature
for law-enforcement equipment. The paged launcher exposes every tool.

Wi-Fi and BLE detection use the ESP32's own radios. GPS, SD, nRF24 and CC1101
are not required to receive camera indicators. Missing add-on receivers are
shown as unavailable. RF Activity reports ambient received energy; it cannot
identify LTE traffic or establish that a camera is nearby.

## Understanding an empty list

- **Received traffic increases, candidates stay at zero:** the radio is receiving,
  but none of the observed packets matched the supported signatures.
- **Received traffic stays at zero:** check the displayed radio error and try a
  location with known Wi-Fi/BLE traffic. These counters do not require a camera.
- **Candidates appear:** inspect the evidence and RSSI. A vendor prefix, name or
  service ID may be shared by unrelated devices; a match is not verified identity.

The scanner receives 2.4 GHz Wi-Fi and legacy BLE advertisements. Probe monitoring
hops across channels 1–11 when hopping is enabled; fixed-channel mode listens on
channel 11. Hopping and time sharing can miss intermittent packets. Devices that
emit no supported local Wi-Fi/BLE indicators will not appear. No result does not
establish that no camera is present. RSSI is not a distance measurement.

## Records and location

Recent observations remain in bounded RAM history without an SD card. SD recording
and export report failures instead of claiming success. GPS must have a fresh valid
fix before coordinates are exported. Coordinates are the receiver's observation
location, not a measured camera location. Exported candidates require review.

Signature provenance: [Flock You](https://github.com/colonelpanichacks/flock-you),
including its documented OUI research, probe fingerprint and firmware-derived BLE
indicators. The local bounded parsers are covered by host regression tests.

## Validation limits

Host tests exercise matching, malformed packets, scan sequencing, GPS parsing,
record handling and radio handoff. The ESP32-S3 build passed with the pinned
Arduino and NimBLE APIs. On 2026-10-24, this build was flashed to an ESP32-S3
revision 0.2 board with 16 MB flash; all four written segments passed hash
verification. It booted as `v1.7.30-flock`. A live full-cycle scan received up to
20 Wi-Fi APs, probe requests and over 1,400 BLE advertisements with zero reported
Wi-Fi/BLE errors. No camera candidate matched during that check. Known-camera
detection, GPS accuracy and physical SD export still need separate field checks.
The public web installer still offers stable v1.7.29, not this development build.
