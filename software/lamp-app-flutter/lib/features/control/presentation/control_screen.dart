import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart' as gr;

import '../../../core/routing/routes.dart';
import '../../../core/theme/brand_colors.dart';
import '../../../core/widgets/friendly_error.dart';
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
        return Column(
          children: [
            if (!state.connected)
              ConnectionBanner(attempt: state.reconnectAttempt),
            Expanded(
              child: ListView(
                padding: const EdgeInsets.symmetric(vertical: 8),
                children: [
                  // "Hello my name is:" nameplate beside the live critter
                  // — ports `CritterNameplate.vue` from the old UI.
                  Padding(
                    padding: const EdgeInsets.symmetric(
                        vertical: 24, horizontal: 16),
                    child: Row(
                      crossAxisAlignment: CrossAxisAlignment.center,
                      children: [
                        // LampPreview watches its own (shade, base) slice
                        // — only rebuilds when those change, not when the
                        // surrounding state churns from coalesced writes
                        // or inventory writebacks.
                        LampPreview(
                          deviceId: lampId,
                          // Smaller than the previous centred 140 so the
                          // name text gets enough room to the right.
                          size: 100,
                        ),
                        const SizedBox(width: 24),
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              const Text(
                                'Hello, my name is:',
                                style: TextStyle(
                                  color: BrandColors.nameplateGrey,
                                  fontSize: 13,
                                  fontWeight: FontWeight.w300,
                                ),
                              ),
                              Text(
                                state.lamp.name,
                                style: const TextStyle(
                                  color: BrandColors.lampWhite,
                                  fontSize: 28,
                                  fontWeight: FontWeight.w800,
                                  height: 1.0,
                                ),
                              ),
                            ],
                          ),
                        ),
                        // Top-right wisp indicator. Hides itself when no
                        // wisp is currently controlling either surface;
                        // pops on with a soft glow + drift when wisp
                        // takes over (firmware sends a CHAR_WISP_STATUS
                        // notify on every override transition).
                        WispIndicator(lampId: lampId, size: 56),
                      ],
                    ),
                  ),
                  // ShadeCard / BaseCard watch their own slices; ControlScreen
                  // hands them lampId + the edit-session callback. The
                  // onChanged write path is internalised in the card.
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
                  const SizedBox(height: 12),
                  BrightnessCard(
                    lampId: lampId,
                    onEditSessionChanged: (open) => notifier
                        .setEditSession(EditSurface.brightness, open),
                  ),
                  const SizedBox(height: 8),
                  _ConfigurationRow(lampId: lampId),
                ],
              ),
            ),
          ],
        );
      },
    );
  }
}

/// Inline "Lamp configuration" row at the bottom of the Setup tab body.
/// Replaces the AppBar gear that used to live on every tab — the gear
/// felt like chrome bolted on top; an inline row reads as "this is just
/// the next thing in Setup, after the colour cards." Navigates to the
/// same `/lamp/:id/setup` route (lamp name, password, home mode,
/// advanced LEDs, About / firmware update / version footer).
class _ConfigurationRow extends StatelessWidget {
  const _ConfigurationRow({required this.lampId});

  final String lampId;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: InkWell(
        borderRadius: BorderRadius.circular(12),
        onTap: () =>
            gr.GoRouter.maybeOf(context)?.push(AppRoutes.setup(lampId)),
        child: Container(
          padding: const EdgeInsets.all(16),
          decoration: BoxDecoration(
            color: Colors.white.withValues(alpha: 0.04),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: Colors.white.withValues(alpha: 0.06)),
          ),
          child: const Row(
            children: [
              Icon(Icons.settings, color: BrandColors.lampWhite, size: 22),
              SizedBox(width: 16),
              Expanded(
                child: Text(
                  'Configuration',
                  style: TextStyle(
                    color: BrandColors.lampWhite,
                    fontSize: 14,
                    fontWeight: FontWeight.w600,
                    letterSpacing: 0.4,
                  ),
                ),
              ),
              Icon(Icons.chevron_right, color: BrandColors.slateGrey),
            ],
          ),
        ),
      ),
    );
  }
}
