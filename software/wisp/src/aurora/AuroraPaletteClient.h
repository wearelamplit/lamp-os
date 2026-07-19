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
    // Called for EVERY zone with a palette-state announcement, BEFORE
    // setDesired runs. Lets the wisp's ZoneSelector populate its
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

    // Source-mode gate. Active runs discover -> connect -> stream; inactive
    // closes the WS, cancels in-flight resolution, and idles loop() to a no-op.
    // Arming clears resolved ids so palettes changed while idle re-resolve.
    void setActive(bool on);

    // StatusEmitter needs to mirror this into the wispStatus JSON
    // payload's `auroraConnected` field. "Streaming" is the only state with
    // an established WS session processing announcements; earlier states
    // (discovering / connecting) are best reported as not yet connected.
    // Read-cheap, no caching at the call site.
    bool isStreaming() const { return state_ == State::Streaming; }

private:
    enum class State { Idle, Discovering, Connecting, Streaming };

    // Latest active palette id per zone, plus the last id resolved+emitted
    // (so a fetch only happens when it changes; an id seen before connect
    // still resolves once streaming).
    struct ZoneState { int zone; String desiredId; String resolvedId; };

    // A group palette resolves one child fetch per loop pass; this carries
    // the accumulating result across passes.
    struct GroupResolve {
        bool active = false;
        int zone = 0;
        String desiredId;
        Palette acc;
        size_t nextChild = 0;
    };

    void handleFrame(const uint8_t* data, size_t len);
    void sendSubscriptions();
    void setDesired(int zone, const char* paletteId);
    void serviceFetches();
    void serviceGroupResolve();

    State state_ = State::Idle;
    String instanceId_ = "esp32-aurora-client";
    bool staticHost_ = false;
    IPAddress staticIp_;
    uint16_t staticPort_ = 0;
    AuroraDiscovery discovery_;
    AuroraWsConnection ws_;
    PaletteFetcher fetcher_;
    std::vector<ZoneState> zones_;
    GroupResolve group_;
    uint32_t lastFetchMs_ = 0;
    uint32_t lastDiscoverMs_ = 0;
    PaletteHandler onPalette_;
    ZoneObservedHandler onZoneObserved_;

    static constexpr uint32_t kFetchRetryMs    = 1000;  // min spacing between GETs
    static constexpr uint32_t kRediscoverFails = 5;     // re-run mDNS after N WS fails
    // queryService blocks 50-300 ms per call; pacing bounds the loop-stall duty.
    static constexpr uint32_t kDiscoverRetryMs = 5000;
};
