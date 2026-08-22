<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Apple / iPhone target

The JAGLINK iPhone project is `app/ios/JAGLINK.xcodeproj`.

JAGLINK owns its `JagLinkBLETransport` CoreBluetooth byte-stream provider and `JagLinkDiagnosticsController`. Together they perform adapter/GATT discovery, ELM327 initialisation, standard OBD-II capability discovery, generic stored/pending/permanent DTC reads and generic live-data scheduling. The target contains no external vehicle-diagnostics source dependency.

The SwiftUI app exposes the X400 network profile, generic faults, live OBD-II parameters, favourites and diagnostic CSV export. Jaguar manufacturer-specific module requests remain disabled in 0.1.0.

Build an unsigned simulator target with:

```sh
xcodebuild -project app/ios/JAGLINK.xcodeproj \
  -scheme JAGLINK \
  -configuration Debug \
  -sdk iphonesimulator \
  -destination 'generic/platform=iOS Simulator' \
  CODE_SIGNING_ALLOWED=NO build
```

A recursive Git checkout is required so the directly pinned `src/infiltratr-common` shared-library submodule is present.

## Unsigned physical-device IPA

On macOS with Xcode installed, the repository script builds the Release target for generic physical iPhone hardware, verifies that the application binary contains arm64 code, packages the `.app` under `Payload/`, verifies the ZIP structure and writes a SHA-256 checksum:

```sh
scripts/build-ios-ipa.sh dist
```

Outputs:

- `dist/JAGLINK-unsigned.ipa`
- `dist/JAGLINK-unsigned.ipa.sha256`

The IPA is intentionally unsigned. Installing it requires a separate signing process appropriate to the operator's Apple account and device.

The GitHub Actions workflow **Build unsigned physical-device IPA** is manual (`workflow_dispatch`) rather than a claim that every source change has been physically installed. When run, it uses a macOS runner and uploads both files as the `JAGLINK-unsigned-physical-device` workflow artifact. A successful portable build on Linux or Windows does not establish that this physical-device build passed.
