import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/app_snackbar.dart';
import '../../../core/widgets/empty_state_pane.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../control/application/control_notifier.dart';
import '../../control/presentation/widgets/connecting_view.dart';
import '../../control/presentation/widgets/disconnect_aware_body.dart';
import '../application/wisp_notifier.dart';
import '../domain/wisp_source_mode.dart';
import '../domain/wisp_status.dart';
import '../domain/zone_source.dart';
import 'palette_gradient_bar.dart';
import 'widgets/drift_controls.dart';
import 'widgets/wisp_manual_palette.dart';
import 'widgets/wisp_off_color.dart';
import 'widgets/wisp_painted_lamps.dart';
import 'widgets/wisp_password_field.dart';
import 'widgets/wisp_source_picker.dart';
import 'widgets/wisp_wifi_config.dart';
import 'widgets/wisp_zones.dart';

/// Wisp config screen. Controls how the wisp drives the lamp grid's paint.
///
/// The wisp is a separate ESP32-C6 node; the app talks to it through
/// the relay lamp's BLE control service which proxies wispOps onto the
/// mesh and caches the wisp's status broadcasts. [lampId] is the BLE
/// proxy lamp.
class WispConfigScreen extends ConsumerWidget {
  const WispConfigScreen({super.key, required this.lampId});

  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final controlAsync = ref.watch(controlNotifierProvider(lampId));
    final wispName = ref
            .watch(wispNotifierProvider(lampId))
            .value
            ?.name ??
        '';
    final appBarTitle = wispName.isNotEmpty
        ? Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              const _TwoOrbsIcon(size: 20),
              const SizedBox(width: 8),
              Text(wispName),
            ],
          )
        : const Text('Wisp');
    return Scaffold(
      appBar: AppBar(
        title: appBarTitle,
      ),
      body: controlAsync.when(
        loading: () => ConnectingView(deviceId: lampId),
        error: (e, _) => FriendlyError.page(
          title: "Couldn't reach your lamp.",
          subtitle:
              "They may have wandered out of range. Bring your phone closer "
              'and try again.',
          rawError: e,
          onRetry: () => ref.invalidate(controlNotifierProvider(lampId)),
        ),
        data: (state) {
          return DisconnectAwareBody(
            lampId: lampId,
            child: _WispBody(lampId: lampId),
          );
        },
      ),
    );
  }
}

class _WispBody extends ConsumerStatefulWidget {
  const _WispBody({required this.lampId});
  final String lampId;

  @override
  ConsumerState<_WispBody> createState() => _WispBodyState();
}

class _WispBodyState extends ConsumerState<_WispBody>
    with SingleTickerProviderStateMixin {
  late final TabController _tabController;

  @override
  void initState() {
    super.initState();
    _tabController = TabController(length: 3, vsync: this);
  }

  @override
  void dispose() {
    _tabController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final async = ref.watch(wispNotifierProvider(widget.lampId));
    return async.when(
      loading: () => const _WispLoading(),
      // Read error likely means this lamp has no wisp characteristic;
      // render the no-wisp empty state.
      error: (_, _) => const _NoWispEmpty(),
      data: (status) {
        if (!status.present) {
          return const _NoWispEmpty();
        }
        return _buildBody(context, status);
      },
    );
  }

  Widget _buildBody(BuildContext context, WispStatus status) {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    final source = status.source;
    final auroraEnabled = status.auroraDetected;

    // Schedule on the next frame to avoid mutating notifier state mid-build.
    if (source == WispSourceMode.manual &&
        notifier.draftManualPalette.isEmpty &&
        notifier.savedManualPalette.isNotEmpty) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        notifier.resetManualPaletteDraft();
      });
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        TabBar(
          controller: _tabController,
          tabs: const [
            Tab(icon: Icon(Icons.palette_outlined), text: 'Sources'),
            Tab(icon: Icon(Icons.tune), text: 'Settings'),
            Tab(icon: Icon(Icons.lightbulb_outline), text: 'Lamps'),
          ],
        ),
        Expanded(
          child: TabBarView(
            controller: _tabController,
            children: [
              _SourcesTab(
                lampId: widget.lampId,
                status: status,
                notifier: notifier,
                source: source,
                auroraEnabled: auroraEnabled,
              ),
              _SettingsTab(
                lampId: widget.lampId,
                status: status,
              ),
              _LampsTab(
                lampId: widget.lampId,
                status: status,
                notifier: notifier,
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _SourcesTab extends StatefulWidget {
  const _SourcesTab({
    required this.lampId,
    required this.status,
    required this.notifier,
    required this.source,
    required this.auroraEnabled,
  });

  final String lampId;
  final WispStatus status;
  final WispNotifier notifier;
  final WispSourceMode source;
  final bool auroraEnabled;

  @override
  State<_SourcesTab> createState() => _SourcesTabState();
}

class _SourcesTabState extends State<_SourcesTab> {
  Future<void> _runWispOp(Future<void> Function() op) async {
    try {
      await op();
    } catch (_) {
      if (!mounted) return;
      AppSnackbar.error(context, "Couldn't reach the wisp. Try again.");
    }
  }

  @override
  Widget build(BuildContext context) {
    final source = widget.source;
    final status = widget.status;
    final notifier = widget.notifier;
    final lampId = widget.lampId;

    return ListView(
      padding: const EdgeInsets.fromLTRB(
          AppSpace.lg, AppSpace.lg, AppSpace.lg, AppSpace.xxl),
      children: [
        SourcePicker(
          current: source,
          auroraEnabled: widget.auroraEnabled,
          onSelect: (m) => _runWispOp(() => notifier.setSource(m)),
        ),
        if (source == WispSourceMode.off) ...[
          const SizedBox(height: AppSpace.xl),
          OffColorPicker(
            lampId: lampId,
            current: status.offColor,
          ),
        ],
        if (source == WispSourceMode.manual) ...[
          const SizedBox(height: AppSpace.xl),
          ManualPaletteEditor(lampId: lampId),
        ],
        if (source == WispSourceMode.aurora) ...[
          const SizedBox(height: AppSpace.xl),
          if (!status.auroraConnected) ...[
            Semantics(
              liveRegion: true,
              label: 'Aurora not connected',
              child: AuroraNotConnectedNotice(
                wifiConnected: status.wifiConnected,
              ),
            ),
            const SizedBox(height: AppSpace.lg),
          ],
          WifiConfigRow(lampId: lampId, status: status),
          const SizedBox(height: AppSpace.lg),
          CurrentZone(status: status),
          const SizedBox(height: AppSpace.lg),
          ObservedZonesPicker(
            status: status,
            onPickZone: (z) => _runWispOp(() => notifier.setZone(z)),
          ),
          if (_canClearSelection(status)) ...[
            const SizedBox(height: AppSpace.lg),
            Align(
              alignment: Alignment.centerLeft,
              child: TextButton.icon(
                onPressed: () => _runWispOp(notifier.clearZone),
                icon: const Icon(Icons.close, size: 16),
                label: const Text('Clear selection'),
                style: TextButton.styleFrom(
                  foregroundColor:
                      Theme.of(context).colorScheme.onSurfaceVariant,
                ),
              ),
            ),
          ],
        ],
      ],
    );
  }

  bool _canClearSelection(WispStatus s) =>
      s.zoneSource == ZoneSource.appOp || s.zoneSource == ZoneSource.nvs;
}

class _SettingsTab extends StatefulWidget {
  const _SettingsTab({
    required this.lampId,
    required this.status,
  });

  final String lampId;
  final WispStatus status;

  @override
  State<_SettingsTab> createState() => _SettingsTabState();
}

class _SettingsTabState extends State<_SettingsTab> {
  late final TextEditingController _nameCtrl;

  @override
  void initState() {
    super.initState();
    _nameCtrl = TextEditingController(text: widget.status.name);
  }

  @override
  void didUpdateWidget(_SettingsTab old) {
    super.didUpdateWidget(old);
    // Sync name field if the wisp echoes back a new name and the field
    // is not focused (user not mid-edit).
    if (old.status.name != widget.status.name &&
        !_nameCtrl.selection.isValid) {
      _nameCtrl.text = widget.status.name;
    }
  }

  @override
  void dispose() {
    _nameCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.fromLTRB(
          AppSpace.lg, AppSpace.lg, AppSpace.lg, AppSpace.xxl),
      children: [
        _WispNameField(lampId: widget.lampId, ctrl: _nameCtrl),
        const SizedBox(height: AppSpace.lg),
        WispPasswordField(lampId: widget.lampId),
        const SizedBox(height: AppSpace.xl),
        DriftControls(lampId: widget.lampId, status: widget.status),
      ],
    );
  }
}

class _LampsTab extends ConsumerWidget {
  const _LampsTab({
    required this.lampId,
    required this.status,
    required this.notifier,
  });

  final String lampId;
  final WispStatus status;
  final WispNotifier notifier;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        PaletteGradientBar(
          sourceMode: status.source,
          manualPalette: notifier.draftManualPalette,
          offColor: status.offColor,
        ),
        Expanded(
          child: ListView(
            padding: const EdgeInsets.fromLTRB(
                AppSpace.lg, AppSpace.lg, AppSpace.lg, AppSpace.xxl),
            children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(0, AppSpace.sm, 0, AppSpace.sm),
                child: Row(
                  children: [
                    Text(
                      'LAMPS',
                      style: Theme.of(context).textTheme.labelLarge?.copyWith(
                            color: Theme.of(context).colorScheme.secondary,
                            fontWeight: FontWeight.w700,
                          ),
                    ),
                    const Spacer(),
                    IconButton(
                      icon: Icon(Icons.shuffle,
                          size: 18,
                          color: Theme.of(context).colorScheme.secondary),
                      visualDensity: VisualDensity.compact,
                      padding: EdgeInsets.zero,
                      constraints: const BoxConstraints(),
                      tooltip: 'Shuffle colours',
                      onPressed: () => ref
                          .read(wispNotifierProvider(lampId).notifier)
                          .shuffle(),
                    ),
                  ],
                ),
              ),
              PaintedLampsList(lampId: lampId),
            ],
          ),
        ),
      ],
    );
  }
}

/// Name text field. Submits via `setName` on submit/done; clamped to 20 chars
/// matching the firmware limit.
class _WispNameField extends ConsumerWidget {
  const _WispNameField({required this.lampId, required this.ctrl});

  final String lampId;
  final TextEditingController ctrl;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: AppSpace.xs),
      child: TextField(
        key: const Key('wisp-name-field'),
        controller: ctrl,
        maxLength: 20,
        decoration: const InputDecoration(
          labelText: 'Wisp name',
          counterText: '',
          hintText: 'Unnamed',
        ),
        textInputAction: TextInputAction.done,
        onSubmitted: (v) {
          final name = v.trim();
          if (name.isEmpty) return;
          ref
              .read(wispNotifierProvider(lampId).notifier)
              .setName(name)
              .catchError((_) {
            if (context.mounted) {
              AppSnackbar.error(context, "Couldn't set name. Try again.");
            }
          });
        },
      ),
    );
  }
}

/// Loading state during the initial `CHAR_WISP_STATUS` BLE round-trip.
/// "Connecting to wisp" wording distinguishes this from the earlier
/// lamp-connect phase the user already passed.
class _WispLoading extends StatelessWidget {
  const _WispLoading();

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const CircularProgressIndicator(),
          const SizedBox(height: AppSpace.md),
          Text(
            'Connecting to wisp...',
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ],
      ),
    );
  }
}

/// Shown when the lamp returned [WispStatus.empty] (no wispMac): no
/// wisp heard on the mesh from this lamp, or the lamp predates the
/// wisp characteristic.
class _NoWispEmpty extends StatelessWidget {
  const _NoWispEmpty();

  @override
  Widget build(BuildContext context) {
    return const EmptyStatePane(
      icon: _TwoOrbsIcon(size: 56),
      title: 'No wisp detected',
      subtitle: "This lamp hasn't heard a wisp on the mesh yet. "
          "Make sure the wisp is powered on and within range.",
    );
  }
}

/// Static two-orb glyph for the no-wisp / loading affordances.
/// Two orbs: the wisp paints exactly two surfaces (base + shade).
class _TwoOrbsIcon extends StatelessWidget {
  const _TwoOrbsIcon({required this.size});
  final double size;

  @override
  Widget build(BuildContext context) {
    return SizedBox.square(
      dimension: size,
      child: CustomPaint(
        painter:
            _TwoOrbsPainter(Theme.of(context).colorScheme.onSurfaceVariant),
      ),
    );
  }
}

class _TwoOrbsPainter extends CustomPainter {
  const _TwoOrbsPainter(this.color);
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2;
    // Shade orb larger than base so they read as two distinct entities.
    // Greyscale per empty-state visual language.
    final paint = Paint()
      ..color = color
      ..style = PaintingStyle.fill;
    final rShade = size.width * 0.22;
    final rBase = size.width * 0.16;
    canvas.drawCircle(
      Offset(cx + size.width * 0.13, cy - size.height * 0.16),
      rShade,
      paint,
    );
    canvas.drawCircle(
      Offset(cx - size.width * 0.13, cy + size.height * 0.16),
      rBase,
      paint,
    );
  }

  @override
  bool shouldRepaint(_TwoOrbsPainter old) => old.color != color;
}
