#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "aurora_notifications.pb.h"

// Pure (host-portable). Builds the exact byte payloads the device expects.
namespace SubscriptionEncoder {
    // Envelope{type=INSTANCE_INFO, msg=Any(InstanceInfo{instance_id})}.
    std::vector<uint8_t> instanceInfo(const char* instanceId);

    // Envelope{type=LIVE_DATA_SUBSCRIPTION,
    //          msg=Any(NotificationSubscriptionRequest{subs, replace_all=true})}.
    // Each type becomes one NotificationSubscription{subscription_id="essential:<type>",
    // type, action=SUBSCRIBE}.
    std::vector<uint8_t> subscriptionRequest(const aurora_NotificationType* types,
                                             size_t count);
}
