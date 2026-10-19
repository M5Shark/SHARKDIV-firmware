# SHARKDIV tools — remote-test bridge & app builder (MCP)

`sharkdiv_mcp.py` is a zero-dependency (python3 + pyserial) MCP server that
turns a USB-connected SHARKDIV into a drivable test target. The firmware's
built-in serial console (v1.7.22+) accepts single-letter commands; this
server exposes them as tools:

| Tool | What it does |
|---|---|
| `sharkdiv_press` | tap a nav-bar button (touch-identical injection) |
| `sharkdiv_state` | menu / selected item / feature-running / theme |
| `sharkdiv_battery` | battery gauge (IP5306) over serial |
| `sharkdiv_reset` | reboot the device, return the boot log |
| `sharkdiv_log` | capture serial diagnostics for N seconds |
| `sharkdiv_run` | navigate to a menu item, launch it, capture its log |
| `sharkdiv_app_list` | list registered user apps |
| `sharkdiv_app_create` | scaffold + register a new app from the template |
| `sharkdiv_app_build` | compile the firmware |
| `sharkdiv_app_flash` | build + flash + verify boot |

Together with the app registry in `ESP32-DIV/UserApps.cpp`, apps can be
created, built, flashed and verified end to end without touching the device.

## Setup

Any MCP client that speaks stdio. Example registration:

```json
{
  "mcp": { "servers": { "sharkdiv": {
    "command": "/usr/bin/python3",
    "args": ["/path/to/SHARKDIV-firmware/tools/sharkdiv_mcp.py"],
    "env": { "SHARKDIV_PORT": "/dev/cu.usbserial-210" }
  }}}
}
```

Defaults: port `/dev/cu.usbserial-210` at 115200 baud (override with
`SHARKDIV_PORT`). Build/flash paths expect the repo layout and the pinned
toolchain from `build-div-mac.sh`.
