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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_committed_headers_match_generator);
  RUN_TEST(test_only_two_distinct_catalogs);
  return UNITY_END();
}
