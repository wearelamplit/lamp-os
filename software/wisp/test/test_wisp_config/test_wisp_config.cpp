// Native test seam: include the .cpp directly (it is excluded from the native
// build filter). Arduino.h / Preferences.h stubs live alongside this file.
#include "config/wisp_config.cpp"  // NOLINT(build/include)

#include <unity.h>
#include <lampos/led_types.hpp>

// kv_reset() is defined inside Preferences.h's anonymous namespace.
// Call it in setUp() to isolate each test.
void setUp()    { kv_reset(); }
void tearDown() {}

// ---- Tests ------------------------------------------------------------------

void test_name_default_is_empty() {
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_STRING("", cfg.name().c_str());
}

void test_setname_persists_and_getter_returns_it() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setName("living-room");
  TEST_ASSERT_EQUAL_STRING("living-room", cfg.name().c_str());
}

void test_setname_clamps_to_20_chars() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setName("123456789012345678901234");  // 24 chars
  TEST_ASSERT_EQUAL_INT(20, (int)cfg.name().length());
  TEST_ASSERT_EQUAL_STRING("12345678901234567890", cfg.name().c_str());
}

void test_setname_20_chars_exact_not_clamped() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setName("12345678901234567890");  // exactly 20
  TEST_ASSERT_EQUAL_INT(20, (int)cfg.name().length());
}

void test_setname_empty_allowed() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setName("something");
  cfg.setName("");
  TEST_ASSERT_EQUAL_STRING("", cfg.name().c_str());
}

void test_password_default_is_empty() {
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_STRING("", cfg.password().c_str());
}

void test_setpassword_persists_and_getter_returns_it() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setPassword("secret");
  TEST_ASSERT_EQUAL_STRING("secret", cfg.password().c_str());
}

void test_setpassword_empty_clears() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setPassword("secret");
  cfg.setPassword("");
  TEST_ASSERT_EQUAL_STRING("", cfg.password().c_str());
}

// --- LED config ---

void test_led_format_default_is_grb() {
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_INT((int)lampos::led::ByteOrder::GRB, (int)cfg.ledFormat());
}

void test_set_led_format_roundtrips() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setLedFormat(lampos::led::ByteOrder::BGR);
  TEST_ASSERT_EQUAL_INT((int)lampos::led::ByteOrder::BGR, (int)cfg.ledFormat());
}

void test_pixel_count_default_is_30() {
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_INT(30, (int)cfg.pixelCount());
}

void test_set_pixel_count_roundtrips() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setPixelCount(60);
  TEST_ASSERT_EQUAL_INT(60, (int)cfg.pixelCount());
}

void test_set_pixel_count_clamps_to_max() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setPixelCount(999);
  TEST_ASSERT_EQUAL_INT((int)wisp::kMaxRingPixels, (int)cfg.pixelCount());
}

void test_set_pixel_count_zero_clamped_to_one() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setPixelCount(0);
  TEST_ASSERT_EQUAL_INT(1, (int)cfg.pixelCount());
}

void test_brightness_default_is_full() {
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_INT(100, (int)cfg.brightness());
}

void test_set_brightness_roundtrips_and_clamps() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setBrightness(40);
  TEST_ASSERT_EQUAL_INT(40, (int)cfg.brightness());

  wisp::WispConfig reload;
  reload.begin();
  TEST_ASSERT_EQUAL_INT(40, (int)reload.brightness());

  cfg.setBrightness(200);
  TEST_ASSERT_EQUAL_INT(100, (int)cfg.brightness());
}

// A wisp that stored the retired Aurora mode (2) boots into Off.
void test_legacy_aurora_source_loads_as_off() {
  {
    Preferences p;
    p.putInt("srcMode", 2);
  }
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_INT((int)wisp::WispSourceMode::Off, (int)cfg.sourceMode());
}

void test_osc_source_roundtrips() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setSourceMode(wisp::WispSourceMode::Osc);
  wisp::WispConfig reload;
  reload.begin();
  TEST_ASSERT_EQUAL_INT((int)wisp::WispSourceMode::Osc, (int)reload.sourceMode());
}

void test_osc_role_defaults_primary_and_persists() {
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_INT((int)wisp::OscRole::Primary, (int)cfg.oscRole());
  cfg.setOscRole(wisp::OscRole::Tertiary);
  wisp::WispConfig reload;
  reload.begin();
  TEST_ASSERT_EQUAL_INT((int)wisp::OscRole::Tertiary, (int)reload.oscRole());
}

void test_corrupt_osc_role_loads_as_primary() {
  {
    Preferences p;
    p.putUChar("oscRole", 9);
  }
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_INT((int)wisp::OscRole::Primary, (int)cfg.oscRole());
}

// A fresh wisp reads Straylight, slot 1, and the stored target is Straylight's
// own address (P1).
void test_osc_defaults_to_straylight_slot_1() {
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Straylight, (int)cfg.oscMode());
  TEST_ASSERT_EQUAL_UINT8(1, cfg.oscSlot());
  char addr[wisp::kOscTargetAddressCap];
  wisp::oscTargetAddress(cfg.oscTarget(), addr, sizeof(addr));
  TEST_ASSERT_EQUAL_STRING("/straylight/palette/primary/colours", addr);
}

void test_osc_settings_persist() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setOsc(wisp::OscMode::Maestro, 7);
  wisp::WispConfig reload;
  reload.begin();
  TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Maestro, (int)reload.oscMode());
  TEST_ASSERT_EQUAL_UINT8(7, reload.oscSlot());
  const wisp::OscTarget t = reload.oscTarget();
  TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Maestro, (int)t.mode);
  TEST_ASSERT_EQUAL_UINT8(7, t.slot);
}

void test_corrupt_osc_settings_load_as_straylight_slot_1() {
  {
    Preferences p;
    p.putUChar("oscMode", 9);
    p.putUChar("oscSlot", 0);
  }
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Straylight, (int)cfg.oscMode());
  TEST_ASSERT_EQUAL_UINT8(1, cfg.oscSlot());
  {
    Preferences p;
    p.putUChar("oscMode", 1);
    p.putUChar("oscSlot", 11);
  }
  wisp::WispConfig high;
  high.begin();
  TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Maestro, (int)high.oscMode());
  TEST_ASSERT_EQUAL_UINT8(1, high.oscSlot());
}

void test_osc_address_built_per_role() {
  TEST_ASSERT_EQUAL_STRING("/straylight/palette/primary/colours",
                           wisp::oscPaletteAddress(wisp::OscRole::Primary));
  TEST_ASSERT_EQUAL_STRING("/straylight/palette/secondary/colours",
                           wisp::oscPaletteAddress(wisp::OscRole::Secondary));
  TEST_ASSERT_EQUAL_STRING("/straylight/palette/tertiary/colours",
                           wisp::oscPaletteAddress(wisp::OscRole::Tertiary));
  TEST_ASSERT_EQUAL_STRING("/straylight/palette/fill/colours",
                           wisp::oscPaletteAddress(wisp::OscRole::Fill));
  wisp::OscRole r = wisp::OscRole::Primary;
  TEST_ASSERT_TRUE(wisp::oscRoleFromName("secondary", r));
  TEST_ASSERT_EQUAL_INT((int)wisp::OscRole::Secondary, (int)r);
  TEST_ASSERT_FALSE(wisp::oscRoleFromName("Primary", r));
  TEST_ASSERT_FALSE(wisp::oscRoleFromName(nullptr, r));
}

// Clearing settings that were never stored removes nothing that isn't there,
// so the wisp logs no NVS NOT_FOUND error.
void test_clearing_unset_settings_removes_no_missing_key() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setManualPalette({{10, 20, 30, 40}});
  cfg.setManualPalette({});
  cfg.setPassword("");
  cfg.clearWifi();
  TEST_ASSERT_EQUAL_INT(0, s_removeMissing);
}

// The status reports the stored Manual palette in every source mode: "" when
// it is empty, its id otherwise.
void test_manual_palette_status_reports_the_stored_palette() {
  wisp::WispConfig cfg;
  cfg.begin();
  const wisp::WispSourceMode modes[] = {wisp::WispSourceMode::Off,
                                        wisp::WispSourceMode::Manual,
                                        wisp::WispSourceMode::Osc};
  for (const auto mode : modes) {
    cfg.setSourceMode(mode);
    cfg.setManualPalette({});
    TEST_ASSERT_EQUAL_STRING("", cfg.manualPaletteStatus().id);
    cfg.setManualPalette({{10, 20, 30, 0}, {40, 50, 60, 70}});
    TEST_ASSERT_EQUAL_STRING("59a8ae37", cfg.manualPaletteStatus().id);
  }
}

// The status echoes the seq of the save that stored the palette, and 0 once a
// save without one replaces it.
void test_manual_palette_status_echoes_the_save_seq() {
  wisp::WispConfig cfg;
  cfg.begin();
  TEST_ASSERT_EQUAL_UINT16(0, cfg.manualPaletteStatus().seq);
  cfg.setManualPalette({{1, 2, 3, 0}}, 42);
  TEST_ASSERT_EQUAL_UINT16(42, cfg.manualPaletteStatus().seq);
  cfg.setManualPalette({});
  TEST_ASSERT_EQUAL_UINT16(0, cfg.manualPaletteStatus().seq);
  TEST_ASSERT_EQUAL_STRING("", cfg.manualPaletteStatus().id);
}

// 20 colours, each distinct in every channel, so a kept tail or a shifted
// window shows.
static std::vector<wisp::ManualPaletteColor> twentyColours() {
  std::vector<wisp::ManualPaletteColor> out;
  for (uint8_t i = 0; i < 20; ++i) {
    out.push_back({static_cast<uint8_t>(i + 1), static_cast<uint8_t>(40 + i),
                   static_cast<uint8_t>(200 - i), static_cast<uint8_t>(i * 3)});
  }
  return out;
}

static void assertFirstSixteen(const wisp::WispConfig& cfg) {
  const auto all = twentyColours();
  const auto& pal = cfg.manualPalette();
  TEST_ASSERT_EQUAL(16, (int)pal.size());
  for (size_t i = 0; i < pal.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT8(all[i].r, pal[i].r);
    TEST_ASSERT_EQUAL_UINT8(all[i].g, pal[i].g);
    TEST_ASSERT_EQUAL_UINT8(all[i].b, pal[i].b);
    TEST_ASSERT_EQUAL_UINT8(all[i].w, pal[i].w);
  }
  const std::vector<wisp::ManualPaletteColor> first(all.begin(),
                                                    all.begin() + 16);
  TEST_ASSERT_EQUAL_STRING(wisp::statusManualId(first).c_str(),
                           cfg.manualPaletteStatus().id);
}

// A Manual palette holds at most 16 colours, the most a Straylight palette
// carries; a longer one keeps its first 16, in RAM and in NVS.
void test_manual_palette_keeps_its_first_16_colours() {
  wisp::WispConfig cfg;
  cfg.begin();
  cfg.setManualPalette(twentyColours());
  assertFirstSixteen(cfg);

  wisp::WispConfig reloaded;
  reloaded.begin();
  assertFirstSixteen(reloaded);
}

// A palette stored before the cap, RGBW or legacy RGB, loads as its first 16.
void test_longer_stored_palette_loads_its_first_16_colours() {
  const auto all = twentyColours();
  Preferences prefs;
  std::vector<uint8_t> rgbw;
  for (const auto& c : all) rgbw.insert(rgbw.end(), {c.r, c.g, c.b, c.w});
  prefs.putBytes("manualPal4", rgbw.data(), rgbw.size());
  wisp::WispConfig cfg;
  cfg.begin();
  assertFirstSixteen(cfg);

  kv_reset();
  std::vector<uint8_t> rgb;
  for (const auto& c : all) rgb.insert(rgb.end(), {c.r, c.g, c.b});
  prefs.putBytes("manualPal", rgb.data(), rgb.size());
  wisp::WispConfig legacy;
  legacy.begin();
  const auto& pal = legacy.manualPalette();
  TEST_ASSERT_EQUAL(16, (int)pal.size());
  TEST_ASSERT_EQUAL_UINT8(all[15].r, pal[15].r);
  TEST_ASSERT_EQUAL_UINT8(all[15].b, pal[15].b);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_name_default_is_empty);
  RUN_TEST(test_setname_persists_and_getter_returns_it);
  RUN_TEST(test_setname_clamps_to_20_chars);
  RUN_TEST(test_setname_20_chars_exact_not_clamped);
  RUN_TEST(test_setname_empty_allowed);
  RUN_TEST(test_password_default_is_empty);
  RUN_TEST(test_setpassword_persists_and_getter_returns_it);
  RUN_TEST(test_setpassword_empty_clears);
  RUN_TEST(test_led_format_default_is_grb);
  RUN_TEST(test_set_led_format_roundtrips);
  RUN_TEST(test_pixel_count_default_is_30);
  RUN_TEST(test_set_pixel_count_roundtrips);
  RUN_TEST(test_set_pixel_count_clamps_to_max);
  RUN_TEST(test_set_pixel_count_zero_clamped_to_one);
  RUN_TEST(test_brightness_default_is_full);
  RUN_TEST(test_set_brightness_roundtrips_and_clamps);
  RUN_TEST(test_legacy_aurora_source_loads_as_off);
  RUN_TEST(test_osc_source_roundtrips);
  RUN_TEST(test_osc_role_defaults_primary_and_persists);
  RUN_TEST(test_corrupt_osc_role_loads_as_primary);
  RUN_TEST(test_osc_address_built_per_role);
  RUN_TEST(test_osc_defaults_to_straylight_slot_1);
  RUN_TEST(test_osc_settings_persist);
  RUN_TEST(test_corrupt_osc_settings_load_as_straylight_slot_1);
  RUN_TEST(test_clearing_unset_settings_removes_no_missing_key);
  RUN_TEST(test_manual_palette_status_reports_the_stored_palette);
  RUN_TEST(test_manual_palette_status_echoes_the_save_seq);
  RUN_TEST(test_manual_palette_keeps_its_first_16_colours);
  RUN_TEST(test_longer_stored_palette_loads_its_first_16_colours);
  return UNITY_END();
}
