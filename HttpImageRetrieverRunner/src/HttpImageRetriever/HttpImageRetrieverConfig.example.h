#ifndef HTTP_IMAGE_RETRIEVER_CONFIG_EXAMPLE_H
#define HTTP_IMAGE_RETRIEVER_CONFIG_EXAMPLE_H

#include <Arduino.h>

namespace HttpImageRetrieverConfig {

// WiFi credentials.
static const char kWifiSsid[] = "";
static const char kWifiPassword[] = "";
static const bool kLogWifiSsid = false;

// Server settings.
static const char kServerHost[] = "192.168.1.100";
static const uint16_t kServerPort = 8000;
static const bool kUseTls = false;  // Reserved for future HTTPS support.

// Optional API key header. Leave empty to omit the header.
static const char kApiKeyHeaderName[] = "X-API-Key";
static const char kApiKeyValue[] = "";

// Endpoints.
static const char kVersionEndpoint[] = "/version";
static const char kImageEndpoint[] = "/current-image";

// HTTP request behavior.
static const char kVersionAcceptHeader[] = "application/json";
static const char kImageAcceptHeader[] = "*/*";
static const char kConnectionHeaderValue[] = "close";
static const unsigned long kConnectTimeoutMs = 10000UL;
static const unsigned long kReadTimeoutMs = 60000UL;
static const unsigned long kSocketPollDelayMs = 5UL;

}  // namespace HttpImageRetrieverConfig

#endif
