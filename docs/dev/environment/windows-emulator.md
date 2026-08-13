# Windows: emulator + bridge

Android emulators on Windows have no in-app Bluetooth access, so the Python
bridge is required. This is the only supported Windows dev path.

## Git Bash prerequisite

The `app:*` npm tasks use bash `$()` substitution via a `.sh` helper. npm on
Windows defaults to `cmd.exe`, which can't run them. Fix it once:

```sh
npm config set script-shell "C:\\Program Files\\Git\\bin\\bash.exe"
```

WSL is not an alternative here: WSL2 has no Bluetooth access, and the bridge
needs the host radio. The bridge and the npm tasks run on native Windows via
Git Bash.

## Steps

1. Flash a lamp over USB first — `npm run lamp:flash` (local dev build), or
   `npm run lamp:flash:release` if you don't have the firmware toolchain and
   just need a stock lamp. Keep `npm run lamp:monitor` running in a spare
   terminal — reset reasons and GATT/auth logs land there.

2. Install the Python bridge deps (once):

   ```sh
   npm run bridge:setup
   ```

3. Start the bridge (leave it running):

   ```sh
   npm run bridge
   ```

4. Boot the API 34 Android emulator in Android Studio.

5. Run the app:

   ```sh
   npm run app:bridge
   # Uses BRIDGE_HOST=10.0.2.2:8080 (Android emulator alias for the host)
   ```

## Known rough edges

`bleak` on WinRT is the flakiest BLE stack — MTU negotiation and pairing quirks
are the common failure modes. If the bridge loses the lamp or shows GATT errors,
a dedicated USB Bluetooth dongle (e.g., a CSR8510-based one) often resolves it;
bleak picks it up automatically on Windows.

OTA is not supported over the bridge. Flash firmware updates over USB. While the
app is connected (real or bridged), the lamp suppresses its greeting — intentional
coex behavior; disconnect to test greetings.
