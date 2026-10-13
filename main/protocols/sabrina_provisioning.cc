#include "sabrina_provisioning.h"

#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <mbedtls/platform_util.h>
#include <nvs.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

namespace {
constexpr size_t kMaxRequest = 2048;
constexpr gpio_num_t kProvisionButton = GPIO_NUM_42;
constexpr char kBlobKey[] = "config_v1";

struct SecretBufferDeleter {
    void operator()(char* value) const {
        if (value)
            mbedtls_platform_zeroize(value, kMaxRequest + 1);
        free(value);
    }
};
using SecretBuffer = std::unique_ptr<char, SecretBufferDeleter>;
SecretBuffer AllocateBuffer() {
    return SecretBuffer(static_cast<char*>(calloc(kMaxRequest + 1, 1)));
}

bool SafeToken(std::string_view value, size_t minimum, size_t maximum) {
    if (value.size() < minimum || value.size() > maximum)
        return false;
    for (char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_'))
            return false;
    }
    return true;
}

std::string_view Authority(std::string_view value, std::string_view scheme, std::string_view path) {
    if (value.size() > 256 || !value.starts_with(scheme) || !value.ends_with(path))
        return {};
    for (unsigned char c : value)
        if (c <= 32 || c >= 127 || c == '@' || c == '?' || c == '#')
            return {};
    auto rest = value.substr(scheme.size());
    auto slash = rest.find('/');
    if (slash == std::string_view::npos || slash == 0 || rest.substr(slash) != path)
        return {};
    return rest.substr(0, slash);
}

const char* String(cJSON* root, const char* name) {
    auto field = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(field) ? field->valuestring : nullptr;
}

bool ValidConfig(cJSON* root) {
    const char* url = String(root, "url");
    const char* stt = String(root, "stt_url");
    const char* key = String(root, "device_key");
    if (!url || !stt || !key)
        return false;
    auto authority = Authority(url, "wss://", "/ws/voice");
    return !authority.empty() && authority == Authority(stt, "https://", "/api/stt") &&
           std::string_view(key).starts_with("sb_voice_") && SafeToken(key + 9, 32, 96);
}

void DeleteSecretJson(cJSON* root) {
    if (!root)
        return;
    auto key = cJSON_GetObjectItemCaseSensitive(root, "device_key");
    if (cJSON_IsString(key) && key->valuestring)
        mbedtls_platform_zeroize(key->valuestring, strlen(key->valuestring));
    cJSON_Delete(root);
}

void Send(std::string_view text) {
    usb_serial_jtag_write_bytes(text.data(), text.size(), pdMS_TO_TICKS(1000));
}

void Reply(const char* request_id, bool okay, const char* code) {
    auto root = cJSON_CreateObject();
    if (!root)
        return;
    cJSON_AddStringToObject(root, "request_id", request_id);
    cJSON_AddStringToObject(root, "status", okay ? "ok" : "error");
    cJSON_AddStringToObject(root, "code", code);
    char* serialized = cJSON_PrintUnformatted(root);
    if (serialized) {
        Send("SABRINA_PROVISION_RESULT ");
        Send(serialized);
        Send("\n");
        cJSON_free(serialized);
    }
    cJSON_Delete(root);
}

bool Store(cJSON* root) {
    // One versioned NVS value prevents a torn update from mixing endpoint and credential.
    auto config = cJSON_CreateObject();
    if (!config)
        return false;
    cJSON_AddStringToObject(config, "url", String(root, "url"));
    cJSON_AddStringToObject(config, "stt_url", String(root, "stt_url"));
    cJSON_AddStringToObject(config, "device_key", String(root, "device_key"));
    char* serialized = ValidConfig(config) ? cJSON_PrintUnformatted(config) : nullptr;
    DeleteSecretJson(config);
    if (!serialized)
        return false;
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open("sabrina", NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_erase_key(handle, "session_id");
        if (result == ESP_ERR_NVS_NOT_FOUND)
            result = ESP_OK;
        if (result == ESP_OK)
            result = nvs_set_str(handle, kBlobKey, serialized);
        if (result == ESP_OK)
            result = nvs_commit(handle);
        auto check = AllocateBuffer();
        size_t size = kMaxRequest + 1;
        if (!check)
            result = ESP_ERR_NO_MEM;
        if (result == ESP_OK)
            result = nvs_get_str(handle, kBlobKey, check.get(), &size);
        if (result == ESP_OK && strcmp(check.get(), serialized) != 0)
            result = ESP_FAIL;
        nvs_close(handle);
    }
    mbedtls_platform_zeroize(serialized, strlen(serialized));
    cJSON_free(serialized);
    return result == ESP_OK;
}
}  // namespace

bool LoadSabrinaProvisionedConfig(std::string& url, std::string& stt_url, std::string& key) {
    nvs_handle_t handle = 0;
    if (nvs_open("sabrina", NVS_READONLY, &handle) != ESP_OK)
        return false;
    auto buffer = AllocateBuffer();
    size_t length = kMaxRequest + 1;
    auto result = buffer ? nvs_get_str(handle, kBlobKey, buffer.get(), &length) : ESP_ERR_NO_MEM;
    nvs_close(handle);
    if (result != ESP_OK)
        return false;
    cJSON* root = cJSON_ParseWithOpts(buffer.get(), nullptr, true);
    bool valid = root && ValidConfig(root);
    if (valid) {
        url = String(root, "url");
        stt_url = String(root, "stt_url");
        key = String(root, "device_key");
    }
    DeleteSecretJson(root);
    return valid;
}

void RunSabrinaProvisioningIfRequested() {
    gpio_config_t gpio{};
    gpio.pin_bit_mask = 1ULL << kProvisionButton;
    gpio.mode = GPIO_MODE_INPUT;
    gpio.pull_up_en = GPIO_PULLUP_ENABLE;
    if (gpio_config(&gpio) != ESP_OK || gpio_get_level(kProvisionButton) != 0)
        return;
    const bool installed_here = !usb_serial_jtag_is_driver_installed();
    if (installed_here) {
        usb_serial_jtag_driver_config_t config{.tx_buffer_size = 512, .rx_buffer_size = 4096};
        if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
            ESP_LOGE("SabrinaProvision", "USB provisioning unavailable");
            return;
        }
    }
    auto line = AllocateBuffer();
    if (!line) {
        if (installed_here)
            usb_serial_jtag_driver_uninstall();
        return;
    }
    size_t used = 0;
    const int64_t deadline = esp_timer_get_time() + 60LL * 1000000;
    int64_t next_ready = 0;
    while (esp_timer_get_time() < deadline) {
        if (used == 0 && esp_timer_get_time() >= next_ready) {
            Send("SABRINA_PROVISION_READY v1\n");
            next_ready = esp_timer_get_time() + 1000000;
        }
        char c;
        if (usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(100)) != 1)
            continue;
        if (c == '\r')
            continue;
        if (c != '\n') {
            if (used == kMaxRequest || c == '\0') {
                Reply("", false, "invalid_request");
                break;
            }
            line.get()[used++] = c;
            continue;
        }
        cJSON* root = cJSON_ParseWithOpts(line.get(), nullptr, true);
        const char* id = root ? String(root, "request_id") : nullptr;
        const char* op = root ? String(root, "op") : nullptr;
        bool id_valid = id && strlen(id) == 32;
        if (id_valid)
            for (const char* p = id; *p; ++p)
                if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
                    id_valid = false;
        if (!id_valid || !op || strcmp(op, "sabrina_provision") != 0 || !ValidConfig(root)) {
            Reply(id_valid ? id : "", false, "invalid_request");
        } else {
            bool okay = Store(root);
            Reply(id, okay, okay ? "stored" : "storage_failed");
        }
        DeleteSecretJson(root);
        break;
    }
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1500));
    if (installed_here)
        usb_serial_jtag_driver_uninstall();
}
