<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# JAGLINK

JAGLINK is a standalone, C-first, open-source Jaguar diagnostics platform for the **Jaguar X-Type (X400), 2001–2009**, the Jaguar/Ford CD132 platform related to the contemporary Mondeo.

**Current release: 0.1.0 — X400 foundation.**

The repository contains its own Jaguar-branded public C API, portable protocol implementation, native iPhone application, native Linux application, and a Windows OpenPort 2.0/J2534 evidence tool named `jaglink-discover`. It has no MBLINK source, submodule, build-time dependency, runtime dependency, API namespace, or user-facing identity. Infiltratr Common 1.10.0 is the only directly pinned shared-library submodule.

JAGLINK includes ELM327 transport/session handling, standard OBD-II, telemetry, scheduling, ISO-TP and UDS foundations together with its Jaguar manufacturer layer under `src/jaguar`. It models the source-corroborated X400 network topology without inventing module addresses, proprietary PIDs or request formats that have not been verified.

## X400 network foundation

Jaguar service training and the 2002 X-Type Electrical Guide describe four relevant vehicle networks:

- CAN — 500 kbit/s — engine, transmission and braking systems;
- SCP — 41.6 kbit/s — lower-speed body systems;
- ISO 9141 serial data link — 10.4 kbit/s — diagnostic link/ECM and diagnostic-capable modules outside CAN/SCP;
- D2B optical — 5.6 Mbit/s — in-car entertainment, with the audio unit acting as a network gateway.

These are represented as network definitions and provenance, not as claims that every network is already implemented by the adapter/provider layer.

## Build the portable core

```sh
git clone --recurse-submodules https://github.com/The-First-Infiltrator/Jaglink.git
cd Jaglink
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

If cloned without submodules:

```sh
git submodule update --init --recursive
```

Infiltratr Common 1.10.0 is pinned directly at `src/infiltratr-common`, commit `182e64cb8b8992879e443b941565058166fe0161`.

## iPhone

The native project is `app/ios/JAGLINK.xcodeproj`. Its Jaguar-branded CoreBluetooth provider and diagnostics controller are maintained in this repository. The 0.1.0 app provides adapter connection, generic OBD-II capability discovery, generic fault scans, live parameters, X400 network provenance and CSV export.

On macOS with Xcode, build an unsigned physical-device IPA and checksum with:

```sh
scripts/build-ios-ipa.sh dist
```

The manually runnable **Build unsigned physical-device IPA** GitHub Actions workflow performs the same build and uploads `JAGLINK-unsigned.ipa` plus `JAGLINK-unsigned.ipa.sha256` as workflow artifacts.

## Windows discovery tool

`jaglink-discover` is a Win32 GUI for an installed OpenPort 2.0-compatible J2534 DLL. It passively captures raw 500 kbit/s CAN and appends timestamped traffic and operator annotations to JSON Lines. Its optional inventory is deliberately bounded to a fixed set of standard, read-only OBD requests.

```powershell
cmake -S . -B build -A Win32 -DJAGLINK_BUILD_WINDOWS_DISCOVER=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The executable is `build/Release/jaglink-discover.exe`. The Win32 architecture is intentional for compatibility with the commonly installed 32-bit Tactrix J2534 driver. See `docs/DISCOVER.md` for the capture format, inventory boundary and operating model.

## Linux

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DJAGLINK_BUILD_LINUX_APP=ON
cmake --build build-linux --target jaglink-linux
./build-linux/jaglink-linux
```

## Engineering policy

- `main` is the development branch.
- Generic diagnostic behaviour stays portable C.
- Jaguar definitions live in JAGLINK, not in the ELM/BLE provider.
- Manufacturer-specific requests remain unimplemented until source evidence and/or reproducible vehicle captures establish their meaning.
- The source, public API, Apple classes, build targets and user interfaces use the JAGLINK/JagLink namespace.

See `docs/JAGUAR.md`, `docs/DISCOVER.md`, `docs/APPLE.md`, `docs/ORIGIN.md` and `docs/ROADMAP.md`.

## Licence

Copyright (C) 2026 Shannon Smith.

JAGLINK is licensed under `GPL-3.0-or-later`.
