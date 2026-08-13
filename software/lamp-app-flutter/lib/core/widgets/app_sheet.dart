import 'package:flutter/material.dart';

/// Thin wrapper around [showModalBottomSheet] with [isScrollControlled] on.
/// Drag handle is provided by the theme's [BottomSheetThemeData].
Future<T?> showAppSheet<T>(
  BuildContext context, {
  required WidgetBuilder builder,
}) =>
    showModalBottomSheet<T>(
      context: context,
      isScrollControlled: true,
      builder: builder,
    );
