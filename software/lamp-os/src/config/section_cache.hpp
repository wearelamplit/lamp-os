#pragma once

#include <ArduinoJson.h>

#include <atomic>
#include <cstddef>
#include <string>

#include "util/heap_guard.hpp"
#include "util/json_writer.hpp"

namespace lamp {

// One config section's serialized JSON, rebuilt after invalidate(). The owner
// holds one lock across rebuild, copyOut and release. invalidate() may run
// outside it, from either core; one that lands during a rebuild or before the
// copy out costs one extra rebuild on the next read.
class SectionCache {
 public:
  void invalidate() { dirty_.store(true); }

  // Rebuilds from `build()`, which returns a JsonDocument, when invalidated.
  // A rebuild the heap cannot spare leaves the section invalid.
  template <class Build>
  void rebuild(const char* name, Build build) {
    if (!dirty_.exchange(false)) return;
    if (heapCanSpare(kHeapHeadroom)) {
      store(build(), name);
    } else {
      fail(name);
    }
  }

  // Rebuilds by running `write(JsonWriter&)` twice, once to measure and once
  // into a single exactly-sized block, when invalidated. A rebuild the heap
  // cannot spare leaves the section invalid.
  template <class Write>
  void rebuildExactly(const char* name, Write write) {
    if (!dirty_.exchange(false)) return;
    JsonWriter measure(nullptr, 0);
    write(measure);
    const size_t len = measure.length();
    if (heapCanSpare(len + 1) && writeExactly(json_, len, write)) {
      valid_ = true;
    } else {
      fail(name);
    }
  }

  // Copies the section into `out`. False, with `out` empty, while the section
  // is invalid (never built, released, or its last rebuild failed) or the heap
  // cannot spare the copy, so a reader never gets a section from before a
  // failed rebuild, or a partial one.
  bool copyOut(std::string& out, const char* name) const;

  // Frees the cached bytes and invalidates the section.
  void release();

 private:
  void store(const JsonDocument& doc, const char* name);
  void fail(const char* name);
  static void logOom(const char* name);

  std::string       json_;
  std::atomic<bool> dirty_{true};
  bool              valid_ = false;
};

}  // namespace lamp
