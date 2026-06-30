import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/settings_row.dart';

void main() {
  testWidgets('SettingsRow title uses titleMedium (JosefinSans) from theme', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: const Scaffold(
        body: SettingsRow(icon: Icons.wifi, title: 'Network'),
      ),
    ));
    final titleText = tester.widget<Text>(find.text('Network'));
    expect(titleText.style?.fontFamily, 'JosefinSans');
  });

  testWidgets('SettingsRow subtitle uses bodySmall from theme', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: const Scaffold(
        body: SettingsRow(icon: Icons.wifi, title: 'Network', subtitle: 'Off'),
      ),
    ));
    final subtitleText = tester.widget<Text>(find.text('Off'));
    expect(subtitleText.style?.fontFamily, 'Inter');
  });

  testWidgets('SettingsRow shows chevron when onTap set and no trailing', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: SettingsRow(icon: Icons.wifi, title: 'Network', onTap: () {}),
      ),
    ));
    expect(find.byIcon(Icons.chevron_right), findsOneWidget);
  });

  testWidgets('SettingsRow fires onTap', (tester) async {
    var tapped = false;
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(
        body: SettingsRow(
          icon: Icons.wifi,
          title: 'Network',
          onTap: () => tapped = true,
        ),
      ),
    ));
    await tester.tap(find.byType(SettingsRow));
    await tester.pump();
    expect(tapped, isTrue);
  });

  testWidgets('SettingsGroupHeading renders uppercased label', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: const Scaffold(body: SettingsGroupHeading('general')),
    ));
    expect(find.text('GENERAL'), findsOneWidget);
  });

  testWidgets('drillChevron shows chevron alongside a trailing widget', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(body: SettingsRow(
        icon: Icons.home_outlined, title: 'Home Mode',
        trailing: Switch(value: true, onChanged: (_) {}),
        onTap: () {}, drillChevron: true,
      )),
    ));
    expect(find.byType(Switch), findsOneWidget);
    expect(find.byIcon(Icons.chevron_right), findsOneWidget);
  });

  testWidgets('without drillChevron a trailing widget suppresses the chevron', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(body: SettingsRow(
        icon: Icons.wifi_tethering, title: 'Toggle only',
        trailing: Switch(value: false, onChanged: (_) {}),
        onTap: () {},
      )),
    ));
    expect(find.byType(Switch), findsOneWidget);
    expect(find.byIcon(Icons.chevron_right), findsNothing);
  });

  testWidgets('a plain drill row still shows the chevron', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Scaffold(body: SettingsRow(
        icon: Icons.memory, title: 'Drill', onTap: () {},
      )),
    ));
    expect(find.byIcon(Icons.chevron_right), findsOneWidget);
  });
}
