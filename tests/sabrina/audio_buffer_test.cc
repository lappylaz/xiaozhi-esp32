#include <cassert>
#include <cstring>
#include <iostream>
#include "sabrina_audio_buffer.h"

int main() {
    sabrina::ReleaseHangover hangover(240000);
    assert(!hangover.ShouldStop(true, 0));
    assert(!hangover.ShouldStop(false, 1000000));
    assert(!hangover.ShouldStop(true, 1100000));
    assert(!hangover.ShouldStop(true, 1400000));
    // The second release must receive a full tail, regardless of how old the
    // first release timestamp is.
    assert(!hangover.ShouldStop(false, 1500000));
    assert(!hangover.ShouldStop(false, 1739999));
    assert(hangover.ShouldStop(false, 1740000));
    // A new recording and a press/release consumed between worker polls both
    // explicitly reset the helper; a zero timestamp is a valid release time.
    hangover.Reset();
    assert(!hangover.ShouldStop(false, 0));
    assert(!hangover.ShouldStop(false, 239999));
    assert(hangover.ShouldStop(false, 240000));
    hangover.Reset();
    assert(!hangover.ShouldStop(false, 500000));

    const int16_t a[] = {1, 2, 3};
    sabrina::Recording recording(4);
    recording.Begin();
    assert(recording.Append(a, 3));
    auto first = recording.Freeze();
    recording.Begin();
    assert(recording.Append(a, 2));
    assert(first == std::vector<int16_t>({1, 2, 3}));
    assert(!recording.Append(a, 3));
    assert(recording.overflow());
    assert(recording.Freeze() == std::vector<int16_t>({1, 2}));
    assert(recording.Append(a, 3));  // frozen buffer ignores late callback

    // Pre-roll keeps only the newest samples, in order, and never grows a
    // shorter buffer.
    const int16_t c[] = {1, 2, 3, 4, 5, 6};
    sabrina::Recording preroll(8);
    preroll.Begin();
    assert(preroll.Append(c, 6));
    preroll.KeepLast(2);
    assert(preroll.size() == 2);
    preroll.KeepLast(4);
    assert(preroll.size() == 2);
    assert(preroll.Append(c, 6));  // trimming freed capacity for more speech
    assert(preroll.Freeze() == std::vector<int16_t>({5, 6, 1, 2, 3, 4, 5, 6}));

    // Voice mode turn taking: 1.5 s pause ends real speech; a blip is noise;
    // two minutes without speech ends voice mode.
    using Action = sabrina::Endpointer::Action;
    sabrina::Endpointer turn(sabrina::kEndOfTurnSilenceUs, sabrina::kVoiceIdleUs,
                             sabrina::kMinSpeechUs);
    turn.Arm(0);
    assert(turn.Update(false, 60000000) == Action::Listen && !turn.heard());
    assert(turn.Update(true, 60000000) == Action::Listen && turn.heard());
    assert(turn.Update(true, 61000000) == Action::Listen);
    assert(turn.Update(false, 61200000) == Action::Listen);
    // A pause shorter than the timeout continues the same turn.
    assert(turn.Update(false, 62000000) == Action::Listen);
    assert(turn.Update(true, 62100000) == Action::Listen);
    assert(turn.Update(false, 62300000) == Action::Listen);
    assert(turn.Update(false, 63799999) == Action::Listen);
    assert(turn.Update(false, 63800000) == Action::EndTurn);
    // A blip is forgotten and does not extend the idle window, which counts from Arm.
    turn.Arm(100000000);
    assert(turn.Update(true, 100000000) == Action::Listen);
    assert(turn.Update(false, 100100000) == Action::Listen);  // 100 ms blip
    assert(turn.Update(false, 101600000) == Action::Listen && !turn.heard());
    assert(turn.Update(false, 219999999) == Action::Listen);
    assert(turn.Update(false, 220000000) == Action::GoIdle);
    // Speech that spans the idle deadline is still a turn, not idle.
    turn.Arm(0);
    assert(turn.Update(true, 119000000) == Action::Listen);
    assert(turn.Update(false, 121000000) == Action::Listen);
    assert(turn.Update(false, 122500000) == Action::EndTurn);

    sabrina::MessageAssembler assembler(8);
    const uint8_t b[] = {1, 2, 3, 4};
    bool done;
    assert(assembler.Add(2, false, 4, 0, b, 2, done) && !done);
    assert(assembler.Add(2, false, 4, 2, b + 2, 2, done) && !done);
    assert(assembler.Add(9, true, 0, 0, b, 0, done) && !done);
    assert(assembler.Add(0, true, 2, 0, b, 2, done) && done);
    assert(assembler.binary());
    assert(assembler.Take() == std::vector<uint8_t>({1, 2, 3, 4, 1, 2}));
    assert(!assembler.Add(0, true, 2, 0, b, 2, done));
    assert(std::strcmp(assembler.failure_reason(), "frame_assembly") == 0);
    assembler.Reset();
    assert(assembler.Add(1, false, 4, 0, b, 4, done));
    assert(!assembler.Add(1, true, 4, 0, b, 4, done));
    assembler.Reset();
    assert(!assembler.Add(2, true, 4, 1, b, 2, done));
    assembler.Reset();
    assert(!assembler.Add(2, true, 1, 2, b, 1, done));
    assembler.Reset();
    assert(assembler.Add(2, false, 4, 0, b, 4, done));
    assert(assembler.Add(0, false, 4, 0, b, 4, done));
    assert(!assembler.Add(0, true, 4, 0, b, 4, done));
    assert(std::strcmp(assembler.failure_reason(), "message_byte_cap") == 0);
    assert(assembler.size() == 8);
    std::cout << "recording ownership/overflow and websocket fragmentation tests passed\n";
}
