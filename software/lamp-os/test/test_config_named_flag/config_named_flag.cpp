// Native-host test for the explicit `named` adoption flag that replaced the
// name-string sentinel in applyDefaults + the adopt latch. Covers the pre-flag
// NVS migration (infer `named` from the stored name once), the flag-gated name
// substitution (a lamp named the literal default word survives), and the
// toJson/fromJson round trip that persists the flag to NVS.
//
// applyDefaults itself can't link natively (config.cpp pulls in the store +
// NimBLE), so this exercises resolveNamed — the pure predicate applyDefaults
// and the adopt latch route through — plus the real config_codec.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "config/config_types.hpp"
#include "config/config_codec.hpp"

// Native tests don't build src/, so compile the real implementations into
// this TU (same pattern as test_config_codec).
#include "../../src/util/color.cpp"
#include "../../src/config/config_codec.cpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

// Mirror applyDefaults' name step: resolve `named`, then substitute the
// variant default only when the lamp was never named.
static std::string resolvedName(bool keyPresent, bool loaded,
                                const std::string& name,
                                const std::string& variantDefault) {
  bool named = resolveNamed(keyPresent, loaded, name, variantDefault);
  if (!variantDefault.empty() && !named) return variantDefault;
  return name;
}

// Fresh lamp: empty NVS loads name="stray"; every variant reads unnamed and
// gets its own default name.
void test_fresh_lamp_infers_unnamed_and_gets_default() {
  TEST_ASSERT_FALSE(resolveNamed(false, false, "stray", "stray"));
  TEST_ASSERT_FALSE(resolveNamed(false, false, "stray", "snafu"));
  TEST_ASSERT_EQUAL_STRING("snafu",
                           resolvedName(false, false, "stray", "snafu").c_str());
  TEST_ASSERT_EQUAL_STRING("stray",
                           resolvedName(false, false, "stray", "stray").c_str());
}

// Old NVS (flag absent) whose stored name equals the variant default is a
// previously-substituted unconfigured lamp: still unnamed, keeps the default.
void test_migration_stored_default_name_is_unnamed() {
  TEST_ASSERT_FALSE(resolveNamed(false, false, "snafu", "snafu"));
  TEST_ASSERT_EQUAL_STRING("snafu",
                           resolvedName(false, false, "snafu", "snafu").c_str());
}

// Old NVS (flag absent) whose stored name differs from both sentinels was
// user-named: inferred named, name preserved across applyDefaults. This is the
// fleet-wide no-data-loss guarantee for already-named field lamps.
void test_migration_custom_name_preserved() {
  TEST_ASSERT_TRUE(resolveNamed(false, false, "kitchen", "snafu"));
  TEST_ASSERT_EQUAL_STRING(
      "kitchen", resolvedName(false, false, "kitchen", "snafu").c_str());
  // A standard lamp (default "stray") named "snafu" is a legit custom name.
  TEST_ASSERT_TRUE(resolveNamed(false, false, "snafu", "stray"));
  TEST_ASSERT_EQUAL_STRING(
      "snafu", resolvedName(false, false, "snafu", "stray").c_str());
}

// Flag present: trust it, ignore the name. A lamp explicitly named the literal
// variant default word survives reboot (the bug this fix targets).
void test_flag_present_is_authoritative() {
  TEST_ASSERT_TRUE(resolveNamed(true, true, "snafu", "snafu"));
  TEST_ASSERT_EQUAL_STRING("snafu",
                           resolvedName(true, true, "snafu", "snafu").c_str());
  TEST_ASSERT_TRUE(resolveNamed(true, true, "stray", "stray"));
  TEST_ASSERT_EQUAL_STRING("stray",
                           resolvedName(true, true, "stray", "stray").c_str());
  // Present-and-false stays false even if the name looks custom.
  TEST_ASSERT_FALSE(resolveNamed(true, false, "kitchen", "snafu"));
}

// The app-rename apply path sets named=true; it round-trips through the codec
// so NVS persists it and the next boot reads the flag as present.
void test_named_round_trips_through_codec() {
  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expr;
  HomeModeSettings home;
  lamp.name = "snafu";
  lamp.named = true;

  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  config_codec::toJson(root, lamp, base, shade, expr, home);
  TEST_ASSERT_TRUE(doc["lamp"]["named"].is<bool>());

  LampSettings lamp2;
  BaseSettings base2;
  ShadeSettings shade2;
  ExpressionSettings expr2;
  HomeModeSettings home2;
  config_codec::fromJson(doc.as<JsonObject>(), lamp2, base2, shade2, expr2,
                         home2);
  TEST_ASSERT_TRUE(lamp2.named);
}

// Presence detection: old-style blob without the key reads absent; a blob that
// carries it reads present (Config::Config keys the migration on this).
void test_named_key_presence_detection() {
  JsonDocument oldDoc;
  deserializeJson(oldDoc, R"({"lamp":{"name":"kitchen"}})");
  TEST_ASSERT_FALSE(oldDoc["lamp"]["named"].is<bool>());

  JsonDocument newDoc;
  deserializeJson(newDoc, R"({"lamp":{"name":"kitchen","named":true}})");
  TEST_ASSERT_TRUE(newDoc["lamp"]["named"].is<bool>());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_lamp_infers_unnamed_and_gets_default);
  RUN_TEST(test_migration_stored_default_name_is_unnamed);
  RUN_TEST(test_migration_custom_name_preserved);
  RUN_TEST(test_flag_present_is_authoritative);
  RUN_TEST(test_named_round_trips_through_codec);
  RUN_TEST(test_named_key_presence_detection);
  return UNITY_END();
}
