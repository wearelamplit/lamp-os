#pragma once

#include <cstdint>

#include "util/alloc_boundary.hpp"

namespace ble_control {

// Wraps a characteristic's callbacks so a std::bad_alloc thrown on the host
// task drops that write, or reads empty, instead of aborting the lamp. The
// NimBLE types are parameters so the native suite can drive it with fakes.
template <class Callbacks, class Characteristic, class ConnInfo>
class AllocGuarded : public Callbacks {
 public:
  explicit AllocGuarded(Callbacks* inner) : inner_(inner) {}
  void onRead(Characteristic* c, ConnInfo& connInfo) override {
    if (!lamp::catchBadAlloc("BLE onRead", [&] { inner_->onRead(c, connInfo); })) {
      c->setValue("");
    }
  }
  void onWrite(Characteristic* c, ConnInfo& connInfo) override {
    lamp::catchBadAlloc("BLE onWrite", [&] { inner_->onWrite(c, connInfo); });
  }
  void onSubscribe(Characteristic* c, ConnInfo& connInfo, uint16_t subValue) override {
    lamp::catchBadAlloc("BLE onSubscribe",
                        [&] { inner_->onSubscribe(c, connInfo, subValue); });
  }

 private:
  Callbacks* inner_;
};

}  // namespace ble_control
