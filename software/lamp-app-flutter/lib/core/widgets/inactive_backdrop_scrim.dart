import 'dart:ui';

import 'package:flutter/material.dart';

/// Sigma for the subtle blur applied to the inactive-background scrim
/// behind modal sheets / dialogs.
const double _kScrimBlurSigma = 4.0;

/// Translucent dark fill layered on top of the blur. ~50% opaque so the
/// blur reads cleanly without the page going pitch black.
const Color _kScrimColor = Color(0x80000000);

/// Slide-in for the sheet wrapper.
const Duration _kSheetTransitionDuration = Duration(milliseconds: 250);

/// Cross-fade for dialog wrapper.
const Duration _kDialogTransitionDuration = Duration(milliseconds: 200);

/// The blurred + dimmed scrim itself. Used internally as the first child
/// of a `Stack` inside the modal route's page, so its `BackdropFilter`
/// samples only what's painted behind the route (the host page) and
/// later siblings in the Stack — the modal content — render crisply on
/// top.
class _BlurredScrim extends StatelessWidget {
  final VoidCallback? onTap;
  const _BlurredScrim({this.onTap});

  @override
  Widget build(BuildContext context) {
    return Positioned.fill(
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: ClipRect(
          child: BackdropFilter(
            filter: ImageFilter.blur(
              sigmaX: _kScrimBlurSigma,
              sigmaY: _kScrimBlurSigma,
            ),
            child: const ColoredBox(color: _kScrimColor),
          ),
        ),
      ),
    );
  }
}

/// Drop-in replacement for `showModalBottomSheet` that renders the
/// behind-page content through a blurred + dimmed scrim while keeping
/// the sheet content crisp.
///
/// Implemented over `showGeneralDialog` so the `BackdropFilter` can sit
/// in the same Stack as the sheet content, painted BEFORE it. Wrapping a
/// `BackdropFilter` in a separate `OverlayEntry` doesn't compose
/// correctly with showModalBottomSheet's overlay model: the filter ends
/// up sampling the modal content as well.
///
/// API mirrors the most-used subset of `showModalBottomSheet`.
Future<T?> showBlurredModalBottomSheet<T>({
  required BuildContext context,
  required WidgetBuilder builder,
  Color? backgroundColor,
  ShapeBorder? shape,
  bool isScrollControlled = false,
  bool useSafeArea = false,
  bool useRootNavigator = false,
}) {
  return showGeneralDialog<T>(
    context: context,
    barrierColor: Colors.transparent,
    barrierDismissible: true,
    barrierLabel: 'Dismiss',
    useRootNavigator: useRootNavigator,
    transitionDuration: _kSheetTransitionDuration,
    pageBuilder: (ctx, _, _) {
      final theme = Theme.of(ctx);
      final sheetTheme = theme.bottomSheetTheme;
      final resolvedShape = shape ??
          sheetTheme.shape ??
          const RoundedRectangleBorder(
            borderRadius: BorderRadius.vertical(top: Radius.circular(16)),
          );
      final resolvedBg = backgroundColor ??
          sheetTheme.backgroundColor ??
          theme.colorScheme.surface;
      Widget content = Material(
        color: resolvedBg,
        shape: resolvedShape,
        clipBehavior: Clip.antiAlias,
        child: Builder(builder: builder),
      );
      if (useSafeArea) {
        content = SafeArea(top: false, child: content);
      }
      final sheet = Align(
        alignment: Alignment.bottomCenter,
        child: isScrollControlled
            ? content
            : ConstrainedBox(
                constraints: BoxConstraints(
                  maxHeight: MediaQuery.of(ctx).size.height * 9 / 16,
                ),
                child: content,
              ),
      );
      return Stack(
        children: [
          _BlurredScrim(onTap: () => Navigator.of(ctx).pop()),
          sheet,
        ],
      );
    },
    transitionBuilder: (_, anim, _, child) {
      return SlideTransition(
        position: Tween<Offset>(
          begin: const Offset(0, 1),
          end: Offset.zero,
        ).animate(CurvedAnimation(parent: anim, curve: Curves.easeOut)),
        child: child,
      );
    },
  );
}

/// Drop-in replacement for `showDialog` that wraps the dialog content
/// in a blurred + dimmed full-screen scrim.
///
/// Implemented over `showGeneralDialog` for the same reason as
/// `showBlurredModalBottomSheet` — the BackdropFilter has to be a
/// sibling of the dialog content (painted first), not an overlay entry
/// inserted around it.
Future<T?> showBlurredDialog<T>({
  required BuildContext context,
  required WidgetBuilder builder,
  bool barrierDismissible = true,
  bool useRootNavigator = true,
}) {
  return showGeneralDialog<T>(
    context: context,
    barrierColor: Colors.transparent,
    barrierDismissible: barrierDismissible,
    barrierLabel: barrierDismissible ? 'Dismiss' : null,
    useRootNavigator: useRootNavigator,
    transitionDuration: _kDialogTransitionDuration,
    pageBuilder: (ctx, _, _) {
      return Stack(
        children: [
          _BlurredScrim(
            onTap: barrierDismissible ? () => Navigator.of(ctx).pop() : null,
          ),
          Center(child: Builder(builder: builder)),
        ],
      );
    },
    transitionBuilder: (_, anim, _, child) {
      return FadeTransition(
        opacity: CurvedAnimation(parent: anim, curve: Curves.easeOut),
        child: child,
      );
    },
  );
}
