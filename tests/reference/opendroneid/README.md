# OpenDroneID core

Unmodified `libopendroneid/opendroneid.c` and `opendroneid.h` from
[opendroneid-core-c](https://github.com/opendroneid/opendroneid-core-c),
retrieved 2026-10-24. Apache-2.0; full license in LICENSE; original source
copyright/SPDX notices preserved.

Git blob IDs:
- opendroneid.c: 13f7e2bb9cc036f09b88d3c31bd0f489f3862692
- opendroneid.h: 0c11f57923e4861ca7b171c87aaf29bfed9183a3

Only the two portable core files are included, for host-test fixture generation
and comparison against SHARKDIV's original protocol parser. No upstream Wi-Fi
transmitter, MAVLink, Linux tools, or radio driver is included. These files live
outside the Arduino sketch and are NOT linked into firmware. The production
RemoteIdParser has no include or call into this reference library.
