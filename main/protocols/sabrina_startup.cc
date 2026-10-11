#include "sabrina_startup.h"
#include "assets.h"
#include "audio_service.h"
#include "cjson_utils.h"

#include <esp_log.h>
#include <esp_ota_ops.h>

namespace {
constexpr const char* kTag = "SabrinaStartup";

bool ApplyLocalAssets() {
    auto& assets = Assets::GetInstance();
    if (!assets.partition_valid()) {
        ESP_LOGE(kTag, "Local assets partition failed integrity validation");
        return false;
    }
    void* data = nullptr;
    size_t size = 0;
    if (!assets.GetAssetData("index.json", data, size))
        return false;
    CJsonUniquePtr index(cJSON_ParseWithLength(static_cast<const char*>(data), size));
    const auto* version = cJSON_GetObjectItemCaseSensitive(index.get(), "version");
    if (!cJSON_IsObject(index.get()) || !cJSON_IsNumber(version) || version->valuedouble != 1) {
        ESP_LOGE(kTag, "Local assets index is invalid or has an unsupported version");
        return false;
    }
    const auto* models = cJSON_GetObjectItemCaseSensitive(index.get(), "srmodels");
    if (models && (!cJSON_IsString(models) || models->valuestring[0] == '\0'))
        return false;
    if (!models) {
#if CONFIG_WAKE_WORD_DISABLED
        // The matching wake-disabled asset bundle intentionally has no neural
        // model. ESP-SR uses WebRTC VAD when vad_model_name is null.
        ESP_LOGI(kTag, "Wake word disabled; using local AFE/WebRTC VAD without neural models");
#else
        ESP_LOGE(kTag, "Local assets are missing required speech models");
        return false;
#endif
    }
    // Apply only the already-flashed bundle. Never consult download_url or OTA.
    if (!assets.Apply())
        return false;
    if (models && !assets.srmodels_loaded()) {
        ESP_LOGE(kTag, "Declared local speech models failed to load");
        return false;
    }
    return true;
}

sabrina::RunningImageState ReadRunningImageState() {
    const auto* partition = esp_ota_get_running_partition();
    if (!partition)
        return sabrina::RunningImageState::Error;
    if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY)
        return sabrina::RunningImageState::Unmanaged;
    esp_ota_img_states_t state;
    auto error = esp_ota_get_state_partition(partition, &state);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Cannot read running OTA image state: %s", esp_err_to_name(error));
        return sabrina::RunningImageState::Error;
    }
    // A retained bootloader without rollback support leaves the active image
    // NEW. It still requires exactly the same local checks and acceptance call.
    if (state == ESP_OTA_IMG_NEW)
        return sabrina::RunningImageState::New;
    if (state == ESP_OTA_IMG_PENDING_VERIFY)
        return sabrina::RunningImageState::PendingVerify;
    if (state == ESP_OTA_IMG_VALID)
        return sabrina::RunningImageState::Valid;
    if (state == ESP_OTA_IMG_UNDEFINED)
        return sabrina::RunningImageState::Unmanaged;
    ESP_LOGE(kTag, "Unexpected running OTA image state: %d", static_cast<int>(state));
    return sabrina::RunningImageState::Error;
}
}  // namespace

bool PrepareSabrinaLocalStartup(AudioService& audio) {
    const auto result = sabrina::VerifyLocalStartup(
        ApplyLocalAssets, [&audio]() { return audio.PrepareLocalVoice(); }, ReadRunningImageState,
        []() {
            auto error = esp_ota_mark_app_valid_cancel_rollback();
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "Cannot accept new/pending OTA image: %s", esp_err_to_name(error));
                return false;
            }
            ESP_LOGI(kTag, "Accepted new/pending OTA image after local assets/audio readiness");
            return true;
        });
    if (result != sabrina::StartupResult::Ready) {
        ESP_LOGE(kTag, "Local startup failed at stage %d; OTA acceptance withheld",
                 static_cast<int>(result));
        return false;
    }
    ESP_LOGI(kTag, "Local startup ready; network and device credentials checked separately");
    return true;
}
