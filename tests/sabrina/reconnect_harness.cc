// Fault injection uses the real callback/worker/failure paths. Boundaries model
// time, transport and audio; no live network or speaker claim is made here.
void run_for(SabrinaProtocol& p, int iterations, std::function<void()> step = [] {}) {
    p.stopping_ = false;
    p.iterations = 0;
    tick = [&] {
        step();
        if (++p.iterations >= iterations)
            p.stopping_ = true;
    };
    p.Run();
}
void drop(SabrinaProtocol& p, int event = WEBSOCKET_EVENT_DISCONNECTED, int http = 0, int close = 0,
          int tls = 0) {
    esp_websocket_event_data_t data;
    data.error_handle.esp_ws_handshake_status_code = http;
    data.error_handle.esp_tls_cert_verify_flags = tls;
    data.close_status_code = close;
    SabrinaProtocol::SocketEvent(&p, 0, event, &data);
}
int main() {
    // An idle disconnect / clean close must reconnect with the saved session,
    // without touching capture or resubmitting a possibly executed request.
    for (int event : {WEBSOCKET_EVENT_DISCONNECTED, WEBSOCKET_EVENT_CLOSED}) {
        SabrinaProtocol p;
        connected(p);
        p.last_seq_ = 99;
        drop(p, event);
        bool resumed = false;
        connection_urls.clear();
        run_for(p, 200, [&] {
            if (p.socket_ && !p.connected_) {
                assert(p.last_seq_ == -1 && p.last_chunk_id_ == 0);
                connected(p);
                resumed = true;
            }
            assert(!p.audio_.capturing && p.transcripts_sent == 0);
        });
        if (!resumed) {
            std::cerr << "FAIL: transport drop never reconnects without button/power cycle\n";
            return 1;
        }
        assert(connection_urls.size() == 1);
        assert(connection_urls.front().find("session_id=session-1") != std::string::npos);
        assert(p.retry_delay_seconds_ == 1);
    }
    // Persistent outages back off at 1/2/4/8/16/30, then remain capped.
    SabrinaProtocol outage;
    connected(outage);
    connection_attempts.clear();
    auto start = clock_us;
    drop(outage);
    run_for(outage, 12500, [&] {
        assert(!outage.audio_.capturing);
        if (outage.socket_)
            drop(outage);
    });
    assert(connection_attempts.size() == 8);
    int64_t previous = start;
    for (size_t i = 0; i < connection_attempts.size(); ++i) {
        int64_t gap = connection_attempts[i] - previous;
        int expected = std::min(1 << i, 30);
        assert(gap >= expected * kSecond && gap <= expected * kSecond + 10000);
        previous = connection_attempts[i];
    }
    // Auth/protocol/certificate refusals must never turn into retry storms.
    for (int failure : {401, 403, 4001, 4002, -1}) {
        SabrinaProtocol rejected;
        connected(rejected);
        connection_attempts.clear();
        drop(rejected, WEBSOCKET_EVENT_ERROR, failure < 1000 && failure > 0 ? failure : 0,
             failure >= 1000 ? failure : 0, failure == -1 ? 1 : 0);
        // A subsequent generic disconnect must not erase rejection evidence.
        drop(rejected);
        run_for(rejected, 4000);
        assert(connection_attempts.empty() && rejected.error && !rejected.reconnect_requested_);
    }
    // Idle heartbeat, write failure and handshake timeout all recover.
    for (int failure = 0; failure < 3; ++failure) {
        SabrinaProtocol p;
        connected(p);
        if (failure == 0) {
            p.last_pong_ = clock_us - 46 * kSecond;
        }
        if (failure == 1) {
            p.last_ping_ = clock_us - 16 * kSecond;
            p.send_ok = false;
        }
        if (failure == 2) {
            p.connected_ = false;
            p.connection_at_ = clock_us - 21 * kSecond;
        }
        connection_attempts.clear();
        run_for(p, 200, [&] {
            p.send_ok = true;
            if (p.socket_ && !p.connected_)
                connected(p);
        });
        assert(connection_attempts.size() == 1 && !p.error);
    }
    // Dropped recording/transcription/playback is invalidated, never replayed.
    for (auto state : {SabrinaProtocol::State::Recording, SabrinaProtocol::State::Transcribing,
                       SabrinaProtocol::State::Speaking}) {
        SabrinaProtocol p;
        connected(p);
        p.state_ = state;
        p.audio_.capturing = state == SabrinaProtocol::State::Recording;
        p.stt_done_ = true;
        p.stt_text_ = "do not replay this action";
        p.playback_ = {1, 2};
        p.pending_chunk_id_ = 8;
        p.expect_binary_ = true;
        drop(p);
        run_for(p, 200, [&] {
            assert(!p.audio_.capturing && p.playback_.empty() && !p.expect_binary_);
            if (p.socket_ && !p.connected_)
                connected(p);
            assert(p.transcripts_sent == 0);
        });
        assert(p.transcripts_sent == 0 && p.queued_bytes_ == 0);
    }
    // New press concurrent with old disconnect still starts capture immediately.
    SabrinaProtocol press;
    connected(press);
    drop(press);
    press.pressed_ = press.press_pending_ = true;
    run_for(press, 1, [&] { assert(press.audio_.capturing && press.socket_); });
    // Explicit close cancels any retry request (and does not restart capture).
    SabrinaProtocol closed;
    connected(closed);
    drop(closed);
    closed.close_pending_ = true;
    connection_attempts.clear();
    run_for(closed, 4000);
    assert(connection_attempts.empty() && !closed.audio_.capturing);
    std::cout << "PASS: disconnect/clean close, session resume, bounded backoff, auth/TLS refusal, "
                 "heartbeat/send/handshake, stale turn invalidation and concurrent press\n";
}
