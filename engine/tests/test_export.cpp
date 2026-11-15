#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/export.h"
#include "chordlab/feel.h"
#include "chordlab/smf.h"
#include "chordlab/tracks.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

// Clip export (tracks design D12): compileTrack, sectionSpanBeats, trackClipName and exportClip. The writer's own
// bytes are pinned in test_smf.cpp; here the files are read back with an independent multi-track reader.

using namespace chordlab;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr double kTicks = kFeelTicksPerBeat;

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

// --- A reader for format 0 and 1 files ---------------------------------------------------------------------

struct FileEvent {
    std::uint32_t tick;
    int status;  // 0xFF for a meta event
    int meta;
    Bytes data;
};

struct FileTrack {
    std::vector<FileEvent> events;
};

struct File {
    bool ok = false;
    int format = -1;
    int ntrks = -1;
    int division = -1;
    std::vector<FileTrack> tracks;
};

std::uint32_t be32(const Bytes& b, std::size_t at) {
    return (std::uint32_t{b[at]} << 24) | (std::uint32_t{b[at + 1]} << 16) | (std::uint32_t{b[at + 2]} << 8) |
           std::uint32_t{b[at + 3]};
}

File parseFile(const Bytes& b) {
    File f;
    if (b.size() < 14 || std::string(b.begin(), b.begin() + 4) != "MThd" || be32(b, 4) != 6) return f;
    f.format = (b[8] << 8) | b[9];
    f.ntrks = (b[10] << 8) | b[11];
    f.division = (b[12] << 8) | b[13];
    std::size_t pos = 14;
    while (pos < b.size()) {
        if (pos + 8 > b.size() || std::string(b.begin() + static_cast<std::ptrdiff_t>(pos),
                                              b.begin() + static_cast<std::ptrdiff_t>(pos) + 4) != "MTrk") {
            return f;
        }
        const std::size_t end = pos + 8 + be32(b, pos + 4);
        if (end > b.size()) return f;
        pos += 8;
        FileTrack track;
        std::uint32_t tick = 0;
        bool eot = false;
        const auto vlqAt = [&]() {
            std::uint32_t v = 0;
            for (int n = 0; n < 4 && pos < end; ++n) {
                const std::uint8_t c = b[pos++];
                v = (v << 7) | (c & 0x7Fu);
                if ((c & 0x80u) == 0) break;
            }
            return v;
        };
        while (pos < end) {
            if (eot) return f;
            tick += vlqAt();
            if (pos >= end) return f;
            FileEvent e{tick, b[pos++], 0, {}};
            if (e.status == 0xFF) {
                e.meta = b[pos++];
                const std::uint32_t length = vlqAt();
                if (pos + length > end) return f;
                e.data.assign(b.begin() + static_cast<std::ptrdiff_t>(pos),
                              b.begin() + static_cast<std::ptrdiff_t>(pos + length));
                pos += length;
                eot = eot || e.meta == 0x2F;
            } else {
                if (e.status < 0x80 || e.status >= 0xF0 || pos + 2 > end) return f;
                e.data = {b[pos], b[pos + 1]};
                pos += 2;
            }
            track.events.push_back(e);
        }
        if (!eot) return f;
        f.tracks.push_back(track);
    }
    f.ok = static_cast<int>(f.tracks.size()) == f.ntrks;
    return f;
}

File requireFile(const Bytes& bytes) {
    const File f = parseFile(bytes);
    REQUIRE(f.ok);
    CHECK(f.division == 480);
    return f;
}

struct N {
    std::uint32_t tick;
    int status;
    int pitch;
    int velocity;
};

bool operator==(const N& a, const N& b) {
    return a.tick == b.tick && a.status == b.status && a.pitch == b.pitch && a.velocity == b.velocity;
}

std::vector<N> notesOf(const FileTrack& t) {
    std::vector<N> out;
    for (const FileEvent& e : t.events) {
        if (e.status < 0xF0) out.push_back(N{e.tick, e.status, e.data[0], e.data[1]});
    }
    return out;
}

std::string nameOf(const FileTrack& t) {
    for (const FileEvent& e : t.events) {
        if (e.status == 0xFF && e.meta == 0x03) return std::string(e.data.begin(), e.data.end());
    }
    return {};
}

std::uint32_t endTickOf(const FileTrack& t) {
    REQUIRE_FALSE(t.events.empty());
    REQUIRE(t.events.back().meta == 0x2F);
    return t.events.back().tick;
}

/// A note as the compiled form holds it, paired per pitch, for readable expectations.
struct S {
    double start;
    double end;
    int pitch;
    int velocity;
};

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

/// The notes of `compiled` per pitch, ordered by (pitch, start).
std::vector<S> soundings(const Compiled& compiled) {
    std::vector<S> out;
    for (const NoteEvent& e : compiled.events) {
        if (e.on) {
            out.push_back(S{e.beat, e.beat, e.pitch, e.velocity});
        } else {
            for (auto it = out.rbegin(); it != out.rend(); ++it) {
                if (it->pitch == e.pitch && it->end == it->start) {
                    it->end = e.beat;
                    break;
                }
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const S& a, const S& b) {
        return a.pitch != b.pitch ? a.pitch < b.pitch : a.start < b.start;
    });
    return out;
}

std::vector<S> pitchNotes(const Compiled& compiled, int pitch) {
    std::vector<S> out;
    for (const S& s : soundings(compiled)) {
        if (s.pitch == pitch) out.push_back(s);
    }
    return out;
}

bool matches(const std::vector<S>& actual, const std::vector<S>& expected) {
    if (actual.size() != expected.size()) return false;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (!near(actual[i].start, expected[i].start) || !near(actual[i].end, expected[i].end) ||
            actual[i].pitch != expected[i].pitch || actual[i].velocity != expected[i].velocity) {
            return false;
        }
    }
    return true;
}

// --- Documents ----------------------------------------------------------------------------------------------

NoteSpec ns(int pitch, std::int64_t startTick, std::int64_t lengthTicks, int velocity = 100) {
    return NoteSpec{pitch, startTick, lengthTicks, velocity};
}

int addTrackWith(DocumentStore& store, const std::vector<NoteSpec>& specs, TrackRole role = TrackRole::Other,
                 std::optional<std::string> name = std::nullopt) {
    const int id = store.addTrack(role, name);
    REQUIRE(id != kNoTrackId);
    if (!specs.empty()) {
        std::string why;
        REQUIRE_MESSAGE(store.insertNotes(id, specs, &why).size() == specs.size(), why);
    }
    return id;
}

/// Adds to track `trackId` an inactive note anchored to the chord at `eventIndex` (the chord is lengthened, a note
/// inserted at its old end, the length restored).
void addInactiveNote(DocumentStore& store, int trackId, std::size_t eventIndex, int pitch) {
    const ChordEvent event = store.doc().events.at(eventIndex);
    REQUIRE(event.lengthBeats + 1.0 <= kMaxLengthBeats);
    const std::int32_t oldEnd = chordBoundaryTicks(store.doc()).at(eventIndex + 1);
    REQUIRE(store.setLength(event.id, event.lengthBeats + 1.0));
    std::string why;
    const std::vector<int> ids = store.insertNotes(trackId, {ns(pitch, oldEnd, kMinTrackNoteTicks)}, &why);
    REQUIRE_MESSAGE(ids.size() == 1, why);
    REQUIRE(store.setLength(event.id, event.lengthBeats));
    const Track& track = store.doc().tracks.at(static_cast<std::size_t>(store.findTrackIndex(trackId)));
    const auto note =
        std::find_if(track.notes.begin(), track.notes.end(), [&](const TrackNote& n) { return n.id == ids[0]; });
    REQUIRE(note != track.notes.end());
    REQUIRE_FALSE(noteActive(store.doc(), *note));
}

/// C, Am, F, G7 of four beats each: boundaries 0, 3840, 7680, 11520, 15360 ticks, sixteen beats.
DocumentStore fourBars() {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    store.appendChord(chord(9, "min"), 4.0, 100);
    store.appendChord(chord(5, "maj"), 4.0, 100);
    store.appendChord(chord(7, "7"), 4.0, 100);
    return store;
}

/// Four bars in two sections: "Section 1" over the first two chords (beats 0..8) and a second, named "", over the rest.
DocumentStore twoSections() {
    DocumentStore store = fourBars();
    REQUIRE(store.addSection(2, "") != kNoSectionId);  // takes the default name "Section 2"
    store.renameSection(store.doc().sections[1].id, "");  // ... which renaming can clear
    REQUIRE(store.doc().sections[1].name.empty());
    return store;
}

/// C (4 beats), Am (2), F (2), G7 (4) in sections at chords 0, 1 and 2: the middle section's span is beats [4, 6).
DocumentStore narrowMiddle() {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    store.appendChord(chord(9, "min"), 2.0, 100);
    store.appendChord(chord(5, "maj"), 2.0, 100);
    store.appendChord(chord(7, "7"), 4.0, 100);
    REQUIRE(store.addSection(1, "Mid") != kNoSectionId);
    REQUIRE(store.addSection(2, "Out") != kNoSectionId);
    return store;
}

const double kBpm = 120.0;

/// Twelve chords of fractional lengths (0.5 to 0.63 beats, steps of 0.00131) in four sections of three, under a
/// strummed feel (the first strummed note of each chord sits on its onset tick): boundaries on odd ticks, so a section's length at 960 is often odd and a half tick at 480. `seed`
/// shifts the lengths; the sweep over seeds is what keeps a case from passing by luck of its inputs.
DocumentStore oddTickDoc(int seed) {
    DocumentStore store;
    for (int j = 0; j < 12; ++j) {
        store.appendChord(chord(static_cast<PitchClass>(j % 12), "min"), 0.5 + ((seed * 31 + j * 17) % 101) * 0.00131, 100);
    }
    for (int first : {3, 6, 9}) REQUIRE(store.addSection(first, "") != kNoSectionId);
    store.setFeel(Feel{Rhythm::Block, Strum::Down, StrumSpeed::Tight});
    REQUIRE(feelPerforms(store.doc().feel));
    REQUIRE_FALSE(compile(store.doc()).feelLimited);
    return store;
}

constexpr int kSweep = 120;

}  // namespace

// --- exportMidi is untouched by tracks -----------------------------------------------------------------------

TEST_CASE("exportMidi: populated tracks under every mute and solo combination change nothing") {
    for (const bool performing : {false, true}) {
        DocumentStore bare = twoSections();
        if (performing) bare.setFeel(Feel{Rhythm::Pulse, Strum::Down, StrumSpeed::Tight, Humanise::Light});
        DocumentStore populated = bare;
        const int bass = addTrackWith(populated, {ns(36, 0, 1920), ns(43, 3840, 960), ns(36, 11520, 3840)},
                                      TrackRole::Bass);
        const int lead = addTrackWith(populated, {ns(72, 480, 480), ns(74, 7680, 960)}, TrackRole::Lead);
        addTrackWith(populated, {ns(60, 100, 5000)});
        for (const int section : {-1, 0, 1}) {
            const Bytes baseline = exportMidi(bare.doc(), section, kBpm);
            REQUIRE_FALSE(baseline.empty());
            for (int mask = 0; mask < 64; ++mask) {
                DocumentStore store = populated;
                store.setChordsMute((mask & 1) != 0);
                store.setChordsSolo((mask & 2) != 0);
                store.setTrackMute(bass, (mask & 4) != 0);
                store.setTrackSolo(bass, (mask & 8) != 0);
                store.setTrackMute(lead, (mask & 16) != 0);
                store.setTrackSolo(lead, (mask & 32) != 0);
                CAPTURE(performing);
                CAPTURE(section);
                CAPTURE(mask);
                CHECK(exportMidi(store.doc(), section, kBpm) == baseline);
            }
        }
    }
}

// --- sectionSpanBeats ----------------------------------------------------------------------------------------

TEST_CASE("sectionSpanBeats: the whole progression, each section, and what names no section") {
    const DocumentStore store = twoSections();
    const Document& doc = store.doc();
    const PlaySpan whole = sectionSpanBeats(doc, -1);
    CHECK(whole.start == 0.0);
    CHECK(whole.end == compile(doc).endBeat);
    CHECK(whole.end == 16.0);
    CHECK(sectionSpanBeats(doc, 0).start == 0.0);
    CHECK(sectionSpanBeats(doc, 0).end == 8.0);
    CHECK(sectionSpanBeats(doc, 1).start == 8.0);
    CHECK(sectionSpanBeats(doc, 1).end == 16.0);
    CHECK(sectionSpanBeats(doc, 2).end == 0.0);  // no such section: an empty span
    CHECK(sectionSpanBeats(doc, 2).start == 0.0);

    const DocumentStore plain = fourBars();  // no sections: index 0 is the whole progression
    CHECK(sectionSpanBeats(plain.doc(), 0).start == 0.0);
    CHECK(sectionSpanBeats(plain.doc(), 0).end == 16.0);
    CHECK(sectionSpanBeats(plain.doc(), 1).end == 0.0);

    CHECK(sectionSpanBeats(Document{}, -1).end == 0.0);
    CHECK(sectionSpanBeats(Document{}, 0).end == 0.0);
}

TEST_CASE("sectionSpanBeats: under a performing feel its ends are the section's boundary ticks over 960") {
    for (int seed = 0; seed < kSweep; ++seed) {
        const DocumentStore store = oddTickDoc(seed);
        const Document& doc = store.doc();
        const std::vector<std::int32_t> ticks = chordBoundaryTicks(doc);
        for (int section = 0; section < 4; ++section) {
            CAPTURE(seed);
            CAPTURE(section);
            const std::size_t first = static_cast<std::size_t>(doc.sections[static_cast<std::size_t>(section)].firstEventIndex);
            const std::size_t end = section + 1 < 4 ? static_cast<std::size_t>(doc.sections[static_cast<std::size_t>(section) + 1].firstEventIndex) : doc.events.size();
            const PlaySpan span = sectionSpanBeats(doc, section);
            CHECK(span.start == ticks[first] / kTicks);
            CHECK(span.end == ticks[end] / kTicks);
            // The difference of the two ends is the tick count over 960 to within an ulp or two (two correctly
            // rounded values subtracted), and exactly that many ticks when rounded back.
            const Compiled compiled = compileSection(doc, section);
            CHECK(compiled.endBeat == (ticks[end] - ticks[first]) / kTicks);
            CHECK(std::fabs((span.end - span.start) - compiled.endBeat) < 1e-12);
            CHECK(std::lround((span.end - span.start) * kTicks) == ticks[end] - ticks[first]);
        }
        CHECK(sectionSpanBeats(doc, -1).end == compile(doc).endBeat);
    }
}

TEST_CASE("sectionSpanBeats: on the block path the length is compileSection's to well under a tick") {
    DocumentStore store;
    for (const double length : {0.2506, 1.0013, 0.7497, 2.0, 0.2504, 3.0}) store.appendChord(chord(0, "maj"), length, 100);
    REQUIRE(store.addSection(2, "B") != kNoSectionId);
    REQUIRE(store.addSection(4, "C") != kNoSectionId);
    const Document& doc = store.doc();
    REQUIRE_FALSE(feelPerforms(doc.feel));
    for (int section = 0; section < 3; ++section) {
        const PlaySpan span = sectionSpanBeats(doc, section);
        CHECK(near(span.end - span.start, compileSection(doc, section).endBeat));
    }
    CHECK(sectionSpanBeats(doc, -1).end == compile(doc).endBeat);  // bit for bit
}

// --- compileTrack: the whole track ---------------------------------------------------------------------------

TEST_CASE("compileTrack: the notes of a track in beats, as the track's own clip") {
    DocumentStore store = fourBars();
    const int id = addTrackWith(store, {ns(36, 0, 3840, 90), ns(43, 480, 960, 70), ns(40, 3840, 1920)}, TrackRole::Bass);
    const Compiled c = compileTrack(store.doc(), id, -1);
    CHECK(c.endBeat == compile(store.doc()).endBeat);
    CHECK_FALSE(c.feelLimited);
    CHECK(matches(soundings(c), {{0.0, 4.0, 36, 90}, {4.0, 6.0, 40, 100}, {0.5, 1.5, 43, 70}}));
    // Events sorted as compile's are: (beat, off before on, pitch).
    REQUIRE(c.events.size() == 6);
    for (std::size_t i = 1; i < c.events.size(); ++i) {
        const NoteEvent& a = c.events[i - 1];
        const NoteEvent& b = c.events[i];
        CHECK((a.beat < b.beat || (a.beat == b.beat && (a.on ? 1 : 0) < (b.on ? 1 : 0)) ||
               (a.beat == b.beat && a.on == b.on && a.pitch < b.pitch)));
    }
    // Offs carry velocity 0.
    for (const NoteEvent& e : c.events) CHECK((e.on ? e.velocity >= 1 : e.velocity == 0));
}

TEST_CASE("compileTrack: unknown track, bad section and empty progression give an empty result") {
    DocumentStore store = twoSections();
    const int id = addTrackWith(store, {ns(36, 0, 960)});
    CHECK(compileTrack(store.doc(), id + 100, -1).events.empty());
    CHECK(compileTrack(store.doc(), id + 100, -1).endBeat == 0.0);
    CHECK(compileTrack(store.doc(), kNoTrackId, -1).events.empty());
    CHECK(compileTrack(store.doc(), id, 2).events.empty());
    CHECK(compileTrack(store.doc(), id, 2).endBeat == 0.0);
    CHECK_FALSE(compileTrack(store.doc(), id, 0).events.empty());

    DocumentStore noChords;
    const int empty = addTrackWith(noChords, {});
    CHECK(compileTrack(noChords.doc(), empty, -1).events.empty());
    CHECK(compileTrack(noChords.doc(), empty, 0).events.empty());
}

TEST_CASE("compileTrack: inactive notes are absent; mute and solo are ignored") {
    DocumentStore store = fourBars();
    const int id = addTrackWith(store, {ns(36, 0, 960)});
    addInactiveNote(store, id, 1, 50);
    addInactiveNote(store, id, 3, 52);
    DocumentStore muted = store;
    REQUIRE(muted.setTrackMute(id, true));
    REQUIRE(muted.setChordsSolo(true));
    for (const DocumentStore* s : {&store, &muted}) {
        const Compiled c = compileTrack(s->doc(), id, -1);
        CHECK(matches(soundings(c), {{0.0, 1.0, 36, 100}}));
        CHECK(compileTrack(s->doc(), id, 0).events.size() == 2);
    }
}

TEST_CASE("compileTrack: a note running past the end is clipped to endBeat, and a clipped remainder under five ticks is dropped") {
    DocumentStore store = fourBars();  // endBeat 16, tick 15360
    const int id = addTrackWith(store, {ns(60, 14400, 4800),                      // beats 15..20: clipped to 15..16
                                        ns(62, 15355, kMinTrackNoteTicks),        // five ticks left: kept
                                        ns(64, 15356, kMinTrackNoteTicks),        // four ticks left: dropped
                                        ns(65, 15357, kMinTrackNoteTicks)});      // three: dropped
    const Compiled c = compileTrack(store.doc(), id, -1);
    CHECK(c.endBeat == 16.0);
    CHECK(matches(pitchNotes(c, 60), {{15.0, 16.0, 60, 100}}));
    REQUIRE(pitchNotes(c, 62).size() == 1);
    CHECK(near(pitchNotes(c, 62)[0].start, 15355 / kTicks));
    CHECK(pitchNotes(c, 62)[0].end == 16.0);
    CHECK(pitchNotes(c, 64).empty());
    CHECK(pitchNotes(c, 65).empty());
    for (const NoteEvent& e : c.events) CHECK(e.beat <= c.endBeat);
}

TEST_CASE("compileTrack: a note that nothing shortened is never dropped, however short") {
    DocumentStore store = fourBars();
    const int id = addTrackWith(store, {ns(60, 960, kMinTrackNoteTicks), ns(61, 3000, kMinTrackNoteTicks)});
    const Compiled c = compileTrack(store.doc(), id, -1);
    CHECK(pitchNotes(c, 60).size() == 1);
    CHECK(pitchNotes(c, 61).size() == 1);
    CHECK(std::lround((pitchNotes(c, 60)[0].end - pitchNotes(c, 60)[0].start) * kTicks) == kMinTrackNoteTicks);
}

TEST_CASE("compileTrack: a track's own same-pitch overlaps follow D8's paired rule") {
    DocumentStore store = fourBars();
    const int id = addTrackWith(store, {ns(50, 0, 1920, 80), ns(50, 960, 1920, 90),     // later retriggers: 0..1, 1..3
                                        ns(52, 480, 480, 80), ns(52, 480, 960, 110),    // equal starts: one note, end 1.5, vel 110
                                        ns(55, 1000, 100), ns(55, 1003, 500),           // earlier cut to 3 ticks: dropped
                                        ns(57, 2000, 100), ns(57, 2005, 500),           // earlier cut to exactly 5 ticks: kept
                                        ns(59, 4000, 960), ns(59, 4000 + 960, 960)});   // touching: untouched
    const Compiled c = compileTrack(store.doc(), id, -1);
    CHECK(matches(pitchNotes(c, 50), {{0.0, 1.0, 50, 80}, {1.0, 3.0, 50, 90}}));
    CHECK(matches(pitchNotes(c, 52), {{0.5, 1.5, 52, 110}}));
    REQUIRE(pitchNotes(c, 55).size() == 1);
    CHECK(near(pitchNotes(c, 55)[0].start, 1003 / kTicks));
    CHECK(near(pitchNotes(c, 55)[0].end, 1503 / kTicks));
    REQUIRE(pitchNotes(c, 57).size() == 2);
    CHECK(near(pitchNotes(c, 57)[0].start, 2000 / kTicks));
    CHECK(near(pitchNotes(c, 57)[0].end, 2005 / kTicks));
    CHECK(pitchNotes(c, 59).size() == 2);
}

TEST_CASE("compileTrack: the merge is the one compilePlayback uses (a lone track's playback is its clip)") {
    // With the chords muted and one track audible, compilePlayback is that track's merge, clipped; compileTrack
    // of the whole track must agree event for event.
    DocumentStore store = fourBars();
    const int id = addTrackWith(store, {ns(50, 0, 1920, 80), ns(50, 960, 1920, 90), ns(52, 480, 480, 80),
                                        ns(52, 480, 960, 110), ns(55, 1000, 100), ns(55, 1003, 500),
                                        ns(57, 2000, 100), ns(57, 2005, 500), ns(60, 14400, 4800)});
    store.setChordsMute(true);
    const Compiled playback = compilePlayback(store.doc());
    const Compiled clip = compileTrack(store.doc(), id, -1);
    CHECK(playback.events == clip.events);
}

// --- compileTrack: sections ----------------------------------------------------------------------------------

TEST_CASE("compileTrack: a section takes the notes that intersect it, cut at both ends and re-based") {
    DocumentStore store = twoSections();  // the second section is beats [8, 16)
    const int id = addTrackWith(store, {ns(60, 6720, 1920),        // beats 7..9: cut at the start to 8..9, re-based 0..1
                                        ns(62, 14400, 1920),       // beats 15..17: clipped to 16, re-based 7..8
                                        ns(64, 6000, 1685),        // ends at tick 7685: five ticks inside: kept
                                        ns(65, 6000, 1683),        // three ticks inside: dropped
                                        ns(67, 3840, 960),         // wholly in the first section
                                        ns(69, 6720, 960),         // ends exactly at the section's start: absent
                                        ns(71, 7680, 960),         // starts exactly at the start
                                        ns(72, 3840, 12000)});     // spans the whole section: cut at both ends
    const Compiled c = compileTrack(store.doc(), id, 1);
    CHECK(c.endBeat == 8.0);
    CHECK(matches(pitchNotes(c, 60), {{0.0, 1.0, 60, 100}}));
    CHECK(matches(pitchNotes(c, 62), {{7.0, 8.0, 62, 100}}));
    REQUIRE(pitchNotes(c, 64).size() == 1);
    CHECK(near(pitchNotes(c, 64)[0].start, 0.0));
    CHECK(near(pitchNotes(c, 64)[0].end, 5 / kTicks));
    CHECK(pitchNotes(c, 65).empty());
    CHECK(pitchNotes(c, 67).empty());
    CHECK(pitchNotes(c, 69).empty());
    CHECK(matches(pitchNotes(c, 71), {{0.0, 1.0, 71, 100}}));
    CHECK(matches(pitchNotes(c, 72), {{0.0, 8.0, 72, 100}}));

    const Compiled first = compileTrack(store.doc(), id, 0);
    CHECK(first.endBeat == 8.0);
    CHECK(matches(pitchNotes(first, 67), {{4.0, 5.0, 67, 100}}));
    CHECK(matches(pitchNotes(first, 60), {{7.0, 8.0, 60, 100}}));  // cut at the section's end
    CHECK(matches(pitchNotes(first, 69), {{7.0, 8.0, 69, 100}}));
    CHECK(pitchNotes(first, 62).empty());
    CHECK(pitchNotes(first, 71).empty());
}

TEST_CASE("compileTrack: a remainder of exactly five ticks is kept and of four is dropped, at either end of a section") {
    DocumentStore store = twoSections();  // second section: ticks [7680, 15360)
    const int id = addTrackWith(store, {ns(60, 7000, 685),     // ends at 7685: 5 ticks inside
                                        ns(61, 7000, 684),     // ends at 7684: 4 ticks inside
                                        ns(62, 7675, 700),     // starts 5 ticks before the first section's end
                                        ns(63, 7676, 700)});   // 4 ticks before
    const Compiled second = compileTrack(store.doc(), id, 1);
    REQUIRE(pitchNotes(second, 60).size() == 1);
    CHECK(near(pitchNotes(second, 60)[0].end, 5 / kTicks));
    CHECK(pitchNotes(second, 61).empty());
    const Compiled first = compileTrack(store.doc(), id, 0);
    REQUIRE(pitchNotes(first, 62).size() == 1);
    CHECK(near(pitchNotes(first, 62)[0].start, 7675 / kTicks));
    CHECK(first.endBeat == 8.0);
    CHECK(near(pitchNotes(first, 62)[0].end, 8.0));
    CHECK(pitchNotes(first, 63).empty());
}

TEST_CASE("compileTrack: same-pitch [0,8) and [3,5) over a [4,6) section give one note [0,1): merge before slice") {
    DocumentStore store = narrowMiddle();  // sections [0,4), [4,6), [6,10)
    const int id = addTrackWith(store, {ns(71, 0, 7680), ns(71, 2880, 1920)});
    REQUIRE(sectionSpanBeats(store.doc(), 1).start == 4.0);
    REQUIRE(sectionSpanBeats(store.doc(), 1).end == 6.0);
    const Compiled mid = compileTrack(store.doc(), id, 1);
    CHECK(mid.endBeat == 2.0);
    CHECK(matches(soundings(mid), {{0.0, 1.0, 71, 100}}));
    // The whole track is the merge: [0,3) and [3,5).
    CHECK(matches(soundings(compileTrack(store.doc(), id, -1)), {{0.0, 3.0, 71, 100}, {3.0, 5.0, 71, 100}}));
    // The other sections see their own slices of it.
    CHECK(matches(soundings(compileTrack(store.doc(), id, 0)), {{0.0, 3.0, 71, 100}, {3.0, 4.0, 71, 100}}));
    CHECK(matches(soundings(compileTrack(store.doc(), id, 2)), {}));
}

TEST_CASE("compileTrack: a section's clip under a performing feel is cut at the performance's boundaries") {
    DocumentStore store;
    for (const double length : {0.2506, 1.0013, 0.7497, 2.0}) store.appendChord(chord(0, "maj"), length, 100);
    REQUIRE(store.addSection(2, "B") != kNoSectionId);
    store.setFeel(Feel{Rhythm::Pulse});
    const std::vector<std::int32_t> ticks = chordBoundaryTicks(store.doc());
    const int id = addTrackWith(store, {ns(60, 0, 4000)});
    const PlaySpan span = sectionSpanBeats(store.doc(), 1);
    CHECK(span.start == ticks[2] / kTicks);
    const Compiled c = compileTrack(store.doc(), id, 1);
    REQUIRE(c.events.size() == 2);
    CHECK(c.events[0].beat == 0.0);
    CHECK(c.events[1].beat == span.end - span.start);
    CHECK(near(c.endBeat, compileSection(store.doc(), 1).endBeat));
}

// --- trackClipName -------------------------------------------------------------------------------------------

TEST_CASE("trackClipName: the track's name, with an en dash and the section's name for a section") {
    DocumentStore store = twoSections();
    const int bass = addTrackWith(store, {}, TrackRole::Bass);                       // "Bass"
    const int lead = addTrackWith(store, {}, TrackRole::Lead, std::string("Lead 1"));
    store.renameSection(store.doc().sections[0].id, "Verse");
    const Document& doc = store.doc();
    CHECK(trackClipName(doc, bass, -1) == "Bass");
    CHECK(trackClipName(doc, lead, -1) == "Lead 1");
    CHECK(trackClipName(doc, bass, 0) == "Bass \xE2\x80\x93 Verse");
    CHECK(trackClipName(doc, lead, 0) == "Lead 1 \xE2\x80\x93 Verse");
    // The second section's name is empty: its chords, as exportClipName spells them.
    CHECK(exportClipName(doc, 1) == "F G7");
    CHECK(trackClipName(doc, bass, 1) == "Bass \xE2\x80\x93 F G7");
    // The dash is U+2013 with a space either side: bytes 20 E2 80 93 20.
    const std::string name = trackClipName(doc, bass, 0);
    CHECK(name.find("\x20\xE2\x80\x93\x20") == 4);
    CHECK(name.size() == 4 + 5 + 5);
}

TEST_CASE("trackClipName: unknown track or section is empty; no sections means index 0 is the whole progression") {
    DocumentStore store = fourBars();
    const int id = addTrackWith(store, {}, TrackRole::Bass);
    const Document& doc = store.doc();
    CHECK(trackClipName(doc, id, -1) == "Bass");
    CHECK(trackClipName(doc, id, 0) == "Bass");  // a document without sections: index 0 is the whole
    CHECK(trackClipName(doc, id, 1).empty());
    CHECK(trackClipName(doc, id + 1, -1).empty());
    CHECK(trackClipName(doc, kNoTrackId, 0).empty());

    DocumentStore twoSec = twoSections();
    const int t = addTrackWith(twoSec, {}, TrackRole::Bass);
    CHECK(trackClipName(twoSec.doc(), t, 2).empty());
    CHECK(trackClipName(twoSec.doc(), t, 7).empty());
}

TEST_CASE("trackClipName: an empty track name is carried as it is, with no dash before a section") {
    DocumentStore store = twoSections();
    const int id = addTrackWith(store, {}, TrackRole::Other);
    REQUIRE(store.patchTrack(id, std::string(), std::nullopt));
    const Document& doc = store.doc();
    CHECK(trackClipName(doc, id, -1).empty());
    CHECK(trackClipName(doc, id, 1) == "F G7");  // no dash after nothing: the section part alone
    CHECK(trackClipName(doc, id, 0) == "Section 1");
    store.renameSection(doc.sections[1].id, "Out");
    CHECK(trackClipName(store.doc(), id, 1) == "Out");
}

// --- exportClip ----------------------------------------------------------------------------------------------

TEST_CASE("exportClip: one chords lane is exportMidi byte for byte, whole and per section, block and performing") {
    for (const bool performing : {false, true}) {
        DocumentStore store = twoSections();
        store.renameSection(store.doc().sections[0].id, "Verse");
        if (performing) store.setFeel(Feel{Rhythm::Subdivided, Strum::DownUp, StrumSpeed::Medium, Humanise::Heavy});
        addTrackWith(store, {ns(36, 0, 1920), ns(40, 7000, 2000)}, TrackRole::Bass);  // present, not asked for
        for (const int section : {-1, 0, 1}) {
            CAPTURE(performing);
            CAPTURE(section);
            const Bytes expected = exportMidi(store.doc(), section, kBpm);
            REQUIRE_FALSE(expected.empty());
            const Bytes clip = exportClip(store.doc(), ClipSpec{{ClipLane::chordsLane()}, false, section}, kBpm);
            CHECK(clip == expected);
            CHECK(requireFile(clip).format == 0);
        }
        // A document without sections: index 0 is the whole.
        DocumentStore plain = fourBars();
        CHECK(exportClip(plain.doc(), ClipSpec{{ClipLane::chordsLane()}, false, 0}, kBpm) ==
              exportMidi(plain.doc(), 0, kBpm));
        CHECK(exportClip(plain.doc(), ClipSpec{{ClipLane::chordsLane()}, false, -1}, kBpm) ==
              exportMidi(plain.doc(), -1, kBpm));
    }
}

TEST_CASE("exportClip: one track lane is a format 0 file of compileTrack, named by trackClipName") {
    DocumentStore store = twoSections();
    const int id = addTrackWith(store, {ns(36, 0, 1920), ns(40, 6720, 1920), ns(43, 14400, 4800)}, TrackRole::Bass);
    for (const int section : {-1, 0, 1}) {
        CAPTURE(section);
        const Bytes clip = exportClip(store.doc(), ClipSpec{{ClipLane::trackLane(id)}, false, section}, kBpm);
        CHECK(clip == writeSmf0(compileTrack(store.doc(), id, section), kBpm, 4, 4, 480, 0,
                                trackClipName(store.doc(), id, section)));
        const File f = requireFile(clip);
        CHECK(f.format == 0);
        CHECK(f.ntrks == 1);
        CHECK(nameOf(f.tracks[0]) == trackClipName(store.doc(), id, section));
    }
    // Literal ticks for the second section: 6720..8640 cut to [7680, 8640) re-based 0..960 ticks at 480 ppq
    // (beats 0..1 -> ticks 0..480), and the clipped 15..16 beat note at beats 7..8.
    const File second = requireFile(exportClip(store.doc(), ClipSpec{{ClipLane::trackLane(id)}, false, 1}, kBpm));
    const std::vector<N> expected = {{0, 0x90, 40, 100}, {480, 0x80, 40, 0}, {3360, 0x90, 43, 100}, {3840, 0x80, 43, 0}};
    CHECK(notesOf(second.tracks[0]) == expected);
    CHECK(endTickOf(second.tracks[0]) == 3840);  // the span's length, 8 beats
}

TEST_CASE("exportClip: several lanes are a format 1 file, ntrks = lanes + 1, lanes in request order") {
    DocumentStore store = twoSections();
    const int bass = addTrackWith(store, {ns(36, 0, 960), ns(38, 8000, 960)}, TrackRole::Bass);
    const int lead = addTrackWith(store, {ns(72, 480, 480)}, TrackRole::Lead);
    const Document& doc = store.doc();

    const Bytes clip = exportClip(doc, ClipSpec{{ClipLane::trackLane(lead), ClipLane::chordsLane(), ClipLane::trackLane(bass)}, false, -1}, kBpm);
    const File f = requireFile(clip);
    CHECK(f.format == 1);
    CHECK(f.ntrks == 4);
    REQUIRE(f.tracks.size() == 4);
    CHECK(nameOf(f.tracks[0]).empty());  // the conductor
    CHECK(nameOf(f.tracks[1]) == "Lead");
    CHECK(nameOf(f.tracks[2]) == exportClipName(doc, -1));
    CHECK(nameOf(f.tracks[3]) == "Bass");
    // The conductor carries the tempo and the meter.
    REQUIRE(f.tracks[0].events.size() == 3);
    CHECK(f.tracks[0].events[0].meta == 0x51);
    CHECK(f.tracks[0].events[0].data == Bytes({0x07, 0xA1, 0x20}));
    CHECK(f.tracks[0].events[1].meta == 0x58);
    CHECK(f.tracks[0].events[1].data == Bytes({0x04, 0x02, 0x18, 0x08}));
    // Every MTrk ends at the chords' end: 16 beats = 7680 ticks.
    for (const FileTrack& t : f.tracks) CHECK(endTickOf(t) == 7680);
    // Each lane holds its own notes.
    CHECK(notesOf(f.tracks[1]) == std::vector<N>({{240, 0x90, 72, 100}, {480, 0x80, 72, 0}}));
    CHECK(notesOf(f.tracks[3]) == std::vector<N>({{0, 0x90, 36, 100}, {480, 0x80, 36, 0}, {4000, 0x90, 38, 100}, {4480, 0x80, 38, 0}}));
    // And the whole file is the definition: the writer over the lanes' own events.
    CHECK(clip == writeSmf1({SmfLane{"Lead", compileTrack(doc, lead, -1).events},
                             SmfLane{exportClipName(doc, -1), compile(doc).events},
                             SmfLane{"Bass", compileTrack(doc, bass, -1).events}},
                            kBpm, 4, 4, 480, compile(doc).endBeat));
}

TEST_CASE("exportClip: the chords lane of a format 1 file is the whole chords, not the merged playback") {
    // A bass note on a chord tone shortens that tone in playback; in the chords clip it keeps its full length.
    DocumentStore store = fourBars();
    const int bass = addTrackWith(store, {ns(60, 960, 960)}, TrackRole::Bass);  // C4 is a tone of the C chord
    REQUIRE(compilePlayback(store.doc()).events != compile(store.doc()).events);
    const File f = requireFile(exportClip(store.doc(), ClipSpec{{ClipLane::chordsLane(), ClipLane::trackLane(bass)}, false, -1}, kBpm));
    REQUIRE(f.tracks.size() == 3);
    std::vector<N> chordNotes = notesOf(f.tracks[1]);
    const auto c4off = std::find_if(chordNotes.begin(), chordNotes.end(), [](const N& n) { return n.status == 0x80 && n.pitch == 60; });
    REQUIRE(c4off != chordNotes.end());
    CHECK(c4off->tick == 1920);  // the chord's full four beats
}

TEST_CASE("exportClip: allLanes is the chords plus every non-empty track, mute and solo ignored") {
    DocumentStore store = twoSections();
    const int bass = addTrackWith(store, {ns(36, 0, 960)}, TrackRole::Bass);
    const int lead = addTrackWith(store, {}, TrackRole::Lead);                       // empty
    addTrackWith(store, {ns(60, 8000, 960)}, TrackRole::Other);                      // second section only
    REQUIRE(store.setTrackMute(bass, true));
    REQUIRE(store.setTrackSolo(lead, true));
    REQUIRE(store.setChordsMute(true));
    const Document& doc = store.doc();
    ClipSpec all;
    all.allLanes = true;

    all.sectionIndex = -1;
    File f = requireFile(exportClip(doc, all, kBpm));
    CHECK(f.format == 1);
    CHECK(f.ntrks == 4);  // conductor, chords, Bass, Track
    REQUIRE(f.tracks.size() == 4);
    CHECK(nameOf(f.tracks[1]) == exportClipName(doc, -1));
    CHECK(nameOf(f.tracks[2]) == "Bass");
    CHECK(nameOf(f.tracks[3]) == "Track");

    all.sectionIndex = 0;  // the first section: Bass has a note, the third track has none
    f = requireFile(exportClip(doc, all, kBpm));
    CHECK(f.ntrks == 3);
    REQUIRE(f.tracks.size() == 3);
    CHECK(nameOf(f.tracks[1]) == "Section 1");
    CHECK(nameOf(f.tracks[2]) == "Bass \xE2\x80\x93 Section 1");

    all.sectionIndex = 1;  // the second section: the third track has a note, Bass none
    f = requireFile(exportClip(doc, all, kBpm));
    CHECK(f.ntrks == 3);
    REQUIRE(f.tracks.size() == 3);
    CHECK(nameOf(f.tracks[1]) == "F G7");
    CHECK(nameOf(f.tracks[2]) == "Track \xE2\x80\x93 F G7");
}

TEST_CASE("exportClip: allLanes with only the chords lane non-empty is the format 0 chords file") {
    DocumentStore store = twoSections();
    addTrackWith(store, {}, TrackRole::Bass);
    addTrackWith(store, {ns(60, 8000, 960)}, TrackRole::Other);  // second section only
    ClipSpec all;
    all.allLanes = true;
    for (const int section : {-1, 0, 1}) {
        all.sectionIndex = section;
        const Bytes clip = exportClip(store.doc(), all, kBpm);
        CAPTURE(section);
        if (section == 0) {
            CHECK(clip == exportMidi(store.doc(), 0, kBpm));
            CHECK(requireFile(clip).format == 0);
        } else {
            CHECK(requireFile(clip).format == 1);
        }
    }
    DocumentStore bare = twoSections();
    addTrackWith(bare, {}, TrackRole::Bass);
    for (const int section : {-1, 0, 1}) {
        all.sectionIndex = section;
        CHECK(exportClip(bare.doc(), all, kBpm) == exportMidi(bare.doc(), section, kBpm));
    }
}

TEST_CASE("exportClip: an explicit lane with no notes is an empty lane of full length next to lanes that have some") {
    DocumentStore store = twoSections();
    const int lead = addTrackWith(store, {}, TrackRole::Lead);
    const int bass = addTrackWith(store, {ns(36, 0, 960)}, TrackRole::Bass);
    const Document& doc = store.doc();
    const File f = requireFile(exportClip(doc, ClipSpec{{ClipLane::chordsLane(), ClipLane::trackLane(lead)}, false, -1}, kBpm));
    CHECK(f.format == 1);
    REQUIRE(f.tracks.size() == 3);
    CHECK(nameOf(f.tracks[2]) == "Lead");
    CHECK(notesOf(f.tracks[2]).empty());
    CHECK(endTickOf(f.tracks[2]) == 7680);
    // Only empty lanes, or one empty lane: nothing to export.
    CHECK(exportClip(doc, ClipSpec{{ClipLane::trackLane(lead)}, false, -1}, kBpm).empty());
    CHECK(exportClip(doc, ClipSpec{{ClipLane::trackLane(lead), ClipLane::trackLane(lead)}, false, -1}, kBpm).empty());
    // A track with notes only in the second section: its first-section lane is empty, next to the chords it is kept.
    CHECK(exportClip(doc, ClipSpec{{ClipLane::trackLane(bass)}, false, 1}, kBpm).empty());
    const File mixed = requireFile(exportClip(doc, ClipSpec{{ClipLane::trackLane(bass), ClipLane::chordsLane()}, false, 1}, kBpm));
    CHECK(mixed.ntrks == 3);
    CHECK(notesOf(mixed.tracks[1]).empty());
    CHECK(endTickOf(mixed.tracks[1]) == endTickOf(mixed.tracks[2]));
}

TEST_CASE("exportClip: nothing to export gives no bytes") {
    ClipSpec all;
    all.allLanes = true;
    CHECK(exportClip(Document{}, all, kBpm).empty());                                         // empty progression
    CHECK(exportClip(Document{}, ClipSpec{{ClipLane::chordsLane()}, false, -1}, kBpm).empty());
    CHECK(exportClip(Document{}, ClipSpec{{ClipLane::chordsLane()}, false, 0}, kBpm).empty());

    DocumentStore store = twoSections();
    const int id = addTrackWith(store, {ns(36, 0, 960)});
    const Document& doc = store.doc();
    CHECK(exportClip(doc, ClipSpec{}, kBpm).empty());                                         // no lanes
    CHECK(exportClip(doc, ClipSpec{{ClipLane::trackLane(id + 1)}, false, -1}, kBpm).empty());  // unknown track
    CHECK(exportClip(doc, ClipSpec{{ClipLane::chordsLane(), ClipLane::trackLane(id + 1)}, false, -1}, kBpm).empty());
    CHECK(exportClip(doc, ClipSpec{{ClipLane::trackLane(kNoTrackId)}, false, -1}, kBpm).empty());
    CHECK(exportClip(doc, ClipSpec{{ClipLane::chordsLane()}, false, 2}, kBpm).empty());       // bad section
    CHECK(exportClip(doc, ClipSpec{{ClipLane::trackLane(id)}, false, 2}, kBpm).empty());
    all.sectionIndex = 2;
    CHECK(exportClip(doc, all, kBpm).empty());
    CHECK(exportClip(fourBars().doc(), ClipSpec{{ClipLane::chordsLane()}, false, 1}, kBpm).empty());  // no sections: only 0
    // A tempo the writer refuses.
    CHECK(exportClip(doc, ClipSpec{{ClipLane::chordsLane()}, false, -1}, 0.0).empty());
    CHECK(exportClip(doc, ClipSpec{{ClipLane::chordsLane(), ClipLane::trackLane(id)}, false, -1}, 0.0).empty());
    CHECK(exportClip(doc, ClipSpec{{ClipLane::chordsLane(), ClipLane::trackLane(id)}, false, -1},
                     std::numeric_limits<double>::quiet_NaN()).empty());
    CHECK(exportClip(doc, all, 1e9).empty());
}

TEST_CASE("exportClip: the meter goes into the conductor, and allLanes ignores allLanes' lane list") {
    DocumentStore store = fourBars();
    addTrackWith(store, {ns(36, 0, 960)}, TrackRole::Bass);
    store.setMeter(Meter{6, 8});
    ClipSpec all;
    all.allLanes = true;
    all.lanes = {ClipLane::trackLane(999)};  // ignored
    const File f = requireFile(exportClip(store.doc(), all, 90.0));
    REQUIRE(f.tracks[0].events.size() == 3);
    CHECK(f.tracks[0].events[1].data == Bytes({0x06, 0x03, 0x18, 0x08}));
    CHECK(f.tracks[0].events[0].data == Bytes({0x0A, 0x2C, 0x2B}));  // 60e6 / 90 = 666667 us
}

TEST_CASE("exportClip: every lane of a file ends at the same tick, on the block path and under a feel") {
    for (const bool performing : {false, true}) {
        DocumentStore store;
        for (const double length : {0.2506, 1.0013, 0.7497, 2.0, 0.2504, 3.0}) store.appendChord(chord(0, "maj"), length, 100);
        REQUIRE(store.addSection(2, "B") != kNoSectionId);
        REQUIRE(store.addSection(4, "C") != kNoSectionId);
        if (performing) store.setFeel(Feel{Rhythm::Pulse});
        const int bass = addTrackWith(store, {ns(36, 0, 100), ns(40, 1500, 200), ns(43, 2200, 90)}, TrackRole::Bass);
        const int lead = addTrackWith(store, {ns(72, 100, 5000)}, TrackRole::Lead);
        for (const int section : {-1, 0, 1, 2}) {
            CAPTURE(performing);
            CAPTURE(section);
            const File f = requireFile(exportClip(
                store.doc(), ClipSpec{{ClipLane::trackLane(lead), ClipLane::chordsLane(), ClipLane::trackLane(bass)}, false, section}, kBpm));
            REQUIRE(f.tracks.size() == 4);
            const Compiled chords = section < 0 ? compile(store.doc()) : compileSection(store.doc(), section);
            for (const FileTrack& t : f.tracks) CHECK(endTickOf(t) == std::lround(chords.endBeat * 480));
            // ... which is the tick exportMidi writes for the same section.
            CHECK(endTickOf(f.tracks[0]) == endTickOf(requireFile(exportMidi(store.doc(), section, kBpm)).tracks[0]));
        }
    }
}

// --- Performing sections are re-based in integer ticks -------------------------------------------------------

namespace {

/// `oddTickDoc(seed)` with a bass note of 60 ticks on every chord's onset tick, pitch 30 + chord index.
DocumentStore oddTickDocWithBass(int seed, int& bass) {
    DocumentStore store = oddTickDoc(seed);
    const std::vector<std::int32_t> ticks = chordBoundaryTicks(store.doc());
    std::vector<NoteSpec> specs;
    for (std::size_t i = 0; i < 12; ++i) specs.push_back(ns(30 + static_cast<int>(i), ticks[i], 60));
    bass = addTrackWith(store, specs, TrackRole::Bass);
    return store;
}

/// The chord index range [first, end) of `section` (-1: all twelve) of `oddTickDoc`.
std::pair<std::size_t, std::size_t> chordRange(const Document& doc, int section) {
    if (section < 0) return {0, doc.events.size()};
    const std::size_t at = static_cast<std::size_t>(section);
    return {static_cast<std::size_t>(doc.sections[at].firstEventIndex),
            at + 1 < doc.sections.size() ? static_cast<std::size_t>(doc.sections[at + 1].firstEventIndex) : doc.events.size()};
}

}  // namespace

TEST_CASE("exportClip: under a feel a track note on a chord's onset tick exports at the chord's own 480-ppq tick") {
    for (int seed = 0; seed < kSweep; ++seed) {
        int bass = kNoTrackId;
        const DocumentStore store = oddTickDocWithBass(seed, bass);
        const Document& doc = store.doc();
        const std::vector<std::int32_t> ticks = chordBoundaryTicks(doc);
        for (int section = -1; section < 4; ++section) {
            const auto [first, end] = chordRange(doc, section);
            const File g = requireFile(exportClip(doc, ClipSpec{{ClipLane::chordsLane(), ClipLane::trackLane(bass)}, false, section}, kBpm));
            REQUIRE(g.tracks.size() == 3);
            ClipSpec all;
            all.allLanes = true;
            all.sectionIndex = section;
            const File f = requireFile(exportClip(doc, all, kBpm));
            REQUIRE(f.tracks.size() == 3);
            const Compiled chords = section < 0 ? compile(doc) : compileSection(doc, section);
            for (std::size_t i = first; i < end; ++i) {
                CAPTURE(seed);
                CAPTURE(section);
                CAPTURE(i);
                // The chord's note-on tick: its first on event, at (tick - base) / 960.0 exactly as sliceRange gives it.
                const double onBeat = static_cast<double>(ticks[i] - ticks[first]) / kTicks;
                long chordOn = -1;
                for (const NoteEvent& e : chords.events) {
                    if (e.on && e.beat == onBeat) {
                        chordOn = std::lround(e.beat * 480);
                        break;
                    }
                }
                REQUIRE(chordOn >= 0);
                for (const File* file : {&f, &g}) {
                    const FileTrack& lane = file->tracks.back();
                    long trackOn = -1;
                    for (const N& n : notesOf(lane)) {
                        if (n.status == 0x90 && n.pitch == 30 + static_cast<int>(i)) trackOn = n.tick;
                    }
                    CHECK(trackOn == chordOn);
                }
            }
        }
    }
}

TEST_CASE("exportClip: under a feel a one-lane track clip is as long as the chords clip of its section, and a format 1 file ends where exportMidi does") {
    for (int seed = 0; seed < kSweep; ++seed) {
        int bass = kNoTrackId;
        const DocumentStore store = oddTickDocWithBass(seed, bass);
        const Document& doc = store.doc();
        for (int section = -1; section < 4; ++section) {
            CAPTURE(seed);
            CAPTURE(section);
            const File chordsOnly = requireFile(exportClip(doc, ClipSpec{{ClipLane::chordsLane()}, false, section}, kBpm));
            const File trackOnly = requireFile(exportClip(doc, ClipSpec{{ClipLane::trackLane(bass)}, false, section}, kBpm));
            REQUIRE(chordsOnly.format == 0);
            REQUIRE(trackOnly.format == 0);
            CHECK(endTickOf(trackOnly.tracks[0]) == endTickOf(chordsOnly.tracks[0]));
            const Compiled chords = section < 0 ? compile(doc) : compileSection(doc, section);
            CHECK(compileTrack(doc, bass, section).endBeat == chords.endBeat);  // bit for bit
            ClipSpec all;
            all.allLanes = true;
            all.sectionIndex = section;
            const File both = requireFile(exportClip(doc, all, kBpm));
            REQUIRE(both.format == 1);
            const File midi = requireFile(exportMidi(doc, section, kBpm));
            for (const FileTrack& t : both.tracks) CHECK(endTickOf(t) == endTickOf(midi.tracks[0]));
        }
    }
}

TEST_CASE("compileTrack: under a feel the cut and the minimum length are judged in the integer ticks") {
    // Boundaries 0, 965, 1687, 2647: chords of 1.0052, 0.7526 and 1.0 beats; section "B" is [965, 1687).
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 1.0052, 100);
    store.appendChord(chord(5, "maj"), 0.7526, 100);
    store.appendChord(chord(7, "7"), 1.0, 100);
    REQUIRE(store.addSection(1, "B") != kNoSectionId);
    REQUIRE(store.addSection(2, "C") != kNoSectionId);
    store.setFeel(Feel{Rhythm::Pulse});
    REQUIRE_FALSE(compile(store.doc()).feelLimited);
    REQUIRE(chordBoundaryTicks(store.doc()) == std::vector<std::int32_t>({0, 965, 1687, 2647}));
    // [900, 970) runs 5 ticks into the section (kMinNoteTicks: kept), [880, 966) 1 tick (dropped); [1000, 1700) and
    // [1680, 1780) are cut at the section's end 1687.
    const int id = addTrackWith(store, {ns(40, 900, 70), ns(41, 880, 86), ns(42, 1000, 700), ns(43, 1680, 100)});
    const Compiled one = compileTrack(store.doc(), id, 1);
    CHECK(one.endBeat == 722 / kTicks);
    const std::vector<S> got = soundings(one);
    REQUIRE(got.size() == 3);
    CHECK(got[0].pitch == 40);
    CHECK(got[0].start == 0.0);
    CHECK(got[0].end == 5 / kTicks);
    CHECK(got[1].pitch == 42);
    CHECK(got[1].start == 35 / kTicks);
    CHECK(got[1].end == 722 / kTicks);
    CHECK(got[2].pitch == 43);
    CHECK(got[2].start == 715 / kTicks);
    CHECK(got[2].end == 722 / kTicks);
}

TEST_CASE("exportClip: a lane listed twice is written twice") {
    DocumentStore store = fourBars();
    const File f = requireFile(exportClip(store.doc(), ClipSpec{{ClipLane::chordsLane(), ClipLane::chordsLane()}, false, -1}, kBpm));
    CHECK(f.format == 1);
    CHECK(f.ntrks == 3);
    CHECK(notesOf(f.tracks[1]) == notesOf(f.tracks[2]));
}
