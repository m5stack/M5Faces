#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_M5FACES_EXAMPLE_HOST_CORE2
#define M5FACES_HOST_CORE2 1
#elif CONFIG_M5FACES_EXAMPLE_HOST_CORES3
#define M5FACES_HOST_CORES3 1
#endif

#define M5FACES_VALIDATION_FW "IDF2.8.0"

// Keep the Arduino and native ESP-IDF examples on the same validation logic.
// The shared source selects only ESP-IDF, FreeRTOS, and M5Unified APIs here.
#include "../../../Arduino/Faces3Validation/Faces3Validation.cpp"

extern "C" void app_main(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);

    setup();
    for (;;) {
        loop();
    }
}
