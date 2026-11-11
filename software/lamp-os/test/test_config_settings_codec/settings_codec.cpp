// Native tests for the binary settings blob (config_codec::encodeSettings /
// decodeSettings): a decode loads, field for field, what fromJson loads from
// the same model's JSON, so the web document and every BLE section built from
// the model are unchanged; the worst case fits kMaxSettingsBlobLen; and every
// malformed blob is refused without touching the model.

#include <unity.h>

#include <ArduinoJson.h>

#include <cstdio>
#include <string>
#include <vector>

#include "config/config_codec.hpp"

#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../../src/config/config_codec.cpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

namespace {

struct Model {
  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expressions;
  HomeModeSettings homeMode;
};

std::string webDocument(const Model& m) {
  JsonDocument doc;
  config_codec::toJson(doc.to<JsonObject>(), m.lamp, m.base, m.shade, m.expressions, m.homeMode);
  std::string out;
  serializeJson(doc, out);
  return out;
}

// What a load of the model's stored JSON gives, onto class defaults.
Model viaJson(const Model& m) {
  JsonDocument doc;
  deserializeJson(doc, webDocument(m));
  Model out;
  config_codec::fromJson(doc.as<JsonObject>(), out.lamp, out.base, out.shade, out.expressions,
                         out.homeMode);
  out.expressions = {};
  return out;
}

std::vector<uint8_t> encode(const Model& m) {
  std::vector<uint8_t> blob(config_codec::kMaxSettingsBlobLen);
  blob.resize(config_codec::encodeSettings(m.lamp, m.base, m.shade, m.homeMode, blob.data()));
  return blob;
}

bool decode(const std::vector<uint8_t>& blob, Model& out) {
  return config_codec::decodeSettings(blob.data(), blob.size(), out.lamp, out.base, out.shade,
                                      out.homeMode);
}

Model viaBlob(const Model& m) {
  Model out;
  TEST_ASSERT_TRUE(decode(encode(m), out));
  return out;
}

// Every field of the stored sections, the first that differs named.
std::string firstDifference(const Model& a, const Model& b) {
  const auto& x = a.lamp;
  const auto& y = b.lamp;
  if (x.name != y.name) return "lamp.name " + x.name + " / " + y.name;
  if (x.brightness != y.brightness) return "lamp.brightness";
  if (x.password != y.password) return "lamp.password";
  if (x.setup != y.setup) return "lamp.setup";
  if (x.named != y.named) return "lamp.named";
  if (x.advancedEnabled != y.advancedEnabled) return "lamp.advancedEnabled";
  if (x.webappEnabled != y.webappEnabled) return "lamp.webappEnabled";
  if (x.apBootMinutes != y.apBootMinutes) return "lamp.apBootMinutes";
  if (x.brightnessCeiling != y.brightnessCeiling) return "lamp.brightnessCeiling";
  if (x.socialMode != y.socialMode) return "lamp.socialMode";
  if (x.lampType != y.lampType) return "lamp.lampType";
  auto segments = [](const char* role, const std::vector<SegmentSettings>& p,
                     const std::vector<SegmentSettings>& q) -> std::string {
    if (p.size() != q.size()) return std::string(role) + ".segments size";
    for (size_t i = 0; i < p.size(); ++i) {
      if (p[i].name != q[i].name) return std::string(role) + ".name";
      if (p[i].px != q[i].px) return std::string(role) + ".px";
      if (p[i].pin != q[i].pin) return std::string(role) + ".pin";
      if (p[i].builtinPin != q[i].builtinPin) return std::string(role) + ".builtinPin";
      if (p[i].colors != q[i].colors) return std::string(role) + ".colors";
    }
    return "";
  };
  std::string d = segments("base", a.base.segments, b.base.segments);
  if (!d.empty()) return d;
  d = segments("shade", a.shade.segments, b.shade.segments);
  if (!d.empty()) return d;
  if (a.base.knockoutPixels != b.base.knockoutPixels) return "base.knockout";
  if (a.base.byteOrder != b.base.byteOrder) return "base.byteOrder";
  if (a.shade.byteOrder != b.shade.byteOrder) return "shade.byteOrder";
  if (a.base.colorsEditable != b.base.colorsEditable) return "base.colorsEditable";
  if (a.shade.colorsEditable != b.shade.colorsEditable) return "shade.colorsEditable";
  if (a.base.rotation != b.base.rotation) return "base.rotation";
  if (a.shade.rotation != b.shade.rotation) return "shade.rotation";
  if (a.base.rotationSupported != b.base.rotationSupported) return "base.rotationSupported";
  if (a.shade.rotationSupported != b.shade.rotationSupported) return "shade.rotationSupported";
  const auto& h = a.homeMode;
  const auto& k = b.homeMode;
  if (h.ssid != k.ssid) return "home.ssid";
  if (h.brightness != k.brightness) return "home.brightness";
  if (h.enabled != k.enabled) return "home.enabled";
  if (h.networkBound != k.networkBound) return "home.networkBound";
  if (h.socialDisabled != k.socialDisabled) return "home.socialDisabled";
  if (h.disabledExpressionTypes != k.disabledExpressionTypes) return "home.types";
  if (webDocument(a) != webDocument(b)) return "web document";
  return "";
}

// The blob loads what the JSON loads, the web document is byte-equal, and the
// blob matches its own model and no other.
void assertSameAsJson(const Model& m, const char* what) {
  const Model json = viaJson(m);
  const Model blob = viaBlob(m);
  const std::string d = firstDifference(json, blob);
  char msg[160];
  std::snprintf(msg, sizeof(msg), "%s: %s", what, d.c_str());
  TEST_ASSERT_TRUE_MESSAGE(d.empty(), msg);
  const auto bytes = encode(m);
  TEST_ASSERT_TRUE_MESSAGE(config_codec::settingsMatch(bytes.data(), bytes.size(), m.lamp, m.base,
                                                       m.shade, m.homeMode),
                           what);
  // Saved again, a loaded model loads the same.
  TEST_ASSERT_TRUE_MESSAGE(firstDifference(blob, viaBlob(blob)).empty(), what);
}

Model worstCaseModel(char fill) {
  Model m;
  m.lamp.name = std::string(kMaxLampNameLen, fill);
  m.lamp.password = std::string(kMaxPasswordLen, fill);
  m.lamp.brightness = m.lamp.apBootMinutes = m.lamp.brightnessCeiling = 255;
  m.lamp.socialMode = SocialMode::Extrovert;
  for (auto* segs : {&m.base.segments, &m.shade.segments}) {
    segs->clear();
    for (size_t s = 0; s < kMaxSegments; ++s) {
      segs->push_back({std::string(kMaxSegmentNameLen, fill), 63, {}, 33});
      segs->back().colors.assign(kMaxSurfaceColors, Color(255, 254, 253, 252));
    }
  }
  m.base.byteOrder = "GRBW";
  m.shade.byteOrder = "BGR";
  m.base.knockoutPixels.assign(config_codec::kMaxKnockoutLevels, 7);
  m.homeMode.ssid = std::string(kMaxSsidLen, fill);
  m.homeMode.disabledExpressionTypes.assign(kMaxDisabledExpressionTypes,
                                            std::string(kMaxExpressionTypeLen, 't'));
  return m;
}

uint32_t nextRandom(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state >> 8;
}

// Bytes 1..255 (a stored name or password never holds a NUL), weighted to
// whitespace, upper case, JSON escapes and UTF-8 lead and continuation bytes.
std::string randomText(uint32_t& seed, size_t maxLen) {
  static const char kPool[] = " \t\nAZaz09\"\\/\x01\x1f\x7f\xc3\xa9\xe2\x82\xac\xf0\x9f\x92\xa1";
  const size_t len = nextRandom(seed) % (maxLen + 1);
  std::string s;
  for (size_t i = 0; i < len; ++i) {
    s += nextRandom(seed) % 4 == 0 ? static_cast<char>(1 + nextRandom(seed) % 255)
                                   : kPool[nextRandom(seed) % (sizeof(kPool) - 1)];
  }
  return s;
}

std::vector<SegmentSettings> randomSegments(uint32_t& seed) {
  std::vector<SegmentSettings> segs(nextRandom(seed) % (kMaxSegments + 2));
  for (auto& seg : segs) {
    seg.name = randomText(seed, kMaxSegmentNameLen + 6);
    seg.px = static_cast<uint8_t>(nextRandom(seed));
    seg.pin = static_cast<uint8_t>(nextRandom(seed) % 3 == 0 ? nextRandom(seed) % 40 : 0);
    seg.colors.resize(nextRandom(seed) % (kMaxSurfaceColors + 2));
    for (auto& c : seg.colors) {
      c = Color(nextRandom(seed), nextRandom(seed), nextRandom(seed), nextRandom(seed));
    }
  }
  return segs;
}

ColorRotation randomRotation(uint32_t& seed) {
  return {nextRandom(seed) % 2 == 0, static_cast<uint8_t>(nextRandom(seed)),
          nextRandom(seed) % 2 ? RotationDir::Down : RotationDir::Up};
}

Model randomModel(uint32_t seed) {
  static const char* kOrders[] = {"", "GRBW", "GRB", "BGR", "grbw", "RGBW"};
  static const char* kTypes[] = {"glitchy", "pulse", "Pulse", "", "a_b9", "has space",
                                 "abcdefghijklmnop", "abcdefghijklmno"};
  Model m;
  m.lamp.name = randomText(seed, kMaxLampNameLen + 12);
  m.lamp.password = randomText(seed, kMaxPasswordLen + 6);
  m.lamp.brightness = static_cast<uint8_t>(nextRandom(seed));
  m.lamp.apBootMinutes = static_cast<uint8_t>(nextRandom(seed));
  m.lamp.brightnessCeiling = static_cast<uint8_t>(nextRandom(seed));
  m.lamp.socialMode = static_cast<SocialMode>(nextRandom(seed) % 5);
  m.lamp.setup = nextRandom(seed) % 2;
  m.lamp.named = nextRandom(seed) % 2;
  m.lamp.advancedEnabled = nextRandom(seed) % 2;
  m.lamp.webappEnabled = nextRandom(seed) % 2;
  m.base.segments = randomSegments(seed);
  m.shade.segments = randomSegments(seed);
  m.base.byteOrder = kOrders[nextRandom(seed) % 6];
  m.shade.byteOrder = kOrders[nextRandom(seed) % 6];
  m.base.rotation = randomRotation(seed);
  m.shade.rotation = randomRotation(seed);
  m.base.knockoutPixels.resize(nextRandom(seed) % 300);
  for (auto& k : m.base.knockoutPixels) k = static_cast<uint8_t>(nextRandom(seed));
  m.homeMode.ssid = randomText(seed, kMaxSsidLen + 6);
  m.homeMode.brightness = static_cast<uint8_t>(nextRandom(seed));
  m.homeMode.enabled = nextRandom(seed) % 2;
  m.homeMode.networkBound = nextRandom(seed) % 2;
  m.homeMode.socialDisabled = nextRandom(seed) % 2;
  m.homeMode.disabledExpressionTypes.resize(nextRandom(seed) % 11);
  for (auto& t : m.homeMode.disabledExpressionTypes) t = kTypes[nextRandom(seed) % 8];
  return m;
}

}  // namespace

void test_defaults_load_as_the_json_loads_them() {
  assertSameAsJson(Model{}, "defaults");
}

void test_worst_cases_load_as_the_json_loads_them() {
  for (char fill : {'z', 'Z', '"', '\\', ' '}) {
    char what[16];
    std::snprintf(what, sizeof(what), "worst '%c'", fill);
    assertSameAsJson(worstCaseModel(fill), what);
  }
}

// Rotation is stored only on a one-segment role off its default, and loads
// clamped.
void test_rotation_variants_load_as_the_json_loads_them() {
  for (size_t baseSegs : {0, 1, 2}) {
    for (uint8_t period : {0, 4, 5, 40, 120, 121, 255}) {
      for (bool on : {false, true}) {
        for (RotationDir dir : {RotationDir::Up, RotationDir::Down}) {
          Model m;
          m.base.segments.resize(baseSegs, {"b", 3, {Color(1, 2, 3, 4)}});
          m.base.rotation = {on, period, dir};
          m.shade.rotation = {on, period, dir};
          assertSameAsJson(m, "rotation");
        }
      }
    }
  }
}

// A pin is stored only when the strip driver can drive it.
void test_pin_variants_load_as_the_json_loads_them() {
  for (int pin = 0; pin < 40; ++pin) {
    Model m;
    m.base.segments = {{"a", 4, {Color(1, 1, 1, 1)}, static_cast<uint8_t>(pin)},
                       {"b", 4, {Color(2, 2, 2, 2)}, 0}};
    m.shade.segments[0].pin = static_cast<uint8_t>(pin);
    assertSameAsJson(m, "pin");
    TEST_ASSERT_EQUAL_UINT8(isStripDataPin(pin) ? pin : 0, viaBlob(m).base.segments[0].pin);
  }
}

// Names past the cap with whitespace and multi-byte characters straddling it,
// and every other field at random.
void test_seeded_models_load_as_the_json_loads_them() {
  for (uint32_t seed = 1; seed <= 500; ++seed) {
    char what[32];
    std::snprintf(what, sizeof(what), "seed %u", static_cast<unsigned>(seed));
    assertSameAsJson(randomModel(seed), what);
  }
}

// A blob loads onto the model it is decoded into as fromJson does: a role
// with no segments keeps the ones passed in, and a lamp with no password
// keeps the one it had.
void test_absent_fields_keep_the_passed_values() {
  Model stored;
  stored.base.segments.clear();
  stored.lamp.password.clear();
  const auto blob = encode(stored);
  Model into;
  into.base.segments = {{"kept", 9, {Color(9, 9, 9, 9)}}};
  into.lamp.password = "kept";
  Model json = into;
  JsonDocument doc;
  deserializeJson(doc, webDocument(stored));
  config_codec::fromJson(doc.as<JsonObject>(), json.lamp, json.base, json.shade, json.expressions,
                         json.homeMode);
  json.expressions = {};
  TEST_ASSERT_TRUE(decode(blob, into));
  TEST_ASSERT_EQUAL_STRING("kept", into.base.segments[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("kept", into.lamp.password.c_str());
  TEST_ASSERT_EQUAL_STRING("", firstDifference(json, into).c_str());
}

void test_the_worst_case_fits_the_blob() {
  size_t longest = 0;
  for (char fill : {'z', '"', '\\'}) {
    longest = std::max(longest, encode(worstCaseModel(fill)).size());
  }
  std::printf("[budget] settings blob %zu of %zu\n", longest, config_codec::kMaxSettingsBlobLen);
  TEST_ASSERT_EQUAL(config_codec::kMaxSettingsLen, longest);
  TEST_ASSERT_TRUE(longest <= config_codec::kMaxSettingsBlobLen);
  // Over every cap, the blob still holds only the caps.
  Model over = worstCaseModel('z');
  over.lamp.name += "zzzz";
  over.lamp.password += "zzzz";
  for (auto* segs : {&over.base.segments, &over.shade.segments}) {
    segs->push_back(segs->front());
    for (auto& seg : *segs) {
      seg.name += "zz";
      seg.colors.push_back(Color(1, 1, 1, 1));
    }
  }
  over.base.knockoutPixels.resize(400, 1);
  over.homeMode.ssid += "zz";
  over.homeMode.disabledExpressionTypes.push_back("extra");
  TEST_ASSERT_EQUAL(config_codec::kMaxSettingsLen, encode(over).size());
}

namespace {

bool refused(const std::vector<uint8_t>& blob) {
  Model m;
  m.lamp.name = "marker";
  m.base.segments = {{"marker", 5, {Color(5, 5, 5, 5)}}};
  const std::string before = webDocument(m);
  if (decode(blob, m)) return false;
  TEST_ASSERT_EQUAL_STRING(before.c_str(), webDocument(m).c_str());
  return true;
}

// The blob of a small model, laid out byte by byte as encodeSettings writes
// it, with one field overridable.
struct Layout {
  uint8_t format = 1;
  uint8_t lampFlags = 0x0F;
  uint8_t socialMode = 1;
  std::string name = "n";
  std::string password = "";
  uint8_t roleFlags = 0;
  uint8_t byteOrder = 0;
  uint8_t period = 0;
  uint8_t segments = 1;
  std::string segmentName = "s";
  uint8_t pin = 0;
  uint8_t colors = 1;
  uint8_t knockout = 1;
  uint8_t homeFlags = 0;
  std::string ssid = "";
  uint8_t types = 1;
  std::string type = "pulse";

  std::vector<uint8_t> bytes() const {
    std::vector<uint8_t> b = {format, lampFlags, 100, 0, 170, socialMode};
    auto str = [&](const std::string& s) {
      b.push_back(static_cast<uint8_t>(s.size()));
      b.insert(b.end(), s.begin(), s.end());
    };
    str(name);
    str(password);
    for (int role = 0; role < 2; ++role) {
      b.insert(b.end(), {roleFlags, byteOrder, period, segments});
      for (uint8_t s = 0; s < segments; ++s) {
        str(segmentName);
        b.insert(b.end(), {3, pin, colors});
        for (uint8_t c = 0; c < colors; ++c) b.insert(b.end(), {1, 2, 3, 4});
      }
    }
    b.push_back(knockout);
    for (uint8_t k = 0; k < knockout; ++k) b.push_back(50);
    b.insert(b.end(), {homeFlags, 60});
    str(ssid);
    b.push_back(types);
    for (uint8_t t = 0; t < types; ++t) str(type);
    return b;
  }
};

}  // namespace

// The test's layout is the codec's, so the over-cap cases below change only
// the field they name.
void test_the_layout_matches_the_encoder() {
  Model m;
  m.lamp.name = "n";
  m.lamp.setup = m.lamp.named = m.lamp.advancedEnabled = m.lamp.webappEnabled = true;
  m.base.segments = {{"s", 3, {Color(1, 2, 3, 4)}}};
  m.shade.segments = {{"s", 3, {Color(1, 2, 3, 4)}}};
  m.base.knockoutPixels = {50};
  m.homeMode.socialDisabled = false;
  m.homeMode.disabledExpressionTypes = {"pulse"};
  TEST_ASSERT_TRUE(encode(m) == Layout{}.bytes());
  Model back;
  TEST_ASSERT_TRUE(decode(Layout{}.bytes(), back));
}

void test_every_truncation_is_refused() {
  for (const auto& blob : {Layout{}.bytes(), encode(worstCaseModel('"'))}) {
    for (size_t cut = 0; cut < blob.size(); ++cut) {
      TEST_ASSERT_TRUE(refused(std::vector<uint8_t>(blob.begin(), blob.begin() + cut)));
    }
  }
}

void test_malformed_blobs_are_refused() {
  auto with = [](auto edit) {
    Layout l;
    edit(l);
    return l.bytes();
  };
  std::vector<uint8_t> trailing = Layout{}.bytes();
  trailing.push_back(0);
  TEST_ASSERT_TRUE(refused(trailing));
  for (uint8_t format : {0, 2, 255}) {
    TEST_ASSERT_TRUE(refused(with([&](Layout& l) { l.format = format; })));
  }
  TEST_ASSERT_TRUE(refused(with([](Layout& l) { l.lampFlags = 0x10; })));
  TEST_ASSERT_TRUE(refused(with([](Layout& l) { l.name.assign(kMaxLampNameLen + 1, 'n'); })));
  TEST_ASSERT_TRUE(refused(with([](Layout& l) { l.password.assign(kMaxPasswordLen + 1, 'p'); })));
  TEST_ASSERT_TRUE(refused(with([](Layout& l) { l.roleFlags = 0x08; })));
  TEST_ASSERT_TRUE(refused(with([](Layout& l) { l.byteOrder = 4; })));
  TEST_ASSERT_TRUE(refused(with([](Layout& l) { l.segments = kMaxSegments + 1; })));
  TEST_ASSERT_TRUE(
      refused(with([](Layout& l) { l.segmentName.assign(kMaxSegmentNameLen + 1, 's'); })));
  TEST_ASSERT_TRUE(refused(with([](Layout& l) { l.colors = kMaxSurfaceColors + 1; })));
  TEST_ASSERT_TRUE(refused(with([](Layout& l) { l.homeFlags = 0x08; })));
  TEST_ASSERT_TRUE(refused(with([](Layout& l) { l.ssid.assign(kMaxSsidLen + 1, 's'); })));
  TEST_ASSERT_TRUE(refused(with([](Layout& l) { l.types = kMaxDisabledExpressionTypes + 1; })));
  TEST_ASSERT_TRUE(
      refused(with([](Layout& l) { l.type.assign(kMaxExpressionTypeLen + 1, 't'); })));
  // At each cap, the blob loads.
  for (auto edit : std::vector<void (*)(Layout&)>{
           [](Layout& l) { l.name.assign(kMaxLampNameLen, 'n'); },
           [](Layout& l) { l.password.assign(kMaxPasswordLen, 'p'); },
           [](Layout& l) { l.byteOrder = 3; },
           [](Layout& l) { l.segments = kMaxSegments; },
           [](Layout& l) { l.segmentName.assign(kMaxSegmentNameLen, 's'); },
           [](Layout& l) { l.colors = kMaxSurfaceColors; },
           [](Layout& l) { l.ssid.assign(kMaxSsidLen, 's'); },
           [](Layout& l) { l.types = kMaxDisabledExpressionTypes; },
           [](Layout& l) { l.type.assign(kMaxExpressionTypeLen, 't'); },
       }) {
    TEST_ASSERT_FALSE(refused(with(edit)));
  }
}

// Values a blob carries are loaded under the rules fromJson applies: a pin
// the strip driver cannot drive, a social mode past Extrovert, a rotation
// period out of range, a type id that names no expression.
void test_decoded_values_are_bounded_as_json_bounds_them() {
  Model m;
  Layout l;
  l.pin = 99;
  l.socialMode = 9;
  l.roleFlags = 0x07;
  l.period = 200;
  l.type = "Pulse";
  TEST_ASSERT_TRUE(decode(l.bytes(), m));
  TEST_ASSERT_EQUAL_UINT8(0, m.base.segments[0].pin);
  TEST_ASSERT_TRUE(m.lamp.socialMode == SocialMode::Ambivert);
  const ColorRotation clamped{true, kRotationMaxPeriodS, RotationDir::Down};
  TEST_ASSERT_TRUE(m.base.rotation == clamped);
  TEST_ASSERT_TRUE(m.homeMode.disabledExpressionTypes.empty());
  l.period = 1;
  l.pin = 33;
  TEST_ASSERT_TRUE(decode(l.bytes(), m));
  TEST_ASSERT_EQUAL_UINT8(33, m.base.segments[0].pin);
  TEST_ASSERT_EQUAL_UINT8(kRotationMinPeriodS, m.shade.rotation.periodS);
  // Two segments offer no rotation, whatever the blob stores.
  l.segments = 2;
  TEST_ASSERT_TRUE(decode(l.bytes(), m));
  TEST_ASSERT_TRUE(m.base.rotation == BaseSettings::kDefaultRotation);
}

// A blob that differs from the model's encoding by one byte, or by length,
// does not match it.
void test_a_changed_blob_does_not_match() {
  const Model m = worstCaseModel('z');
  const auto blob = encode(m);
  for (size_t i = 0; i < blob.size(); ++i) {
    auto changed = blob;
    changed[i] ^= 0x01;
    TEST_ASSERT_FALSE(config_codec::settingsMatch(changed.data(), changed.size(), m.lamp, m.base,
                                                  m.shade, m.homeMode));
  }
  auto longer = blob;
  longer.push_back(0);
  TEST_ASSERT_FALSE(config_codec::settingsMatch(longer.data(), longer.size(), m.lamp, m.base,
                                                m.shade, m.homeMode));
  TEST_ASSERT_FALSE(config_codec::settingsMatch(blob.data(), blob.size() - 1, m.lamp, m.base,
                                                m.shade, m.homeMode));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_load_as_the_json_loads_them);
  RUN_TEST(test_worst_cases_load_as_the_json_loads_them);
  RUN_TEST(test_rotation_variants_load_as_the_json_loads_them);
  RUN_TEST(test_pin_variants_load_as_the_json_loads_them);
  RUN_TEST(test_seeded_models_load_as_the_json_loads_them);
  RUN_TEST(test_absent_fields_keep_the_passed_values);
  RUN_TEST(test_the_worst_case_fits_the_blob);
  RUN_TEST(test_the_layout_matches_the_encoder);
  RUN_TEST(test_every_truncation_is_refused);
  RUN_TEST(test_malformed_blobs_are_refused);
  RUN_TEST(test_decoded_values_are_bounded_as_json_bounds_them);
  RUN_TEST(test_a_changed_blob_does_not_match);
  return UNITY_END();
}
