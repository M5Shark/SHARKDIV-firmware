# Host checks

Run `sh tests/run_host_tests.sh` from any directory. Requires a C/C++ compiler
with AddressSanitizer/UBSan, Python 3 and Node.js 18 or later. All builds go to
temporary directories and are removed when the scripts finish.

- Passive radio: exact-length Find My advertisements, malformed/truncated input,
  bounded history, expiry across timer rollover and RF occupancy arithmetic.
- Flock: published Wi-Fi/BLE indicators, malformed probe/advertisement rejection,
  GPS GGA/RMC parsing, late phase entry, scan deadlines and single-radio modes.
- Camera detectors: bounded observation tables, prefix validation and recovery
  from Arduino's stale Wi-Fi started flag across repeated launches.
- Background handoff: executes the actual scan loops while a foreground tool
  opens; the former owner must finish before the new receiver starts.
- Remote ID: independent on-wire fixtures, upstream reference comparisons,
  BLE/beacon framing, malformed/truncated messages and output atomicity.
- Touch navigation: compiles the actual helper from the sketch; checks tap,
  hold, release, latched/serial taps and rollover without duplicate actions.
- BLE lifecycle: runs the actual host-queue helper against mock FreeRTOS/NimBLE
  APIs and verifies teardown cannot delete a result until the host callback finishes.
- Detection audio: executes the production task against a simulated clock/PWM;
  checks finite tones, mute, exit cancellation, background silence and backlight
  isolation. Policy checks cover bursts, duplicates, capacity and time rollover.
- Boot animation: guarded bitmap writes, exact doubled display pixels, complete
  SPI transactions, finite frame timing including overruns/timer rollover,
  unsupported display fallback and skull-to-logo ordering (hardware and VM).
- Menu/settings: production text fitting, overflow paging, held-touch protection,
  opening-press handling, brightness bounds and persistence failure paths.
- Receiver alerts: production receive handlers, partial radio startup failures,
  passive Radar deadlines, GPS validation, BLE borrowed ownership and WPS results.
- Web flasher: runs the actual installer script with a simulated serial device;
  checks successful writes, cancellation, wrong-chip rejection, error recovery
  and that the manifest points to an existing merged ESP32-S3 image.

The OpenDroneID reference is only compiled by host tests and has its own license
under `reference/opendroneid/`. No tests transmit radio signals or flash hardware.

Build the firmware separately with `./build-div-mac.sh` and the dependencies in
`FLASHING.md`. Hardware acceptance still needs a physical board: inspect touch
navigation and text, confirm receiver start/stop repeatedly, compare RF occupancy
against a known signal, receive known Find My/Remote ID test broadcasts and test
USB installation/power behavior. The browser manifest intentionally continues to
serve stable v1.7.29 while the source is v1.7.34-fullscreen.
