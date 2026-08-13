#include "SubscriptionEncoder.h"
#include "pb_encode.h"
#include <cstdio>
#include <cstring>

namespace {
const char* TYPE_URL_PREFIX = "type.googleapis.com/AURORA.Command.";

// Encode a message into env.msg.value (the Any payload buffer).
template <typename T>
bool packAny(aurora_Any& any, const char* typeName,
             const pb_msgdesc_t* fields, const T& msg) {
    any.has_type_url = true;
    std::snprintf(any.type_url, sizeof(any.type_url), "%s%s",
                  TYPE_URL_PREFIX, typeName);
    pb_ostream_t os = pb_ostream_from_buffer(any.value.bytes, sizeof(any.value.bytes));
    if (!pb_encode(&os, fields, &msg)) return false;
    any.value.size = os.bytes_written;
    any.has_value = true;
    return true;
}

std::vector<uint8_t> finalizeEnvelope(aurora_NotificationEnvelope& env) {
    uint8_t buf[2304];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    if (!pb_encode(&os, aurora_NotificationEnvelope_fields, &env))
        return {};
    return std::vector<uint8_t>(buf, buf + os.bytes_written);
}
}  // namespace

namespace SubscriptionEncoder {

std::vector<uint8_t> instanceInfo(const char* instanceId) {
    aurora_InstanceInfo ii = aurora_InstanceInfo_init_zero;
    ii.has_instance_id = true;
    std::strncpy(ii.instance_id, instanceId, sizeof(ii.instance_id) - 1);

    aurora_NotificationEnvelope env = aurora_NotificationEnvelope_init_zero;
    env.has_type = true; env.type = aurora_NotificationType_INSTANCE_INFO;
    env.has_msg = true;
    if (!packAny(env.msg, "InstanceInfo", aurora_InstanceInfo_fields, ii)) return {};
    return finalizeEnvelope(env);
}

std::vector<uint8_t> subscriptionRequest(const aurora_NotificationType* types,
                                         size_t count) {
    aurora_NotificationSubscriptionRequest req =
        aurora_NotificationSubscriptionRequest_init_zero;
    req.has_replace_all = true; req.replace_all = true;
    pb_size_t n = (count > 24) ? 24 : (pb_size_t)count;
    req.subscriptions_count = n;
    for (pb_size_t i = 0; i < n; ++i) {
        req.subscriptions[i].has_subscription_id = true;
        std::snprintf(req.subscriptions[i].subscription_id,
                      sizeof(req.subscriptions[i].subscription_id),
                      "essential:%d", (int)types[i]);
        req.subscriptions[i].has_type = true;
        req.subscriptions[i].type = types[i];
        req.subscriptions[i].has_action = true;
        req.subscriptions[i].action = aurora_SubscriptionAction_SUBSCRIBE;
    }

    aurora_NotificationEnvelope env = aurora_NotificationEnvelope_init_zero;
    env.has_type = true; env.type = aurora_NotificationType_LIVE_DATA_SUBSCRIPTION;
    env.has_msg = true;
    if (!packAny(env.msg, "NotificationSubscriptionRequest",
                 aurora_NotificationSubscriptionRequest_fields, req)) return {};
    return finalizeEnvelope(env);
}

}  // namespace SubscriptionEncoder
