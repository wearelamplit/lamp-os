# Setup

## Prerequisites

1. **Node.js** — every dev task is an `npm run …` script from the repo root.
   macOS: `brew install node`; Windows: `winget install OpenJS.NodeJS` (or nvm).
   The root `package.json` has no dependencies — no `npm install` step, Node
   itself is enough.
2. **VS Code** + the [Flutter extension](https://marketplace.visualstudio.com/items?itemName=Dart-Code.flutter) — installs the Flutter/Dart SDK automatically. Google any gaps; the extension's "Getting Started" is the canonical source.
3. **Android Studio** — for the Android SDK and an API 34 emulator image. Install via the Android Studio IDE manager or `sdkmanager`.
4. **Python** (≥3.10) — the bridge and firmware tooling. On macOS use a brew-
   or pyenv-managed Python; the stock system 3.9 fails building bleak's pyobjc.
5. **Firmware tools** (only for flashing/building lamps): `pip install platformio esptool`.
   `lamp:flash:release` needs `esptool` + `curl`; local builds need `pio`.

Verify Flutter is wired up:

```sh
npm run app:doctor   # flutter doctor; all green except iOS on non-Mac
```

## First build

A fresh clone won't compile without Riverpod / Freezed generated files:

```sh
npm run app:clean    # flutter clean + pub get + codegen
```

Run this once on initial checkout, and again after pulling changes to
`@riverpod`/`@freezed` models.

## Task catalog

All tasks run from the repo root.

### App + bridge

| Task | What it does |
|---|---|
| `npm run app:run` | `flutter run` against a connected device |
| `npm run app:bridge` | `flutter run -t lib/main_dev.dart` against the Android emulator (`BRIDGE_HOST=10.0.2.2:8080`, the emulator's host alias) |
| `npm run app:bridge:ios` | Same, for the iOS Simulator (`BRIDGE_HOST=localhost:8080`) — boot the simulator first |
| `npm run bridge:setup` | Once: create `tools/fake-lamp-bridge/.venv` + install deps (needs python3 ≥3.10) |
| `npm run bridge` | Start the Python host bridge (`tools/fake-lamp-bridge/bridge.py`) |
| `npm run app:install` | Build debug APK + `adb install -r` (no data wipe) + launch |
| `npm run app:launch` | Launch the already-installed app on the connected device (no build) |
| `npm run app:test` | `flutter test` |
| `npm run app:analyze` | `flutter analyze` |
| `npm run app:codegen` | `build_runner build` — regenerate Freezed/Riverpod/JSON files |
| `npm run app:watch` | `build_runner watch` — only needed while actively editing `@riverpod`/serializable models |
| `npm run app:clean` | `flutter clean` + `pub get` + codegen — the fresh-clone / weird-state reset |
| `npm run app:devices` | List connected Flutter devices |
| `npm run app:doctor` | `flutter doctor` |

### Firmware + wisp

| Task | What it does |
|---|---|
| `npm run lamp:flash:release` | Download the CI-signed release + flash whole image over USB (no build, no key) |
| `npm run lamp:flash` | Build unsigned locally + flash over USB (`VARIANT`/`CHANNEL`/`PORT` env params) — dev flashes; boots and meshes fine, can't source OTA |
| `npm run lamp:build` | Build lamp firmware, unsigned (`VARIANT` env param) |
| `npm run lamp:test` | Native firmware unit tests (the CI suite) |
| `npm run lamp:monitor` | Serial monitor on the connected lamp — first stop for crashes/reset reasons |
| `npm run lamp:buildfs` / `lamp:flashfs` | Build / flash the SPIFFS web-config UI (runs `ui:build` first) |
| `npm run ui:build` | Build the lamp-ui web app |
| `npm run wisp:build` / `wisp:flash` / `wisp:test` / `wisp:monitor` | Same as the lamp tasks, for the wisp node |

## Flash a lamp

The dev flash — build locally (unsigned) and flash over USB:

```sh
npm run lamp:flash
# VARIANT / CHANNEL / PORT env params override defaults (standard / beta / auto-detect)
```

Unsigned dev flashes boot and mesh normally; they only can't *source* OTA.
Needs the firmware toolchain (`pio`).

To flash the CI-signed release instead — a stock lamp with no build and no
toolchain beyond `esptool` + `curl`:

```sh
npm run lamp:flash:release
# VARIANT / RELEASE_TAG / PORT env params (standard / beta / auto-detect)
```

## Update the app on a phone

```sh
npm run app:install    # build + adb install -r; does NOT wipe lamp inventory
```

Never use `flutter install` — the uninstall step wipes stored lamp data.

## Python bridge setup

```sh
npm run bridge:setup   # once: creates tools/fake-lamp-bridge/.venv + installs deps
npm run bridge         # scan for lamps, start the GATT proxy on :8080
```

`bridge:setup` uses whatever `python3` resolves to — needs ≥3.10 (brew/pyenv
on macOS; the stock 3.9 fails building bleak's pyobjc dep).

Then run the app against it:

```sh
npm run app:bridge       # Android emulator
npm run app:bridge:ios   # iOS Simulator (boot it first)
```

**Limits:** OTA is not supported over the bridge; flash firmware over USB. Slider
responsiveness over the bridge is not representative of a real phone. While the
app is connected (real or bridged), the lamp suppresses its greeting — intentional
BLE coex behaviour.
