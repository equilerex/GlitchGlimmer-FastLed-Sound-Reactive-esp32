#include "CommunicationService.h"

void CommunicationService::publish(const String& topic, const String& msg) {
    auto it = subscribers.find(topic);
    if (it != subscribers.end()) {
        for (auto &cb : it->second) cb(topic, msg);
    }
}

void CommunicationService::subscribe(const String& topic, Callback cb) {
    subscribers[topic].push_back(cb);
}
