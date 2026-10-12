// Queue consumption is modeled; worker, message handling and ACK methods are real.
struct Audio {
    static constexpr size_t capacity = PLAYBACK_QUEUE_CAPACITY;
    bool capturing = false, accept_playback = true, started = false;
    size_t played_samples = 0, consumed = 0, missing_samples = 0, current = 0, peak = 0;
    std::deque<size_t> queue;
    bool PushPcmPlayback(const int16_t*, size_t size, uint32_t) {
        assert(size <= 480);  // Interruption must retain 20 ms output blocks.
        if (!accept_playback || queue.size() >= capacity)
            return false;
        queue.push_back(size);
        peak = std::max(peak, queue.size());
        played_samples += size;
        started = true;
        return true;
    }
    void Advance(size_t total_samples) {
        size_t needed = std::min<size_t>(240, total_samples - consumed);
        while (needed) {
            if (!current && !queue.empty()) {
                current = queue.front();
                queue.pop_front();
            }
            if (!current) {
                if (started)
                    missing_samples += needed;
                return;
            }
            const size_t used = std::min(needed, current);
            needed -= used;
            current -= used;
            consumed += used;
        }
    }
    void StopPcmCaptureAndFlush() { capturing = false; }
    void ResetDecoder() { queue.clear(); }
    uint32_t PlaybackGeneration() { return 0; }
    bool IsPlaybackIdle() { return queue.empty() && !current; }
};
// TIMED_MAIN
void EnqueueChunk(SabrinaProtocol& p, int id, size_t bytes) {
    p.inbox_.push_back(json(
        "{\"type\":\"audio\",\"format\":\"pcm_24000\",\"chunk_id\":" + std::to_string(id) + "}"));
    p.inbox_.push_back({true, std::vector<uint8_t>(bytes, 0x2a)});
    // Real Run() accounts for inbox bytes when it removes each frame.
    p.inbox_bytes_ = 0;
    for (const auto& frame : p.inbox_)
        p.inbox_bytes_ += frame.bytes.size();
}

size_t Scenario(int delay_ticks, int chunks, bool stalled_speaker = false) {
    SabrinaProtocol p;
    connected(p);
    int delivered = 1, due = -1, ticks = 0, last_ack = 0;
    const size_t total_samples = (chunks - 1) * 4800 + 73;
    EnqueueChunk(p, 1, 9600);
    p.audio_.accept_playback = !stalled_speaker;
    tick = [&] {
        ++ticks;
        p.last_pong_ = clock_us;
        if (p.last_chunk_id_ != last_ack) {
            last_ack = p.last_chunk_id_;
            if (delivered < chunks)
                due = ticks + delay_ticks;
        }
        if (due >= 0 && ticks >= due) {
            ++delivered;
            EnqueueChunk(p, delivered, delivered == chunks ? 146 : 9600);
            due = -1;
        }
        if (stalled_speaker && ticks < 25) {
            assert(last_ack == 0 && delivered == 1);
            assert(p.audio_.queue.empty());
        } else {
            p.audio_.accept_playback = true;
            p.audio_.Advance(total_samples);
        }
        assert(p.audio_.queue.size() <= Audio::capacity);
        assert(p.playback_.size() <= 9600);
        if (p.audio_.consumed == total_samples || ticks >= chunks * 40 + 100)
            p.stopping_ = true;
    };
    p.Run();
    assert(!p.error && p.audio_.played_samples == total_samples);
    assert(p.audio_.consumed == total_samples);
    assert(p.audio_.queue.empty());  // Run's cancellation flushes queued audio.
    assert(p.audio_.peak <= Audio::capacity);
    std::cout << "delay_ms=" << delay_ticks * 10 << " chunks=" << chunks
              << " missing_samples=" << p.audio_.missing_samples << " peak_blocks=" << p.audio_.peak
              << std::endl;
    return p.audio_.missing_samples;
}

int main() {
    assert(Scenario(0, 20) == 0);
    assert(Scenario(8, 20) == 0);
    assert(Scenario(12, 20) == 0);
    assert(Scenario(16, 1500) == 0);  // Five minutes, ACK plus network/frame handling delay.
    assert(Scenario(8, 20, true) == 0);
}
