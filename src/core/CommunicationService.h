#pragma once
#include <functional>
#include <map>
#include <vector>
#include <Arduino.h>

class CommunicationService {
public:
    using Callback = std::function<void(const String& topic, const String& msg)>;

    void publish(const String& topic, const String& msg);
    void subscribe(const String& topic, Callback cb);

private:
    std::map<String, std::vector<Callback>> subscribers;
};
