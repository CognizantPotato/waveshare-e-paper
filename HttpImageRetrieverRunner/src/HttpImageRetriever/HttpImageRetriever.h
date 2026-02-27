#ifndef HTTP_IMAGE_RETRIEVER_H
#define HTTP_IMAGE_RETRIEVER_H

#include <Arduino.h>
#include <stdint.h>

struct VersionCheckResult {
    bool request_succeeded = false;
    bool json_parsed = false;
    bool local_version_exists = false;
    bool update_needed = true;
    int http_status_code = 0;
    uint64_t local_version = 0;
    uint64_t remote_version = 0;
};

class HttpImageRetriever {
  public:
    HttpImageRetriever();

    // Performs GET /version and compares the result to the locally stored version.
    VersionCheckResult CheckVersion();

    // Allows the caller to update the stored version once an image update succeeds.
    bool StoreLocalVersion(uint64_t version);

    // Exposed for debugging/telemetry.
    bool LoadLocalVersion(uint64_t &version_out, bool &exists_out);

    // Fetches the image payload from the configured image endpoint.
    // Caller owns the returned buffer and must free it with FreeHttpBuffer().
    bool FetchImageBuffer(char *&image_out, size_t &image_size_out, int &http_status_code_out);
    void FreeHttpBuffer(char *buffer);

  private:
    bool EnsureWifiConnected();
    bool HttpGet(const char *endpoint, String &response_body, int &http_status_code);
    bool HttpGetBuffer(const char *endpoint, char *&response_body_out, size_t &response_size_out, int &http_status_code);
    bool ParseVersionFromJson(const String &json, uint64_t &version_out);
    bool ParseHttpResponse(const String &raw_response, String &body_out, int &status_code_out);

    // Placeholder storage hooks. On ESP32 these use RTC memory (survives deep sleep, not power loss).
    bool LoadLocalVersionInternal(uint64_t &version_out, bool &exists_out);
    bool StoreLocalVersionInternal(uint64_t version);
};

#endif
