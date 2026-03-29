#include "HttpImageRetriever.h"

#include "../Config/Debug.h"
#include "HttpImageRetrieverConfig.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP32)
#include <WiFi.h>
#include <esp_heap_caps.h>
#define HTTP_IMAGE_RETRIEVER_HAS_WIFI 1
#else
#define HTTP_IMAGE_RETRIEVER_HAS_WIFI 0
#endif

namespace {

#if defined(ESP32)
RTC_DATA_ATTR bool g_local_version_exists = false;
RTC_DATA_ATTR uint64_t g_local_version = 0;
#else
bool g_local_version_exists = false;
uint64_t g_local_version = 0;
#endif

bool ParseContentLengthHeader(const String &header, size_t &content_length_out)
{
    content_length_out = 0;

    int key_pos = -1;
    for (int idx = 0; idx <= (header.length() - 15); ++idx) {
        char c = header[idx];
        if (c == 'C' || c == 'c') {
            if (header.substring(idx, idx + 15).equalsIgnoreCase("Content-Length")) {
                key_pos = idx;
                break;
            }
        }
    }
    if (key_pos < 0) {
        return false;
    }

    int i = key_pos;
    while (i < header.length() && header[i] != ':') {
        ++i;
    }
    if (i >= header.length()) {
        return false;
    }
    ++i;

    while (i < header.length() && isspace(static_cast<unsigned char>(header[i]))) {
        ++i;
    }

    if (i >= header.length() || !isdigit(static_cast<unsigned char>(header[i]))) {
        return false;
    }

    size_t value = 0;
    while (i < header.length() && isdigit(static_cast<unsigned char>(header[i]))) {
        value = (value * 10U) + static_cast<size_t>(header[i] - '0');
        ++i;
    }

    content_length_out = value;
    return true;
}

bool HasChunkedTransferEncoding(const String &header)
{
    int key_pos = -1;
    for (int idx = 0; idx <= (header.length() - 17); ++idx) {
        char c = header[idx];
        if (c == 'T' || c == 't') {
            if (header.substring(idx, idx + 17).equalsIgnoreCase("Transfer-Encoding")) {
                key_pos = idx;
                break;
            }
        }
    }
    if (key_pos < 0) {
        return false;
    }

    int line_end = header.indexOf("\r\n", key_pos);
    if (line_end < 0) {
        line_end = header.length();
    }

    String line = header.substring(key_pos, line_end);
    line.toLowerCase();
    return (line.indexOf("chunked") >= 0);
}

void *HttpBufferRealloc(void *ptr, size_t size)
{
#if defined(ESP32)
    // Prefer PSRAM for large HTTP payloads (image body) to avoid fragmenting internal heap.
    if (psramFound()) {
        void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p != NULL) {
            return p;
        }
    }
#endif
    return realloc(ptr, size);
}

void AppendOptionalApiKeyHeader(WiFiClient &client)
{
    if (HttpImageRetrieverConfig::kApiKeyHeaderName[0] == '\0' ||
        HttpImageRetrieverConfig::kApiKeyValue[0] == '\0') {
        return;
    }

    client.print(HttpImageRetrieverConfig::kApiKeyHeaderName);
    client.print(": ");
    client.print(HttpImageRetrieverConfig::kApiKeyValue);
    client.print("\r\n");
}

}  // namespace

HttpImageRetriever::HttpImageRetriever() {}

VersionCheckResult HttpImageRetriever::CheckVersion()
{
    VersionCheckResult result;
    LoadLocalVersionInternal(result.local_version, result.local_version_exists);

    Serial.println("CheckVersion: starting GET /version");

    String body;
    if (!HttpGet(HttpImageRetrieverConfig::kVersionEndpoint, body, result.http_status_code)) {
        Debug("GET /version failed. HTTP status: ");
        Serial.println(result.http_status_code);
        Debug("GET /version body: ");
        Serial.println(body);
        return result;
    }

    Debug("GET /version succeeded. HTTP status: ");
    Serial.println(result.http_status_code);
    Debug("GET /version body: ");
    Serial.println(body);

    result.request_succeeded = true;

    if (!ParseVersionFromJson(body, result.remote_version)) {
        return result;
    }

    result.json_parsed = true;

    if (!result.local_version_exists) {
        result.update_needed = true;
        return result;
    }

    result.update_needed = (result.local_version != result.remote_version);
    return result;
}

bool HttpImageRetriever::StoreLocalVersion(uint64_t version)
{
    return StoreLocalVersionInternal(version);
}

bool HttpImageRetriever::LoadLocalVersion(uint64_t &version_out, bool &exists_out)
{
    return LoadLocalVersionInternal(version_out, exists_out);
}

bool HttpImageRetriever::FetchImageBuffer(char *&image_out, size_t &image_size_out, int &http_status_code_out)
{
    return HttpGetBuffer(HttpImageRetrieverConfig::kImageEndpoint, image_out, image_size_out, http_status_code_out);
}

void HttpImageRetriever::FreeHttpBuffer(char *buffer)
{
    if (buffer != NULL) {
        free(buffer);
    }
}

bool HttpImageRetriever::EnsureWifiConnected()
{
#if !HTTP_IMAGE_RETRIEVER_HAS_WIFI
    Debug("This build target is not ESP32. Select an ESP32-S3 board in Arduino IDE.\r\n");
    return false;
#else
    Serial.print("EnsureWifiConnected: WiFi.status()=");
    Serial.println(static_cast<int>(WiFi.status()));

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("EnsureWifiConnected: already connected");
        return true;
    }

    if (HttpImageRetrieverConfig::kWifiSsid[0] == '\0') {
        Debug("WiFi SSID is empty in HttpImageRetrieverConfig.h\r\n");
        return false;
    }

    Debug("Connecting to WiFi...\r\n");
    if (HttpImageRetrieverConfig::kLogWifiSsid) {
        Serial.print("WiFi SSID: ");
        Serial.println(HttpImageRetrieverConfig::kWifiSsid);
    }
    WiFi.begin(HttpImageRetrieverConfig::kWifiSsid, HttpImageRetrieverConfig::kWifiPassword);

    unsigned long start_ms = millis();
    unsigned long last_log_ms = start_ms;
    while (WiFi.status() != WL_CONNECTED &&
           (millis() - start_ms) < HttpImageRetrieverConfig::kConnectTimeoutMs) {
        unsigned long now = millis();
        if ((now - last_log_ms) >= 1000UL) {
            Serial.print("Waiting for WiFi... status=");
            Serial.print(static_cast<int>(WiFi.status()));
            Serial.print(" elapsed_ms=");
            Serial.println(static_cast<unsigned long>(now - start_ms));
            last_log_ms = now;
        }
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Debug("WiFi connection failed.\r\n");
        Serial.print("EnsureWifiConnected: final WiFi.status()=");
        Serial.println(static_cast<int>(WiFi.status()));
        return false;
    }

    Debug("WiFi connected.\r\n");
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    return true;
#endif
}

bool HttpImageRetriever::HttpGet(const char *endpoint, String &response_body, int &http_status_code)
{
    response_body = "";
    http_status_code = 0;

    Serial.print("HttpGet: endpoint=");
    Serial.println(endpoint);

    if (!EnsureWifiConnected()) {
        Serial.println("HttpGet: EnsureWifiConnected failed");
        return false;
    }

#if !HTTP_IMAGE_RETRIEVER_HAS_WIFI
    return false;
#else
    if (HttpImageRetrieverConfig::kUseTls) {
        Debug("HTTPS not implemented yet (kUseTls=true).\r\n");
        return false;
    }

    WiFiClient client;
    client.setTimeout(HttpImageRetrieverConfig::kReadTimeoutMs);

    Debug("Opening HTTP connection...\r\n");
    Serial.print("HttpGet: connecting to ");
    Serial.print(HttpImageRetrieverConfig::kServerHost);
    Serial.print(":");
    Serial.println(HttpImageRetrieverConfig::kServerPort);
    if (!client.connect(HttpImageRetrieverConfig::kServerHost, HttpImageRetrieverConfig::kServerPort)) {
        Debug("HTTP connect failed.\r\n");
        Serial.println("HttpGet: client.connect failed");
        return false;
    }
    Serial.println("HttpGet: client.connect succeeded");

    client.print("GET ");
    client.print(endpoint);
    client.print(" HTTP/1.1\r\nHost: ");
    client.print(HttpImageRetrieverConfig::kServerHost);
    client.print("\r\n");
    AppendOptionalApiKeyHeader(client);
    client.print("Connection: ");
    client.print(HttpImageRetrieverConfig::kConnectionHeaderValue);
    client.print("\r\nAccept: ");
    client.print(HttpImageRetrieverConfig::kVersionAcceptHeader);
    client.print("\r\n\r\n");

    String raw_response;
    unsigned long last_data_ms = millis();

    while (client.connected() || client.available()) {
        while (client.available()) {
            raw_response += static_cast<char>(client.read());
            last_data_ms = millis();
        }

        if ((millis() - last_data_ms) > HttpImageRetrieverConfig::kReadTimeoutMs) {
            Debug("HTTP read timeout.\r\n");
            Serial.println("HttpGet: read timeout");
            client.stop();
            return false;
        }

        delay(HttpImageRetrieverConfig::kSocketPollDelayMs);
    }

    client.stop();

    if (!ParseHttpResponse(raw_response, response_body, http_status_code)) {
        Debug("Failed to parse HTTP response.\r\n");
        Serial.println("HttpGet: ParseHttpResponse failed");
        return false;
    }

    if (http_status_code != 200) {
        Debug("HTTP status was not 200.\r\n");
        return false;
    }

    return true;
#endif
}

bool HttpImageRetriever::HttpGetBuffer(const char *endpoint, char *&response_body_out, size_t &response_size_out, int &http_status_code)
{
    response_body_out = NULL;
    response_size_out = 0;
    http_status_code = 0;

    if (!EnsureWifiConnected()) {
        return false;
    }

#if !HTTP_IMAGE_RETRIEVER_HAS_WIFI
    return false;
#else
#if defined(ESP32)
    Serial.print("ESP32 free heap before image fetch: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("ESP32 free PSRAM before image fetch: ");
    Serial.println(ESP.getFreePsram());
#endif

    if (HttpImageRetrieverConfig::kUseTls) {
        Debug("HTTPS not implemented yet (kUseTls=true).\r\n");
        return false;
    }

    WiFiClient client;
    client.setTimeout(HttpImageRetrieverConfig::kReadTimeoutMs);

    Debug("Opening HTTP image connection...\r\n");
    if (!client.connect(HttpImageRetrieverConfig::kServerHost, HttpImageRetrieverConfig::kServerPort)) {
        Debug("HTTP image connect failed.\r\n");
        return false;
    }

    client.print("GET ");
    client.print(endpoint);
    client.print(" HTTP/1.1\r\nHost: ");
    client.print(HttpImageRetrieverConfig::kServerHost);
    client.print("\r\n");
    AppendOptionalApiKeyHeader(client);
    client.print("Connection: ");
    client.print(HttpImageRetrieverConfig::kConnectionHeaderValue);
    client.print("\r\nAccept: ");
    client.print(HttpImageRetrieverConfig::kImageAcceptHeader);
    client.print("\r\n\r\n");

    String header;
    bool headers_complete = false;
    unsigned long last_data_ms = millis();
    size_t capacity = 0;
    char *buffer = NULL;
    size_t length = 0;
    size_t content_length = 0;
    bool has_content_length = false;
    bool is_chunked_transfer = false;
    String chunk_line;
    String trailer_line;
    size_t chunk_bytes_remaining = 0;
    enum ChunkState {
        kChunkSizeLine,
        kChunkData,
        kChunkDataCR,
        kChunkDataLF,
        kChunkTrailers,
        kChunkDone
    };
    ChunkState chunk_state = kChunkSizeLine;

    while (client.connected() || client.available()) {
        while (client.available()) {
            char c = static_cast<char>(client.read());
            last_data_ms = millis();

            if (!headers_complete) {
                header += c;
                if (header.endsWith("\r\n\r\n")) {
                    headers_complete = true;

                    int status_line_end = header.indexOf("\r\n");
                    if (status_line_end < 0) {
                        client.stop();
                        FreeHttpBuffer(buffer);
                        return false;
                    }

                    String status_line = header.substring(0, status_line_end);
                    int first_space = status_line.indexOf(' ');
                    if (first_space < 0) {
                        client.stop();
                        FreeHttpBuffer(buffer);
                        return false;
                    }
                    int second_space = status_line.indexOf(' ', first_space + 1);
                    if (second_space < 0) {
                        second_space = status_line.length();
                    }
                    http_status_code = status_line.substring(first_space + 1, second_space).toInt();

                    has_content_length = ParseContentLengthHeader(header, content_length);
                    if (has_content_length) {
                        Serial.print("Image HTTP Content-Length: ");
                        Serial.println(static_cast<unsigned long>(content_length));
                    } else {
                        Serial.println("Image HTTP Content-Length header not found.");
                    }

                    is_chunked_transfer = HasChunkedTransferEncoding(header);
                    if (is_chunked_transfer) {
                        Serial.println("Image HTTP Transfer-Encoding: chunked");
                    }
                }
                continue;
            }

            if (is_chunked_transfer) {
                if (chunk_state == kChunkDone) {
                    continue;
                }

                if (chunk_state == kChunkSizeLine) {
                    if (c == '\r') {
                        continue;
                    }
                    if (c == '\n') {
                        int semicolon_pos = chunk_line.indexOf(';');
                        String size_str = (semicolon_pos >= 0) ? chunk_line.substring(0, semicolon_pos) : chunk_line;
                        size_str.trim();
                        if (size_str.length() == 0) {
                            client.stop();
                            FreeHttpBuffer(buffer);
                            return false;
                        }

                        chunk_bytes_remaining = static_cast<size_t>(strtoul(size_str.c_str(), NULL, 16));
                        chunk_line = "";

                        if (chunk_bytes_remaining == 0) {
                            chunk_state = kChunkTrailers;
                        } else {
                            chunk_state = kChunkData;
                        }
                        continue;
                    }

                    chunk_line += c;
                    continue;
                }

                if (chunk_state == kChunkData) {
                    if (length + 1 >= capacity) {
                        size_t new_capacity;
                        if (capacity == 0 && has_content_length && content_length > 0) {
                            new_capacity = content_length + 1;  // +1 for terminal NUL
                        } else {
                            // Avoid large power-of-two jumps on ESP32 heaps; they fail due to fragmentation.
                            if (capacity == 0) {
                                new_capacity = 8192;
                            } else if (capacity < 65536) {
                                new_capacity = capacity * 2;
                            } else {
                                new_capacity = capacity + 32768;
                            }
                        }

                        Serial.print("Image buffer grow: ");
                        Serial.print(static_cast<unsigned long>(capacity));
                        Serial.print(" -> ");
                        Serial.println(static_cast<unsigned long>(new_capacity));
                        char *new_buffer = static_cast<char *>(HttpBufferRealloc(buffer, new_capacity));
                        if (new_buffer == NULL) {
                            Debug("Image buffer realloc failed.\r\n");
                            Serial.print("Image buffer realloc failed at length=");
                            Serial.print(static_cast<unsigned long>(length));
                            Serial.print(" requested_capacity=");
                            Serial.println(static_cast<unsigned long>(new_capacity));
#if defined(ESP32)
                            Serial.print("ESP32 free heap at realloc failure: ");
                            Serial.println(ESP.getFreeHeap());
                            Serial.print("ESP32 free PSRAM at realloc failure: ");
                            Serial.println(ESP.getFreePsram());
#endif
                            client.stop();
                            FreeHttpBuffer(buffer);
                            return false;
                        }
                        buffer = new_buffer;
                        capacity = new_capacity;
                    }

                    buffer[length++] = c;
                    --chunk_bytes_remaining;
                    if (chunk_bytes_remaining == 0) {
                        chunk_state = kChunkDataCR;
                    }
                    continue;
                }

                if (chunk_state == kChunkDataCR) {
                    if (c != '\r') {
                        client.stop();
                        FreeHttpBuffer(buffer);
                        return false;
                    }
                    chunk_state = kChunkDataLF;
                    continue;
                }

                if (chunk_state == kChunkDataLF) {
                    if (c != '\n') {
                        client.stop();
                        FreeHttpBuffer(buffer);
                        return false;
                    }
                    chunk_state = kChunkSizeLine;
                    continue;
                }

                if (chunk_state == kChunkTrailers) {
                    trailer_line += c;
                    if (trailer_line.endsWith("\r\n")) {
                        if (trailer_line == "\r\n") {
                            chunk_state = kChunkDone;
                        }
                        trailer_line = "";
                    }
                    continue;
                }
            }

            if (length + 1 >= capacity) {
                size_t new_capacity;
                if (capacity == 0 && has_content_length && content_length > 0) {
                    new_capacity = content_length + 1;  // +1 for terminal NUL
                } else {
                    // Avoid large power-of-two jumps on ESP32 heaps; they fail due to fragmentation.
                    if (capacity == 0) {
                        new_capacity = 8192;
                    } else if (capacity < 65536) {
                        new_capacity = capacity * 2;
                    } else {
                        new_capacity = capacity + 32768;
                    }
                }

                Serial.print("Image buffer grow: ");
                Serial.print(static_cast<unsigned long>(capacity));
                Serial.print(" -> ");
                Serial.println(static_cast<unsigned long>(new_capacity));
                char *new_buffer = static_cast<char *>(HttpBufferRealloc(buffer, new_capacity));
                if (new_buffer == NULL) {
                    Debug("Image buffer realloc failed.\r\n");
                    Serial.print("Image buffer realloc failed at length=");
                    Serial.print(static_cast<unsigned long>(length));
                    Serial.print(" requested_capacity=");
                    Serial.println(static_cast<unsigned long>(new_capacity));
#if defined(ESP32)
                    Serial.print("ESP32 free heap at realloc failure: ");
                    Serial.println(ESP.getFreeHeap());
                    Serial.print("ESP32 free PSRAM at realloc failure: ");
                    Serial.println(ESP.getFreePsram());
#endif
                    client.stop();
                    FreeHttpBuffer(buffer);
                    return false;
                }
                buffer = new_buffer;
                capacity = new_capacity;
            }

            buffer[length++] = c;
        }

        if ((millis() - last_data_ms) > HttpImageRetrieverConfig::kReadTimeoutMs) {
            Debug("HTTP image read timeout.\r\n");
            client.stop();
            FreeHttpBuffer(buffer);
            return false;
        }

        delay(HttpImageRetrieverConfig::kSocketPollDelayMs);
    }

    client.stop();

    if (!headers_complete || http_status_code <= 0) {
        Debug("Failed to read image HTTP headers.\r\n");
        FreeHttpBuffer(buffer);
        return false;
    }

    if (http_status_code != 200) {
        Debug("Image HTTP status was not 200.\r\n");
        FreeHttpBuffer(buffer);
        return false;
    }

    if (is_chunked_transfer && chunk_state != kChunkDone) {
        Debug("Incomplete chunked image response.\r\n");
        FreeHttpBuffer(buffer);
        return false;
    }

    if (has_content_length && !is_chunked_transfer && content_length != length) {
        Serial.print("Image payload length mismatch. Expected ");
        Serial.print(static_cast<unsigned long>(content_length));
        Serial.print(", got ");
        Serial.println(static_cast<unsigned long>(length));
    }

    // NUL-terminate for convenience even if payload is binary; size_out carries the true length.
    char *final_buffer = static_cast<char *>(HttpBufferRealloc(buffer, length + 1));
    if (final_buffer == NULL && length > 0) {
        Debug("Final image buffer resize failed.\r\n");
        FreeHttpBuffer(buffer);
        return false;
    }
    buffer = (final_buffer != NULL) ? final_buffer : buffer;
    if (buffer != NULL) {
        buffer[length] = '\0';
    }

    response_body_out = buffer;
    response_size_out = length;
    return true;
#endif
}

bool HttpImageRetriever::ParseHttpResponse(const String &raw_response, String &body_out, int &status_code_out)
{
    body_out = "";
    status_code_out = 0;

    int header_end = raw_response.indexOf("\r\n\r\n");
    if (header_end < 0) {
        return false;
    }

    int status_line_end = raw_response.indexOf("\r\n");
    if (status_line_end < 0) {
        return false;
    }

    String status_line = raw_response.substring(0, status_line_end);
    int first_space = status_line.indexOf(' ');
    if (first_space < 0) {
        return false;
    }
    int second_space = status_line.indexOf(' ', first_space + 1);
    if (second_space < 0) {
        second_space = status_line.length();
    }

    String status_code_str = status_line.substring(first_space + 1, second_space);
    status_code_out = status_code_str.toInt();
    body_out = raw_response.substring(header_end + 4);

    return (status_code_out > 0);
}

bool HttpImageRetriever::ParseVersionFromJson(const String &json, uint64_t &version_out)
{
    version_out = 0;

    int key_pos = json.indexOf("\"version\"");
    if (key_pos < 0) {
        return false;
    }

    int colon_pos = json.indexOf(':', key_pos);
    if (colon_pos < 0) {
        return false;
    }

    int i = colon_pos + 1;
    while (i < json.length() && isspace(static_cast<unsigned char>(json[i]))) {
        ++i;
    }

    bool quoted = false;
    if (i < json.length() && json[i] == '"') {
        quoted = true;
        ++i;
    }

    if (i >= json.length() || !isdigit(static_cast<unsigned char>(json[i]))) {
        return false;
    }

    uint64_t value = 0;
    while (i < json.length() && isdigit(static_cast<unsigned char>(json[i]))) {
        uint8_t digit = static_cast<uint8_t>(json[i] - '0');
        value = (value * 10ULL) + digit;
        ++i;
    }

    if (quoted) {
        if (i >= json.length() || json[i] != '"') {
            return false;
        }
        ++i;
    }

    version_out = value;
    return true;
}

bool HttpImageRetriever::LoadLocalVersionInternal(uint64_t &version_out, bool &exists_out)
{
    version_out = g_local_version;
    exists_out = g_local_version_exists;
    return true;
}

bool HttpImageRetriever::StoreLocalVersionInternal(uint64_t version)
{
    g_local_version = version;
    g_local_version_exists = true;
    return true;
}
