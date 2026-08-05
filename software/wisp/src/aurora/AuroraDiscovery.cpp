#include "AuroraDiscovery.h"
#include <ESPmDNS.h>
#include <mdns.h>

bool AuroraDiscovery::start(uint32_t timeoutMs) {
    if (search_) return false;
    // MDNS.begin() re-inits the responder and can fail or leak if re-called
    // without end(); begin once, then query with the raw async API.
    if (!begun_) {
        if (!MDNS.begin("aurora-esp32-client")) {
            Serial.println("[mdns] MDNS.begin failed");
            return false;
        }
        begun_ = true;
    }
    found_ = false;
    search_ = mdns_query_async_new(nullptr, "_aurora", "_tcp", MDNS_TYPE_PTR,
                                   timeoutMs, 1, nullptr);
    return search_ != nullptr;
}

bool AuroraDiscovery::poll() {
    if (!search_) return false;
    mdns_result_t* results = nullptr;
    if (!mdns_query_async_get_results(search_, 0, &results, nullptr)) return false;

    for (mdns_result_t* r = results; r && !found_; r = r->next) {
        for (mdns_ip_addr_t* a = r->addr; a; a = a->next) {
            if (a->addr.type == MDNS_IP_PROTOCOL_V4) {
                ip_ = IPAddress(a->addr.u_addr.ip4.addr);
                port_ = r->port;
                found_ = (port_ != 0);
                break;
            }
        }
    }
    if (results) mdns_query_results_free(results);
    mdns_query_async_delete(search_);
    search_ = nullptr;
    if (found_) {
        Serial.printf("[mdns] found aurora at %s:%u\n",
                      ip_.toString().c_str(), port_);
    }
    return true;
}

void AuroraDiscovery::cancel() {
    if (!search_) return;
    found_ = false;
    // delete only frees a finished search; an unfinished one returns
    // ESP_ERR_INVALID_STATE and must drain before it can be reaped.
    if (mdns_query_async_delete(search_) == ESP_OK) {
        search_ = nullptr;
    } else {
        draining_ = true;
    }
}

void AuroraDiscovery::drain() {
    if (!draining_) return;
    mdns_result_t* results = nullptr;
    if (!mdns_query_async_get_results(search_, 0, &results, nullptr)) return;
    if (results) mdns_query_results_free(results);
    mdns_query_async_delete(search_);
    search_ = nullptr;
    draining_ = false;
}
