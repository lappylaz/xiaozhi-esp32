// Voice mode runs the real worker, toggle, endpointing and discard methods.
// VAD, capture and transport are modeled; this is no microphone or network claim.
void run_for(SabrinaProtocol& p, int64_t us, std::function<void()> step = [] {}) {
    p.stopping_ = false;
    const int64_t end = clock_us + us;
    tick = [&] {
        p.last_pong_ = clock_us;  // heartbeat stays healthy across long windows
        step();
        if (clock_us >= end || p.error)
            p.stopping_ = true;
    };
    p.Run();
    assert(!p.error);
}
void transcript_ready(SabrinaProtocol& p, const char* text, bool no_speech = false) {
    std::lock_guard<std::mutex> lock(p.stt_mutex_);
    p.stt_done_ = true;
    p.stt_text_ = text;
    p.stt_error_ = no_speech ? "No speech recognized." : "";
    p.stt_no_speech_ = no_speech;
    p.stt_generation_ = p.generation_;
}
void speak(SabrinaProtocol& p, int64_t speech_us, int64_t pause_us) {
    p.audio_.voice = true;
    run_for(p, speech_us);
    p.audio_.voice = false;
    run_for(p, pause_us);
}
using S = SabrinaProtocol::State;
SabrinaProtocol* listening(SabrinaProtocol& p) {
    p.turn_active_ = false;
    connected(p);
    p.toggle_pending_ = true;
    run_for(p, 400000);
    assert(p.conversation_ && p.state_ == S::Recording && p.audio_.capturing);
    return &p;
}
int main() {
    // Connected with voice mode off: nothing listens until a click.
    SabrinaProtocol p;
    p.turn_active_ = false;
    connected(p);
    run_for(p, 2 * kSecond);
    assert(p.state_ == S::Ready && !p.audio_.capturing && !p.conversation_);

    // A click turns voice mode on; listening starts after the re-arm gap.
    p.toggle_pending_ = true;
    run_for(p, 200000);
    assert(p.conversation_ && p.state_ == S::Ready && !p.audio_.capturing);
    run_for(p, 200000);
    assert(p.state_ == S::Recording && p.audio_.capturing);

    // Waiting in silence keeps only pre-roll, so it can never hit the 20 s cap.
    p.recording_.Begin();
    std::vector<int16_t> frame(160, 7);  // 10 ms at 16 kHz
    size_t peak = 0;
    run_for(p, 30 * kSecond, [&] {
        std::lock_guard<std::mutex> lock(p.recording_mutex_);
        assert(p.recording_.Append(frame.data(), frame.size()));
        peak = std::max(peak, p.recording_.size());
    });
    assert(p.state_ == S::Recording && !p.capture_failed_);
    assert(peak <= 2 * sabrina::kPreRollSamples + frame.size());

    // A 100 ms blip is noise: the window stays open.
    speak(p, 100000, 3 * kSecond);
    assert(p.state_ == S::Recording && p.transcripts_sent == 0);

    // Real speech then a pause ends the turn exactly 1.5 s after the last speech.
    speak(p, kSecond, 1400000);
    assert(p.state_ == S::Recording);
    run_for(p, 200000);
    assert(p.state_ == S::Transcribing && !p.audio_.capturing);

    // The transcript goes out; the reply plays; listening resumes with no click.
    transcript_ready(p, "what time is it");
    run_for(p, 50000);
    assert(p.transcripts_sent == 1 && p.state_ == S::Thinking);
    audio_chunk(p, 1, 9600);
    p.ProcessMessage(json(R"({"type":"done"})"));
    run_for(p, 100000);
    assert(p.state_ == S::Ready && !p.audio_.capturing);
    run_for(p, 300000);
    assert(p.state_ == S::Recording && p.audio_.capturing && p.conversation_);

    // An empty transcript (noise held VAD open) quietly keeps listening.
    speak(p, kSecond, 1600000);
    assert(p.state_ == S::Transcribing);
    transcript_ready(p, "", true);
    run_for(p, 400000);
    assert(p.state_ == S::Recording && p.conversation_ && p.transcripts_sent == 1);

    // A click while listening mutes: nothing is sent and nothing re-arms.
    p.toggle_pending_ = true;
    run_for(p, 2 * kSecond);
    assert(!p.conversation_ && p.state_ == S::Ready && !p.audio_.capturing);
    assert(p.transcripts_sent == 1);

    // A click during a reply interrupts it, drops the audio and stands by.
    SabrinaProtocol reply;
    listening(reply);
    speak(reply, kSecond, 1600000);
    transcript_ready(reply, "tell me a long story");
    run_for(reply, 50000);
    audio_chunk(reply, 1, 9600);
    assert(reply.state_ == S::Speaking);
    reply.audio_.accept_playback = false;
    reply.toggle_pending_ = true;
    run_for(reply, 50000);
    assert(!reply.conversation_ && reply.playback_.empty());
    assert(std::find(reply.sent.begin(), reply.sent.end(), R"({"type":"interrupt"})") !=
           reply.sent.end());
    assert(reply.socket_ != nullptr && !reply.connected_);  // reconnecting at once
    connected(reply);
    run_for(reply, kSecond);
    assert(reply.state_ == S::Ready && !reply.audio_.capturing);

    // Two minutes with no speech ends voice mode by itself.
    SabrinaProtocol idle;
    listening(idle);
    run_for(idle, 119 * kSecond);
    assert(idle.conversation_ && idle.state_ == S::Recording);
    run_for(idle, 2 * kSecond);
    assert(!idle.conversation_ && idle.state_ == S::Ready && !idle.audio_.capturing);

    // A click while offline reconnects immediately instead of waiting out backoff.
    SabrinaProtocol offline;
    offline.turn_active_ = false;
    offline.state_ = S::Offline;
    offline.socket_ = nullptr;
    offline.retry_at_ = clock_us + 30 * kSecond;
    offline.toggle_pending_ = true;
    run_for(offline, 50000);
    assert(offline.conversation_ && offline.socket_ != nullptr);
    connected(offline);
    run_for(offline, 400000);
    assert(offline.state_ == S::Recording);

    std::cout << "PASS: voice mode toggle, pre-roll, endpointing, noise, auto re-arm, "
                 "mute, reply interrupt, idle shutoff and offline reconnect\n";
}
