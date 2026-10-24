#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sabrina {
// Hands-free voice mode timing. 1.5 s matches the iOS app's default silence
// timeout; the pre-roll keeps the first syllable that VAD reports late.
constexpr int64_t kEndOfTurnSilenceUs = 1500000;
constexpr int64_t kMinSpeechUs = 250000;
constexpr int64_t kVoiceIdleUs = 120000000;
constexpr int64_t kVoiceRearmUs = 300000;
constexpr size_t kPreRollSamples = 8000;  // 0.5 s at 16 kHz

// Turn taking from VAD state. Speech followed by a pause ends the turn; a blip
// shorter than min_speech is noise and is forgotten; no speech for idle ends
// voice mode. The caller owns the clock and re-arms for each listening window.
class Endpointer {
public:
    enum class Action { Listen, EndTurn, GoIdle };
    Endpointer(int64_t silence_us, int64_t idle_us, int64_t min_speech_us)
        : silence_us_(silence_us), idle_us_(idle_us), min_speech_us_(min_speech_us) {}
    void Arm(int64_t now_us) {
        heard_ = speaking_ = false;
        voiced_us_ = 0;
        armed_at_ = now_us;
    }
    Action Update(bool voice, int64_t now_us) {
        if (voice) {
            if (!speaking_) {
                speaking_ = true;
                spoke_at_ = now_us;
            }
            heard_ = true;
            return Action::Listen;
        }
        if (speaking_) {
            speaking_ = false;
            voiced_us_ += now_us - spoke_at_;
            quiet_at_ = now_us;
        }
        if (heard_) {
            if (now_us - quiet_at_ < silence_us_)
                return Action::Listen;
            if (voiced_us_ >= min_speech_us_)
                return Action::EndTurn;
            heard_ = false;
            voiced_us_ = 0;
            return Action::Listen;
        }
        return now_us - armed_at_ >= idle_us_ ? Action::GoIdle : Action::Listen;
    }
    bool heard() const { return heard_; }

private:
    const int64_t silence_us_, idle_us_, min_speech_us_;
    bool heard_ = false, speaking_ = false;
    int64_t voiced_us_ = 0, armed_at_ = 0, spoke_at_ = 0, quiet_at_ = 0;
};

// Each release gets its own hangover. A re-press continues the recording.
class ReleaseHangover {
public:
    explicit ReleaseHangover(int64_t duration_us) : duration_us_(duration_us) {}
    void Reset() { released_ = false; }
    bool ShouldStop(bool pressed, int64_t now_us) {
        if (pressed) {
            Reset();
            return false;
        }
        if (!released_) {
            released_ = true;
            released_at_ = now_us;
        }
        return now_us - released_at_ >= duration_us_;
    }

private:
    const int64_t duration_us_;
    bool released_ = false;
    int64_t released_at_ = 0;
};

// Caller serializes access. Freeze transfers ownership: another press cannot
// mutate an HTTP request still using the completed recording.
class Recording {
public:
    explicit Recording(size_t max_samples) : max_samples_(max_samples) {}
    void Begin() {
        samples_.clear();
        samples_.reserve(max_samples_);
        active_ = true;
        overflow_ = false;
    }
    bool Append(const int16_t* data, size_t size) {
        if (!active_)
            return true;
        if (size > max_samples_ - samples_.size()) {
            overflow_ = true;
            active_ = false;
            return false;
        }
        samples_.insert(samples_.end(), data, data + size);
        return true;
    }
    std::vector<int16_t> Freeze() {
        active_ = false;
        return std::move(samples_);
    }
    // Waiting for speech keeps only the newest samples as pre-roll.
    void KeepLast(size_t count) {
        if (samples_.size() > count)
            samples_.erase(samples_.begin(),
                           samples_.end() - static_cast<std::ptrdiff_t>(count));
    }
    size_t size() const { return samples_.size(); }
    bool overflow() const { return overflow_; }

private:
    size_t max_samples_;
    bool active_ = false;
    bool overflow_ = false;
    std::vector<int16_t> samples_;
};

// ESP websocket events may split a frame into multiple buffer-sized events,
// and a message into continuation frames. Never parse a partial JSON/PCM body.
class MessageAssembler {
public:
    explicit MessageAssembler(size_t limit) : limit_(limit) {}
    bool Add(int opcode, bool fin, size_t frame_size, size_t offset, const uint8_t* data,
             size_t size, bool& complete) {
        complete = false;
        failure_reason_ = "frame_assembly";
        if (opcode >= 8)
            return true;  // control frames may interrupt a fragmented message
        if (offset == 0) {
            if (opcode == 1 || opcode == 2) {
                if (active_)
                    return false;
                active_ = true;
                binary_ = opcode == 2;
                bytes_.clear();
            } else if (opcode != 0 || !active_)
                return false;
            frame_received_ = 0;
        }
        if (!active_ || offset > frame_size || offset != frame_received_ ||
            size > frame_size - offset)
            return false;
        if (bytes_.size() > limit_ || size > limit_ - bytes_.size()) {
            failure_reason_ = "message_byte_cap";
            return false;
        }
        bytes_.insert(bytes_.end(), data, data + size);
        frame_received_ += size;
        complete = fin && frame_received_ == frame_size;
        if (complete)
            active_ = false;
        return true;
    }
    bool binary() const { return binary_; }
    const char* failure_reason() const { return failure_reason_; }
    size_t size() const { return bytes_.size(); }
    std::vector<uint8_t> Take() { return std::move(bytes_); }
    void Reset() {
        active_ = false;
        bytes_.clear();
        frame_received_ = 0;
    }

private:
    const char* failure_reason_ = "frame_assembly";
    size_t limit_, frame_received_ = 0;
    bool active_ = false, binary_ = false;
    std::vector<uint8_t> bytes_;
};
}  // namespace sabrina
