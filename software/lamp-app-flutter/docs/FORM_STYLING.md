# Form Styling Convention

## Spacing rule

All gaps, padding, and margins use `AppSpace` tokens. All card radii use `AppRadius`.

| Token | Value |
|-------|-------|
| `AppSpace.xs` | 4 |
| `AppSpace.sm` | 8 |
| `AppSpace.md` | 12 |
| `AppSpace.lg` | 16 |
| `AppSpace.xl` | 24 |
| `AppSpace.xxl` | 32 |
| `AppRadius.card` | 12 |
| `AppRadius.swatch` | 14 |

**Snap to the nearest token — do not add tokens.**

| Off-grid value | Snap to |
|----------------|---------|
| 6 | `sm` (8) |
| 10 | `md` (12) |
| 14 | `md` (12) |
| 20 | `xl` (24) for section gaps; `lg` (16) for in-group field gaps |

**Allowed literal exceptions** — each must carry `// deliberate dimension, not spacing` at the call site:
- Hairlines / border widths: `1`, `2`
- Deliberate pill: `BorderRadius.circular(999)`
- Component dimensions: icon sizes, avatar/control widths/heights, paint-geometry constants

## Grouping idiom

Pick the **first** primitive that fits the content:

### `FormSection(title:, children:)` — mixed controls, one config object
Use when the card contains a cluster of different control types (text field + segmented button + nav row) that all edit one config object, e.g. the LED setup screen's Shade/Base strips.

> **Gotcha:** `FormSection`'s `LampCard` has `padding: EdgeInsets.zero`. Wrap each non-self-padding child in `Padding(EdgeInsets.all(AppSpace.lg))`. Self-padding primitives (`NavRow`, `SettingsRow`) need no extra wrapper.

```dart
// ✓ DO
FormSection(
  title: 'LED Setup',
  children: [
    Padding(
      padding: EdgeInsets.all(AppSpace.lg),
      child: CountField(...),
    ),
    NavRow(title: 'Personality', ...),  // self-padding, no wrapper needed
  ],
)

// ✗ DON'T — ad-hoc Card + raw padding literal
Card(
  child: Padding(
    padding: const EdgeInsets.all(14), // off-grid, snap to AppSpace.md or .lg
    child: Column(children: [...]),
  ),
)
```

### `SettingsRow` under `SettingsGroupHeading` — homogeneous tap rows
Use for a list of same-type tappable rows (icon + title + subtitle + chevron/switch), e.g. the Setup tab.

```dart
// ✓ DO
SettingsGroupHeading('Network'),
SettingsRow(icon: Icons.wifi, title: 'Wi-Fi', onTap: ...),
SettingsRow(icon: Icons.bluetooth, title: 'BLE', onTap: ...),
```

### Loose (`SectionHeader` + tokenized spacing) — single control or full-bleed selector
Use for a single standalone control, an info/non-form screen, or a full-bleed pill selector (target/personality choosers) designed to span width.

```dart
// ✓ DO
SectionHeader('Target'),
SizedBox(height: AppSpace.sm),
PillSelector(...),
```

## General rules

- Build forms from the primitives: `FormSection`, `SettingsRow`/`SettingsGroupHeading`, `LampCard`, `NavRow`, `SectionHeader`. Not ad-hoc `Card`/`Container` stacks.
- Colors via `Theme.of(context).colorScheme` or `context.brandExtras`. Never raw hex.
