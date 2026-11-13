#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <string>

#include "util/heap_guard.hpp"
#include "util/json_writer.hpp"

namespace lamp {

// One config section's serialized JSON, rebuilt after invalidate(). The owner
// holds one lock across rebuild, copyOut and release; invalidate() may run
// outside it, and a race costs at most one extra rebuild.
class SectionCache {
 public:
  void invalidate() { dirty_ = true; }

  // Rebuilds from `build()`, which returns a JsonDocument, when invalidated.
  // A rebuild the heap cannot spare leaves the section invalid.
  template <class Build>
  void rebuild(const char* name, Build build) {
    if (!dirty_) return;
    if (heapCanSpare(kHeapHeadroom)) {
      store(build(), name);
    } else {
      logOom(name);
    }
  }

  // Rebuilds by running `write(JsonWriter&)` twice, once to measure and once
  // into a single exactly-sized block, when invalidated. A rebuild the heap
  // cannot spare leaves the section invalid.
  template <class Write>
  void rebuildExactly(const char* name, Write write) {
    if (!dirty_) return;
    JsonWriter measure(nullptr, 0);
    write(measure);
    const size_t len = measure.length();
    if (heapCanSpare(len + 1) && writeExactly(json_, len, write)) {
      dirty_ = false;
    } else {
      logOom(name);
    }
  }

  // Copies the section into `out`. False, with `out` empty, while the section
  // is invalid (never built, or its last rebuild failed) or the heap cannot
  // spare the copy, so a reader never gets a stale or partial section.
  bool copyOut(std::string& out, const char* name) const;

  // Frees the cached bytes and invalidates the section.
  void release();

 private:
  void store(const JsonDocument& doc, const char* name);
  static void logOom(const char* name);

  std::string json_;
  bool dirty_ = true;
};

}  // namespace lamp
