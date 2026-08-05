// Drift gate: the committed generated/<variant>/expr_catalog.h must be exactly
// what `npm run catalog:gen` renders today. A stale header (descriptor changed,
// subset changed, hand-edit) fails here. Byte-equivalence of the base catalog
// to the on-device serializeCatalog output is separately pinned by
// test_builtin_descriptors (same kExprDescriptorData constants).
#include <unity.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "../expr_catalog_gen/catalog_gen.hpp"

using namespace lamp::catalog_gen;

namespace {

std::filesystem::path generatedDir() {
  // <lamp-os>/test/test_expr_catalog_flash/<this file>
  return std::filesystem::absolute(__FILE__)
      .parent_path()
      .parent_path()
      .parent_path() /
      "generated";
}

std::string readFile(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_committed_headers_match_generator() {
  for (const auto& v : variantSpecs()) {
    const std::filesystem::path path = generatedDir() / v.name / "expr_catalog.h";
    const std::string committed = readFile(path);
    TEST_ASSERT_FALSE_MESSAGE(committed.empty(),
                              (std::string("missing/empty ") + path.string()).c_str());
    const std::string expected = emitHeader(v);
    if (committed != expected) {
      TEST_FAIL_MESSAGE(
          (std::string("stale generated header for '") + v.name +
           "'; run `npm run catalog:gen`")
              .c_str());
    }
  }
}

void test_only_two_distinct_catalogs() {
  std::set<std::string> catalogs;
  for (const auto& v : variantSpecs()) catalogs.insert(buildCatalog(v));
  TEST_ASSERT_EQUAL_size_t(2, catalogs.size());
}

std::string committedHash(const std::string& variant) {
  const std::string header = readFile(generatedDir() / variant / "expr_catalog.h");
  const std::string key = "kExprCatalogHash = ";
  const size_t at = header.find(key);
  TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos,
                           (std::string("no kExprCatalogHash in ") + variant).c_str());
  const size_t start = at + key.size();
  return header.substr(start, header.find(';', start) - start);
}

void test_committed_hash_matches_fnv() {
  for (const auto& v : variantSpecs()) {
    char expected[24];
    std::snprintf(expected, sizeof(expected), "0x%08xu", fnv1a32(buildCatalog(v)));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, committedHash(v.name).c_str(), v.name);
  }
}

void test_distinct_catalogs_have_distinct_hashes() {
  std::set<std::string> hashes;
  for (const auto& v : variantSpecs()) hashes.insert(committedHash(v.name));
  TEST_ASSERT_EQUAL_size_t(2, hashes.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_committed_headers_match_generator);
  RUN_TEST(test_only_two_distinct_catalogs);
  RUN_TEST(test_committed_hash_matches_fnv);
  RUN_TEST(test_distinct_catalogs_have_distinct_hashes);
  return UNITY_END();
}
