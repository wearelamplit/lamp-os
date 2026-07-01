#include "espnow_link.hpp"

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace catch_ota {

// SPSC ring — 64 slots × 250 bytes (~2s of buffer at the ~30ms chunk cadence).
//
// Single producer: WiFi task (recvTrampoline) writes a frame then publishes
// s_head with release. Single consumer: loop task (espnowPoll) acquires s_head,
// reads the frame, then publishes s_tail with release. Release/acquire (not bare
// compiler barriers) is required across Core 0 / Core 1: the consumer must see
// the slot body before it observes the index advance, or it reads a torn slot.

static constexpr size_t   kSlotCount = 64;
static constexpr size_t   kSlotBytes = 250;
static_assert(kSlotBytes >= 250, "SPSC slot must cover max ESP-NOW payload");
static constexpr uint32_t kRingMask  = kSlotCount - 1;
static_assert((kSlotCount & kRingMask) == 0, "kSlotCount must be a power of two");

struct RingSlot {
    uint8_t mac[6];
    uint8_t len;
    uint8_t data[kSlotBytes];
};

static RingSlot              s_ring[kSlotCount];
static std::atomic<uint32_t> s_head{0};  // published by WiFi task
static std::atomic<uint32_t> s_tail{0};  // published by loop task

static RecvFn s_recv = nullptr;

// Recv trampoline — runs on the WiFi task.
// ONLY copies the frame into the ring; never calls s_recv, never touches flash.
static void recvTrampoline(const esp_now_recv_info_t* info,
                           const uint8_t* data, int len) {
    if (info == nullptr || data == nullptr) return;
    if (len <= 0 || len > static_cast<int>(kSlotBytes)) return;

    const uint32_t head = s_head.load(std::memory_order_relaxed);
    const uint32_t tail = s_tail.load(std::memory_order_acquire);
    if (head - tail >= kSlotCount) {
        // Ring full — overflow-drop; frame is silently discarded.
        return;
    }

    RingSlot& slot = s_ring[head & kRingMask];
    std::memcpy(slot.mac, info->src_addr, 6);
    slot.len = static_cast<uint8_t>(len);
    std::memcpy(slot.data, data, static_cast<size_t>(len));

    // Publish with release so the consumer that acquires this head sees the
    // fully-written slot body.
    s_head.store(head + 1, std::memory_order_release);
}

// Register the broadcast peer (FF:FF:FF:FF:FF:FF). channel=0 means "current
// channel" so the record stays valid regardless of which channel the radio is
// on when esp_now_send fires.
static bool addBroadcastPeer() {
    esp_now_peer_info_t peer = {};
    std::fill_n(peer.peer_addr, 6, 0xFF);
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool espnowBegin(RecvFn recv) {
    s_recv = recv;

    // WiFi is already up (STA+AP mode via WifiComponent::begin in setup).
    // Do NOT call WiFi.mode / WiFi.disconnect here — those clobber the
    // radio state owned by the wifi module. Layer ESP-NOW on top instead.

    if (esp_now_init() != ESP_OK) {
        Serial.println("[catch_ota.espnow] esp_now_init failed");
        return false;
    }

    esp_now_register_recv_cb(recvTrampoline);

    if (!addBroadcastPeer()) {
        Serial.println("[catch_ota.espnow] esp_now_add_peer(broadcast) failed");
        return false;
    }

    return true;
}

// espnowReinit — re-establish ESP-NOW after radioEnterOtaMode's WiFi mode switch
// clobbers the driver's peer table / interface binding (esp_now ops fail until
// re-init). Caller re-adds the unicast sender peer after this.
void espnowReinit() {
    esp_now_deinit();
    if (esp_now_init() != ESP_OK) {
        Serial.println("[catch_ota.espnow] esp_now reinit failed");
        return;
    }
    esp_now_register_recv_cb(recvTrampoline);
    addBroadcastPeer();
}

void espnowStop() {
    esp_now_deinit();
    s_recv = nullptr;
}

// espnowAddPeer — register a unicast sender MAC before the first unicast send.
// Guarding against ESP_ERR_ESPNOW_NOT_FOUND on esp_now_send.
void espnowAddPeer(const uint8_t mac[6]) {
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t peer = {};
    std::memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[catch_ota.espnow] esp_now_add_peer(unicast) failed");
    }
}

bool espnowSend(const uint8_t mac[6], const uint8_t* data, size_t len) {
    return esp_now_send(mac, data, len) == ESP_OK;
}

// espnowPoll — drain the SPSC ring on the loop task.
void espnowPoll() {
    if (s_recv == nullptr) return;

    while (true) {
        const uint32_t tail = s_tail.load(std::memory_order_relaxed);
        const uint32_t head = s_head.load(std::memory_order_acquire);
        if (tail == head) break;
        const RingSlot& slot = s_ring[tail & kRingMask];
        s_recv(slot.mac, slot.data, static_cast<size_t>(slot.len));
        // Publish consumption with release so the producer sees the slot freed.
        s_tail.store(tail + 1, std::memory_order_release);
    }
}

}  // namespace catch_ota
