#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ble_control {

// One connection's page-protocol read (docs/dev/networking.md): a
// CHAR_PAGE_CTRL write opens a section, each CHAR_PAGE_DATA read takes the
// next chunk, and an empty chunk ends it.
class SectionPage {
 public:
  // What a config section reads as when the heap cannot build or copy it out.
  static constexpr std::string_view kBusy{"!"};

  // Fills `out` with a section; may throw std::bad_alloc.
  using Serializer = void (*)(std::string& out);
  // Fills `out` with a config section. False, with `out` empty, when the heap
  // cannot build or copy it out; may throw std::bad_alloc.
  using ConfigCopy = bool (*)(std::string& out);

  // Each open replaces the section being read, in chunks of at most `chunk`
  // bytes (non-zero).
  // Serves `bytes` by reference; they must outlive the page.
  void openStatic(std::string_view bytes, uint16_t chunk);
  // Serializes into the page's own buffer. A bad_alloc serves the section
  // empty and returns false.
  bool open(Serializer serialize, uint16_t chunk);
  // Copies into the page's own buffer. When the copy fails or throws
  // bad_alloc, serves kBusy without allocating and returns false.
  bool openConfig(ConfigCopy copy, uint16_t chunk);

  // Ends the section, keeping the buffer for the next open.
  void close();
  // Ends the section and frees the buffer.
  void release();

  // The next chunk. Empty past the end, with no section open, and to a link
  // that has not authenticated, which never sees a section.
  std::string_view read(bool authed);

  size_t size() const { return source_.size(); }
  size_t cursor() const { return cursor_; }

 private:
  void serve(std::string_view source, uint16_t chunk);

  std::string      snapshot_;
  std::string_view source_;
  size_t           cursor_ = 0;
  uint16_t         chunk_  = 0;
};

}  // namespace ble_control
