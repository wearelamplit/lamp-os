// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'expression_draft.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// Working copy of an expression while the user is editing it. Held in a
/// keep-alive provider so navigating away from the editor and back doesn't
/// lose in-progress edits. Save / Delete invalidate the entry.
///
/// Family key is `(lampId, type, target)`. If the family entry doesn't
/// match any existing expression, a fresh draft is created with that
/// (type, target) — this is the path used for new entries after the user
/// picks them in `AddExpressionPickerScreen`.

@ProviderFor(ExpressionDraft)
final expressionDraftProvider = ExpressionDraftFamily._();

/// Working copy of an expression while the user is editing it. Held in a
/// keep-alive provider so navigating away from the editor and back doesn't
/// lose in-progress edits. Save / Delete invalidate the entry.
///
/// Family key is `(lampId, type, target)`. If the family entry doesn't
/// match any existing expression, a fresh draft is created with that
/// (type, target) — this is the path used for new entries after the user
/// picks them in `AddExpressionPickerScreen`.
final class ExpressionDraftProvider
    extends $NotifierProvider<ExpressionDraft, ExpressionConfig> {
  /// Working copy of an expression while the user is editing it. Held in a
  /// keep-alive provider so navigating away from the editor and back doesn't
  /// lose in-progress edits. Save / Delete invalidate the entry.
  ///
  /// Family key is `(lampId, type, target)`. If the family entry doesn't
  /// match any existing expression, a fresh draft is created with that
  /// (type, target) — this is the path used for new entries after the user
  /// picks them in `AddExpressionPickerScreen`.
  ExpressionDraftProvider._({
    required ExpressionDraftFamily super.from,
    required (String, String, int) super.argument,
  }) : super(
         retry: null,
         name: r'expressionDraftProvider',
         isAutoDispose: false,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$expressionDraftHash();

  @override
  String toString() {
    return r'expressionDraftProvider'
        ''
        '$argument';
  }

  @$internal
  @override
  ExpressionDraft create() => ExpressionDraft();

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(ExpressionConfig value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<ExpressionConfig>(value),
    );
  }

  @override
  bool operator ==(Object other) {
    return other is ExpressionDraftProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$expressionDraftHash() => r'2c04a96ea89450be0bebdd194131a25cf28d7147';

/// Working copy of an expression while the user is editing it. Held in a
/// keep-alive provider so navigating away from the editor and back doesn't
/// lose in-progress edits. Save / Delete invalidate the entry.
///
/// Family key is `(lampId, type, target)`. If the family entry doesn't
/// match any existing expression, a fresh draft is created with that
/// (type, target) — this is the path used for new entries after the user
/// picks them in `AddExpressionPickerScreen`.

final class ExpressionDraftFamily extends $Family
    with
        $ClassFamilyOverride<
          ExpressionDraft,
          ExpressionConfig,
          ExpressionConfig,
          ExpressionConfig,
          (String, String, int)
        > {
  ExpressionDraftFamily._()
    : super(
        retry: null,
        name: r'expressionDraftProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: false,
      );

  /// Working copy of an expression while the user is editing it. Held in a
  /// keep-alive provider so navigating away from the editor and back doesn't
  /// lose in-progress edits. Save / Delete invalidate the entry.
  ///
  /// Family key is `(lampId, type, target)`. If the family entry doesn't
  /// match any existing expression, a fresh draft is created with that
  /// (type, target) — this is the path used for new entries after the user
  /// picks them in `AddExpressionPickerScreen`.

  ExpressionDraftProvider call(String lampId, String type, int target) =>
      ExpressionDraftProvider._(argument: (lampId, type, target), from: this);

  @override
  String toString() => r'expressionDraftProvider';
}

/// Working copy of an expression while the user is editing it. Held in a
/// keep-alive provider so navigating away from the editor and back doesn't
/// lose in-progress edits. Save / Delete invalidate the entry.
///
/// Family key is `(lampId, type, target)`. If the family entry doesn't
/// match any existing expression, a fresh draft is created with that
/// (type, target) — this is the path used for new entries after the user
/// picks them in `AddExpressionPickerScreen`.

abstract class _$ExpressionDraft extends $Notifier<ExpressionConfig> {
  late final _$args = ref.$arg as (String, String, int);
  String get lampId => _$args.$1;
  String get type => _$args.$2;
  int get target => _$args.$3;

  ExpressionConfig build(String lampId, String type, int target);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<ExpressionConfig, ExpressionConfig>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<ExpressionConfig, ExpressionConfig>,
              ExpressionConfig,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args.$1, _$args.$2, _$args.$3));
  }
}
