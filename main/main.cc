#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "application.h"
#if CONFIG_SABRINA_VOICE
#include "sabrina_provisioning.h"
#endif

#define TAG "main"

extern "C" void app_main(void) {
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
#if !CONFIG_SABRINA_VOICE
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
#endif
    ESP_ERROR_CHECK(ret);

#if CONFIG_SABRINA_VOICE
    RunSabrinaProvisioningIfRequested();
#endif
    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
}
