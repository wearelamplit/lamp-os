#include "components/network/ble/section_page.hpp"

#include <new>

namespace ble_control {

void SectionPage::openStatic(std::string_view bytes, uint16_t chunk) {
  serve(bytes, chunk);
}

bool SectionPage::open(Serializer serialize, uint16_t chunk) {
  bool served = true;
  try {
    serialize(snapshot_);
  } catch (const std::bad_alloc&) {
    snapshot_.clear();
    served = false;
  }
  serve(snapshot_, chunk);
  return served;
}

bool SectionPage::openConfig(ConfigCopy copy, uint16_t chunk) {
  bool copied = false;
  try {
    copied = copy(snapshot_);
  } catch (const std::bad_alloc&) {
  }
  serve(copied ? std::string_view(snapshot_) : kBusy, chunk);
  return copied;
}

void SectionPage::close() {
  snapshot_.clear();
  serve({}, 0);
}

void SectionPage::release() {
  std::string().swap(snapshot_);
  serve({}, 0);
}

std::string_view SectionPage::read(bool authed) {
  if (!authed || chunk_ == 0 || cursor_ >= source_.size()) return {};
  const std::string_view chunk = source_.substr(cursor_, chunk_);
  cursor_ += chunk.size();
  return chunk;
}

void SectionPage::serve(std::string_view source, uint16_t chunk) {
  source_ = source;
  cursor_ = 0;
  chunk_  = chunk;
}

}  // namespace ble_control
