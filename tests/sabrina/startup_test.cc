#include <cassert>
#include <iostream>
#include <string>
#include "sabrina_startup.h"

using sabrina::RunningImageState;
using sabrina::StartupResult;

int main() {
    std::string calls;
    bool assets_ok = true, audio_ok = true, accept_ok = true;
    auto image_state = RunningImageState::PendingVerify;
    auto run = [&]() {
        calls.clear();
        return sabrina::VerifyLocalStartup(
            [&]() {
                calls += 'A';
                return assets_ok;
            },
            [&]() {
                calls += 'M';
                return audio_ok;
            },
            [&]() {
                calls += 'S';
                return image_state;
            },
            [&]() {
                calls += 'V';
                return accept_ok;
            });
    };
    // Older bootloaders may leave NEW unchanged rather than transitioning to
    // PENDING_VERIFY. Both states must pass the same local checks and checked
    // acceptance call; failures must never falsely certify either image.
    for (auto candidate_state : {RunningImageState::New, RunningImageState::PendingVerify}) {
        image_state = candidate_state;
        assets_ok = audio_ok = accept_ok = true;
        assert(run() == StartupResult::Ready && calls == "AMSV");
        assets_ok = false;
        assert(run() == StartupResult::AssetsFailed && calls == "A");
        assets_ok = true;
        audio_ok = false;
        assert(run() == StartupResult::AudioFailed && calls == "AM");
        audio_ok = true;
        accept_ok = false;
        assert(run() == StartupResult::AcceptanceFailed && calls == "AMSV");
    }
    image_state = RunningImageState::Error;
    assert(run() == StartupResult::ImageStateFailed && calls == "AMS");
    // Existing valid/factory images must never call the OTA acceptance API,
    // even if that API would fail. No Wi-Fi/provisioning dependency is present.
    image_state = RunningImageState::Valid;
    assert(run() == StartupResult::Ready && calls == "AMS");
    image_state = RunningImageState::Unmanaged;
    assert(run() == StartupResult::Ready && calls == "AMS");
    std::cout << "local startup ordering and OTA failure/acceptance tests passed\n";
}
