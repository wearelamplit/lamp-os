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
/// the adopt-confirm view and the lamp preview so users associate the lamp
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

  /// Post-reboot reconnect is a retry loop, not a single shot: the lamp isn't
  /// connectable for ~5-12s after the claim, so the first connect reliably
  /// hits GATT-133 and a lone attempt bounces the user to an error. Retry with
  /// a settle delay between attempts until the lamp comes back or we hit the
  /// attempt ceiling. 12 × (0.5 + 1.5)s ≈ 24s window.
  @visibleForTesting
  static int reconnectAttempts = 12;
  @visibleForTesting
  static Duration reconnectSettleDelay = const Duration(milliseconds: 500);
  @visibleForTesting
  static Duration reconnectBackoff = const Duration(milliseconds: 1500);

  /// The in-flight background reconnect+verify, exposed so tests can await it
  /// (production fires it and forgets). Null before the first claim.
  @visibleForTesting
  Future<void>? verifyDone;

  @override
  AddLampState build() => const AddLampState();

  void select(String deviceId, {int baseRgb = 0, int shadeRgb = 0}) {
    // Record the picked device and jump to the adopt-confirm step. We
    // deliberately do NOT open the BLE link here — the AdoptConfirmStep
    // widget owns the connection pulse. submit() opens the link
    // immediately before the setup writes that need it (connect-then-
    // immediately-use, same pattern as ControlNotifier).
    //
    // The scan step passes the lamp's advertised colours through so the
    // Meet-your-lamp pane can still show them after the lamp reboots off the
    // scan list.
    state = state.copyWith(
      deviceId: deviceId,
      baseRgb: baseRgb,
      shadeRgb: shadeRgb,
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
    // Leave the password page immediately — its fields are still editable and
    // a spinner sitting on top of them reads as confusing. The Meet pane owns
    // the whole "claiming + rebooting + reconnecting" wait; a claim-phase
    // failure below routes back to the password step so the user can retry.
    state = state.copyWith(
      step: AddLampStep.verifying,
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
        step: AddLampStep.password,
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
    // Accepted threat: the password chosen here is the new admin credential
    // and goes on the wire in plaintext, so a passive sniffer at adoption
    // captures it. Fleet-wide mesh auth is rejected; bounded by physical
    // proximity at the adoption moment.
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
        step: AddLampStep.password,
        status: AddLampStatus.error,
        error: AddLampError.claimFailed,
        errorMessage: e.toString(),
      );
      return;
    }
    if (!ref.mounted) return;

    // Step 2: claim landed; the lamp is rebooting. We're already on the Meet
    // pane — reconnect + verify in the BACKGROUND so the user reads about
    // their lamp while it restarts. The pane's Continue enables when status
    // flips to `ready`; failures surface inline (or route back for a wrong
    // password).
    verifyDone = _reconnectAndVerify();
  }

  /// Reboot-wait → reconnect-with-retry → (password path only) auth + a
  /// section-read probe to confirm the password stuck. Success flips status to
  /// `ready`. A wrong password routes back to the password step; an
  /// unreachable lamp stays on the Meet pane with a recoverable error + Retry.
  Future<void> _reconnectAndVerify() async {
    final ble = ref.read(bleClientProvider);
    final isSkipPath = state.password.isEmpty;
    try {
      await Future<void>.delayed(isSkipPath ? verifySkipDelay : verifyDelay);
      await _reconnectWithRetry(ble);
      if (!isSkipPath) {
        // Empty-password lamps are open-access, so there's nothing to verify.
        // For a real password, auth then read the auth-gated lamp section:
        // empty/undecodable bytes mean the password didn't take.
        await AuthClient(ble: ble)
            .authenticate(deviceId: state.deviceId, password: state.password)
            .timeout(verifyOpTimeout);
        final bytes = await ble
            .readSection(state.deviceId, 'lamp')
            .timeout(verifyOpTimeout);
        if (bytes.isEmpty) throw const FormatException('auth-rejected');
        final j = jsonDecode(utf8.decode(bytes)) as Map<String, dynamic>;
        if (j['name'] == null) throw const FormatException('auth-rejected');
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
    } catch (_) {
      // Couldn't get the lamp back within the retry window — stay on the Meet
      // pane and let the user Retry rather than dumping them out.
      if (!ref.mounted) return;
      state = state.copyWith(
        status: AddLampStatus.error,
        error: AddLampError.connectFailed,
      );
      return;
    }
    if (!ref.mounted) return;
    state = state.copyWith(status: AddLampStatus.ready);
  }

  /// Disconnect the stale handle, then connect with backoff until the rebooted
  /// lamp answers or we exhaust [reconnectAttempts]. A single connect fires
  /// while the lamp is still down and fails GATT-133; the loop is the fix.
  Future<void> _reconnectWithRetry(BleClient ble) async {
    for (var attempt = 0;; attempt++) {
      if (!ref.mounted) return;
      // flutter_blue_plus may still believe the pre-reboot link is up (it only
      // notices via LINK_SUPERVISION_TIMEOUT). Drop it so connect() isn't a
      // no-op against a dead handle.
      try {
        await ble.disconnect(state.deviceId);
      } catch (_) {
        // already-disconnected is fine
      }
      await Future<void>.delayed(reconnectSettleDelay);
      try {
        await ble.connect(state.deviceId).timeout(verifyConnectTimeout);
        return;
      } catch (_) {
        if (attempt >= reconnectAttempts - 1) rethrow;
        await Future<void>.delayed(reconnectBackoff);
      }
    }
  }

  /// Meet-pane Retry after a reconnect failure.
  void retryVerify() {
    state = state.copyWith(
      status: AddLampStatus.working,
      error: AddLampError.none,
      errorMessage: null,
    );
    verifyDone = _reconnectAndVerify();
  }

  /// Meet-pane Continue: persist the lamp to inventory, make it active, and
  /// advance to the done screen. Only reachable once status is `ready`.
  Future<void> finishAdoption() async {
    await ref.read(inventoryNotifierProvider.notifier).add(
          InventoryLamp(
            id: state.deviceId,
            name: state.name,
            controlPassword: state.password,
            critterIndex: _pickCritterIndex(),
          ),
        );
    if (!ref.mounted) return;
    await ref.read(activeLampNotifierProvider.notifier).set(state.deviceId);
    if (!ref.mounted) return;
    state = state.copyWith(step: AddLampStep.done, status: AddLampStatus.idle);
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
