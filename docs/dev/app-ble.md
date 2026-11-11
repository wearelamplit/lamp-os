# App BLE links

How the Flutter app opens and holds BLE links to lamps, how a connect shares the
phone's radio with the scan, and how My Lamps decides which paired lamps to
list. Source: `software/lamp-app-flutter/lib/core/ble/` and the inventory,
nearby and control features. The GATT layout and payloads the link carries are
in [`networking.md`](networking.md).

The app links only to a lamp the user opens (or is onboarding). My Lamps never
connects on its own; it lists lamps from the scan and from stored last-seen
times.

## Connect retry: `connectWithRetry`

`connectWithRetry` (`core/ble/connect_with_retry.dart`) is the app's one connect
policy. Android intermittently fails a first connect in a crowded BLE
environment with GATT_ERROR(133) or a `deviceIsDisconnected` race, and its stack
needs about a second to release the slot after a failed attempt.

- Makes at most `attempts` tries (default 4) with `backoff` between them
  (default 1.2 s).
- Checks `shouldAbort` before each attempt and after a successful one, and
  returns false once it is true. Returns true once connected.
- Rethrows the last attempt's failure, so the caller maps it to a user-visible
  error.
- Drops the link when it gives up (an abort, or a failure on the last
  attempt) if the lamp is still connected and its `owner` still holds the
  claim (see below). A connect that gives up leaves no link behind on a lamp
  no later connect has claimed.

Callers:

| Caller | Policy |
|---|---|
| `ControlNotifier.build` (opening a lamp) | defaults, aborts when the notifier is unmounted |
| `ControlNotifier._tryReconnect` (the reconnect ladder) | `attempts: 1` per ladder step |
| `ControlNotifier._awaitReconnectAndReload` (after a settings save reboots the lamp) | `attempts: 1` per poll |
| `AddLampNotifier.submit` (onboarding) | `attempts: 2`, 1.5 s backoff |
| `AdoptPulseController.start` (onboarding pulse) | defaults, aborts on `stop` |

`AddLampNotifier._reconnectWithRetry`, the reconnect after an adopted lamp
reboots, is a separate policy: it drops the stale handle, waits out the reboot
and calls `BleClient.connect` directly with a per-attempt timeout.

## Link ownership: `claimLink` / `ownsLink`

A connect can outlive the screen that started it: the user backs out while an
attempt is still in flight, and the link lands afterwards. `BleClient` keeps
one owner per device id so a connect that gives up knows whether the link it
left is still its own to drop.

- `BleClient.claimLink(deviceId, owner)` records `owner` as the latest connect
  to the lamp. The latest claim wins.
- `BleClient.ownsLink(deviceId, owner)` is true while `owner` holds the latest
  claim.

Only `connectWithRetry` claims, on entry, and only `connectWithRetry` checks,
when it gives up. A caller whose `shouldAbort` is already true on entry returns
false without claiming, so it can't take the lamp from a newer connect. Its give-up checks `ownsLink` and `isConnected` and calls
`BleClient.disconnect` in one synchronous step, so a connect that claims the
lamp afterwards is ordered behind that disconnect.

### Ordering the app relies on

`FbpBleClient` passes `connect` and `disconnect` straight to flutter_blue_plus
2.3.4, which serializes them per device:

- `BluetoothDevice.disconnect` holds the device's disconnect `_Mutex` for its
  whole run: the Android `androidDelay` wait, the platform disconnect and the
  wait for the disconnected event.
- `BluetoothDevice.connect` takes the same mutex before it issues its request.
- `_Mutex.take` serves callers in the order they called it.

So a connect issued while a disconnect of that lamp is pending waits until the
link is down, then connects afresh. `BleClient.disconnect` takes its place in
that queue during the call itself.

On Android, `BluetoothDevice.disconnect` waits until `androidDelay` (2 s) after
`_connectTimestamp`, which `BluetoothDevice.connect` sets on every connect
request, including one on a lamp that is already connected, so the gap runs
from the latest connect request. Until the disconnect lands, the link and
`isConnected` stay up. iOS has no such gap.

### Backing out and re-opening

`ControlNotifier.build` always opens through `connectWithRetry`, even when the
lamp is already linked; it never adopts a live link. Once that connect
succeeds it registers a `ref.onDispose` that calls `ble.disconnect`
unconditionally.

- Backing out while the session is still connecting: `connectWithRetry` gives
  up at its next `shouldAbort` check and drops the link it left, unless a
  re-opened session has claimed the lamp since.
- Backing out after the link is up, while the screen is still loading (auth,
  section reads): the session's dispose disconnects. A re-opened session
  builds after that dispose, so its connect queues behind the disconnect.

Either way, re-opening the lamp leaves the new session connected, or at worst
retrying through `connectWithRetry` like any other open. Its connect returns
only after what the old session left in the lamp's queue:

- after a back-out while connecting, the rest of the abandoned attempt: up to
  its 8 s connect timeout on either platform. On Android, once the link lands,
  the abandoned attempt's MTU request (up to 0.35 s + 15 s) runs after the new
  connect's own connect request and before that connect returns.
- after a back-out while loading, the old disconnect: it waits behind any GATT
  op the old session still has in flight, then for the disconnect round trip,
  on either platform. On Android it also waits until about 2 s after the old
  connect request.

A connect abandoned with no re-open drops its link, so a lamp the user walked
away from is not held open.

A caller of `connectWithRetry` passes an owner that is stable for its session
(`ControlNotifier` uses one `_linkOwner` object per notifier).

## Holding an open lamp's link

`ControlNotifier` (`features/control/application/control_notifier.dart`)
watches an open lamp's link and reconnects it.

**Section read.** Every connect (opening the lamp, a ladder reconnect, a
reboot-save reload) authenticates, then reads each section once through
`_readSections`, the `lamp` section first. The lamp serves that section empty
to a link that has not authenticated, since it carries the password, so empty
bytes make `_readSections` throw `LampAuthRequiredException`. The control
screen turns that into a password prompt; on a ladder reconnect it also parks
the ladder.

**Liveness probe.** flutter_blue_plus can miss the link-down edge when the lamp
drops the link itself (a mesh OTA kicking the GATT client, a reboot), so
`_probeLink` makes one real GATT round trip every 3 s while the lamp is open,
and when the app returns to the foreground. It reads `CHAR_SCHEMA_VERSION`, one
byte. A lamp from before GATT schema v1 has no such characteristic; the client
finds it absent from the services it already discovered, with no radio time,
and the probe reads the `lamp` section instead (a page-protocol session: a CTRL
write and at least two reads). A disconnect error from either read starts the
reconnect ladder; any other error is ignored. The probe skips while the app
pushes the lamp firmware (`FirmwareState.isPushing`): the push's own writes fail
on a dead link, and a probe would queue behind its chunks.

**Reconnect owners.** An unsolicited drop climbs the reconnect ladder,
`_scheduleReconnect` then `_tryReconnect`: delays from 0.5 s doubling to 8 s,
`cycleAdapter` before each try from the third, parked in an error after 30. A
save that restarts the lamp (a password change, an Advanced LEDs save)
reconnects through `_awaitReconnectAndReload` instead: 2 s of grace, then up to
12 polls of connect, auth and section read. From the save until the reload
restores the link or gives up (`_rebootReloadOwnsLink`), the ladder stands
down: `_tryReconnect` returns at once, a try already in flight when a reload
starts stops at its next step for good (`_rebootReloadsStarted`), and a ladder
step still pending when the reload restores the link or gives up is cancelled.
A drop after the restore climbs the ladder as usual, even when a try from
before the save is still running: when that try stands down it arms the next
step if the link is down and none is pending
(`_resumeLadderAfterStandDown`). A reload that gives up leaves its error on
screen; the ladder does not take over.

## Scan hold during a connect

The phone has one BLE scan, and a running scan competes with a connect
handshake for the radio. Neither flutter_blue_plus platform plugin pauses its
own scan around a connect, so the app does:

- `scanPause` (`core/ble/scan_pause.dart`) is the one shared `ScanPause`. Its
  `during` holds scanning off for the body's duration; holds nest, and
  `released` fires when the last one ends, whether the body completed or
  threw.
- `FbpBleClient.connect` (`core/ble/fbp_ble_client.dart`) stops a running scan
  and runs flutter_blue_plus's `BluetoothDevice.connect` inside
  `scanPause.during`, with an 8 s connect timeout. Connection priority and
  service discovery run after the hold ends.
- `FbpBleScanner` (`core/ble/ble_scanner.dart`) never starts a scan while
  `scanPause.isPaused`. On `released` it restarts through `_restartScan`, which
  waits `_restartBackoff` (3 s) first, and stops a scan that started while a
  new hold was taken. A trigger that lands while a restart is running (a hold
  ending, the scan stopping) runs one more restart after it.

How long the scan stays off:

- **One attempt that fails to connect** holds the scan for up to its 8 s
  timeout. The scan comes back `_restartBackoff` (3 s) after the hold ends, so
  about 11 s at most.
- **A connect that fails every attempt** keeps the scan off for the whole run:
  the 1.2 s backoff between attempts is shorter than the scanner's 3 s restart
  backoff, so the scan never restarts in between. With `connectWithRetry`'s
  defaults that is 4 x 8 s + 3 x 1.2 s + the 3 s restart backoff, about 39 s.
- **A connect that lands on Android** also runs flutter_blue_plus's MTU request
  inside the hold. In flutter_blue_plus 2.3.4, `BluetoothDevice.connect`
  defaults `mtu` to 512 and calls `requestMtu` after the link is up;
  `requestMtu` waits a 0.35 s predelay and times out after 15 s. That request
  is outside the 8 s connect timeout, so an attempt whose link comes up and
  whose MTU request then stalls holds the scan for up to 8 s + 0.35 s + 15 s.
  A failed MTU request throws out of `connect` and counts as a failed attempt,
  with the link still up. When every attempt fails, `connectWithRetry` drops
  that link before rethrowing, while its owner still holds the claim.

While the scan is off, the app hears no lamp. A connect can run with My Lamps
on screen (My Lamps opened over a lamp that is still connecting or
reconnecting, or an abandoned attempt still in flight), and during a long hold
lamps there can show offline until the scan comes back.

## My Lamps presence

`lampPresence` (`features/inventory/domain/lamp_presence.dart`) places each
paired lamp from its last sighting, the later of the stored
`InventoryLamp.lastSeenEpochMs` and the live scan roster's time:

| Last sighting | Presence | On My Lamps |
|---|---|---|
| connected, any age | `reachable` | listed, online |
| under `kCurrentlySeenWindow` (30 s) | `reachable` | listed, online |
| up to `kUnreachableListingWindow` (5 min) | `unreachable` | listed, offline |
| older, or never seen | `hidden` | not listed, still paired |

A hidden lamp keeps its inventory entry and comes back to the list as soon as
it is heard again.

`kCurrentlySeenWindow` is sized against the scan holds above. Lamps advertise
every 100-200 ms (`BLE_ADVERTISING_INTERVAL_MIN` / `_MAX` in the firmware's
`ble_gap.hpp`), so an in-range lamp is heard many times inside 30 s. The window
rides out one failed attempt's hold plus the restart backoff (about 11 s). It
does not ride out a connect that fails every attempt (about 39 s). The nearby
roster ages lamps out on the same window (`_staleAfter` in
`nearby_lamps_notifier.dart`).

## Last-seen stamps

`lastSeenEpochMs` on each `InventoryLamp` survives an app kill and decides
presence after a relaunch. Two `InventoryNotifier` methods write it:
`recordLastSeen`, which writes an entry only when it is newer than the stored
one by at least `minStep`, and `updateSeen`, which writes the time together
with the lamp's cached colours.

**Lamps the scan hears** (`NearbyLampsNotifier`):

- `FbpBleScanner` forwards a scan result only when its fbp `timeStamp` is
  later than the scanner's start and than its last forward for that device.
  fbp's `scanResults` lists every device heard since the scan started on each
  emit and replays the previous scan's list on listen, so forwarding the list
  as-is would stamp lamps unheard for minutes as seen now.
- Each advertisement from a paired lamp calls `recordLastSeen` with
  `minStep: _lastSeenWriteStep` (30 s), which caps disk writes.
- A lamp aging out of the roster has its exact last advertisement time
  recorded, and so does every lamp still in the roster when the scanner is
  disposed (leaving My Lamps).
- An app kill skips both, so it loses at most `_lastSeenWriteStep` of a heard
  lamp's last-seen time.

**A lamp the phone holds open** is not heard by the scan: the lamp takes one
BLE connection (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`) and stops advertising
while it is held. `ControlNotifier` stamps it instead:

- on connect, through `_updateSeen` once the session's sections are read;
- on colour edits, through `_queueSeen`, coalesced into one write 500 ms
  after the last edit;
- when the link drops, in `_onConnectionChange` (`_recordSeenNow`);
- when the lamp is closed while still linked, in the session's dispose
  (`_recordSeenNow`);
- when the app goes to the background, on `AppLifecycleState.paused`
  (`_recordSeenNow`, while linked).

The background stamp records the moment of the pause. Neither app runs in the
background (no `UIBackgroundModes` in `ios/Runner/Info.plist`, no foreground
service on Android), so nothing stamps the held lamp after `paused`. A kill
soon after the pause leaves a recent last-seen time, and the lamp is listed
after a relaunch. If the OS keeps the link up after the pause and kills the
app much later, the stored time is still the pause, so after a relaunch the
lamp can show offline or be hidden until it is heard again.
