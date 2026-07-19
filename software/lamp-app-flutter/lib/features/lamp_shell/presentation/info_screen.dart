import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:package_info_plus/package_info_plus.dart';

import '../../../core/app_channel.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/utils/tap_counter.dart';
import '../../../core/widgets/app_snackbar.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/info_panel.dart';
import '../../../core/widgets/settings_row.dart';
import '../../control/application/advanced_session.dart';
import '../../control/application/control_notifier.dart';
import '../../control/application/control_state.dart';
import '../../control/application/dev_mode.dart';
import '../../firmware/presentation/firmware_update_panel.dart';

/// Info tab: Lamplit branding, firmware + app version footer, and the
/// 5-tap-the-wordmark gesture that unlocks session-only advanced settings.
/// When dev mode is on, a toggle row appears here to disable it.
///
/// Lives as a dedicated tab so the About content isn't buried inside the
/// Setup pane as a Configuration drilldown item.
class InfoScreen extends ConsumerWidget {
  const InfoScreen({super.key, required this.lampId});
  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final async = ref.watch(controlNotifierProvider(lampId));
    return async.when(
      loading: () => const SizedBox.expand(),
      error: (e, _) => FriendlyError.page(
        title: "Couldn't reach your lamp.",
        subtitle:
            "They may have wandered out of range. Bring your phone closer "
            'and try again.',
        rawError: e,
        onRetry: () => ref.invalidate(controlNotifierProvider(lampId)),
      ),
      data: (state) => _InfoBody(lampId: lampId, state: state),
    );
  }
}

class _InfoBody extends ConsumerStatefulWidget {
  const _InfoBody({required this.lampId, required this.state});
  final String lampId;
  final ControlState state;

  @override
  ConsumerState<_InfoBody> createState() => _InfoBodyState();
}

class _InfoBodyState extends ConsumerState<_InfoBody> {
  late final TapCounter _tap;
  // Memoised once per lifetime: PackageInfo.fromPlatform is a platform-
  // channel hop; building the future inside FutureBuilder.build would flash
  // "App ..." on every notifier tick.
  late final Future<PackageInfo> _packageInfo;

  @override
  void initState() {
    super.initState();
    _packageInfo = PackageInfo.fromPlatform();
    _tap = TapCounter(
      count: 5,
      window: const Duration(seconds: 3),
      onTriggered: () {
        // Toggle, not enable: a second 5-tap re-hides advanced UI without
        // needing a disconnect. Session-only: devMode on reflects via
        // effectiveAdvancedProvider; this flips the owned session flag.
        final p = advancedSessionProvider(widget.lampId);
        ref.read(p.notifier).toggle();
        if (mounted) {
          AppSnackbar.info(
            context,
            ref.read(p) ? 'Advanced settings unlocked' : 'Advanced settings hidden',
          );
        }
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    final v = widget.state.lamp.fwVersion;
    final ch = widget.state.lamp.fwChannel;
    final fwLine = (v == null || ch == null)
        ? 'Firmware ...'
        : 'Firmware ${formatFirmwareSemver(v)} ($ch)';
    final devModeOn = ref.watch(devModeOnProvider);
    return ListView(
      padding: const EdgeInsets.fromLTRB(
          AppSpace.lg, AppSpace.xl, AppSpace.lg, AppSpace.xxl),
      children: [
        GestureDetector(
          onTap: _tap.record,
          behavior: HitTestBehavior.opaque,
          child: const Center(
            child: Padding(
              padding: EdgeInsets.symmetric(vertical: AppSpace.lg),
              child: _LamplitWordmark(),
            ),
          ),
        ),
        const SizedBox(height: AppSpace.xs),
        Center(
          child: Text(
            '✦  Sparking inspiration through shared creative experiences',
            textAlign: TextAlign.center,
            style: textTheme.bodySmall?.copyWith(
              color: colorScheme.onSurfaceVariant,
            ),
          ),
        ),
        const SizedBox(height: AppSpace.lg),
        InfoPanel(
          child: Text.rich(
            TextSpan(
              children: [
                const TextSpan(
                    text:
                        'Lamplit Art Society is a non-profit collective sparking connection and creativity through shared lamp art. More at '),
                TextSpan(
                  text: 'lamplit.ca',
                  style: TextStyle(
                    color: colorScheme.primary,
                    fontWeight: FontWeight.w600,
                  ),
                ),
                const TextSpan(text: '.'),
              ],
            ),
          ),
        ),
        if (devModeOn) ...[
          const SizedBox(height: AppSpace.xl),
          SettingsRow(
            icon: Icons.developer_mode,
            title: 'Developer mode',
            trailing: Switch(
              value: true,
              onChanged: (_) =>
                  ref.read(devModeProvider.notifier).disable(),
            ),
          ),
        ],
        if (devModeOn) ...[
          const SizedBox(height: AppSpace.xl),
          FirmwareUpdatePanel(
            deviceId: widget.lampId,
            lampType: widget.state.lamp.lampType,
            lampFwVersion: widget.state.lamp.fwVersion,
          ),
        ],
        const SizedBox(height: AppSpace.xl),
        Center(
          child: Text(
            fwLine,
            style: textTheme.bodySmall?.copyWith(
              color: colorScheme.onSurfaceVariant,
            ),
          ),
        ),
        const SizedBox(height: AppSpace.xs),
        Center(
          child: FutureBuilder<PackageInfo>(
            future: _packageInfo,
            builder: (context, snap) {
              final info = snap.data;
              final v = info != null
                  ? '${info.version}+${info.buildNumber}'
                  : '...';
              return Text(
                'App $v',
                style: textTheme.bodySmall?.copyWith(
                  color: colorScheme.onSurfaceVariant,
                ),
              );
            },
          ),
        ),
      ],
    );
  }
}

/// Lamplit brand mark: SVG glyph + sub-wordmark. Tap target for the
/// 5-tap advanced-unlock wraps the whole column at the call site.
class _LamplitWordmark extends StatelessWidget {
  const _LamplitWordmark();

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        SvgPicture.asset(
          'assets/lamplit-logo.svg',
          height: 140,
          colorFilter: ColorFilter.mode(colorScheme.onSurface, BlendMode.srcIn),
          semanticsLabel: 'Lamplit logo',
        ),
        const SizedBox(height: AppSpace.md),
        Text(
          'Lamplit Art Society',
          style: TextStyle(
            color: colorScheme.secondary,
            fontSize: 12,
            fontWeight: FontWeight.w600,
            letterSpacing: 4,
          ),
        ),
      ],
    );
  }
}
