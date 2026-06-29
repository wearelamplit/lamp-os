import 'dart:async';
import 'dart:convert';
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/lamp_crypto.dart';
import '../../../core/ble/uuids.dart';
import '../../control/application/auth_client.dart';
import '../../inventory/application/active_lamp_notifier.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../../inventory/domain/inventory_lamp.dart';
import '../domain/add_lamp_state.dart';

part 'add_lamp_notifier.g.dart';

/// Pick a 1..16 critter index. Each lamp keeps its own random pick across
/// the connecting view and the lamp preview so users associate the lamp
/// with a consistent little friend.
int _pickCritterIndex() => Random().nextInt(16) + 1;

@Riverpod(keepAlive: true, name: 'addLampNotifierProvider')
class AddLampNotifier extends _$AddLampNotifier {
  /// How long to wait after the firmware applies setup (reboots) before we
  /// attempt to reconnect for the post-claim password-verification probe.
  /// Tests can override this to `Duration.zero`.
  @visibleForTesting
  static Duration verifyDelay = const Duration(seconds: 8);

  /// Shorter post-reboot wait for the empty-password ("Skip") path. There's
  /// no auth to verify — the lamp's `isAuthed()` early-returns true when
  /// `lamp.password.empty()`, so the probe is purely "did the lamp come
  /// back up?". 2s is enough for the BLE link to settle without the user
  /// staring at a "Settling in…" spinner for 8 seconds when they explicitly
  /// asked to skip security.
  @visibleForTesting
  static Duration verifySkipDelay = const Duration(seconds: 2);

  /// Hard ceiling on the reconnect-after-reboot attempt. Without this,
  /// flutter_blue_plus' connect() can hang forever against a stale handle
  /// when Android hasn't yet noticed the link drop — and the UI shows
  /// "Settling in…" indefinitely (the reported "stuck on Skip" bug).
  @visibleForTesting
  static Duration verifyConnectTimeout = const Duration(seconds: 15);

  /// Ceiling on each post-reconnect characteristic op (auth write, lampSection
  /// read). A successful op is sub-second; the timeout exists so a half-open
  /// link surfaces as a recoverable error instead of hanging the wizard.
  @visibleForTesting
  static Duration verifyOpTimeout = const Duration(seconds: 10);

  @override
  AddLampState build() => const AddLampState();

  void select(String deviceId) {
    // Record the picked device and jump to the adopt-confirm step. We
    // deliberately do NOT open the BLE link here — the AdoptConfirmStep
    // widget owns the connection pulse. submit() opens the link
    // immediately before the setup writes that need it (connect-then-
    // immediately-use, same pattern as ControlNotifier).
    state = state.copyWith(
      deviceId: deviceId,
      step: AddLampStep.adoptConfirm,
    );
  }

  void setName(String n) => state = state.copyWith(name: n);
  void setPassword(String p) => state = state.copyWith(password: p);

  void next() {
    state = state.copyWith(step: switch (state.step) {
      AddLampStep.scan => AddLampStep.scan,
      AddLampStep.adoptConfirm => AddLampStep.name,
      AddLampStep.name => AddLampStep.password,
      AddLampStep.password => AddLampStep.verifying,
      AddLampStep.verifying => AddLampStep.done,
      AddLampStep.done => AddLampStep.done,
    });
  }

  void previous() {
    state = state.copyWith(step: switch (state.step) {
      AddLampStep.scan => AddLampStep.scan,
      AddLampStep.adoptConfirm => AddLampStep.scan,
      AddLampStep.name => AddLampStep.scan,
      AddLampStep.password => AddLampStep.name,
      AddLampStep.verifying => AddLampStep.password,
      AddLampStep.done => AddLampStep.password,
    });
  }

  Future<void> submit() async {
    state = state.copyWith(
      status: AddLampStatus.working,
      error: AddLampError.none,
      errorMessage: null,
    );
    final ble = ref.read(bleClientProvider);

    // Step 0: open the BLE link. select() deliberately doesn't connect
    // (avoids LINK_SUPERVISION_TIMEOUT during form-fill), so submit is
    // where the link gets established. Retry once on the
    // android-code 133 / deviceIsDisconnected race that Android throws
    // when the previous link's cleanup is still in flight.
    Future<void> doConnect() => ble.connect(state.deviceId);
    try {
      try {
        await doConnect();
      } catch (_) {
        await Future<void>.delayed(const Duration(milliseconds: 1500));
        await doConnect();
      }
    } catch (e) {
      if (!ref.mounted) return;
      state = state.copyWith(
        status: AddLampStatus.error,
        error: AddLampError.connectFailed,
        errorMessage: e.toString(),
      );
      return;
    }
    if (!ref.mounted) return;

    // Step 1: claim. Writes a single plaintext settings_blob to the
    // control service carrying the new lamp.password + lamp.name. The
    // firmware accepts this unauthenticated because the factory-default
    // `lamp.password` is empty (`ble_control.cpp:96` early-returns true
    // from `isAuthed` in that state). After the drain persists + reboots
    // the lamp, every future write requires GCM auth keyed off the new
    // password.
    //
    // SECURITY (accepted threat T2): the password chosen here is the
    // new admin credential and goes on the wire in plaintext. A passive
    // BLE sniffer in range at adoption captures it. The only real fix —
    // fleet-wide mesh authentication — was deliberately rejected. See
    // docs/accepted-security-threats.md.
    // Threat is bounded by physical proximity at the adoption moment.
    //
    // The lamp tears down its BLE link mid-write as part of fade-out +
    // reboot; the write throws a "not connected" / "disconnected"
    // exception which we treat as success (same pattern as
    // control_notifier.save). Real failures (connect dropped before the
    // write even landed, characteristic missing, etc.) surface as
    // claimFailed.
    try {
      final blob = jsonEncode({
        'lamp': {
          'password': state.password,
          'name': state.name,
          // Flip the lamp to "configured" so it stops advertising as a
          // stray and the app routes it straight in next time.
          'setup': true,
        },
      });
      final payload = Uint8List.fromList([
        LampCrypto.magicPlaintext,
        ...utf8.encode(blob),
      ]);
      try {
        await ble.write(
          state.deviceId,
          BleUuids.controlService,
          BleUuids.settingsBlob,
          payload,
          allowLongWrite: true,
        );
      } on BleDisconnectedException {
        // Expected: lamp reboots mid-write as part of setup-apply.
      } catch (e) {
        if (!isBleDisconnectError(e)) rethrow;
      }
    } catch (e) {
      if (!ref.mounted) return;
      state = state.copyWith(
        status: AddLampStatus.error,
        error: AddLampError.claimFailed,
        errorMessage: e.toString(),
      );
      return;
    }
    if (!ref.mounted) return;

    // Step 2: wait for the lamp to reboot, then reconnect + authenticate
    // + probe a section read to confirm the password stuck. The lampSection
    // characteristic is gated by auth; an unauthenticated read returns
    // empty bytes (or fails to decode) which we treat as a password mismatch.
    //
    // Every await below has an explicit timeout. Without them, a half-open
    // BLE link (Android hasn't yet noticed the reboot-driven disconnect,
    // flutter_blue_plus connect() no-ops against a stale handle) would
    // leave the wizard "Settling in…" forever — the reported "Skip hangs"
    // bug. Timeouts bounce the user back to the password step with a
    // recoverable error instead of trapping them.
    //
    // Skip path (empty password) takes a shortcut: there's no auth to
    // verify, so we use a shorter reboot wait and skip the auth+read probe
    // entirely. The lamp's `isAuthed()` early-returns true for empty
    // passwords, so a probe would always succeed if reachable — adding 8s
    // of dead air for no diagnostic value.
    state = state.copyWith(
      step: AddLampStep.verifying,
      status: AddLampStatus.working,
    );
    final isSkipPath = state.password.isEmpty;
    try {
      await Future<void>.delayed(isSkipPath ? verifySkipDelay : verifyDelay);
      // Force a fresh BLE link. After setupApply the firmware fades + reboots,
      // but flutter_blue_plus typically still believes it's connected (it only
      // notices via LINK_SUPERVISION_TIMEOUT, which can take >1s). Calling
      // connect() in that stale state is a no-op, so the next write fires
      // into a dead handle and Android returns GATT_ERROR (133). Explicitly
      // disconnecting first guarantees a real reconnect against the rebooted
      // lamp.
      try {
        await ble.disconnect(state.deviceId);
      } catch (_) {
        // already-disconnected is fine
      }
      await Future<void>.delayed(const Duration(milliseconds: 500));
      await ble.connect(state.deviceId).timeout(verifyConnectTimeout);
      if (isSkipPath) {
        // Empty-password lamps are open-access. Skipping the auth+read probe
        // shaves ~3-5s off the Skip flow and avoids the "Wrong password"
        // error path firing on a successful-but-slow probe (which is
        // nonsense UX when the user just chose to have no password).
      } else {
        await AuthClient(ble: ble)
            .authenticate(
              deviceId: state.deviceId,
              password: state.password,
            )
            .timeout(verifyOpTimeout);
        final bytes = await ble
            .readSection(state.deviceId, 'lamp')
            .timeout(verifyOpTimeout);
        if (bytes.isEmpty) {
          throw const FormatException('auth-rejected');
        }
        final j = jsonDecode(utf8.decode(bytes)) as Map<String, dynamic>;
        if (j['name'] == null) {
          throw const FormatException('auth-rejected');
        }
      }
    } on FormatException catch (_) {
      if (!ref.mounted) return;
      state = state.copyWith(
        status: AddLampStatus.error,
        step: AddLampStep.password,
        error: AddLampError.wrongPassword,
        errorMessage: "Wrong password — the lamp did not accept it.",
      );
      return;
    } on TimeoutException catch (e) {
      if (!ref.mounted) return;
      state = state.copyWith(
        status: AddLampStatus.error,
        step: AddLampStep.password,
        error: AddLampError.connectFailed,
        errorMessage: isSkipPath
            ? "Setup didn't fully apply — try again."
            : "The lamp took too long to answer — try again. ($e)",
      );
      return;
    } catch (e) {
      if (!ref.mounted) return;
      state = state.copyWith(
        status: AddLampStatus.error,
        step: AddLampStep.password,
        error: AddLampError.connectFailed,
        errorMessage: isSkipPath
            ? "Setup didn't fully apply — try again."
            : e.toString(),
      );
      return;
    }
    if (!ref.mounted) return;

    // Step 3: success — persist and advance.
    await ref.read(inventoryNotifierProvider.notifier).add(
          InventoryLamp(
            id: state.deviceId,
            name: state.name,
            controlPassword: state.password,
            critterIndex: _pickCritterIndex(),
          ),
        );
    if (!ref.mounted) return;
    await ref
        .read(activeLampNotifierProvider.notifier)
        .set(state.deviceId);
    if (!ref.mounted) return;
    state = state.copyWith(
      step: AddLampStep.done,
      status: AddLampStatus.idle,
    );
  }

  Future<void> add({
    required String deviceId,
    required String name,
  }) async {
    state = state.copyWith(status: AddLampStatus.working, errorMessage: null);
    try {
      await ref.read(inventoryNotifierProvider.notifier).add(
            InventoryLamp(
              id: deviceId,
              name: name,
              critterIndex: _pickCritterIndex(),
            ),
          );
      if (!ref.mounted) return;
      await ref.read(activeLampNotifierProvider.notifier).set(deviceId);
      if (!ref.mounted) return;
      state = state.copyWith(
        deviceId: deviceId,
        name: name,
        step: AddLampStep.done,
        status: AddLampStatus.idle,
      );
    } catch (e) {
      if (!ref.mounted) return;
      state = state.copyWith(
        status: AddLampStatus.error,
        errorMessage: e.toString(),
      );
    }
  }

  void reset() => state = const AddLampState();
}
