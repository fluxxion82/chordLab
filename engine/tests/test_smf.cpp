#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/export.h"
#include "chordlab/smf.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace chordlab;

namespace {

using Bytes = std::vector<std::uint8_t>;

const Chord kC{0, &chordType("maj")};

/// A document holding `count` C major chords of four beats at velocity 100.
Document cMajorBars(int count) {
    DocumentStore store;
    for (int i = 0; i < count; ++i) {
        store.appendChord(kC, 4.0, 100);
    }
    return store.doc();
}

/// The golden file from the plan: one C chord, one bar, 120 bpm, 4/4, 480 tpqn, channel 0.
const Bytes kGolden = {
    'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
    'M', 'T', 'r', 'k', 0, 0, 0, 0x2C,                                        // 44 bytes
    0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,                                 // tempo 500000 us
    0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08,                           // 4/4
    0x00, 0x90, 0x3C, 0x64, 0x00, 0x90, 0x40, 0x64, 0x00, 0x90, 0x43, 0x64,   // ons at tick 0
    0x8F, 0x00, 0x80, 0x3C, 0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0x80, 0x43, 0x00,  // offs at tick 1920
    0x00, 0xFF, 0x2F, 0x00,                                                   // end of track
};

std::string hex(const Bytes& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out << (i == 0 ? "" : " ") << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return out.str();
}

// --- An independent reader -------------------------------------------------------------------
//
// Walks a produced file byte by byte with its own understanding of the format, so that the tests do not
// trust the writer's bookkeeping (chunk lengths, delta encoding, event framing).

struct ParsedEvent {
    std::uint32_t tick;        // absolute, accumulated from the deltas
    std::uint8_t status;       // 0xFF for a meta event
    std::uint8_t metaType;     // meta events only
    Bytes data;                // channel data bytes, or the meta payload
};

struct Parsed {
    bool ok = false;
    std::string error;
    int format = -1;
    int ntrks = -1;
    int division = -1;
    std::uint32_t trackLength = 0;
    std::size_t trackBytesWalked = 0;
    std::vector<ParsedEvent> events;
};

std::uint32_t be32(const Bytes& b, std::size_t at) {
    return (std::uint32_t{b[at]} << 24) | (std::uint32_t{b[at + 1]} << 16) | (std::uint32_t{b[at + 2]} << 8) |
           std::uint32_t{b[at + 3]};
}

int be16(const Bytes& b, std::size_t at) {
    return (int{b[at]} << 8) | int{b[at + 1]};
}

Parsed parse(const Bytes& b) {
    Parsed p;
    const auto fail = [&p](const std::string& message) {
        p.ok = false;
        p.error = message;
        return p;
    };
    if (b.size() < 14) {
        return fail("too short for a header");
    }
    if (!(b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd')) {
        return fail("no MThd");
    }
    if (be32(b, 4) != 6) {
        return fail("header length is not 6");
    }
    p.format = be16(b, 8);
    p.ntrks = be16(b, 10);
    p.division = be16(b, 12);

    std::size_t pos = 14;
    if (b.size() < pos + 8) {
        return fail("too short for a track header");
    }
    if (!(b[pos] == 'M' && b[pos + 1] == 'T' && b[pos + 2] == 'r' && b[pos + 3] == 'k')) {
        return fail("no MTrk");
    }
    p.trackLength = be32(b, pos + 4);
    pos += 8;
    const std::size_t trackStart = pos;
    const std::size_t trackEnd = trackStart + p.trackLength;
    if (trackEnd != b.size()) {
        return fail("track length does not reach the end of the file");
    }

    const auto readVlq = [&](std::uint32_t& value) -> bool {
        value = 0;
        for (int n = 0; n < 4; ++n) {
            if (pos >= trackEnd) {
                return false;
            }
            const std::uint8_t c = b[pos++];
            value = (value << 7) | (c & 0x7Fu);
            if ((c & 0x80u) == 0) {
                return true;
            }
        }
        return false;  // a fifth continuation byte
    };

    std::uint32_t tick = 0;
    bool sawEot = false;
    while (pos < trackEnd) {
        if (sawEot) {
            return fail("bytes after end-of-track");
        }
        std::uint32_t delta = 0;
        if (!readVlq(delta)) {
            return fail("bad delta");
        }
        tick += delta;
        if (pos >= trackEnd) {
            return fail("truncated event");
        }
        const std::uint8_t status = b[pos++];
        ParsedEvent e{tick, status, 0, {}};
        if (status == 0xFF) {
            if (pos >= trackEnd) {
                return fail("truncated meta event");
            }
            e.metaType = b[pos++];
            std::uint32_t length = 0;
            if (!readVlq(length)) {
                return fail("bad meta length");
            }
            if (pos + length > trackEnd) {
                return fail("meta payload overruns the track");
            }
            e.data.assign(b.begin() + static_cast<std::ptrdiff_t>(pos),
                          b.begin() + static_cast<std::ptrdiff_t>(pos + length));
            pos += length;
            if (e.metaType == 0x2F) {
                if (length != 0) {
                    return fail("end-of-track with a payload");
                }
                sawEot = true;
            }
        } else if (status == 0xF0 || status == 0xF7) {
            return fail("unexpected sysex");
        } else if (status >= 0xF0) {
            return fail("system common or real-time status in a track");
        } else if ((status & 0x80u) != 0) {
            const int dataCount = ((status & 0xF0u) == 0xC0u || (status & 0xF0u) == 0xD0u) ? 1 : 2;
            for (int i = 0; i < dataCount; ++i) {
                if (pos >= trackEnd) {
                    return fail("truncated channel event");
                }
                if ((b[pos] & 0x80u) != 0) {
                    return fail("data byte with its top bit set");
                }
                e.data.push_back(b[pos++]);
            }
        } else {
            return fail("running status");
        }
        p.events.push_back(e);
    }
    p.trackBytesWalked = pos - trackStart;
    if (!sawEot) {
        return fail("no end-of-track");
    }
    p.ok = true;
    return p;
}

/// Parses `bytes` and checks everything every file the writer produces must satisfy. Returns the parse
/// for the test to inspect further.
Parsed requireWellFormed(const Bytes& bytes, int tpqn = 480) {
    const Parsed parsed = parse(bytes);
    INFO("parse error: " << parsed.error);
    INFO("bytes: " << hex(bytes));
    REQUIRE(parsed.ok);
    CHECK(parsed.format == 0);
    CHECK(parsed.ntrks == 1);
    CHECK(parsed.division == tpqn);
    CHECK(parsed.trackBytesWalked == parsed.trackLength);
    REQUIRE(parsed.events.size() >= 3);
    // Tempo then time signature, both at tick 0.
    CHECK(parsed.events[0].tick == 0);
    CHECK(parsed.events[0].status == 0xFF);
    CHECK(parsed.events[0].metaType == 0x51);
    CHECK(parsed.events[0].data.size() == 3);
    CHECK(parsed.events[1].tick == 0);
    CHECK(parsed.events[1].status == 0xFF);
    CHECK(parsed.events[1].metaType == 0x58);
    CHECK(parsed.events[1].data.size() == 4);
    // A track name, if there is one, comes third, at tick 0, and once.
    int names = 0;
    for (std::size_t i = 0; i < parsed.events.size(); ++i) {
        const ParsedEvent& e = parsed.events[i];
        if (e.status == 0xFF && e.metaType == 0x03) {
            ++names;
            CHECK(i == 2);
            CHECK(e.tick == 0);
        }
    }
    CHECK(names <= 1);
    // End of track last, exactly once.
    CHECK(parsed.events.back().status == 0xFF);
    CHECK(parsed.events.back().metaType == 0x2F);
    int eots = 0;
    for (const ParsedEvent& e : parsed.events) {
        if (e.status == 0xFF && e.metaType == 0x2F) {
            ++eots;
        }
    }
    CHECK(eots == 1);
    return parsed;
}

/// The channel (non-meta) events of a parse, in file order.
std::vector<ParsedEvent> channelEvents(const Parsed& parsed) {
    std::vector<ParsedEvent> out;
    for (const ParsedEvent& e : parsed.events) {
        if (e.status < 0xF0) {
            out.push_back(e);
        }
    }
    return out;
}

/// The payload of the track name meta event (FF 03) as text, or "" when the file has none.
std::string trackNameOf(const Parsed& parsed) {
    for (const ParsedEvent& e : parsed.events) {
        if (e.status == 0xFF && e.metaType == 0x03) {
            return std::string(e.data.begin(), e.data.end());
        }
    }
    return {};
}

Bytes readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

long processId() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

/// A path under the system temp directory that no other test or process writes: the name carries the
/// process id and a per-process counter, so parallel runs and a crashed earlier run cannot collide. The
/// file is removed when the guard goes out of scope, which a failed assertion (an exception) also reaches.
struct TempFile {
    std::filesystem::path path;

    explicit TempFile(const char* tag) {
        static int counter = 0;
        std::ostringstream name;
        name << "chordlab-test_smf-" << tag << "-" << processId() << "-" << ++counter << ".mid";
        path = std::filesystem::temp_directory_path() / name.str();
        remove();
    }

    ~TempFile() {
        remove();
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    std::string string() const {
        return path.string();
    }

private:
    void remove() const noexcept {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

/// The events of `compile(cMajorBars(1))`, which are also what the golden bytes encode: one C major chord
/// of four beats at velocity 100, voiced C4 E4 G4 (60, 64, 67). For hand-built `Compiled` values.
std::vector<NoteEvent> oneBarEvents() {
    return {
        {0.0, true, 60, 100}, {0.0, true, 64, 100}, {0.0, true, 67, 100},
        {4.0, false, 60, 0},  {4.0, false, 64, 0},  {4.0, false, 67, 0},
    };
}

}  // namespace

// --- The reader itself -------------------------------------------------------------------------

TEST_CASE("test reader: rejects system common and real-time statuses inside the track") {
    // The writer never emits these; the check guards the reader, so a file that smuggled one through
    // as a two-byte channel event could not pass `requireWellFormed`.
    REQUIRE(parse(kGolden).ok);
    for (const int status : {0xF0, 0xF1, 0xF6, 0xF7, 0xF8, 0xFE}) {
        CAPTURE(status);
        Bytes bytes = kGolden;
        const auto firstOn = std::find(bytes.begin() + 22, bytes.end(), std::uint8_t{0x90});
        REQUIRE(firstOn != bytes.end());
        *firstOn = static_cast<std::uint8_t>(status);
        CHECK_FALSE(parse(bytes).ok);
    }
}

// --- vlq ---------------------------------------------------------------------------------------

TEST_CASE("vlq: plan values") {
    CHECK(vlq(0) == Bytes{0x00});
    CHECK(vlq(127) == Bytes{0x7F});
    CHECK(vlq(128) == Bytes{0x81, 0x00});
    CHECK(vlq(1920) == Bytes{0x8F, 0x00});
    CHECK(vlq(16383) == Bytes{0xFF, 0x7F});
    CHECK(vlq(0x0FFFFFFF) == Bytes{0xFF, 0xFF, 0xFF, 0x7F});
}

TEST_CASE("vlq: boundaries between byte counts") {
    CHECK(vlq(1) == Bytes{0x01});
    CHECK(vlq(16384) == Bytes{0x81, 0x80, 0x00});
    CHECK(vlq(0x1FFFFF) == Bytes{0xFF, 0xFF, 0x7F});
    CHECK(vlq(0x200000) == Bytes{0x81, 0x80, 0x80, 0x00});
    CHECK(vlq(960) == Bytes{0x87, 0x40});
}

// --- Golden ------------------------------------------------------------------------------------

TEST_CASE("writeSmf0: golden bytes for one C chord, one bar, 120 bpm") {
    const Compiled compiled = compile(cMajorBars(1));
    CHECK(compiled.events == oneBarEvents());
    const Bytes actual = writeSmf0(compiled, 120.0);
    INFO("actual:   " << hex(actual));
    INFO("expected: " << hex(kGolden));
    CHECK(actual == kGolden);
    requireWellFormed(actual);
}

TEST_CASE("writeSmf0: matches the committed fixture cmaj-1bar.mid") {
    const std::filesystem::path path = std::filesystem::path(CHORDLAB_FIXTURES_DIR) / "cmaj-1bar.mid";
    const Bytes fixture = readAll(path);
    CHECK(fixture == kGolden);
    CHECK(writeSmf0(compile(cMajorBars(1)), 120.0) == fixture);
    requireWellFormed(fixture);
}

TEST_CASE("writeSmf0: a track name is a FF 03 meta event after the time signature") {
    const Compiled compiled = compile(cMajorBars(1));
    const Bytes actual = writeSmf0(compiled, 120.0, 4, 4, 480, 0, "C Am F G7");
    // The golden file with the 13-byte meta event spliced in after the time signature, the track 13 longer.
    Bytes expected = kGolden;
    expected[14 + 7] = 0x2C + 13;
    const Bytes meta = {0x00, 0xFF, 0x03, 0x09, 'C', ' ', 'A', 'm', ' ', 'F', ' ', 'G', '7'};
    expected.insert(expected.begin() + 14 + 8 + 7 + 8, meta.begin(), meta.end());
    INFO("actual:   " << hex(actual));
    INFO("expected: " << hex(expected));
    CHECK(actual == expected);
    const Parsed parsed = requireWellFormed(actual);
    CHECK(trackNameOf(parsed) == "C Am F G7");
    CHECK(channelEvents(parsed).size() == 6);
    CHECK(parsed.events.back().tick == 1920);
}

TEST_CASE("writeSmf0: no track name writes no meta event; UTF-8 and long names are carried byte for byte") {
    const Compiled compiled = compile(cMajorBars(1));
    CHECK(writeSmf0(compiled, 120.0, 4, 4, 480, 0, "") == kGolden);
    CHECK(writeSmf0(compiled, 120.0, 4, 4, 480, 0, {}) == kGolden);
    CHECK(writeSmf0(compiled, 120.0, 4, 4, 480, 0) == kGolden);

    const std::string utf8 = std::string("Bb") + "\xCE\x94" + "7 " + "\xE2\x80\xA6";  // "BbΔ7 …"
    CHECK(trackNameOf(requireWellFormed(writeSmf0(compiled, 120.0, 4, 4, 480, 0, utf8))) == utf8);

    // 200 bytes: the meta length takes two bytes, 81 48.
    const std::string longName(200, 'n');
    const Bytes file = writeSmf0(compiled, 120.0, 4, 4, 480, 0, longName);
    const Parsed parsed = requireWellFormed(file);
    CHECK(trackNameOf(parsed) == longName);
    const std::size_t at = 14 + 8 + 7 + 8;  // 00 FF 03 81 48 n n n ...
    REQUIRE(file.size() > at + 5);
    CHECK(file[at] == 0x00);
    CHECK(file[at + 1] == 0xFF);
    CHECK(file[at + 2] == 0x03);
    CHECK(file[at + 3] == 0x81);
    CHECK(file[at + 4] == 0x48);
    CHECK(file[at + 5] == 'n');
}

TEST_CASE("writeSmf0: the track name length is encoded exactly across the VLQ byte boundaries") {
    const Compiled compiled = compile(cMajorBars(1));
    struct Case {
        std::size_t length;
        Bytes vlqBytes;
    };
    const Case cases[] = {
        {127, {0x7F}},
        {128, {0x81, 0x00}},
        {16383, {0xFF, 0x7F}},
        {16384, {0x81, 0x80, 0x00}},
    };
    for (const Case& c : cases) {
        CAPTURE(c.length);
        const std::string name(c.length, 'n');
        const Bytes file = writeSmf0(compiled, 120.0, 4, 4, 480, 0, name);
        const Parsed parsed = requireWellFormed(file);
        CHECK(trackNameOf(parsed) == name);
        const std::size_t at = 14 + 8 + 7 + 8;  // 00 FF 03 <length> name...
        REQUIRE(file.size() > at + 3 + c.vlqBytes.size() + c.length);
        CHECK(Bytes(file.begin() + at, file.begin() + at + 3) == Bytes{0x00, 0xFF, 0x03});
        CHECK(Bytes(file.begin() + at + 3, file.begin() + at + 3 + c.vlqBytes.size()) == c.vlqBytes);
        CHECK(file[at + 3 + c.vlqBytes.size()] == 'n');
        // The track length counts the meta event: the golden 44 bytes plus 3 + length bytes + name.
        CHECK(parsed.trackLength == static_cast<std::uint32_t>(0x2C + 3 + c.vlqBytes.size() + c.length));
        CHECK(file.size() == 14 + 8 + parsed.trackLength);
    }
}

// --- Track contents ----------------------------------------------------------------------------

TEST_CASE("writeSmf0: end-of-track sits at endBeat so trailing silence survives") {
    Compiled compiled;
    compiled.events = oneBarEvents();
    compiled.endBeat = 6.0;  // two beats of silence after the last off
    const Bytes bytes = writeSmf0(compiled, 120.0);
    const Parsed parsed = requireWellFormed(bytes);

    CHECK(parsed.events.back().tick == 2880);
    // The EOT delta is 960 from the offs at 1920, encoded 0x87 0x40, immediately before FF 2F 00.
    REQUIRE(bytes.size() >= 5);
    const Bytes tail(bytes.end() - 5, bytes.end());
    CHECK(tail == Bytes{0x87, 0x40, 0xFF, 0x2F, 0x00});
}

TEST_CASE("writeSmf0: end-of-track moves out to the last event when endBeat is earlier") {
    Compiled compiled;
    compiled.events = oneBarEvents();
    compiled.endBeat = 2.0;  // before the offs at beat 4
    const Bytes bytes = writeSmf0(compiled, 120.0);
    const Parsed parsed = requireWellFormed(bytes);

    CHECK(parsed.events.back().tick == 1920);
    const Bytes tail(bytes.end() - 4, bytes.end());
    CHECK(tail == Bytes{0x00, 0xFF, 0x2F, 0x00});
}

TEST_CASE("writeSmf0: two back-to-back chords put the offs before the ons at tick 1920") {
    const Bytes bytes = writeSmf0(compile(cMajorBars(2)), 120.0);
    const Parsed parsed = requireWellFormed(bytes);
    const std::vector<ParsedEvent> channel = channelEvents(parsed);
    REQUIRE(channel.size() == 12);

    std::vector<ParsedEvent> at1920;
    for (const ParsedEvent& e : channel) {
        if (e.tick == 1920) {
            at1920.push_back(e);
        }
    }
    REQUIRE(at1920.size() == 6);
    for (std::size_t i = 0; i < 3; ++i) {
        CAPTURE(i);
        CHECK(at1920[i].status == 0x80);
        CHECK(at1920[i + 3].status == 0x90);
        CHECK(at1920[i].data[0] == at1920[i + 3].data[0]);  // same pitch, off then on
    }
    CHECK(at1920[0].data[0] == 60);  // the default close voicing of C major: C4 E4 G4
    CHECK(at1920[1].data[0] == 64);
    CHECK(at1920[2].data[0] == 67);
    CHECK(parsed.events.back().tick == 3840);
}

TEST_CASE("writeSmf0: events that collapse onto one tick are re-sorted off before on") {
    // Sorted by beat the on comes first; both round to tick 0, where the off must precede it.
    Compiled compiled;
    compiled.events = {
        {0.0, true, 64, 100},
        {0.0001, false, 60, 0},
        {1.0, false, 64, 0},
    };
    compiled.endBeat = 1.0;
    const Bytes bytes = writeSmf0(compiled, 120.0);
    const Parsed parsed = requireWellFormed(bytes);
    const std::vector<ParsedEvent> channel = channelEvents(parsed);
    REQUIRE(channel.size() == 3);
    CHECK(channel[0].tick == 0);
    CHECK(channel[0].status == 0x80);
    CHECK(channel[0].data == Bytes{60, 0});
    CHECK(channel[1].tick == 0);
    CHECK(channel[1].status == 0x90);
    CHECK(channel[1].data == Bytes{64, 100});
    CHECK(channel[2].tick == 480);
    CHECK(channel[2].status == 0x80);
    CHECK(channel[2].data == Bytes{64, 0});
}

TEST_CASE("writeSmf0: ticks are rounded from beats before the deltas are taken") {
    // 0.3 + 0.3 + 0.3 beats: ticks 144, 288, 432 from the absolute beats, not from accumulated deltas.
    DocumentStore store;
    for (int i = 0; i < 3; ++i) {
        store.appendChord(kC, 0.3, 100);
    }
    const Bytes bytes = writeSmf0(compile(store.doc()), 120.0);
    const Parsed parsed = requireWellFormed(bytes);
    const std::vector<ParsedEvent> channel = channelEvents(parsed);
    REQUIRE(channel.size() == 18);
    const std::vector<std::uint32_t> expectedTicks{0, 0, 0, 144, 144, 144, 144, 144, 144,
                                                   288, 288, 288, 288, 288, 288, 432, 432, 432};
    for (std::size_t i = 0; i < channel.size(); ++i) {
        CAPTURE(i);
        CHECK(channel[i].tick == expectedTicks[i]);
    }
    CHECK(parsed.events.back().tick == 432);
}

TEST_CASE("writeSmf0: a third-of-a-beat grid rounds each boundary from its own absolute beat") {
    // 0.333 beats at 480 tpqn is 159.84 ticks. The boundaries at 0.333, 0.666, 0.999 and 1.332 beats
    // round to 160, 320, 480 and 639: the last is not 4 x 160 = 640, because each tick comes from the
    // absolute beat, never from a running sum of rounded deltas.
    DocumentStore store;
    for (int i = 0; i < 4; ++i) {
        store.appendChord(kC, 0.333, 100);
    }
    const Bytes bytes = writeSmf0(compile(store.doc()), 120.0);
    const Parsed parsed = requireWellFormed(bytes);
    const std::vector<ParsedEvent> channel = channelEvents(parsed);
    REQUIRE(channel.size() == 24);
    const std::vector<std::uint32_t> expectedTicks{0,   0,   0,   160, 160, 160, 160, 160, 160, 320, 320, 320,
                                                   320, 320, 320, 480, 480, 480, 480, 480, 480, 639, 639, 639};
    for (std::size_t i = 0; i < channel.size(); ++i) {
        CAPTURE(i);
        CHECK(channel[i].tick == expectedTicks[i]);
    }
    CHECK(parsed.events.back().tick == 639);
}

TEST_CASE("writeSmf0: an empty progression is tempo, time signature and end-of-track at tick 0") {
    const Bytes bytes = writeSmf0(compile(Document{}), 120.0);
    const Parsed parsed = requireWellFormed(bytes);
    CHECK(parsed.events.size() == 3);
    CHECK(parsed.trackLength == 19);
    CHECK(parsed.events.back().tick == 0);
}

TEST_CASE("writeSmf0: an empty event list still honours endBeat") {
    Compiled compiled;
    compiled.endBeat = 4.0;
    const Bytes bytes = writeSmf0(compiled, 120.0);
    const Parsed parsed = requireWellFormed(bytes);
    CHECK(parsed.events.size() == 3);
    CHECK(parsed.events.back().tick == 1920);
}

// --- Parameters --------------------------------------------------------------------------------

TEST_CASE("writeSmf0: tempo bytes") {
    SUBCASE("100 bpm is 600000 us") {
        const Bytes bytes = writeSmf0(compile(cMajorBars(1)), 100.0);
        const Parsed parsed = requireWellFormed(bytes);
        CHECK(parsed.events[0].data == Bytes{0x09, 0x27, 0xC0});
    }
    SUBCASE("120 bpm is 500000 us") {
        const Parsed parsed = requireWellFormed(writeSmf0(compile(cMajorBars(1)), 120.0));
        CHECK(parsed.events[0].data == Bytes{0x07, 0xA1, 0x20});
    }
    SUBCASE("a non-integer tempo is rounded") {
        // 60e6 / 128.5 = 466926.07 -> 466926 = 0x071FEE
        const Parsed parsed = requireWellFormed(writeSmf0(compile(cMajorBars(1)), 128.5));
        CHECK(parsed.events[0].data == Bytes{0x07, 0x1F, 0xEE});
    }
}

TEST_CASE("writeSmf0: time signature bytes") {
    SUBCASE("den 8 gives dd 3") {
        const Parsed parsed = requireWellFormed(writeSmf0(compile(cMajorBars(1)), 120.0, 6, 8));
        CHECK(parsed.events[1].data == Bytes{0x06, 0x03, 0x18, 0x08});
    }
    SUBCASE("den 1 gives dd 0") {
        const Parsed parsed = requireWellFormed(writeSmf0(compile(cMajorBars(1)), 120.0, 1, 1));
        CHECK(parsed.events[1].data == Bytes{0x01, 0x00, 0x18, 0x08});
    }
    SUBCASE("den 128 gives dd 7") {
        const Parsed parsed = requireWellFormed(writeSmf0(compile(cMajorBars(1)), 120.0, 255, 128));
        CHECK(parsed.events[1].data == Bytes{0xFF, 0x07, 0x18, 0x08});
    }
}

TEST_CASE("writeSmf0: the channel goes into every status byte") {
    const Bytes bytes = writeSmf0(compile(cMajorBars(1)), 120.0, 4, 4, 480, 9);
    const Parsed parsed = requireWellFormed(bytes);
    const std::vector<ParsedEvent> channel = channelEvents(parsed);
    REQUIRE(channel.size() == 6);
    for (std::size_t i = 0; i < 3; ++i) {
        CAPTURE(i);
        CHECK(channel[i].status == 0x99);
        CHECK(channel[i + 3].status == 0x89);
    }
    const Bytes fifteen = writeSmf0(compile(cMajorBars(1)), 120.0, 4, 4, 480, 15);
    CHECK(channelEvents(requireWellFormed(fifteen))[0].status == 0x9F);
}

TEST_CASE("writeSmf0: tpqn sets the division and scales the ticks") {
    const Bytes bytes = writeSmf0(compile(cMajorBars(1)), 120.0, 4, 4, 96);
    const Parsed parsed = requireWellFormed(bytes, 96);
    CHECK(bytes[12] == 0x00);
    CHECK(bytes[13] == 0x60);
    const std::vector<ParsedEvent> channel = channelEvents(parsed);
    REQUIRE(channel.size() == 6);
    CHECK(channel[3].tick == 384);
    CHECK(parsed.events.back().tick == 384);

    const Bytes max = writeSmf0(compile(cMajorBars(1)), 120.0, 4, 4, 32767);
    const Parsed parsedMax = requireWellFormed(max, 32767);
    CHECK(parsedMax.events.back().tick == 4u * 32767u);
}

// --- Rejected parameters -----------------------------------------------------------------------

TEST_CASE("writeSmf0: a denominator that is not a power of two in 1..128 gives an empty vector") {
    const Compiled compiled = compile(cMajorBars(1));
    CHECK(writeSmf0(compiled, 120.0, 4, 3).empty());
    CHECK(writeSmf0(compiled, 120.0, 4, 0).empty());
    CHECK(writeSmf0(compiled, 120.0, 4, -4).empty());
    CHECK(writeSmf0(compiled, 120.0, 4, 6).empty());
    CHECK(writeSmf0(compiled, 120.0, 4, 256).empty());
}

TEST_CASE("writeSmf0: a numerator outside 1..255 gives an empty vector") {
    const Compiled compiled = compile(cMajorBars(1));
    CHECK(writeSmf0(compiled, 120.0, 0, 4).empty());
    CHECK(writeSmf0(compiled, 120.0, -1, 4).empty());
    CHECK(writeSmf0(compiled, 120.0, 256, 4).empty());
    CHECK_FALSE(writeSmf0(compiled, 120.0, 255, 4).empty());
}

TEST_CASE("writeSmf0: a tempo that is not finite and positive gives an empty vector") {
    const Compiled compiled = compile(cMajorBars(1));
    CHECK(writeSmf0(compiled, 0.0).empty());
    CHECK(writeSmf0(compiled, -120.0).empty());
    CHECK(writeSmf0(compiled, std::numeric_limits<double>::quiet_NaN()).empty());
    CHECK(writeSmf0(compiled, std::numeric_limits<double>::infinity()).empty());
    // So slow that the microseconds per quarter would not fit the three tempo bytes.
    CHECK(writeSmf0(compiled, 1.0).empty());
}

TEST_CASE("writeSmf0: tpqn outside 1..32767 gives an empty vector") {
    const Compiled compiled = compile(cMajorBars(1));
    CHECK(writeSmf0(compiled, 120.0, 4, 4, 0).empty());
    CHECK(writeSmf0(compiled, 120.0, 4, 4, -480).empty());
    CHECK(writeSmf0(compiled, 120.0, 4, 4, 32768).empty());
    CHECK_FALSE(writeSmf0(compiled, 120.0, 4, 4, 1).empty());
}

TEST_CASE("writeSmf0: a channel outside 0..15 gives an empty vector") {
    const Compiled compiled = compile(cMajorBars(1));
    CHECK(writeSmf0(compiled, 120.0, 4, 4, 480, -1).empty());
    CHECK(writeSmf0(compiled, 120.0, 4, 4, 480, 16).empty());
}

TEST_CASE("writeSmf0: events that cannot be encoded give an empty vector") {
    Compiled good;
    good.events = oneBarEvents();
    good.endBeat = 4.0;
    REQUIRE_FALSE(writeSmf0(good, 120.0).empty());

    SUBCASE("pitch outside 0..127") {
        Compiled c = good;
        c.events[0].pitch = 128;
        CHECK(writeSmf0(c, 120.0).empty());
        c.events[0].pitch = -1;
        CHECK(writeSmf0(c, 120.0).empty());
    }
    SUBCASE("velocity outside 0..127") {
        Compiled c = good;
        c.events[0].velocity = 128;
        CHECK(writeSmf0(c, 120.0).empty());
        c.events[0].velocity = -1;
        CHECK(writeSmf0(c, 120.0).empty());
    }
    SUBCASE("a beat that is negative, NaN or infinite") {
        Compiled c = good;
        c.events[0].beat = -1.0;
        CHECK(writeSmf0(c, 120.0).empty());
        c.events[0].beat = std::numeric_limits<double>::quiet_NaN();
        CHECK(writeSmf0(c, 120.0).empty());
        c.events[0].beat = std::numeric_limits<double>::infinity();
        CHECK(writeSmf0(c, 120.0).empty());
    }
    SUBCASE("an endBeat that is negative, NaN or infinite") {
        Compiled c = good;
        c.endBeat = -1.0;
        CHECK(writeSmf0(c, 120.0).empty());
        c.endBeat = std::numeric_limits<double>::quiet_NaN();
        CHECK(writeSmf0(c, 120.0).empty());
        c.endBeat = std::numeric_limits<double>::infinity();
        CHECK(writeSmf0(c, 120.0).empty());
    }
    SUBCASE("a tick beyond the four-byte delta range") {
        Compiled c = good;
        c.endBeat = 1e9;  // 4.8e11 ticks at 480 tpqn
        CHECK(writeSmf0(c, 120.0).empty());
    }
}

// --- writeFile ---------------------------------------------------------------------------------

TEST_CASE("writeFile: round trip through a temporary path, overwriting what was there") {
    std::filesystem::path written;
    {
        const TempFile file("roundtrip");
        written = file.path;

        const Bytes twoBars = writeSmf0(compile(cMajorBars(2)), 120.0);
        REQUIRE(writeFile(file.string(), twoBars));
        CHECK(readAll(file.path) == twoBars);

        // A shorter write replaces the longer file rather than overwriting its prefix.
        const Bytes oneBar = writeSmf0(compile(cMajorBars(1)), 120.0);
        REQUIRE(oneBar.size() < twoBars.size());
        REQUIRE(writeFile(file.string(), oneBar));
        CHECK(readAll(file.path) == oneBar);
        CHECK(readAll(file.path) == kGolden);
    }
    CHECK_FALSE(std::filesystem::exists(written));  // the guard cleaned up

    // Two guards never share a path, even with the same tag.
    const TempFile a("same");
    const TempFile b("same");
    CHECK(a.path != b.path);
}

TEST_CASE("writeFile: returns false when the file cannot be created") {
    CHECK_FALSE(writeFile("/nonexistent-dir/x.mid", kGolden));
    CHECK_FALSE(std::filesystem::exists("/nonexistent-dir/x.mid"));
}

TEST_CASE("writeFile: refuses to write nothing") {
    const TempFile file("empty");
    CHECK_FALSE(writeFile(file.string(), Bytes{}));
    CHECK_FALSE(std::filesystem::exists(file.path));
}

// --- exportMidi (feel plan, Task 4) ---------------------------------------------------------------------------

namespace {

/// A channel event as (tick, status, pitch, velocity), for literal comparison.
struct Note {
    std::uint32_t tick;
    int status;
    int pitch;
    int velocity;
};

bool operator==(const Note& a, const Note& b) {
    return a.tick == b.tick && a.status == b.status && a.pitch == b.pitch && a.velocity == b.velocity;
}

std::vector<Note> notesOf(const Parsed& parsed) {
    std::vector<Note> out;
    for (const ParsedEvent& e : channelEvents(parsed)) {
        REQUIRE(e.data.size() == 2);
        out.push_back(Note{e.tick, e.status, e.data[0], e.data[1]});
    }
    return out;
}

std::string describe(const std::vector<Note>& notes) {
    std::ostringstream os;
    for (const Note& n : notes) os << " " << std::hex << n.status << std::dec << ":" << n.pitch << "@" << n.tick;
    return os.str();
}

}  // namespace

TEST_CASE("exportMidi: Feel{} in 4/4 is today's export plus the clip name; the one-bar file is the golden one named") {
    const Bytes oneBar = exportMidi(cMajorBars(1), -1, 120.0);
    CHECK(oneBar == writeSmf0(compile(cMajorBars(1)), 120.0, 4, 4, 480, 0, "C"));
    CHECK(trackNameOf(requireWellFormed(oneBar)) == "C");
    CHECK(notesOf(requireWellFormed(oneBar)) == notesOf(requireWellFormed(kGolden)));
    // C Am F G7, one bar each, auto voice-leading off, 120 bpm: the bytes writeSmf0 wrote before feels
    // existed, plus the track name after the time signature.
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(Chord{9, &chordType("min")}, 4.0, 100);
    store.appendChord(Chord{5, &chordType("maj")}, 4.0, 100);
    store.appendChord(Chord{7, &chordType("7")}, 4.0, 100);
    const Bytes expected = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
        'M', 'T', 'r', 'k', 0, 0, 0, 0x8C,                                                       // 140 bytes
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,                                                // 500000 us
        0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08,                                          // 4/4
        0x00, 0xFF, 0x03, 0x09, 'C', ' ', 'A', 'm', ' ', 'F', ' ', 'G', '7',                     // track name
        0x00, 0x90, 0x3C, 0x64, 0x00, 0x90, 0x40, 0x64, 0x00, 0x90, 0x43, 0x64,                  // C on at 0
        0x8F, 0x00, 0x80, 0x3C, 0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0x80, 0x43, 0x00,            // C off at 1920
        0x00, 0x90, 0x45, 0x64, 0x00, 0x90, 0x48, 0x64, 0x00, 0x90, 0x4C, 0x64,                  // Am on
        0x8F, 0x00, 0x80, 0x45, 0x00, 0x00, 0x80, 0x48, 0x00, 0x00, 0x80, 0x4C, 0x00,            // Am off at 3840
        0x00, 0x90, 0x41, 0x64, 0x00, 0x90, 0x45, 0x64, 0x00, 0x90, 0x48, 0x64,                  // F on
        0x8F, 0x00, 0x80, 0x41, 0x00, 0x00, 0x80, 0x45, 0x00, 0x00, 0x80, 0x48, 0x00,            // F off at 5760
        0x00, 0x90, 0x43, 0x64, 0x00, 0x90, 0x47, 0x64, 0x00, 0x90, 0x4A, 0x64, 0x00, 0x90, 0x4D, 0x64,  // G7 on
        0x8F, 0x00, 0x80, 0x43, 0x00, 0x00, 0x80, 0x47, 0x00, 0x00, 0x80, 0x4A, 0x00, 0x00, 0x80, 0x4D, 0x00,  // 7680
        0x00, 0xFF, 0x2F, 0x00,
    };
    const Bytes actual = exportMidi(store.doc(), -1, 120.0);
    INFO("actual:   " << hex(actual));
    CHECK(actual == expected);
    CHECK(actual == writeSmf0(compile(store.doc()), 120.0, 4, 4, 480, 0, "C Am F G7"));
    // Auto voice-leading on: still exactly writeSmf0 of the block compile.
    store.setAutoVoiceLead(true);
    CHECK(exportMidi(store.doc(), -1, 120.0) == writeSmf0(compile(store.doc()), 120.0, 4, 4, 480, 0, "C Am F G7"));
}

TEST_CASE("exportMidi: a one-bar C major pulse at 120 bpm round-trips to literal ticks") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.setFeel(Feel{Rhythm::Pulse});
    const Parsed parsed = requireWellFormed(exportMidi(store.doc(), -1, 120.0));
    CHECK(parsed.events[1].data == Bytes{0x04, 0x02, 0x18, 0x08});
    const std::vector<Note> notes = notesOf(parsed);
    const std::vector<Note> expected{
        {0, 0x90, 60, 100},    {0, 0x90, 64, 100},    {0, 0x90, 67, 100},    {432, 0x80, 60, 0},
        {432, 0x80, 64, 0},    {432, 0x80, 67, 0},    {480, 0x90, 60, 90},   {480, 0x90, 64, 90},
        {480, 0x90, 67, 90},   {912, 0x80, 60, 0},    {912, 0x80, 64, 0},    {912, 0x80, 67, 0},
        {960, 0x90, 60, 90},   {960, 0x90, 64, 90},   {960, 0x90, 67, 90},   {1392, 0x80, 60, 0},
        {1392, 0x80, 64, 0},   {1392, 0x80, 67, 0},   {1440, 0x90, 60, 90},  {1440, 0x90, 64, 90},
        {1440, 0x90, 67, 90},  {1872, 0x80, 60, 0},   {1872, 0x80, 64, 0},   {1872, 0x80, 67, 0},
    };
    INFO(describe(notes));
    CHECK(notes == expected);
    CHECK(parsed.events.back().tick == 1920);  // end of track at the bar's end: the trailing gap survives
}

TEST_CASE("exportMidi: a strummed pulse round-trips to literal ticks at 480 tpqn") {
    // Pulse + strum down, medium (20 ticks of 960 = 10 of 480): each hit's notes 10 apart, offs together.
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.setFeel(Feel{Rhythm::Pulse, Strum::Down, StrumSpeed::Medium, Humanise::Off});
    const std::vector<Note> notes = notesOf(requireWellFormed(exportMidi(store.doc(), -1, 120.0)));
    const std::vector<Note> expected{
        {0, 0x90, 60, 100},    {10, 0x90, 64, 100},   {20, 0x90, 67, 100},   {432, 0x80, 60, 0},
        {432, 0x80, 64, 0},    {432, 0x80, 67, 0},    {480, 0x90, 60, 90},   {490, 0x90, 64, 90},
        {500, 0x90, 67, 90},   {912, 0x80, 60, 0},    {912, 0x80, 64, 0},    {912, 0x80, 67, 0},
        {960, 0x90, 60, 90},   {970, 0x90, 64, 90},   {980, 0x90, 67, 90},   {1392, 0x80, 60, 0},
        {1392, 0x80, 64, 0},   {1392, 0x80, 67, 0},   {1440, 0x90, 60, 90},  {1450, 0x90, 64, 90},
        {1460, 0x90, 67, 90},  {1872, 0x80, 60, 0},   {1872, 0x80, 64, 0},   {1872, 0x80, 67, 0},
    };
    INFO(describe(notes));
    CHECK(notes == expected);
}

TEST_CASE("exportMidi: the document's meter goes into the time signature") {
    Document doc = cMajorBars(1);
    doc.meter = Meter{6, 8};
    CHECK(requireWellFormed(exportMidi(doc, -1, 120.0)).events[1].data == Bytes{0x06, 0x03, 0x18, 0x08});
    doc.feel = Feel{Rhythm::Subdivided};
    CHECK(requireWellFormed(exportMidi(doc, -1, 120.0)).events[1].data == Bytes{0x06, 0x03, 0x18, 0x08});
    doc.meter = Meter{7, 8};
    CHECK(requireWellFormed(exportMidi(doc, -1, 120.0)).events[1].data == Bytes{0x07, 0x03, 0x18, 0x08});
    doc.meter = Meter{3, 4};
    CHECK(requireWellFormed(exportMidi(doc, -1, 120.0)).events[1].data == Bytes{0x03, 0x02, 0x18, 0x08});
}

TEST_CASE("exportMidi: a section exports its own chords, rebased; nothing to write gives no bytes") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(Chord{9, &chordType("min")}, 2.0, 80);
    store.addSection(1, "B");  // sections at 0 and 1
    store.setFeel(Feel{Rhythm::Pulse});
    const Document& doc = store.doc();
    // Am over [3840, 5760): hits at 3840 (bar start, 80) and 4800 (pulse, round(80 * 0.9) = 72), gated 864.
    const std::vector<Note> notes = notesOf(requireWellFormed(exportMidi(doc, 1, 120.0)));
    const std::vector<Note> expected{
        {0, 0x90, 69, 80},   {0, 0x90, 72, 80},   {0, 0x90, 76, 80},   {432, 0x80, 69, 0},  {432, 0x80, 72, 0},
        {432, 0x80, 76, 0},  {480, 0x90, 69, 72}, {480, 0x90, 72, 72}, {480, 0x90, 76, 72}, {912, 0x80, 69, 0},
        {912, 0x80, 72, 0},  {912, 0x80, 76, 0},
    };
    INFO(describe(notes));
    CHECK(notes == expected);
    CHECK(exportMidi(doc, 1, 120.0) == writeSmf0(compileSection(doc, 1), 120.0, 4, 4, 480, 0, "B"));
    CHECK(exportMidi(doc, 0, 120.0) == writeSmf0(compileSection(doc, 0), 120.0, 4, 4, 480, 0, "Section 1"));
    CHECK(trackNameOf(requireWellFormed(exportMidi(doc, 1, 120.0))) == "B");  // the section's name names the clip

    CHECK(exportMidi(doc, 2, 120.0).empty());          // no such section
    CHECK(exportMidi(Document{}, -1, 120.0).empty());  // nothing to write
    CHECK(exportMidi(Document{}, 0, 120.0).empty());
    CHECK(exportMidi(cMajorBars(1), 1, 120.0).empty());  // no sections: only index 0
    CHECK(exportMidi(cMajorBars(1), 0, 120.0) == exportMidi(cMajorBars(1), -1, 120.0));  // index 0 is the whole
    CHECK(exportMidi(doc, -1, 0.0).empty());           // a tempo the writer refuses
    CHECK(exportMidi(doc, -1, std::numeric_limits<double>::quiet_NaN()).empty());
}

// --- exportClipName -----------------------------------------------------------------------------------

TEST_CASE("exportClipName: the whole progression is its chord symbols in the project key, at most kClipNameChords") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(Chord{9, &chordType("min")}, 4.0, 100);
    store.appendChord(Chord{5, &chordType("maj")}, 4.0, 100);
    store.appendChord(Chord{7, &chordType("7")}, 4.0, 100);
    CHECK(exportClipName(store.doc(), -1) == "C Am F G7");
    CHECK(exportClipName(store.doc(), 0) == "C Am F G7");  // no sections: index 0 is the whole progression
    CHECK(exportClipName(store.doc(), 1).empty());          // no such section
    CHECK(exportClipName(Document{}, -1).empty());
    CHECK(exportClipName(Document{}, 0).empty());

    // Spelled in the project key, as the sequence spells its blocks: Bb in F major, A# in B major.
    DocumentStore spelled;
    spelled.appendChord(Chord{10, &chordType("maj")}, 4.0, 100);
    spelled.setProjectKey(Key{Tonic{'F', 0}, Mode::Ionian});
    CHECK(exportClipName(spelled.doc(), -1) == "Bb");
    spelled.setProjectKey(Key{Tonic{'B', 0}, Mode::Ionian});
    CHECK(exportClipName(spelled.doc(), -1) == "A#");

    // The 12-bar blues: eight chords and an ellipsis; exactly eight chords have none.
    DocumentStore blues;
    const int roots[12] = {0, 5, 0, 0, 5, 5, 0, 0, 7, 5, 0, 7};
    for (int i = 0; i < 8; ++i) {
        blues.appendChord(Chord{roots[i], &chordType("7")}, 4.0, 100);
    }
    CHECK(exportClipName(blues.doc(), -1) == "C7 F7 C7 C7 F7 F7 C7 C7");
    for (int i = 8; i < 12; ++i) {
        blues.appendChord(Chord{roots[i], &chordType("7")}, 4.0, 100);
    }
    CHECK(exportClipName(blues.doc(), -1) == "C7 F7 C7 C7 F7 F7 C7 C7 ...");
    CHECK(trackNameOf(requireWellFormed(exportMidi(blues.doc(), -1, 120.0))) == "C7 F7 C7 C7 F7 F7 C7 C7 ...");
}

TEST_CASE("exportClipName: a section is named after itself, or after its chords when its name is empty") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(Chord{9, &chordType("min")}, 4.0, 100);
    store.appendChord(Chord{5, &chordType("maj")}, 4.0, 100);
    store.appendChord(Chord{7, &chordType("7")}, 4.0, 100);
    const int chorus = store.addSection(2, "Chorus");  // the store first sections the start: "Section 1" at 0
    REQUIRE(chorus != kNoSectionId);
    CHECK(exportClipName(store.doc(), 0) == "Section 1");
    CHECK(exportClipName(store.doc(), 1) == "Chorus");
    CHECK(exportClipName(store.doc(), -1) == "C Am F G7");
    CHECK(exportClipName(store.doc(), 2).empty());
    store.renameSection(chorus, "");
    CHECK(exportClipName(store.doc(), 1) == "F G7");
    CHECK(exportClipName(store.doc(), 0) == "Section 1");
}
