// latency_bench: the tracks design's D6 latency gate (not a test; check.sh only compiles it).
//
// Build and run (Release, its own build directory):
//   cmake -S engine -B engine/build-bench -DCMAKE_BUILD_TYPE=Release
//   cmake --build engine/build-bench --target latency_bench
//   engine/build-bench/latency_bench [runs] [snapshot-dump-path]
// Then time the Swift decode of the dumped snapshot: engine/tools/latency_bench/decode_bench.sh <dump>.
//
// It builds the worst document (256 one-bar chords with auto voice-leading and a section every 16 chords; 4 tracks
// of 1024 notes at the caps: two notes per onset per track, so 8 sound at once, the polyphony cap; a full 200-step
// undo history) and times the shell's command path (ChordLab::OnMessage in ChordLab/ChordLab.mm): applyCommandJson
// with the dirty check, RebuildPaletteLocked, PublishSnapshotLocked (fillSlot into a preallocated slot) and
// PushSnapshot (snapshotJson with SnapshotExtras), for a note edit, a selection change and a chord length edit that
// ripples every note; and (d) the note edit again on a leader whose four tracks are all followed (per-track
// instruments M1): its own fillSlot plays the chords only (AllExcept the four tracks), then it builds the link feed
// with the four tracks compiled and fills four followers' slots from it (fillSlotFromFeed). Then the audio thread: Scheduler::adopt of a full kMaxSnapshotEvents slot, the flush-and-chase
// block after it, seek-and-chase blocks and steady blocks at 64 frames.

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/guide.h"
#include "chordlab/json_api.h"
#include "chordlab/limits.h"
#include "chordlab/link.h"
#include "chordlab/palette.h"
#include "chordlab/publication.h"
#include "chordlab/scheduler.h"
#include "chordlab/tracks.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace chordlab;

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void require(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "latency_bench: %s\n", what);
        std::exit(1);
    }
}

struct Stats {
    double p50 = 0.0, p95 = 0.0, p99 = 0.0, max = 0.0, mean = 0.0;
};

Stats stats(std::vector<double> v) {
    Stats s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    const auto at = [&](double q) {
        const std::size_t i = static_cast<std::size_t>(std::ceil(q * static_cast<double>(v.size()))) - 1;
        return v[std::min(i, v.size() - 1)];
    };
    s.p50 = at(0.50);
    s.p95 = at(0.95);
    s.p99 = at(0.99);
    s.max = v.back();
    double sum = 0.0;
    for (double x : v) sum += x;
    s.mean = sum / static_cast<double>(v.size());
    return s;
}

// --- the worst document --------------------------------------------------------------------------------------

constexpr int kChords = kMaxChords;        // 256 one-bar chords: 1024 beats
constexpr std::int64_t kOnsetSpacing = 1920;  // two beats: 512 onsets x 2 notes = 1024 notes per track
constexpr std::int64_t kNoteLength = 960;
constexpr int kTrackBase[kMaxTracks] = {24, 29, 96, 101};  // clear of the chords' pitches: nothing merges

struct Bench {
    std::unique_ptr<DocumentStore> store = std::make_unique<DocumentStore>();
    std::vector<int> trackIds;
};

Chord chordAt(int i) {
    // A ii-V-I-vi walk in sevenths and ninths, moving through the keys: five-note chords, borrowed roots.
    static const char* const kTypes[] = {"m7", "7", "maj7", "m9", "9", "7#11", "m7b5", "maj9"};
    return Chord{static_cast<PitchClass>((i * 5) % 12), &chordType(kTypes[i % 8])};
}

Bench buildWorst(const Feel& feel) {
    Bench b;
    DocumentStore& store = *b.store;
    for (int i = 0; i < kChords; ++i) {
        require(store.appendChord(chordAt(i), 4.0, 100) != kNoEventId, "appendChord refused");
    }
    store.setAutoVoiceLead(true);
    store.setFeel(feel);
    for (int s = 0; s < kChords; s += 16) {
        require(store.addSection(s) != kNoSectionId, "addSection refused");
    }
    for (int t = 0; t < kMaxTracks; ++t) {
        const int id = store.addTrack(t < 2 ? TrackRole::Bass : TrackRole::Lead);
        require(id != kNoTrackId, "addTrack refused");
        b.trackIds.push_back(id);
        std::vector<NoteSpec> specs;
        for (int k = 0; k < kMaxNotesPerTrack / 2; ++k) {
            specs.push_back(NoteSpec{kTrackBase[t], k * kOnsetSpacing, kNoteLength, 90});
            specs.push_back(NoteSpec{kTrackBase[t] + 2, k * kOnsetSpacing, kNoteLength, 90});
        }
        std::string refusal;
        require(store.insertNotes(id, specs, &refusal).size() == specs.size(), "insertNotes refused");
    }
    require(trackPolyphony(store.doc()) == kMaxTrackPolyphony, "the document is not at the polyphony cap");
    // A full history: velocity edits of one note, alternating, through the store (not timed).
    const int note = store.doc().tracks[3].notes[100].id;
    for (std::size_t i = 0; i < DocumentStore::kMaxUndoDepth + 10; ++i) {
        require(store.updateNotes(b.trackIds[3], {NoteEdit{note, std::nullopt, std::nullopt, std::nullopt,
                                                           (i % 2 == 0) ? 80 : 90}}),
                "history edit refused");
    }
    // A live selection of 16 notes, as the roll would leave after a rubber band.
    std::vector<int> sel;
    for (int k = 0; k < 16; ++k) sel.push_back(store.doc().tracks[0].notes[static_cast<std::size_t>(k)].id);
    store.setNoteSelection(b.trackIds[0], sel);
    return b;
}

// --- the shell's command path ---------------------------------------------------------------------------------

/// What a leader adds when every track of its document is followed by another instance (per-track instruments
/// M1): its own slot carries every lane except the followed ones, and each publish builds a feed and fills each
/// follower's link slot from it (in the plug-in the registry does those fills under its mutex).
struct LinkedLeader {
    LaneUids uids;
    LaneSelection own{LaneSelection::Mode::AllExcept, {}};
    std::vector<std::string> followed;
    std::vector<std::unique_ptr<SnapshotSlot>> followerSlots;
    std::vector<Publication> followerPublications;
    std::shared_ptr<const LinkFeed> feed;

    explicit LinkedLeader(const Document& doc) {
        int n = 0;
        require(uids.cover(doc, {}, [&n] { return "lane-uid-" + std::to_string(++n); }), "cover changed nothing");
        for (const Track& t : doc.tracks) {
            own.lanes.push_back(trackLane(t.id));
            followed.push_back(uids.uidOf(t.id));
            followerSlots.push_back(std::make_unique<SnapshotSlot>());
            followerPublications.emplace_back();
        }
    }

    /// The link part of a publish: the feed, then every follower's fill.
    void publishLinks(const Document& doc) {
        feed = buildLinkFeed(doc, "bench-document", uids, "bench-lineage", "ChordLab A", followed);
        for (std::size_t i = 0; i < followed.size(); ++i) {
            fillSlotFromFeed(feed.get(), followed[i], "bench-document", *followerSlots[i], followerPublications[i]);
        }
    }
};

/// What ChordLab::OnMessage does under mModelMutex for an accepted command, then PushSnapshot's section. With a
/// `LinkedLeader`, the fill is the leader's own (its selection) and the link part follows it.
struct Shell {
    DocumentStore& store;
    Palette palette;
    std::unique_ptr<SnapshotSlot> slot = std::make_unique<SnapshotSlot>();
    Publication publication;
    std::string lastJson;
    std::unique_ptr<LinkedLeader> leader;

    explicit Shell(DocumentStore& s, bool linked = false)
        : store(s), palette(buildPalette(s.doc().browsedKey, s.doc().projectKey)) {
        if (linked) {
            leader = std::make_unique<LinkedLeader>(store.doc());
            fillSlot(store.doc(), leader->own, *slot, publication);
            leader->publishLinks(store.doc());
        } else {
            fillSlot(store.doc(), *slot, publication);
        }
    }
};

struct Sample {
    double apply, palette, fill, link, snapshot, total;
};

Sample runCommand(Shell& shell, const std::string& json) {
    Sample s{};
    std::string error;
    bool documentChanged = false;
    const auto t0 = Clock::now();
    const bool accepted = applyCommandJson(shell.store, json, error, &documentChanged);
    s.apply = msSince(t0);
    if (!accepted) {
        std::fprintf(stderr, "latency_bench: command refused: %s\n  %s\n", error.c_str(), json.c_str());
        std::exit(1);
    }
    const auto t1 = Clock::now();
    shell.palette = buildPalette(shell.store.doc().browsedKey, shell.store.doc().projectKey);  // RebuildPaletteLocked
    s.palette = msSince(t1);
    const auto t2 = Clock::now();
    if (shell.leader) {
        fillSlot(shell.store.doc(), shell.leader->own, *shell.slot, shell.publication);
    } else {
        fillSlot(shell.store.doc(), *shell.slot, shell.publication);  // PublishSnapshotLocked (publish is a swap)
    }
    s.fill = msSince(t2);
    if (shell.leader) {
        const auto tl = Clock::now();
        shell.leader->publishLinks(shell.store.doc());
        s.link = msSince(tl);
    }
    const auto t3 = Clock::now();
    const SnapshotExtras extras{shell.store.noteSelection(), shell.store.revision()};
    shell.lastJson = snapshotJson(shell.store.doc(), shell.palette, shell.store.selectedEventId(),
                                  shell.store.selectedEventIds(), extras);
    s.snapshot = msSince(t3);
    s.total = s.apply + s.palette + s.fill + s.link + s.snapshot;
    return s;
}

void printPath(const char* name, const std::vector<Sample>& samples, bool linked = false) {
    std::vector<double> apply, palette, fill, link, snapshot, total;
    for (const Sample& s : samples) {
        apply.push_back(s.apply);
        palette.push_back(s.palette);
        fill.push_back(s.fill);
        link.push_back(s.link);
        snapshot.push_back(s.snapshot);
        total.push_back(s.total);
    }
    const Stats t = stats(total);
    std::printf("  %s (%zu runs)\n", name, samples.size());
    std::printf("    %-34s %8s %8s %8s %8s %7s\n", "stage (ms)", "p50", "p95", "max", "mean", "share");
    const auto row = [&](const char* label, const std::vector<double>& v) {
        const Stats s = stats(v);
        std::printf("    %-34s %8.3f %8.3f %8.3f %8.3f %6.1f%%\n", label, s.p50, s.p95, s.max, s.mean,
                    100.0 * s.mean / t.mean);
    };
    row("applyCommandJson (+ dirty check)", apply);
    row("buildPalette", palette);
    if (linked) {
        row("fillSlot (AllExcept four tracks)", fill);
        row("buildLinkFeed + 4 fillSlotFromFeed", link);
    } else {
        row("fillSlot (compilePlayback)", fill);
    }
    row("snapshotJson", snapshot);
    row("total", total);
}

template <typename F>
std::vector<double> timeRuns(int runs, F&& f) {
    std::vector<double> v;
    v.reserve(static_cast<std::size_t>(runs));
    for (int i = 0; i < runs; ++i) {
        const auto t0 = Clock::now();
        f(i);
        v.push_back(msSince(t0));
    }
    return v;
}

void printPart(const char* label, const std::vector<double>& v) {
    const Stats s = stats(v);
    std::printf("    %-34s %8.3f %8.3f %8.3f %8.3f\n", label, s.p50, s.p95, s.max, s.mean);
}

volatile std::size_t gSink = 0;  // keeps the parts' results alive

void commandPaths(const char* variant, const Feel& feel, int runs, const char* dumpPath) {
    Bench b = buildWorst(feel);
    DocumentStore& store = *b.store;
    const Compiled chords = compile(store.doc());
    const Compiled playback = compilePlayback(store.doc());
    std::printf("\n== %s: %zu chords, %d tracks x %zu notes, history %zu steps; compile %zu events%s, "
                "compilePlayback %zu events\n",
                variant, store.doc().events.size(), kMaxTracks, store.doc().tracks[0].notes.size(),
                DocumentStore::kMaxUndoDepth, chords.events.size(), chords.feelLimited ? " (feelLimited: block)" : "",
                playback.events.size());
    Shell shell(store);

    // (a) a note edit: a drag of one note (pitch down one, start +240 ticks), as the roll sends it, alternating so
    // that every run is a real change; with baseRevision as the roll's gestures carry it.
    const int trackId = b.trackIds[0];
    const TrackNote moved = store.doc().tracks[0].notes[500];
    const std::int64_t baseStart = noteStart(store.doc(), moved);
    std::vector<Sample> noteEdit;
    for (int i = 0; i < runs; ++i) {
        const bool away = i % 2 == 0;
        const std::string json = "{\"schema\":1,\"cmd\":\"updateNotes\",\"trackId\":" + std::to_string(trackId) +
                                 ",\"baseRevision\":" + std::to_string(store.revision()) + ",\"notes\":[{\"id\":" +
                                 std::to_string(moved.id) + ",\"pitch\":" + std::to_string(moved.pitch - (away ? 1 : 0)) +
                                 ",\"start\":" + std::to_string(baseStart + (away ? 240 : 0)) + "}]}";
        noteEdit.push_back(runCommand(shell, json));
    }
    const std::size_t snapshotBytes = shell.lastJson.size();

    // (b) a selection change: 16 notes, then 16 others.
    std::vector<Sample> selection;
    for (int i = 0; i < runs; ++i) {
        std::string ids;
        for (int k = 0; k < 16; ++k) {
            const std::size_t at = static_cast<std::size_t>((i % 2 == 0 ? 200 : 300) + k);
            ids += (k ? "," : "") + std::to_string(store.doc().tracks[1].notes[at].id);
        }
        const std::string json = "{\"schema\":1,\"cmd\":\"selectNotes\",\"trackId\":" + std::to_string(b.trackIds[1]) +
                                 ",\"ids\":[" + ids + "]}";
        selection.push_back(runCommand(shell, json));
    }

    // (c) a chord timing edit: chord 0 shortened by half a beat and back, which ripples every note after it.
    const int firstChord = store.doc().events[0].id;
    std::vector<Sample> ripple;
    for (int i = 0; i < runs; ++i) {
        const std::string json = "{\"schema\":1,\"cmd\":\"setLength\",\"id\":" + std::to_string(firstChord) +
                                 ",\"lengthBeats\":" + (i % 2 == 0 ? "3.5" : "4") + "}";
        ripple.push_back(runCommand(shell, json));
    }

    // (d) the note edit of (a) on a leader whose four tracks are all followed: its own fill, the feed and the four
    // followers' fills. The edited note is in a followed track, so the leader's own content does not change (its
    // fill compiles and compares) while one follower's does.
    Shell linkedShell(store, true);
    std::vector<Sample> linkedNoteEdit;
    for (int i = 0; i < runs; ++i) {
        const bool away = i % 2 == 0;
        const std::string json = "{\"schema\":1,\"cmd\":\"updateNotes\",\"trackId\":" + std::to_string(trackId) +
                                 ",\"baseRevision\":" + std::to_string(store.revision()) + ",\"notes\":[{\"id\":" +
                                 std::to_string(moved.id) + ",\"pitch\":" + std::to_string(moved.pitch - (away ? 1 : 0)) +
                                 ",\"start\":" + std::to_string(baseStart + (away ? 240 : 0)) + "}]}";
        linkedNoteEdit.push_back(runCommand(linkedShell, json));
    }
    require(linkedShell.leader->followerPublications[0].generation == kLinkGenerationBase + 1 +
                                                                           static_cast<std::uint32_t>(runs),
            "the followed track's follower did not see every edit");

    printPath("(a) note edit: updateNotes", noteEdit);
    printPath("(b) selection change: selectNotes", selection);
    printPath("(c) chord timing edit: setLength chord 0 (ripples every note)", ripple);
    printPath("(d) note edit, four tracks followed: updateNotes on the leader", linkedNoteEdit, true);

    // What the stages are made of, timed alone on the same document.
    std::printf("  parts (ms, %d runs each)\n", runs);
    std::printf("    %-34s %8s %8s %8s %8s\n", "part", "p50", "p95", "max", "mean");
    printPart("Document copy (dirty check)", timeRuns(runs, [&](int) {
                  const Document copy = store.doc();
                  gSink = gSink + copy.tracks.size();
              }));
    const Document copy = store.doc();
    printPart("Document == (dirty check)", timeRuns(runs, [&](int) { gSink = gSink + (copy == store.doc() ? 1 : 0); }));
    printPart("applyCommandJson, no dirty check", timeRuns(runs, [&](int i) {
                  std::string error;
                  const bool away = i % 2 == 0;
                  const std::string json =
                      "{\"schema\":1,\"cmd\":\"updateNotes\",\"trackId\":" + std::to_string(trackId) +
                      ",\"notes\":[{\"id\":" + std::to_string(moved.id) + ",\"pitch\":" +
                      std::to_string(moved.pitch - (away ? 1 : 0)) + ",\"start\":" +
                      std::to_string(baseStart + (away ? 240 : 0)) + "}]}";
                  require(applyCommandJson(store, json, error), "note edit refused");
              }));
    printPart("compile (chords)", timeRuns(runs, [&](int) { gSink = gSink + compile(store.doc()).events.size(); }));
    printPart("compilePlayback", timeRuns(runs, [&](int) { gSink = gSink + compilePlayback(store.doc()).events.size(); }));
    printPart("buildLinkFeed, four tracks followed", timeRuns(runs, [&](int) {
                  gSink = gSink + buildLinkFeed(store.doc(), "d", linkedShell.leader->uids, "l", "L",
                                                linkedShell.leader->followed)->lanes.size();
              }));
    printPart("voicings", timeRuns(runs, [&](int) { gSink = gSink + voicings(store.doc()).size(); }));
    printPart("chordPlaySpans (given compile)", timeRuns(runs, [&](int) {
                  gSink = gSink + chordPlaySpans(store.doc(), chords).size();
              }));
    printPart("harmonicGuide x 256", timeRuns(runs, [&](int) {
                  for (const auto& e : store.doc().events) gSink = gSink + harmonicGuide(e.chord, store.doc().projectKey).scaleMask;
              }));
    {
        // The tracks' share of snapshotJson: the same document with its tracks' notes, then its tracks, removed.
        const SnapshotExtras extras{store.noteSelection(), store.revision()};
        Document noNotes = store.doc();
        for (Track& t : noNotes.tracks) t.notes.clear();
        printPart("snapshotJson, tracks without notes", timeRuns(runs, [&](int) {
                      gSink = gSink + snapshotJson(noNotes, shell.palette, -1, {}, extras).size();
                  }));
        Document noTracks = noNotes;
        noTracks.tracks.clear();
        printPart("snapshotJson, no tracks", timeRuns(runs, [&](int) {
                      gSink = gSink + snapshotJson(noTracks, shell.palette, -1, {}, extras).size();
                  }));
    }
    std::printf("  snapshot JSON: %zu bytes (%.1f KiB)\n", snapshotBytes, static_cast<double>(snapshotBytes) / 1024.0);

    if (dumpPath != nullptr) {
        // The note-edit snapshot, the worst the UI decodes per edit.
        const SnapshotExtras extras{store.noteSelection(), store.revision()};
        const std::string json =
            snapshotJson(store.doc(), shell.palette, store.selectedEventId(), store.selectedEventIds(), extras);
        std::ofstream out(dumpPath, std::ios::binary);
        out << json;
        std::printf("  dumped %zu bytes to %s\n", json.size(), dumpPath);
    }
}

// --- the audio thread ----------------------------------------------------------------------------------------

constexpr int kFrames = 64;

/// The full-slot document of test_publication.cpp: 128 one-bar sevenths played Pulse (kMaxChordEvents) and
/// kMaxTracks x kMaxNotesPerTrack short notes, nothing merged: exactly kMaxSnapshotEvents events.
std::unique_ptr<DocumentStore> fullSlotDocument() {
    auto store = std::make_unique<DocumentStore>();
    for (int i = 0; i < 128; ++i) store->appendChord(Chord{static_cast<PitchClass>(i % 12), &chordType("7")}, 4.0, 100);
    store->setFeel(Feel{Rhythm::Pulse});
    const std::int64_t end = chordBoundaryTicks(store->doc()).back();
    const std::int64_t spacing = end / kMaxNotesPerTrack;
    for (int t = 0; t < kMaxTracks; ++t) {
        const int id = store->addTrack(TrackRole::Bass);
        std::vector<NoteSpec> specs;
        for (int k = 0; k < kMaxNotesPerTrack; ++k) specs.push_back(NoteSpec{24 + 2 * t, k * spacing, kMinTrackNoteTicks, 100});
        require(store->insertNotes(id, specs).size() == specs.size(), "full-slot insertNotes refused");
    }
    return store;
}

double usSince(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

void audioRow(const char* label, const std::vector<double>& us, double deadlineUs) {
    const Stats s = stats(us);
    std::printf("    %-30s %9.2f %9.2f %9.2f  %5.1f%% %5.1f%%\n", label, s.p50, s.p99, s.max, 100.0 * s.p99 / deadlineUs,
                100.0 * s.max / deadlineUs);
}

void audioThread(const SnapshotSlot& slot, const char* label) {
    std::printf("\n== audio thread: %s, %zu events, endBeat %.1f, %d-frame blocks, output capacity 512\n", label,
                slot.count, slot.endBeat, kFrames);
    std::printf("    %-30s %9s %9s %9s  %6s %6s\n", "us", "p50", "p99", "max", "p99/dl", "max/dl");
    std::array<MidiOut, 512> out{};  // ChordLab.h: kMidiOutCapacity
    const double rates[] = {44100.0, 48000.0};
    const double tempos[] = {120.0, 300.0};
    std::mt19937 rng(20261114);
    for (double sr : rates) {
        for (double bpm : tempos) {
            const double deadlineUs = 1e6 * kFrames / sr;
            const double beatsPerBlock = kFrames * bpm / 60.0 / sr;
            std::printf("  %.1f kHz, %.0f bpm (deadline %.0f us)\n", sr / 1000.0, bpm, deadlineUs);
            auto scheduler = std::make_unique<Scheduler>();
            scheduler->reset(sr);
            std::uint32_t generation = 1;
            Snapshot snap{slot.events.data(), slot.count, slot.endBeat, slot.loop, generation};
            scheduler->adopt(snap);
            std::size_t written = 0;
            double ppq = 0.0;
            const auto block = [&](double at) {
                const BlockInfo info{at, kFrames, sr, bpm, true, true};
                scheduler->process(info, out.data(), out.size(), written);
            };
            for (int i = 0; i < 100; ++i, ppq += beatsPerBlock) block(ppq);  // warm, playing

            // adopt of a new generation alone, then the block after it (release all + chase over the slot).
            std::vector<double> adoptUs, flushUs, seekUs, worstSeekUs, steadyUs;
            std::uniform_real_distribution<double> anywhere(0.0, 2.0 * slot.endBeat);
            for (int i = 0; i < 2000; ++i) {
                ppq = anywhere(rng);
                block(ppq);  // a seek to somewhere
                ppq += beatsPerBlock;
                block(ppq);
                ppq += beatsPerBlock;
                snap.generation = ++generation;
                auto t0 = Clock::now();
                scheduler->adopt(snap);
                adoptUs.push_back(usSince(t0));
                t0 = Clock::now();
                block(ppq);
                flushUs.push_back(usSince(t0));
                ppq += beatsPerBlock;
            }
            // Seek-and-chase blocks at random positions over two wraps.
            for (int i = 0; i < 2000; ++i) {
                block(ppq);
                ppq = anywhere(rng);
                const auto t0 = Clock::now();
                block(ppq);
                seekUs.push_back(usSince(t0));
                ppq += beatsPerBlock;
            }
            // The worst seek: just before the end of the second wrap, where placeCursor scans the whole previous
            // wrap and then this one, and the chase scans this one.
            std::uniform_real_distribution<double> nearEnd(2.0 * slot.endBeat - 0.5, 2.0 * slot.endBeat - 0.001);
            for (int i = 0; i < 2000; ++i) {
                block(1.0 + i * 0.01);
                ppq = nearEnd(rng);
                const auto t0 = Clock::now();
                block(ppq);
                worstSeekUs.push_back(usSince(t0));
            }
            // Steady blocks: 200000 consecutive blocks from beat 0 (at 300 bpm and 48 kHz about 267 beats).
            ppq = 0.0;
            block(ppq);
            ppq += beatsPerBlock;
            for (int i = 0; i < 200000; ++i, ppq += beatsPerBlock) {
                const auto t0 = Clock::now();
                block(ppq);
                steadyUs.push_back(usSince(t0));
            }
            audioRow("adopt (new generation)", adoptUs, deadlineUs);
            audioRow("block after adopt (flush+chase)", flushUs, deadlineUs);
            audioRow("seek-and-chase, random", seekUs, deadlineUs);
            audioRow("seek-and-chase, wrap 2 end", worstSeekUs, deadlineUs);
            audioRow("steady block", steadyUs, deadlineUs);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const int runs = argc > 1 ? std::max(10, std::atoi(argv[1])) : 200;
    const char* dumpPath = argc > 2 ? argv[2] : nullptr;
#ifndef NDEBUG
    std::printf("latency_bench: WARNING, a Debug build (asserts on); the gate's numbers are Release\n");
#endif
    commandPaths("worst document, arpeggio (up, subdivision)", Feel{Rhythm::Arpeggio}, runs, dumpPath);
    commandPaths("worst document, block chords", Feel{}, runs, nullptr);

    {
        Bench b = buildWorst(Feel{Rhythm::Arpeggio});
        auto slot = std::make_unique<SnapshotSlot>();
        Publication publication;
        fillSlot(b.store->doc(), *slot, publication);
        audioThread(*slot, "the worst document's slot (arpeggio)");
    }
    {
        auto store = fullSlotDocument();
        auto slot = std::make_unique<SnapshotSlot>();
        Publication publication;
        fillSlot(store->doc(), *slot, publication);
        require(slot->count == kMaxSnapshotEvents, "the full-slot document does not fill the slot");
        audioThread(*slot, "a full slot (test_publication's caps document)");
    }
    return 0;
}
