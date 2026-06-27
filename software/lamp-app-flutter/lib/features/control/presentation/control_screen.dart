import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart' as gr;

import '../../../core/routing/routes.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/nav_row.dart';
import '../application/control_notifier.dart';
import '../application/lamp_auth_required_exception.dart';
import 'widgets/base_card.dart';
import 'widgets/base_editor_sheet.dart';
import 'widgets/brightness_card.dart';
import 'widgets/connect_password_prompt.dart';
import 'widgets/connecting_view.dart';
import 'widgets/connection_banner.dart';
import 'widgets/lamp_preview.dart';
import 'widgets/wisp_indicator.dart';
import 'widgets/shade_card.dart';


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
      // "Try again" retry), so the tap immediately swaps to ConnectingView
      // instead of holding the error page for the whole reconnect.
      skipLoadingOnRefresh: false,
      loading: () => ConnectingView(deviceId: lampId),
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
        return Column(
          children: [
            if (!state.connected)
              ConnectionBanner(attempt: state.reconnectAttempt),
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
                  ShadeCard(
                    lampId: lampId,
                    onEditSessionChanged: (open) =>
                        notifier.setEditSession(EditSurface.shade, open),
                  ),
                  BaseCard(
                    lampId: lampId,
                    onTap: () =>
                        showBaseEditorSheet(context, lampId: lampId),
                  ),
                  const SizedBox(height: AppSpace.md),
                  BrightnessCard(
                    lampId: lampId,
                    onEditSessionChanged: (open) => notifier
                        .setEditSession(EditSurface.brightness, open),
                  ),
                  const SizedBox(height: AppSpace.sm),
                  NavRow(
                    icon: Icons.settings,
                    title: 'Configuration',
                    onTap: () => gr.GoRouter.maybeOf(context)
                        ?.push(AppRoutes.setup(lampId)),
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

