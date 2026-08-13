# Mac: hardware + bridge

macOS is the reference dev platform. Two sub-paths: real phone, or emulator via
the bridge.

## Real phone + real lamp

Flash the lamp over USB:

```sh
npm run lamp:flash            # local dev build
# or: npm run lamp:flash:release   # stock lamp, no firmware toolchain needed
```

Connect an Android phone and run:

```sh
npm run app:run      # flutter run against the connected device
# or
npm run app:install  # build + adb install -r + launch
```

For an **iOS device**: ensure `software/lamp-app-flutter/ios/Podfile` is
present (`pod install` from that directory if not) before running
`flutter run`. The app talks BLE directly to the lamp — no bridge needed.

## Bridge (macOS + emulator or iOS Simulator)

`bleak` on macOS uses CoreBluetooth, the most reliable BLE stack on any
platform.

1. Flash a lamp over USB — `npm run lamp:flash` (local dev build), or
   `npm run lamp:flash:release` for a stock lamp with no firmware toolchain.
   Keep `npm run lamp:monitor` running in a spare terminal — reset reasons
   and GATT/auth logs land there.

2. Install bridge deps (once):

   ```sh
   npm run bridge:setup
   # Needs python3 ≥3.10 (brew/pyenv); the stock macOS Python 3.9 may fail to build pyobjc
   ```

3. Start the bridge:

   ```sh
   npm run bridge
   ```

4. Run the app in an Android emulator:

   ```sh
   npm run app:bridge
   # BRIDGE_HOST=10.0.2.2:8080 (Android emulator host alias)
   ```

   Or in iOS Simulator — boot the simulator first, then:

   ```sh
   npm run app:bridge:ios
   # BRIDGE_HOST=localhost:8080 (the simulator shares the Mac's loopback)
   ```

## Limits

OTA is not supported over the bridge; flash firmware updates over USB. Slider
responsiveness over the bridge is not representative of a real phone. While the
app is connected (real or bridged), the lamp suppresses its greeting — intentional
BLE coex behaviour.
