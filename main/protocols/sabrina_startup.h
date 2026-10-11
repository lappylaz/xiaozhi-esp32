#pragma once

namespace sabrina {
enum class RunningImageState { Valid, PendingVerify, Unmanaged, Error };
enum class StartupResult { Ready, AssetsFailed, AudioFailed, ImageStateFailed, AcceptanceFailed };

// Keep local boot acceptance independent of network or credential availability.
// Each dependency runs only after the previous local readiness check succeeds.
template <typename AssetsCheck, typename AudioCheck, typename ImageCheck, typename AcceptImage>
StartupResult VerifyLocalStartup(AssetsCheck assets, AudioCheck audio, ImageCheck image,
                                 AcceptImage accept) {
    if (!assets())
        return StartupResult::AssetsFailed;
    if (!audio())
        return StartupResult::AudioFailed;
    const auto state = image();
    if (state == RunningImageState::Error)
        return StartupResult::ImageStateFailed;
    if (state == RunningImageState::PendingVerify && !accept())
        return StartupResult::AcceptanceFailed;
    return StartupResult::Ready;
}
}  // namespace sabrina

class AudioService;
bool PrepareSabrinaLocalStartup(AudioService& audio);
