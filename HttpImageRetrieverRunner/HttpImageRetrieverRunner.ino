#include "src/HttpImageRetriever/HttpImageRetriever.h"
#include "src/Config/DEV_Config.h"
#include "src/e-Paper/EPD_7in3e.h"

#if defined(ESP32)
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_heap_caps.h>
#endif

HttpImageRetriever g_http_image_retriever;
static const uint32_t kRefreshIntervalMs = 10UL * 60UL * 1000UL;  // 10 minutes
static const uint32_t kMinimumSleepMs = 1000UL;
static const size_t kEpd7in3eImageBytes = ((EPD_7IN3E_WIDTH + 1) / 2) * EPD_7IN3E_HEIGHT;
static bool g_timer_wake = false;

static void PrintUint64(uint64_t value)
{
    char buf[21];
    int i = 20;
    buf[i] = '\0';

    do {
        --i;
        buf[i] = static_cast<char>('0' + (value % 10ULL));
        value /= 10ULL;
    } while (value > 0 && i > 0);

    Serial.println(&buf[i]);
}

static void PrintVersionCheckResult(const VersionCheckResult &result)
{
    Serial.print("HTTP status: ");
    Serial.println(result.http_status_code);

    Serial.print("Local version exists: ");
    Serial.println(result.local_version_exists ? "yes" : "no");

    Serial.print("Local version: ");
    PrintUint64(result.local_version);

    Serial.print("Remote version: ");
    PrintUint64(result.remote_version);

    Serial.print("Update needed: ");
    Serial.println(result.update_needed ? "yes" : "no");
}

VersionCheckResult RunHttpImageRetrieverVersionCheck()
{
    VersionCheckResult result = g_http_image_retriever.CheckVersion();
    PrintVersionCheckResult(result);
    return result;
}

static bool ClearEpaperOnBoot()
{
    Serial.println("Clearing e-paper...");
    if (DEV_Module_Init() != 0) {
        Serial.println("DEV_Module_Init failed.");
        return false;
    }

    EPD_7IN3E_Init();
    EPD_7IN3E_Clear(EPD_7IN3E_WHITE);
    EPD_7IN3E_Sleep();
    DEV_Module_Exit();

    Serial.println("E-paper cleared.");
    return true;
}

static void HandleVersionCheckCycle(const char *phase_label)
{
    Serial.print("[");
    Serial.print(phase_label);
    Serial.println("] Checking remote version...");

    VersionCheckResult result = RunHttpImageRetrieverVersionCheck();

    if (!result.request_succeeded) {
        Serial.println("Version request failed.");
        return;
    }

    if (!result.json_parsed) {
        Serial.println("Version JSON parse failed.");
        return;
    }

    if (result.update_needed) {
        Serial.println("Update required. Fetching image...");

        char *image_buffer = NULL;
        size_t image_size = 0;
        int image_http_status = 0;

        if (!g_http_image_retriever.FetchImageBuffer(image_buffer, image_size, image_http_status)) {
            Serial.print("Image fetch failed. HTTP status: ");
            Serial.println(image_http_status);
            return;
        }

        Serial.print("Image payload bytes: ");
        Serial.println(static_cast<unsigned long>(image_size));

        if (image_size != kEpd7in3eImageBytes) {
            Serial.print("Unexpected image size. Expected bytes: ");
            Serial.println(static_cast<unsigned long>(kEpd7in3eImageBytes));
            g_http_image_retriever.FreeHttpBuffer(image_buffer);
            return;
        }

        Serial.println("Clearing and drawing image to e-paper...");
        if (DEV_Module_Init() != 0) {
            Serial.println("DEV_Module_Init failed before draw.");
            g_http_image_retriever.FreeHttpBuffer(image_buffer);
            return;
        }

        EPD_7IN3E_Init();
        EPD_7IN3E_Clear(EPD_7IN3E_WHITE);
        EPD_7IN3E_Display(reinterpret_cast<UBYTE *>(image_buffer));
        EPD_7IN3E_Sleep();
        DEV_Module_Exit();

        g_http_image_retriever.FreeHttpBuffer(image_buffer);

        if (g_http_image_retriever.StoreLocalVersion(result.remote_version)) {
            Serial.println("Image draw successful. Stored local version.");
        } else {
            Serial.println("Image draw successful, but failed to store local version.");
        }
    } else {
        Serial.println("No update required.");
    }
}

static void SleepUntilNextRefresh(uint32_t cycle_elapsed_ms)
{
    uint32_t sleep_ms = 0;
    if (cycle_elapsed_ms < kRefreshIntervalMs) {
        sleep_ms = kRefreshIntervalMs - cycle_elapsed_ms;
    }
    if (sleep_ms == 0) {
        Serial.println("Cycle exceeded refresh interval; scheduling minimum sleep before retry.");
        sleep_ms = kMinimumSleepMs;
    }

#if defined(ESP32)
    uint64_t sleep_us = static_cast<uint64_t>(sleep_ms) * 1000ULL;
    Serial.print("Cycle elapsed ms: ");
    Serial.println(static_cast<unsigned long>(cycle_elapsed_ms));
    Serial.print("Entering deep sleep for ms: ");
    Serial.println(static_cast<unsigned long>(sleep_ms));
    Serial.flush();
    esp_err_t sleep_cfg = esp_sleep_enable_timer_wakeup(sleep_us);
    if (sleep_cfg != ESP_OK) {
        Serial.print("esp_sleep_enable_timer_wakeup failed: ");
        Serial.println(static_cast<int>(sleep_cfg));
        return;
    }
    esp_deep_sleep_start();
#else
    Serial.print("ESP32 deep sleep not available; delaying ms: ");
    Serial.println(static_cast<unsigned long>(sleep_ms));
    delay(sleep_ms);
#endif
}

void setup()
{
    Serial.begin(9600);
    delay(250);

#if defined(ESP32)
    Serial.print("psramFound(): ");
    Serial.println(psramFound() ? "yes" : "no");
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("Largest free heap block: ");
    Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Serial.print("Free PSRAM: ");
    Serial.println(ESP.getFreePsram());
    Serial.print("Largest free PSRAM block: ");
    Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif

#if defined(ESP32)
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    g_timer_wake = (wake_cause == ESP_SLEEP_WAKEUP_TIMER);
    Serial.print("Wake cause: ");
    Serial.println(static_cast<int>(wake_cause));
#else
    g_timer_wake = false;
#endif

    if (!g_timer_wake) {
        ClearEpaperOnBoot();
    } else {
        Serial.println("Timer wake: skipping setup clear; loop will perform periodic check.");
    }
}

void loop()
{
    uint32_t cycle_start_ms = millis();
    HandleVersionCheckCycle("loop");
    uint32_t cycle_elapsed_ms = millis() - cycle_start_ms;
    SleepUntilNextRefresh(cycle_elapsed_ms);
}
