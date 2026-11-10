import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_spacing.dart';
import '../../../core/utils/string_case.dart';
import '../../control/application/control_notifier.dart';
import '../application/firmware_notifier.dart';
import '../domain/firmware_state.dart';

/// Dimmed opacity of a frozen lamp surface, matching the shell's
/// disconnected nav bar.
const double _frozenOpacity = 0.4;

bool _busy(WidgetRef ref, String lampId) =>
    ref.watch(firmwareNotifierProvider(lampId).select((s) => s.isBusy));

/// Freezes [child] while the app pushes firmware to [lampId]: the lamp drops
/// every non-OTA write in that window, so its controls must not look live.
/// Wrap every surface that writes to the lamp. Sheets and dialogs opened from
/// inside [child] freeze with it, and keep their draft until the push ends;
/// a tap outside a frozen popup still dismisses it.
class FirmwarePushLock extends ConsumerWidget {
  const FirmwarePushLock({
    super.key,
    required this.lampId,
    required this.child,
  });

  final String lampId;
  final Widget child;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final frozen = _busy(ref, lampId);
    return _PushLockScope(
      lampId: lampId,
      child: ExcludeFocus(
        excluding: frozen,
        child: _Inert(
          inert: frozen,
          child: Opacity(opacity: frozen ? _frozenOpacity : 1, child: child),
        ),
      ),
    );
  }
}

/// Carries the lock into popups: showDialog and showModalBottomSheet wrap
/// their content in the InheritedThemes captured at the launch site.
class _PushLockScope extends InheritedTheme {
  const _PushLockScope({required this.lampId, required super.child});

  final String lampId;

  @override
  Widget wrap(BuildContext context, Widget child) =>
      FirmwarePushLock(lampId: lampId, child: child);

  @override
  bool updateShouldNotify(_PushLockScope oldWidget) =>
      lampId != oldWidget.lampId;
}

/// While [inert], swallows pointers that land on [child] without delivering
/// them, and lets the rest fall through, so a popup's barrier still
/// dismisses it.
class _Inert extends SingleChildRenderObjectWidget {
  const _Inert({required this.inert, super.child});

  final bool inert;

  @override
  _RenderInert createRenderObject(BuildContext context) => _RenderInert(inert);

  @override
  void updateRenderObject(BuildContext context, _RenderInert renderObject) {
    renderObject.inert = inert;
  }
}

class _RenderInert extends RenderProxyBox {
  _RenderInert(this._inert);

  bool _inert;
  set inert(bool value) {
    if (value == _inert) return;
    _inert = value;
    markNeedsSemanticsUpdate();
  }

  @override
  bool hitTest(BoxHitTestResult result, {required Offset position}) {
    if (!_inert) return super.hitTest(result, position: position);
    if (!super.hitTest(BoxHitTestResult(), position: position)) return false;
    result.add(BoxHitTestEntry(this, position));
    return true;
  }

  @override
  void describeSemanticsConfiguration(SemanticsConfiguration config) {
    super.describeSemanticsConfiguration(config);
    config.isBlockingUserActions = _inert;
  }
}

/// App-level strip that reads "Teaching `name` new tricks" while the app
/// pushes firmware to [lampId], the lamp of the current route (null off a
/// lamp route). Sits above the navigator so it stays visible over sheets and
/// dialogs; [child] is laid out below it.
class FirmwarePushBanner extends ConsumerWidget {
  const FirmwarePushBanner({
    super.key,
    required this.lampId,
    required this.child,
  });

  final String? lampId;
  final Widget child;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final id = lampId;
    final showing = id != null && _busy(ref, id);
    return Column(
      children: [
        if (showing) _TeachingStrip(lampId: id) else const SizedBox.shrink(),
        Expanded(
          child: MediaQuery.removePadding(
            context: context,
            removeTop: showing,
            child: child,
          ),
        ),
      ],
    );
  }
}

class _TeachingStrip extends ConsumerWidget {
  const _TeachingStrip({required this.lampId});

  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final cs = Theme.of(context).colorScheme;
    final tt = Theme.of(context).textTheme;
    final name =
        ref.watch(
          controlNotifierProvider(
            lampId,
          ).select((s) => s.value?.lamp.name.toTitleCase()),
        ) ??
        'your lamp';
    final state = ref.watch(firmwareNotifierProvider(lampId));
    final progress = switch (state) {
      FirmwareStreaming(:final progress) => progress,
      _ => null,
    };
    return Material(
      color: cs.tertiaryContainer,
      child: SafeArea(
        bottom: false,
        child: Semantics(
          liveRegion: true,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Padding(
                padding: const EdgeInsets.symmetric(
                  horizontal: AppSpace.lg,
                  vertical: AppSpace.sm,
                ),
                child: Text(
                  'Teaching $name new tricks 🎓',
                  textAlign: TextAlign.center,
                  style: tt.labelLarge?.copyWith(color: cs.onTertiaryContainer),
                ),
              ),
              LinearProgressIndicator(
                value: progress,
                // deliberate dimension, not spacing
                minHeight: 2,
              ),
            ],
          ),
        ),
      ),
    );
  }
}
