#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>
#include "AuroraDiscovery.h"
#include "AuroraWsConnection.h"
#include "PaletteFetcher.h"
#include "PaletteList.h"

// Facade: discover -> connect -> subscribe -> resolve the active palette's
// colors via the single-palette REST endpoint.
class AuroraPaletteClient {
public:
    // Called when a zone's active palette resolves to a concrete Palette.
    using PaletteHandler = std::function<void(int zone, const Palette&)>;
    // Called on EVERY zone we see a palette-state announcement for,
    // BEFORE setDesired runs. Lets the wisp's ZoneSelector populate its
    // observed-zones set even for zones whose palette never resolves (GET
    // rate-limited / stragglers). Decoupled from onPalette_ on purpose:
    // onPalette_ only fires on a successful color resolve.
    using ZoneObservedHandler = std::function<void(int zone)>;

    void onActivePalette(PaletteHandler h) { onPalette_ = std::move(h); }
    void onZoneObserved(ZoneObservedHandler h) { onZoneObserved_ = std::move(h); }
    void setInstanceId(const char* id) { instanceId_ = id; }
    // Optional: skip mDNS and connect to a fixed host (useful on networks where
    // the device doesn't advertise _aurora._tcp). mDNS is the default otherwise.
    void setStaticHost(IPAddress ip, uint16_t port) {
        staticIp_ = ip; staticPort_ = port; staticHost_ = true;
    }

    void begin();
    void loop();

    // StatusBeacon needs to mirror this into the wispStatus JSON
    // payload's `auroraConnected` field. "Streaming" is the only state where
    // we've actually established a WS session and are processing announcements;
    // earlier states (discovering / connecting) are best reported as not yet
    // connected. Read-cheap, no caching at the call site.
    bool isStreaming() const { return state_ == State::Streaming; }

private:
    enum class State { Idle, Discovering, Connecting, Streaming };

    // Latest active palette id per zone, plus the last id we resolved+emitted
    // (so we only fetch when it changes; an id seen before connect still
    // resolves once we're streaming).
    struct ZoneState { int zone; String desiredId; String resolvedId; };

    void handleFrame(const uint8_t* data, size_t len);
    void sendSubscriptions();
    void setDesired(int zone, const char* paletteId);
    void serviceFetches();
    bool resolvePalette(const char* id, Palette& out);  // fetch + group-expand

    State state_ = State::Idle;
    String instanceId_ = "esp32-aurora-client";
    bool staticHost_ = false;
    IPAddress staticIp_;
    uint16_t staticPort_ = 0;
    AuroraDiscovery discovery_;
    AuroraWsConnection ws_;
    PaletteFetcher fetcher_;
    std::vector<ZoneState> zones_;
    uint32_t lastFetchMs_ = 0;
    PaletteHandler onPalette_;
    ZoneObservedHandler onZoneObserved_;

    static constexpr uint32_t kFetchRetryMs    = 1000;  // min spacing between GETs
    static constexpr uint32_t kRediscoverFails = 5;     // re-run mDNS after N WS fails
};
