// Host tool. Writes generated/<variant>/expr_catalog.h for every variant.
// Built + run by scripts/gen_expr_catalog.py (npm run catalog:gen).
#include <filesystem>
#include <fstream>
#include <iostream>

#include "catalog_gen.hpp"

int main(int argc, char** argv) {
  const std::filesystem::path base = (argc > 1) ? argv[1] : "generated";
  for (const auto& v : lamp::catalog_gen::variantSpecs()) {
    const std::filesystem::path dir = base / v.name;
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "expr_catalog.h", std::ios::binary);
    if (!f) {
      std::cerr << "cannot write " << (dir / "expr_catalog.h") << "\n";
      return 1;
    }
    f << lamp::catalog_gen::emitHeader(v);
  }
  return 0;
}
