<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# JAGLINK Discover

`jaglink-discover` is a Windows evidence-acquisition GUI for an OpenPort 2.0 or another SAE J2534-compatible pass-through implementation. Its purpose is to preserve what an X400 actually places on the documented 500 kbit/s CAN network before Jaguar-specific decoding is promoted into the portable library.

## Scope

The application can:

- dynamically load an operator-selected J2534 DLL;
- open a raw CAN channel at exactly 500,000 bit/s;
- capture 11-bit and 29-bit traffic through a pass-all receive filter;
- append timestamped RX and approved TX frames to JSON Lines;
- append free-text operator annotations to the same evidence stream;
- send one fixed, bounded standard OBD inventory sequence.

The capture thread calls only `PassThruReadMsgs`. Starting passive capture does not call `PassThruWriteMsgs`. J2534 does not define a portable vendor-independent hardware "listen-only" configuration, so the application describes its own behaviour precisely rather than claiming the adapter firmware has entered a proprietary silent mode.

## Deny-by-default transmit boundary

All transmit calls are private to `safe_write()`. The decoded diagnostic payload must first pass `jaglink_discover_classify_request()`. The classifier has an allowlist, not a blacklist: any request not explicitly listed below is denied.

Allowed standard OBD reads:

| Service | Parameters | Purpose |
| --- | --- | --- |
| `01` | PIDs `00`, `20`, `40`, `60`, `80`, `A0`, `C0` | Supported live-data PID bitmaps |
| `03` | none | Stored DTC read |
| `07` | none | Pending DTC read |
| `09` | PIDs `00`, `02`, `04`, `06`, `08`, `0A` | Supported vehicle-information items, VIN and standard identity/calibration records |
| `0A` | none | Permanent DTC read |

The GUI's inventory is even narrower: it issues 12 fixed requests—`0100`, `0120`, `0140`, `0900`, `0902`, `0904`, `0906`, `0908`, `090A`, `03`, `07`, and `0A`—once each, with 150 ms spacing. It does not follow proprietary addresses or recursively issue requests based on untrusted responses.

Explicitly classified denial categories include:

- DTC clearing / clear diagnostic information (`04`, `14`);
- ECU reset (`11`);
- security access (`27`);
- session, communication, write-data, I/O-control, write-memory and DTC-setting control (`10`, `28`, `2E`, `2F`, `3D`, `85`);
- routine control (`31`);
- request download/upload, transfer data, transfer exit and file transfer (`34`–`38`);
- every unknown, malformed, manufacturer-specific or otherwise non-allowlisted request.

The safety classifier lives in portable C and is tested independently of Windows and J2534. The GUI cannot offer an arbitrary diagnostic command entry field.

## Evidence format

The output is append-only JSON Lines. Each completed frame or annotation is flushed immediately.

Traffic record:

```json
{"type":"traffic","timestamp_us":1787356800123456,"direction":"rx","channel":"can-500k","arbitration_id":"0x000007e8","extended":false,"data":"064902014a5444","annotation":""}
```

Operator annotation:

```json
{"type":"annotation","timestamp_us":1787356800456789,"text":"ignition on; engine not started"}
```

`timestamp_us` is Unix time in microseconds taken by the Windows host when the record is written. `data` is the raw CAN data field in lowercase hexadecimal without separators. J2534 adapter-relative timestamps are not substituted for wall-clock evidence time. Operator text is JSON escaped.

## Build

Use the Win32 generator because the official OpenPort driver is commonly installed as a 32-bit J2534 DLL:

```powershell
cmake -S . -B build -A Win32 -DJAGLINK_BUILD_WINDOWS_DISCOVER=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The resulting executable is `build/Release/jaglink-discover.exe`. No Tactrix SDK header or proprietary library is compiled into JAGLINK; the program declares the stable J2534 ABI subset it uses and loads the installed provider at runtime.

## Windows CI

The primary CI workflow configures an x86 Windows build, compiles `jaglink-discover`, runs the complete portable test suite and, on success, uploads `jaglink-discover.exe` as `jaglink-discover-windows-x86`. This validates compilation and the portable safety/evidence behaviour. It does not claim physical OpenPort hardware or a vehicle was attached to the hosted runner.
