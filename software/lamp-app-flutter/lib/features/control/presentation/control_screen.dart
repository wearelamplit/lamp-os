import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/section_header.dart';
import '../application/control_notifier.dart';
import '../application/lamp_auth_required_exception.dart';
import 'widgets/base_card.dart';
import 'widgets/base_editor_sheet.dart';
import 'widgets/brightness_card.dart';
import 'widgets/connect_password_prompt.dart';
import 'widgets/lamp_preview.dart';
import 'widgets/wisp_indicator.dart';
import 'widgets/shade_card.dart';
import 'widgets/shade_editor_sheet.dart';


class ControlScreen extends ConsumerStatefulWidget {
  const ControlScreen({super.key, required this.lampId});
  final String lampId;

  @override
  ConsumerState<ControlScreen> createState() => _ControlScreenState();
}

class _ControlScreenState extends ConsumerState<ControlScreen> {
  @override
  Widget build(BuildContext context) {
    final lampId = widget.lampId;
    final async = ref.watch(controlNotifierProvider(lampId));
    return async.when(
      // Render the loading branch on an explicit invalidate too (the
      // "Try again" retry), so the tap immediately swaps away from the
      // error page for the whole reconnect. ReachingLampGate paints the
      // overlay; the shell underneath stays empty.
      skipLoadingOnRefresh: false,
      loading: () => const SizedBox.expand(),
      error: (e, _) {
        if (e is LampAuthRequiredException) {
          return ConnectPasswordPrompt(lampId: lampId);
        }
        return FriendlyError.page(
          title: "Couldn't reach your lamp.",
          subtitle:
              "They may have wandered out of range. Bring your phone closer "
              'and try again.',
          rawError: e,
          onRetry: () =>
              ref.invalidate(controlNotifierProvider(lampId)),
        );
      },
      data: (state) {
        final notifier =
            ref.read(controlNotifierProvider(lampId).notifier);
        final cs = Theme.of(context).colorScheme;
        final tt = Theme.of(context).textTheme;
        final shadeSegs = state.shade.segments;
        final baseSegs = state.base.segments;
        // Once any role has multiple named segments, both roles show a header,
        // so a single-segment role (snafu's Stem) can't visually fold under the
        // other role's header.
        final useHeaders = shadeSegs.length > 1 || baseSegs.length > 1;
        final baseName = state.base.segments.isEmpty
            ? 'Base'
            : state.base.segments.first.name;
        return Column(
          children: [
            Expanded(
              child: ListView(
                padding: const EdgeInsets.symmetric(vertical: AppSpace.sm),
                children: [
                  Padding(
                    padding: const EdgeInsets.symmetric(
                        vertical: AppSpace.xl, horizontal: AppSpace.lg),
                    child: Row(
                      crossAxisAlignment: CrossAxisAlignment.center,
                      children: [
                        LampPreview(
                          deviceId: lampId,
                          size: 100,
                        ),
                        const SizedBox(width: AppSpace.xl),
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Text(
                                'Hello, my name is:',
                                style: tt.bodySmall?.copyWith(
                                  color: cs.onSurfaceVariant,
                                ),
                              ),
                              Text(
                                state.lamp.name,
                                style: tt.displaySmall?.copyWith(
                                  color: cs.onSurface,
                                  fontWeight: FontWeight.w800,
                                  height: 1.0,
                                ),
                              ),
                            ],
                          ),
                        ),
                        WispIndicator(lampId: lampId, size: 56),
                      ],
                    ),
                  ),
                  BrightnessCard(
                    lampId: lampId,
                    onEditSessionChanged: (open) => notifier
                        .setEditSession(EditSurface.brightness, open),
                  ),
                  const SizedBox(height: AppSpace.md),
                  if (!useHeaders)
                    ShadeCard(
                      lampId: lampId,
                      title: shadeSegs.isEmpty ? 'Shade' : shadeSegs.first.name,
                      spec: shadeSegmentSpec(
                          0, shadeSegs.isEmpty ? 'Shade' : shadeSegs.first.name),
                      onEditSessionChanged: (open) =>
                          notifier.setEditSession(EditSurface.shade, open),
                    )
                  else ...[
                    const SectionHeader('Shade'),
                    for (var i = 0; i < shadeSegs.length; i++)
                      ShadeCard(
                        lampId: lampId,
                        title: shadeSegs[i].name,
                        spec: shadeSegmentSpec(i, shadeSegs[i].name),
                        onEditSessionChanged: i == 0
                            ? (open) =>
                                notifier.setEditSession(EditSurface.shade, open)
                            : null,
                      ),
                  ],
                  if (useHeaders) const SectionHeader('Base'),
                  BaseCard(
                    lampId: lampId,
                    title: baseName,
                    onTap: () async {
                      notifier.setEditSession(EditSurface.base, true);
                      try {
                        await showBaseEditorSheet(context,
                            lampId: lampId, title: baseName);
                      } finally {
                        notifier.setEditSession(EditSurface.base, false);
                      }
                    },
                  ),
                ],
              ),
            ),
          ],
        );
      },
    );
  }
}

