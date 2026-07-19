# LampOS

A platform for retrofitting traditional desk lamps with programmable LED controllers to build unique lighted art structures. Using standardized and user friendly hardware based on the ubiquitous ESP32, Neopixels and common sensors, this project seeks to simplify the job of building curious and surreal LED projects for the community to enjoy.

## Lamp Personality & Behaviours

The vision for the lamps is that they remain mostly still and static, as a contrast to the plethora of sound reactive and blinky light art out there. The brightness of the lamps and the colorful glowing base draws attention to the juxtaposition of an ordinary household object in extraordinary places.

Within this vision there is room for the lamps to have personality, shown through subtle behaviour based on things like time, randomness, sensor data, presence/absence of other lamps, etc.

With these sorts of subtle changes, people may begin to realize things are not as static as they seem, creating a somewhat complex puzzle for people to solve and talk about.

## Selecting a lamp to convert

![Preferred lamp guidelines](hardware/build/images/Lamp-Selection-Guide.jpg)

## Lamp Hardware Requirements

![Main Lamp Components](hardware/build/images/important-lamp-parts.jpg)

This software is intended for the ESP32 platform. Our preferred dev board is an ESP32-WROOM32 30 Pin board variant measuring no wider than 28mm with a chip antenna. Unsoldered/unwelded pins are preferred if possible. This space requirement is so the board can fit comfortably into a standard lamp socket. The boards can be had easily from Amazon and AliExpress for $5-10. The model we use has this pinout <https://lastminuteengineers.com/esp32-pinout-reference/>

The USB connector is a USB type A metal shell crimp on connector. These tend to be the most robust.

By default, a lamp will use about 80 LEDs. The limiting factor at the moment is current draw. generally over 100 LEDs you may have stability issues with a conventional USB source. We recommend purchasing LEDs strips with the following specs:

- Around 2m in length
- SK6812 chipset
- IP67 waterproof
- 5VDC
- RGBWW (warm white) LED Strips
- Spacing of 60 LEDs/m

A 10000mAh battery pack with USB will run this device portably for around 7 hours

You'll also need some basic workshop tools, heat shrink, wire connectors, a soldering iron and a way to print the plastic bulb

There's a handy [build guide with images here](hardware/build/README.md)

## Build Tutorials

[![Assembly Guide](hardware/build/images/assembly-guide.jpg)](hardware/build/files/lamp-build-instructions.pdf)

[![Conversion kit Guide](hardware/build/images/conversion-kit.jpg)](hardware/build/README.md)

## Lamp OS

Three components live under [`software/`](software/):

- [`software/lamp-os/`](software/lamp-os/), lamp firmware (ESP32-WROOM, build envs `upesy_wroom_standard` / `upesy_wroom_snafu`)
- [`software/wisp/`](software/wisp/), wisp infrastructure node firmware (Seeed XIAO ESP32-C6, build env `seeed_xiao_esp32_c6`)
- [`software/lamp-app-flutter/`](software/lamp-app-flutter/), iOS/Android control app

See [`CLAUDE.md`](CLAUDE.md) for the project orientation and the v0x04 protocol lock-in. Developer docs, architecture, the mesh wire-format spec, expressions, personality, live in the [`docs/dev/`](docs/dev/) handbook.

## Contributing

We'd love the help. Easiest way in is the Discord: **<https://discord.gg/yHR3XCdrJ2>**. Come say hi, ask questions, show off whatever you're building.

PRs are welcome for new features, fixes, and docs, and for **custom lamps** too: your own variants, behaviours, expressions, whatever you dream up. Rolling your own lamp is half the point, go for it.

One ask: if you build something cool, try to send it back upstream. Open a PR, or poke us on Discord first. Totally optional, but the more we run off a shared base, the less the fleet splinters into forks that can't even talk to each other on the mesh. One compatible family beats a pile of private snowflakes. 💡

## Flashing your ESP32

You do **not** need a development environment or to build anything to get a working lamp. The firmware is prebuilt and signed, and you flash it straight from your browser:

### → [update.lamplit.ca](https://update.lamplit.ca/)

Plug your ESP32 into your computer over USB, open the flasher in **Chrome or Edge** (it uses WebSerial, so Safari and Firefox won't work), pick your board, and hit flash. No toolchain, no repo clone, no `pio`. It writes the whole image, bootloader, partitions, firmware, and the web config UI, so the lamp boots ready to name and pair with the app.

That's the path for almost everyone. Only keep reading into [Development](#development) if you actually want to *modify* the firmware or the app, building it yourself is not required to make a lamp.

## Development

### Flutter app: macOS

```sh
# 1. Install Flutter (Homebrew is easiest)
brew install --cask flutter

# 2. Install Android Studio (for Android build + Android SDK + adb)
brew install --cask android-studio
# Open Android Studio once → Settings → Languages & Frameworks → Android SDK
# Install SDK Platform 34+, SDK Build-Tools, Platform-Tools.

# 3. (Optional, iOS only) Install Xcode from the App Store, then:
sudo xcode-select --switch /Applications/Xcode.app
sudo xcodebuild -runFirstLaunch
sudo gem install cocoapods

# 4. Verify
flutter doctor

# 5. Run the app
cd software/lamp-app-flutter
flutter pub get
flutter run                            # picks any connected Android/iOS device
```

### Flutter app: Windows

```powershell
# 1. Install Flutter
#    Download https://docs.flutter.dev/get-started/install/windows
#    Unzip to C:\src\flutter and add C:\src\flutter\bin to PATH.

# 2. Install Android Studio
#    https://developer.android.com/studio
#    On first launch: SDK Manager → install Platform 34+, Build-Tools,
#    Platform-Tools, and Google USB Driver.

# 3. Install USB drivers for your phone (manufacturer-specific) and
#    enable USB Debugging on the phone (Settings → Developer options).

# 4. Verify
flutter doctor

# 5. Run the app
cd software\lamp-app-flutter
flutter pub get
flutter run                            # picks any connected Android device
```

**Android emulator (Windows):** for UI / navigation / widget-test work without a physical phone. Note: **emulators have no Bluetooth radio**, so you can't actually talk to lamps from one, anything that depends on BLE scan, connect, GATT read/write, or the seen/nearby lamp list won't function. Use the emulator for layout, theme, routing, and form work; switch to a physical device for anything touching `core/ble/`.

```powershell
# 1. Enable hardware acceleration (one-time, requires reboot).
#    Settings → Apps → Optional features → More Windows features →
#    enable "Windows Hypervisor Platform". (Disable Hyper-V if it's on, 
#    Android Emulator uses WHPX, not Hyper-V directly.)

# 2. Create a virtual device.
#    Android Studio → Tools → Device Manager → Create Device.
#    Pick Pixel 7, system image API 34 (download if needed), Finish.

# 3. List + launch from the CLI.
flutter emulators                         # shows installed AVDs
flutter emulators --launch Pixel_7_API_34
flutter run                               # picks the running emulator
```

**iOS from Windows:** there's no local path. Building or running on an iOS device or Simulator requires Xcode, which is macOS-only, there is no Windows iOS Simulator. Options:

- Borrow a Mac and follow the macOS section above.
- Push to a CI service with macOS runners (Codemagic, GitHub Actions `macos-latest`, Bitrise) that builds + signs the IPA and side-loads to a registered device.
- Use a remote-Mac service (MacStadium, MacinCloud) over VNC/SSH and treat it as the dev box.

Android USB debugging from Windows is fine; iOS just isn't.

### Common app commands

Prefer the npm tasks (run from the repo root, they wrap the underlying `flutter`/`adb` calls; see [`package.json`](package.json)):

```sh
npm run app:test         # unit + widget tests
npm run app:analyze      # static analysis
npm run app:run          # debug build + run on a connected Android device
npm run app:install      # build debug APK + install (adb install -r) + launch
npm run app:codegen      # regenerate freezed / riverpod / json (build_runner)
npm run app:clean        # flutter clean + pub get + codegen (full refresh)
```

`app:install` uses `adb install -r`, which installs WITHOUT wiping app data, preferred over `flutter install`, which does an `adb uninstall` first and wipes the lamp inventory.

### Firmware (lamp + wisp)

Both firmwares build with [PlatformIO](https://platformio.org/). Either install the VS Code extension or the CLI:

```sh
pip install platformio                           # CLI only; VS Code extension bundles its own
```

Common commands (npm tasks, run from the repo root, see [`package.json`](package.json)):

```sh
# Lamp (ESP32-WROOM)
npm run lamp:test         # native unit tests (344 cases)
npm run lamp:build        # build (standard variant; VARIANT=snafu for others)
npm run lamp:flash        # flash a connected lamp (standard, beta channel)
                          # VARIANT / CHANNEL / PORT env params override each
npm run lamp:monitor      # serial monitor

# Wisp (Seeed XIAO ESP32-C6)
npm run wisp:build        # build
npm run wisp:flash        # flash a connected wisp
npm run wisp:monitor      # serial monitor
```

The flash tasks upload to the first board PlatformIO finds. With multiple boards connected, target a specific one with the raw PlatformIO command and `--upload-port`, e.g. `cd software/lamp-os && pio run -e upesy_wroom_standard -t upload --upload-port /dev/cu.SLAB_USBtoUART` on macOS or `--upload-port COM5` on Windows.
