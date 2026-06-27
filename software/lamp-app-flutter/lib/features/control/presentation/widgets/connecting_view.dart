import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_svg/flutter_svg.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../inventory/application/inventory_notifier.dart';
import '../../application/lamp_save_status.dart';
import 'critter_asset.dart';

/// Full-screen "we're talking to the lamp" state — a critter SVG that gently
/// pulses, with a single line of text below. Reads the lamp's stored
/// [InventoryLamp.critterIndex] so each lamp keeps the same friend across
/// sessions. Falls back to a deviceId hash for legacy entries adopted
/// before the field existed.
class ConnectingView extends ConsumerStatefulWidget {
  const ConnectingView({super.key, required this.deviceId});
  final String deviceId;

  @override
  ConsumerState<ConnectingView> createState() => _ConnectingViewState();
}

class _ConnectingViewState extends ConsumerState<ConnectingView>
    with SingleTickerProviderStateMixin {
  late final AnimationController _ctrl = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 1400),
  );

  late final Animation<double> _scale = Tween(begin: 0.96, end: 1.04)
      .chain(CurveTween(curve: Curves.easeInOut))
      .animate(_ctrl);

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    // Pause the pulse when off-screen (e.g. another tab is foregrounded).
    if (TickerMode.getValuesNotifier(context).value.enabled) {
      if (!_ctrl.isAnimating) _ctrl.repeat(reverse: true);
    } else {
      _ctrl.stop();
    }
  }

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final inventory = ref.watch(inventoryNotifierProvider).value;
    final lamp =
        inventory?.firstWhereOrNull((l) => l.id == widget.deviceId);
    final asset = critterAssetFor(
      critterIndex: lamp?.critterIndex,
      deviceId: widget.deviceId,
    );
    // Switch the message when we're in the post-save reconnect window —
    // controlNotifier.save() flips the flag true before going AsyncLoading
    // and back to false after the reconnect resolves.
    final saving = ref.watch(lampSaveStatusProvider(widget.deviceId));
    final message = saving ? 'Saving…' : 'Connecting…';
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          ScaleTransition(
            scale: _scale,
            child: SvgPicture.asset(
              asset,
              width: 160,
              height: 160,
            ),
          ),
          const SizedBox(height: AppSpace.xl),
          Text(
            message,
            style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                  color: Theme.of(context).colorScheme.onSurfaceVariant,
                  letterSpacing: 0.5,
                ),
          ),
        ],
      ),
    );
  }
}
