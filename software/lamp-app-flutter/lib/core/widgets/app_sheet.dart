import 'package:flutter/material.dart';

/// Thin wrapper around [showModalBottomSheet] with [isScrollControlled] on.
/// Drag handle is provided by the theme's [BottomSheetThemeData].
Future<T?> showAppSheet<T>(
  BuildContext context, {
  required WidgetBuilder builder,
  Color? backgroundColor,
  ShapeBorder? shape,
}) =>
    showModalBottomSheet<T>(
      context: context,
      isScrollControlled: true,
      backgroundColor: backgroundColor,
      shape: shape,
      builder: builder,
    );
