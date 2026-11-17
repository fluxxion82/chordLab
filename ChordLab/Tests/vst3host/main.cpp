// vst3host: loads a built ChordLab.vst3 into this process, as a VST3 host does, and runs the shell's scenarios
// (per-track instruments plan, M2 Task 8b) against it: life cycle, state, playback of one instance, two instances
// linked through their states alone, a copy, and a state load while playing. Every VST3 call is made on this, the
// main, thread, as Live makes them (plan V14), and `process` is called on the same thread between them. No window,
// no audio or MIDI device, nothing written anywhere.
//
// What the plug-in must play is never computed by the code under test: the expected output of every instance is a
// lone engine scheduler playing a document of its own (the leader's document with the followed lane muted, the
// follower's lane alone by solo, or nothing), compiled by the bare `fillSlot` and adopted at the block where the
// instance under test receives that content, then driven with exactly the blocks the plug-in got (the same
// position, tempo, length and transport flag). It is the reference the engine's two-instance simulation
// (engine/tests/test_link_sim.cpp, `reference`) uses, and what `ChordLab::ProcessBlock` did before linking existed.
// The comparison is message for message: status, pitch, velocity and sample offset in its block.
//
// One line per check; the first failure ends the run with status 1, a crash with status 70 and the step it hit.

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/json_api.h"
#include "chordlab/link_state.h"
#include "chordlab/publication.h"
#include "chordlab/scheduler.h"
#include "chordlab/tracks.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

using namespace Steinberg;
using namespace Steinberg::Vst;
// The engine's names, one by one: Steinberg::Vst has a Chord of its own.
using chordlab::BlockInfo;
using chordlab::chordType;
using chordlab::DocumentStore;
using chordlab::fillSlot;
using chordlab::InstanceState;
using chordlab::kDefaultEditorHeight;
using chordlab::kDefaultEditorWidth;
using chordlab::kNoTrackId;
using chordlab::LinkFollow;
using chordlab::loadStateJson;
using chordlab::MidiOut;
using chordlab::NoteSpec;
using chordlab::PitchClass;
using chordlab::Publication;
using chordlab::Scheduler;
using chordlab::Snapshot;
using chordlab::SnapshotSlot;
using chordlab::stateJson;
using chordlab::TrackRole;

namespace {

// --- reporting ----------------------------------------------------------------------------------------------------

char gStep[512] = "start";   // what the host is doing now, for the crash report

void setStep(const std::string& what) {
    std::snprintf(gStep, sizeof gStep, "%s", what.c_str());
}

void onCrash(int sig) {
    char line[700];
    const int n = std::snprintf(line, sizeof line, "CRASH (signal %d) during: %s\n", sig, gStep);
    if (n > 0) (void) !write(STDOUT_FILENO, line, static_cast<size_t>(std::min(n, static_cast<int>(sizeof line) - 1)));
    _exit(70);
}

void installCrashHandlers() {
    for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP}) std::signal(sig, onCrash);
}

int gChecks = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    std::fflush(stdout);
    if (!ok) std::exit(1);
}

[[noreturn]] void fail(const std::string& what) {
    check(false, what);
    std::exit(1);
}

// --- the transport ----------------------------------------------------------------------------------------------

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTempo = 120.0;
constexpr double kSamplesPerBeat = kSampleRate * 60.0 / kTempo;   // 24000
constexpr int kLoopBlocks = static_cast<int>(8 * kSamplesPerBeat / kBlock) + 1;   // a little more than 8 beats

/// One process call as the host describes it.
struct Span {
    long start;     // its first sample on the host's timeline
    int frames;
    double ppq;     // the host's position at that sample
    bool playing;
};

/// `playing` blocks of kBlock from sample 0 with the transport running, then `stopped` blocks with it stopped where it
/// stopped (as a host does on stop: the position stands).
std::vector<Span> transport(int playing, int stopped) {
    std::vector<Span> spans;
    long position = 0;
    for (int i = 0; i < playing; ++i, position += kBlock)
        spans.push_back(Span{position, kBlock, static_cast<double>(position) / kSamplesPerBeat, true});
    for (int i = 0; i < stopped; ++i)
        spans.push_back(Span{position, kBlock, static_cast<double>(position) / kSamplesPerBeat, false});
    return spans;
}

/// The block that contains beat `beat`.
std::size_t blockAtBeat(double beat) {
    return static_cast<std::size_t>(beat * kSamplesPerBeat / kBlock);
}

// --- MIDI as the plug-in sends it ---------------------------------------------------------------------------------

struct Msg {
    int offset;
    int status;   // 0x90 or 0x80
    int pitch;
    int velocity;
    bool operator==(const Msg& o) const {
        return offset == o.offset && status == o.status && pitch == o.pitch && velocity == o.velocity;
    }
};
using Midi = std::vector<Msg>;
using Run = std::vector<Midi>;   // one Midi per span, by span index

std::string describe(const Midi& midi) {
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < midi.size(); ++i)
        os << (i ? " " : "") << (midi[i].status == 0x90 ? "on" : "off") << midi[i].pitch << "v" << midi[i].velocity
           << "@" << midi[i].offset;
    os << "]";
    return os.str();
}

/// Spans [from, to) of `got` equal `want`, message for message.
void expectRun(const Run& got, const Run& want, std::size_t from, std::size_t to, const std::string& what) {
    std::size_t messages = 0;
    for (std::size_t i = from; i < to; ++i) {
        if (!(got[i] == want[i]))
            fail(what + ": block " + std::to_string(i) + " sent " + describe(got[i]) + ", expected " +
                 describe(want[i]));
        messages += got[i].size();
    }
    check(true, what + " (blocks " + std::to_string(from) + ".." + std::to_string(to - 1) + ", " +
                    std::to_string(messages) + " messages, exact)");
}

/// Every note-on of a silent pitch, every note-off of a sounding one, and nothing sounding after the last span.
void expectClean(const Run& run, std::size_t from, std::size_t to, const std::string& what) {
    std::array<bool, 128> on{};
    for (std::size_t i = from; i < to; ++i) {
        for (const Msg& m : run[i]) {
            const bool noteOn = m.status == 0x90;
            if (noteOn == on[static_cast<std::size_t>(m.pitch)])
                fail(what + ": block " + std::to_string(i) + ": " + (noteOn ? "a note-on of a sounding" : "a note-off of a silent") +
                     " pitch " + std::to_string(m.pitch));
            on[static_cast<std::size_t>(m.pitch)] = noteOn;
        }
    }
    const bool none = std::none_of(on.begin(), on.end(), [](bool b) { return b; });
    check(none, what + ": every note-on has its note-off, nothing left sounding");
}

std::array<bool, 128> pitchesIn(const Run& run, std::size_t from, std::size_t to) {
    std::array<bool, 128> seen{};
    for (std::size_t i = from; i < to; ++i)
        for (const Msg& m : run[i])
            if (m.status == 0x90) seen[static_cast<std::size_t>(m.pitch)] = true;
    return seen;
}

bool silentBetween(const Run& run, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i)
        if (!run[i].empty()) return false;
    return true;
}

// --- documents, states and the expected output ---------------------------------------------------------------------

chordlab::Chord chordOf(PitchClass root, const char* type) {
    return chordlab::Chord{root, &chordType(type)};
}

constexpr int kBass1 = 36;   // the song's bass pitches; no chord of the song contains them
constexpr int kBass2 = 41;

/// A document and the number of its bass track.
struct Song {
    std::string json;   // its state, without an instance block
    int bass = 0;
};

int addTrack(DocumentStore& store, TrackRole role, const char* name, const std::vector<NoteSpec>& notes) {
    const int id = store.addTrack(role, name);
    std::string why;
    if (id == kNoTrackId || store.insertNotes(id, notes, &why).size() != notes.size())
        fail(std::string("building a document: ") + why);
    return id;
}

/// C then Am, four beats each (looped over 8 beats), and a bass track: 36 on beat 0 for one beat, 41 from beat 5
/// for two beats (960 ticks a beat).
Song song() {
    DocumentStore s;
    s.appendChord(chordOf(0, "maj"), 4.0, 100);
    s.appendChord(chordOf(9, "min"), 4.0, 100);
    Song out;
    out.bass = addTrack(s, TrackRole::Bass, "Bass", {NoteSpec{kBass1, 0, 960, 100}, NoteSpec{kBass2, 4800, 1920, 100}});
    out.json = stateJson(s);
    return out;
}

/// Another project: E minor then G, three beats each, and its own bass track (48 on beat 1). Same track number.
Song otherSong() {
    DocumentStore s;
    s.appendChord(chordOf(4, "min"), 3.0, 100);
    s.appendChord(chordOf(7, "maj"), 3.0, 100);
    Song out;
    out.bass = addTrack(s, TrackRole::Bass, "Bass", {NoteSpec{48, 960, 960, 100}});
    out.json = stateJson(s);
    return out;
}

/// A follower's state: its own document (one D major chord, never played while it follows) and an instance block
/// that follows `lane` of `leaderId`'s document `leaderDoc`.
std::string followerState(const std::string& id, const std::string& leaderId, const std::string& leaderDoc,
                          const std::string& lane) {
    DocumentStore s;
    s.appendChord(chordOf(2, "maj"), 4.0, 100);
    InstanceState instance;
    instance.id = id;
    instance.documentId = id + "-doc";
    instance.follow = LinkFollow{leaderId, leaderDoc, lane, "Bass", "bass", "ChordLab A"};
    return stateJson(s, kDefaultEditorWidth, kDefaultEditorHeight, &instance);
}

/// What one instance is given to play from a block on: `json`'s document (empty: nothing) after `tweak`.
struct Content {
    std::size_t span;
    std::string json;
    std::function<void(DocumentStore&)> tweak;
};

/// The expected output of an instance that starts processing at span `from` and receives each content at its span:
/// a lone scheduler, reset as the plug-in's is at setupProcessing, adopting the bare `fillSlot` of each content (a
/// new generation each time) and processed with the plug-in's flags (playing = the transport's AND the content's
/// followHost; thru from the content).
Run reference(const std::vector<Span>& spans, std::size_t from, const std::vector<Content>& contents) {
    std::vector<std::unique_ptr<SnapshotSlot>> slots;
    for (const Content& c : contents) {
        DocumentStore store;
        if (!c.json.empty()) {
            std::string error;
            if (!loadStateJson(store, c.json, error)) fail("reference: " + error);
        }
        if (c.tweak) c.tweak(store);
        auto slot = std::make_unique<SnapshotSlot>();
        Publication publication;
        fillSlot(store.doc(), *slot, publication);
        slots.push_back(std::move(slot));
    }
    auto scheduler = std::make_unique<Scheduler>();
    scheduler->reset(kSampleRate);
    std::array<MidiOut, 512> out{};
    Run run(spans.size());
    const SnapshotSlot* front = nullptr;
    std::uint32_t generation = 0;
    for (std::size_t i = from; i < spans.size(); ++i) {
        for (std::size_t c = 0; c < contents.size(); ++c) {
            if (contents[c].span == i) {
                front = slots[c].get();
                scheduler->adopt(Snapshot{front->events.data(), front->count, front->endBeat, front->loop, ++generation});
            }
        }
        const bool followHost = front ? front->followHost : true;
        const bool thru = front ? front->midiThru : true;
        std::size_t written = 0;
        const BlockInfo info{spans[i].ppq, spans[i].frames, kSampleRate, kTempo, spans[i].playing && followHost, thru};
        if (!scheduler->process(info, out.data(), out.size(), written)) fail("reference: an incomplete block");
        for (std::size_t k = 0; k < written; ++k)
            run[i].push_back(Msg{out[k].sampleOffset, out[k].status, out[k].data1, out[k].data2});
    }
    return run;
}

std::function<void(DocumentStore&)> soloTrack(int id) {
    return [id](DocumentStore& d) { d.setTrackSolo(id, true); };
}
std::function<void(DocumentStore&)> muteTrack(int id) {
    return [id](DocumentStore& d) { d.setTrackMute(id, true); };
}

// --- the state envelope --------------------------------------------------------------------------------------------

using Bytes = std::vector<std::uint8_t>;

/// ChordLab's state as iPlug2's VST3 getState writes it: u8 version, i32 length, the state JSON, then iPlug2's
/// parameters (one double per parameter: ChordLab has one) and the bypass flag (i32).
struct Envelope {
    std::uint8_t version = 0;
    std::string json;
    Bytes trailer;
    std::optional<InstanceState> instance;   // the JSON's instance block, as the engine reads it
};

constexpr std::size_t kTrailerBytes = 1 * sizeof(double) + sizeof(std::int32_t);

Envelope parse(const Bytes& bytes, const std::string& what) {
    Envelope e;
    if (bytes.size() < 5) fail(what + ": a state of " + std::to_string(bytes.size()) + " bytes");
    e.version = bytes[0];
    std::int32_t length = 0;
    std::memcpy(&length, bytes.data() + 1, sizeof length);
    if (length < 0 || static_cast<std::size_t>(length) > bytes.size() - 5) fail(what + ": a bad payload length");
    e.json.assign(reinterpret_cast<const char*>(bytes.data() + 5), static_cast<std::size_t>(length));
    e.trailer.assign(bytes.begin() + 5 + length, bytes.end());
    DocumentStore store;
    std::string error;
    if (!loadStateJson(store, e.json, error, nullptr, nullptr, &e.instance)) fail(what + ": the engine refuses the JSON: " + error);
    return e;
}

Bytes wrap(const std::string& json, const Bytes& trailer) {
    Bytes bytes;
    bytes.push_back(2);
    const std::int32_t length = static_cast<std::int32_t>(json.size());
    const auto* l = reinterpret_cast<const std::uint8_t*>(&length);
    bytes.insert(bytes.end(), l, l + sizeof length);
    bytes.insert(bytes.end(), json.begin(), json.end());
    bytes.insert(bytes.end(), trailer.begin(), trailer.end());
    return bytes;
}

/// The JSON without the instance block and the editor size: what the document part of two states shares.
nlohmann::json documentPart(const std::string& json) {
    nlohmann::json j = nlohmann::json::parse(json);
    j.erase("instance");
    j.erase("editor");
    return j;
}

// --- one plug-in instance -------------------------------------------------------------------------------------------

struct Plug {
    const VST3::Hosting::PluginFactory* factory = nullptr;
    VST3::UID classId;
    FUnknown* host = nullptr;
};

class Instance {
public:
    Instance(const Plug& plug, std::string name) : name_(std::move(name)), events_(4096) {
        setStep(name_ + ": construct (factory createInstance)");
        component_ = plug.factory->createInstance<IComponent>(plug.classId);
        if (!component_) fail(name_ + ": the factory made no IComponent");
        setStep(name_ + ": initialize");
        expectOk(component_->initialize(plug.host), "initialize");
        processor_ = FUnknownPtr<IAudioProcessor>(component_);
        if (!processor_) fail(name_ + ": no IAudioProcessor");
        setStep(name_ + ": alive");
    }

    ~Instance() {
        setStep(name_ + ": destroy");
        if (processing_) {
            processor_->setProcessing(false);
            component_->setActive(false);
        }
        component_->terminate();
        processor_ = nullptr;
        component_ = nullptr;   // the last reference: the plug-in is deleted here
        setStep(name_ + ": destroyed");
    }

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    Bytes getState() {
        setStep(name_ + ": getState");
        IPtr<MemoryStream> stream = owned(new MemoryStream());
        expectOk(component_->getState(stream), "getState");
        const char* data = stream->getData();
        return Bytes(data, data + stream->getSize());
    }

    void setState(Bytes bytes) {
        setStep(name_ + ": setState");
        IPtr<MemoryStream> stream = owned(new MemoryStream(bytes.data(), static_cast<TSize>(bytes.size())));
        expectOk(component_->setState(stream), "setState");
    }

    /// setupProcessing (48 kHz, kBlock frames, 32-bit, real time), the main buses on, setActive, setProcessing.
    void start() {
        setStep(name_ + ": start processing");
        ProcessSetup setup{kRealtime, kSample32, kBlock, kSampleRate};
        expectOk(processor_->setupProcessing(setup), "setupProcessing");
        component_->activateBus(kAudio, kOutput, 0, true);
        component_->activateBus(kEvent, kOutput, 0, true);
        expectOk(component_->setActive(true), "setActive(true)");
        expectOk(processor_->setProcessing(true), "setProcessing(true)");
        processing_ = true;
    }

    /// One process call for `span`; returns the note events the plug-in sent.
    Midi process(const Span& span, std::size_t index) {
        setStep(name_ + ": process block " + std::to_string(index));
        ProcessContext context{};
        context.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid |
                        ProcessContext::kTimeSigValid | (span.playing ? ProcessContext::kPlaying : 0);
        context.sampleRate = kSampleRate;
        context.projectTimeSamples = span.start;
        context.projectTimeMusic = span.ppq;
        context.tempo = kTempo;
        context.timeSigNumerator = 4;
        context.timeSigDenominator = 4;
        float* channels[2] = {left_.data(), right_.data()};
        AudioBusBuffers out{};
        out.numChannels = 2;
        out.channelBuffers32 = channels;
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = span.frames;
        data.numInputs = 0;
        data.numOutputs = 1;
        data.outputs = &out;
        data.outputEvents = &events_;
        data.processContext = &context;
        events_.clear();
        expectOk(processor_->process(data), "process");
        Midi midi;
        for (int32 i = 0; i < events_.getEventCount(); ++i) {
            Event e{};
            events_.getEvent(i, e);
            if (e.type == Event::kNoteOnEvent)
                midi.push_back(Msg{e.sampleOffset, 0x90, e.noteOn.pitch, static_cast<int>(std::lround(e.noteOn.velocity * 127.f))});
            else if (e.type == Event::kNoteOffEvent)
                midi.push_back(Msg{e.sampleOffset, 0x80, e.noteOff.pitch, static_cast<int>(std::lround(e.noteOff.velocity * 127.f))});
            else
                fail(name_ + ": block " + std::to_string(index) + ": an event of type " + std::to_string(e.type));
        }
        if (static_cast<int>(midi.size()) >= 4096) fail(name_ + ": the output event list overflowed");
        return midi;
    }

    const std::string& name() const { return name_; }

private:
    void expectOk(tresult r, const char* call) {
        if (r != kResultOk) fail(name_ + ": " + call + " returned " + std::to_string(r));
    }

    std::string name_;
    IPtr<IComponent> component_;
    IPtr<IAudioProcessor> processor_;
    EventList events_;
    std::array<float, kBlock> left_{};
    std::array<float, kBlock> right_{};
    bool processing_ = false;
};

/// Runs `instances` over `spans` [from, to), each span in the given order (one thread, as a host's single audio
/// thread would for one track after another), appending to `runs` (one Run per instance, sized to spans).
/// `between(i, k)` is called after instance k processed span i.
void play(const std::vector<Instance*>& instances, std::vector<Run*> runs, const std::vector<Span>& spans,
          std::size_t from, std::size_t to, const std::function<void(std::size_t, std::size_t)>& between = nullptr) {
    for (std::size_t i = from; i < to; ++i) {
        for (std::size_t k = 0; k < instances.size(); ++k) {
            if (instances[k]) (*runs[k])[i] = instances[k]->process(spans[i], i);
            if (between) between(i, k);
        }
    }
}

// --- the scenarios --------------------------------------------------------------------------------------------------

Bytes gTrailer;   // the parameter and bypass trailer of a fresh instance's state (scenario 2), for states built here

/// 1. Construction and destruction: twice in a row, then two alive at once destroyed in both orders.
void lifeCycle(const Plug& plug) {
    std::puts("== 1 life cycle");
    for (int n = 1; n <= 2; ++n) {
        { Instance a(plug, "instance " + std::to_string(n)); }
        check(true, "create, initialize, terminate, release, run " + std::to_string(n));
    }
    {
        auto a = std::make_unique<Instance>(plug, "A");
        auto b = std::make_unique<Instance>(plug, "B");
        a.reset();
        b.reset();
        check(true, "two alive at once, destroyed first-made first");
    }
    {
        auto a = std::make_unique<Instance>(plug, "A");
        auto b = std::make_unique<Instance>(plug, "B");
        b.reset();
        a.reset();
        check(true, "two alive at once, destroyed last-made first");
    }
}

/// 2. getState and setState: the envelope, the instance block, a state without a block.
void state(const Plug& plug) {
    std::puts("== 2 state");
    Instance a(plug, "A");
    const Envelope fresh = parse(a.getState(), "fresh getState");
    check(fresh.version == 2, "fresh getState: version byte 2");
    check(fresh.trailer.size() == kTrailerBytes,
          "fresh getState: JSON then the parameters and bypass trailer (" + std::to_string(fresh.trailer.size()) + " bytes)");
    gTrailer = fresh.trailer;
    check(fresh.instance.has_value(), "fresh getState: the JSON loads with the engine and carries an instance block");
    check(!fresh.instance->id.empty() && !fresh.instance->documentId.empty() && fresh.instance->id != fresh.instance->documentId,
          "fresh getState: an instance id (" + fresh.instance->id + ") and a document id (" + fresh.instance->documentId + ")");
    check(!fresh.instance->follow.has_value(), "fresh getState: no follow");

    a.setState(wrap(fresh.json, fresh.trailer));
    const Envelope again = parse(a.getState(), "getState after setState of its own state");
    check(documentPart(again.json) == documentPart(fresh.json), "setState of its own state: the document part is unchanged");
    check(again.instance && again.instance->id == fresh.instance->id && again.instance->documentId == fresh.instance->documentId,
          "setState of its own state: the instance block keeps its id and document id");

    std::ifstream file(CHORDLAB_ENGINE_FIXTURE_DIR "/state-legacy-feel.json", std::ios::binary);
    const std::string legacy((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (legacy.empty()) fail("could not read the fixture state-legacy-feel.json");
    check(nlohmann::json::parse(legacy).count("instance") == 0, "fixture state-legacy-feel.json has no instance block");
    a.setState(wrap(legacy, fresh.trailer));
    const Envelope loaded = parse(a.getState(), "getState after a state without a block");
    DocumentStore expected;
    std::string error;
    if (!loadStateJson(expected, legacy, error)) fail("the engine refuses the fixture: " + error);
    check(documentPart(loaded.json) == documentPart(stateJson(expected)),
          "a state without a block: the plug-in holds the fixture's document (as the engine round-trips it)");
    check(loaded.instance.has_value(), "a state without a block: the next getState has a block");
    check(loaded.instance->id == fresh.instance->id, "a state without a block: the instance keeps its id");
    check(!loaded.instance->documentId.empty() && loaded.instance->documentId != fresh.instance->documentId,
          "a state without a block: a new document id (" + loaded.instance->documentId + ")");
    check(!loaded.instance->follow.has_value(), "a state without a block: no follow");
}

/// 3. One instance plays the song: exactly what a lone scheduler plays from the same document and blocks.
void playback(const Plug& plug) {
    std::puts("== 3 playback of one instance");
    const Song s = song();
    const std::vector<Span> spans = transport(2 * kLoopBlocks, 8);
    Instance a(plug, "A");
    a.setState(wrap(s.json, gTrailer));
    a.start();
    Run got(spans.size());
    play({&a}, {&got}, spans, 0, spans.size());
    const Run want = reference(spans, 0, {Content{0, s.json, nullptr}});
    expectRun(got, want, 0, spans.size(), "A plays the song (two loops, then the transport stops)");
    const auto seen = pitchesIn(got, 0, spans.size());
    check(seen[kBass1] && seen[kBass2] && seen[60], "A's output holds the bass (36, 41) and the chords");
    expectClean(got, 0, spans.size(), "A");
}

/// What a leader's saved state tells a follower: its id, its document id and the bass lane's uid.
struct Leader {
    Bytes saved;
    std::string id;
    std::string doc;
    std::string bassUid;
};

Leader leaderOf(Instance& a, const Song& s) {
    Leader l;
    l.saved = a.getState();
    const Envelope e = parse(l.saved, a.name() + "'s getState");
    if (!e.instance) fail(a.name() + "'s state has no instance block");
    l.id = e.instance->id;
    l.doc = e.instance->documentId;
    const auto it = e.instance->lanes.find(s.bass);
    if (it == e.instance->lanes.end()) fail(a.name() + "'s state has no lane uid for the bass track");
    l.bassUid = it->second;
    return l;
}

/// 4. Two instances linked by their states alone.
void linked(const Plug& plug) {
    std::puts("== 4 two linked instances, through state alone");
    const Song s = song();
    const std::vector<Span> spans = transport(3 * kLoopBlocks, 8);
    const std::size_t end = spans.size();
    const std::string bId = "vst3host-follower-b";

    // A leads, B follows A's bass; both loaded before the transport starts.
    auto a = std::make_unique<Instance>(plug, "A");
    a->setState(wrap(s.json, gTrailer));
    const Leader leader = leaderOf(*a, s);
    check(true, "A: id " + leader.id + ", document " + leader.doc + ", bass lane " + leader.bassUid);
    auto b = std::make_unique<Instance>(plug, "B");
    b->setState(wrap(followerState(bId, leader.id, leader.doc, leader.bassUid), gTrailer));
    a->start();
    b->start();

    // A is destroyed at beat 5.5 (B's 41 sounds), A2 takes A's saved state about 300 blocks later (beat 0.7 of the
    // second loop, where B's 36 sounds again): B must stop, wait, and play again with nothing done to it.
    const std::size_t gone = blockAtBeat(5.5);
    const std::size_t back = gone + 300;
    Run ra(end), rb(end), ra2(end);
    play({a.get(), b.get()}, {&ra, &rb}, spans, 0, gone);
    expectRun(ra, reference(spans, 0, {Content{0, s.json, muteTrack(s.bass)}}), 0, gone, "A plays everything but the bass");
    expectRun(rb, reference(spans, 0, {Content{0, s.json, soloTrack(s.bass)}}), 0, gone, "B plays exactly the bass lane");
    const auto aSeen = pitchesIn(ra, 0, gone), bSeen = pitchesIn(rb, 0, gone);
    check(!aSeen[kBass1] && !aSeen[kBass2], "no pitch of the bass on A");
    check(bSeen[kBass1] && bSeen[kBass2] && std::count(bSeen.begin(), bSeen.end(), true) == 2, "nothing but the bass on B");

    a.reset();
    check(true, "A destroyed while B lives");
    play({b.get()}, {&rb}, spans, gone, back);
    bool releasedFirst = !rb[gone].empty() && std::all_of(rb[gone].begin(), rb[gone].end(), [](const Msg& m) { return m.status == 0x80; });
    check(releasedFirst && std::any_of(rb[gone].begin(), rb[gone].end(), [](const Msg& m) { return m.pitch == kBass2; }),
          "B's next block releases what sounds (" + describe(rb[gone]) + ")");
    check(silentBetween(rb, gone + 1, back), "then B stays silent");
    const Envelope waiting = parse(b->getState(), "B's getState while A is gone");
    check(waiting.instance && waiting.instance->follow && waiting.instance->follow->leaderId == leader.id &&
              waiting.instance->follow->laneUid == leader.bassUid && waiting.instance->follow->documentId == leader.doc,
          "B's getState still carries the follow (it waits for " + leader.id + ")");

    auto a2 = std::make_unique<Instance>(plug, "A2");
    a2->setState(leader.saved);
    a2->start();
    const Envelope a2State = parse(a2->getState(), "A2's getState");
    check(a2State.instance && a2State.instance->id == leader.id, "A2 took A's id from A's saved state");
    play({a2.get(), b.get()}, {&ra2, &rb}, spans, back, end);
    expectRun(rb,
              reference(spans, 0, {Content{0, s.json, soloTrack(s.bass)}, Content{gone, "", nullptr},
                                   Content{back, s.json, soloTrack(s.bass)}}),
              0, end, "B over the whole run: the bass, released and silent while A is gone, the bass again with A2");
    expectRun(ra2, reference(spans, back, {Content{back, s.json, muteTrack(s.bass)}}), back, end,
              "A2 plays everything but the bass from its first block");
    expectClean(rb, 0, end, "B");
    expectClean(ra2, back, end, "A2");
    a2.reset();
    b.reset();

    // The same pair loaded follower first, then the leader with its saved state.
    {
        auto b1 = std::make_unique<Instance>(plug, "B (loaded first)");
        b1->setState(wrap(followerState(bId, leader.id, leader.doc, leader.bassUid), gTrailer));
        auto a1 = std::make_unique<Instance>(plug, "A (loaded second)");
        a1->setState(leader.saved);
        a1->start();
        b1->start();
        Run r1(end), r2(end);
        play({a1.get(), b1.get()}, {&r1, &r2}, spans, 0, end);
        expectRun(r1, reference(spans, 0, {Content{0, s.json, muteTrack(s.bass)}}), 0, end,
                  "follower loaded first: A plays everything but the bass");
        expectRun(r2, reference(spans, 0, {Content{0, s.json, soloTrack(s.bass)}}), 0, end,
                  "follower loaded first: B plays exactly the bass lane");
        expectClean(r1, 0, end, "A (loaded second)");
        expectClean(r2, 0, end, "B (loaded first)");
    }

    // A follow of a lane uid the leader does not have.
    {
        auto a1 = std::make_unique<Instance>(plug, "A");
        a1->setState(leader.saved);
        auto b1 = std::make_unique<Instance>(plug, "B (missing lane)");
        b1->setState(wrap(followerState(bId, leader.id, leader.doc, "vst3host-no-such-lane"), gTrailer));
        a1->start();
        b1->start();
        Run r1(end), r2(end);
        play({a1.get(), b1.get()}, {&r1, &r2}, spans, 0, end);
        expectRun(r1, reference(spans, 0, {Content{0, s.json, nullptr}}), 0, end, "a missing lane: A plays everything");
        check(silentBetween(r2, 0, end), "a missing lane: B is silent");
    }
}

/// 5. A copy: a third instance given A's exact state while A lives (Live's duplicate).
void copy(const Plug& plug) {
    std::puts("== 5 a copy");
    const Song s = song();
    const std::vector<Span> spans = transport(2 * kLoopBlocks, 8);
    const std::size_t end = spans.size();
    Instance a(plug, "A");
    a.setState(wrap(s.json, gTrailer));
    const Leader leader = leaderOf(a, s);
    Instance b(plug, "B");
    b.setState(wrap(followerState("vst3host-follower-b", leader.id, leader.doc, leader.bassUid), gTrailer));
    Instance c(plug, "C (copy of A)");
    c.setState(leader.saved);
    const Envelope cs = parse(c.getState(), "C's getState");
    check(cs.instance && cs.instance->id != leader.id && !cs.instance->id.empty(),
          "the copy's getState shows a different instance id (" + (cs.instance ? cs.instance->id : std::string("none")) + ")");
    const Envelope bs = parse(b.getState(), "B's getState");
    check(bs.instance && bs.instance->follow && bs.instance->follow->leaderId == leader.id, "B still follows A");
    a.start();
    b.start();
    c.start();
    Run ra(end), rb(end), rc(end);
    play({&a, &b, &c}, {&ra, &rb, &rc}, spans, 0, end);
    expectRun(ra, reference(spans, 0, {Content{0, s.json, muteTrack(s.bass)}}), 0, end, "A keeps its follower: everything but the bass");
    expectRun(rb, reference(spans, 0, {Content{0, s.json, soloTrack(s.bass)}}), 0, end, "B plays the bass");
    expectRun(rc, reference(spans, 0, {Content{0, s.json, nullptr}}), 0, end, "the copy plays every lane itself");
}

/// 6. A state load on the leader between its process call and its follower's.
void loadWhilePlaying(const Plug& plug) {
    std::puts("== 6 state load while playing");
    const Song s = song();
    const Song other = otherSong();
    const std::vector<Span> spans = transport(2 * kLoopBlocks, 8);
    const std::size_t end = spans.size();
    Instance a(plug, "A");
    a.setState(wrap(s.json, gTrailer));
    const Leader leader = leaderOf(a, s);
    Instance b(plug, "B");
    b.setState(wrap(followerState("vst3host-follower-b", leader.id, leader.doc, leader.bassUid), gTrailer));
    a.start();
    b.start();
    const std::size_t at = blockAtBeat(5.5);   // B's 41 sounds
    Run ra(end), rb(end);
    play({&a, &b}, {&ra, &rb}, spans, 0, end, [&](std::size_t i, std::size_t k) {
        if (i == at && k == 0) a.setState(wrap(other.json, gTrailer));   // after A's block, before B's
    });
    expectRun(ra,
              reference(spans, 0, {Content{0, s.json, muteTrack(s.bass)}, Content{at + 1, other.json, nullptr}}), 0, end,
              "A plays everything but the bass, then the new document whole from its next block");
    expectRun(rb, reference(spans, 0, {Content{0, s.json, soloTrack(s.bass)}, Content{at, "", nullptr}}), 0, end,
              "B plays the bass, then releases it in the block after the load and stays silent");
    check(silentBetween(rb, at + 1, end), "B is silent after the load");
    expectClean(ra, 0, end, "A");
    expectClean(rb, 0, end, "B");
}

}  // namespace

int main(int argc, char** argv) {
    installCrashHandlers();
    if (argc != 2) {
        std::fprintf(stderr, "usage: vst3host <path to ChordLab.vst3>\n");
        return 2;
    }
    const std::string path = argv[1];
    std::printf("vst3host: %s\n", path.c_str());
    setStep("load the module");
    std::string error;
    VST3::Hosting::Module::Ptr module = VST3::Hosting::Module::create(path, error);
    if (!module) fail("load the module: " + error);
    check(true, "module loaded");
    IPtr<HostApplication> hostApp = owned(new HostApplication());
    const VST3::Hosting::PluginFactory& factory = module->getFactory();
    factory.setHostContext(hostApp);
    Plug plug;
    plug.factory = &factory;
    plug.host = hostApp;
    bool found = false;
    for (const auto& info : factory.classInfos()) {
        if (info.category() == kVstAudioEffectClass) {
            plug.classId = info.ID();
            found = true;
            check(true, "audio module class '" + info.name() + "' (" + info.subCategoriesString() + ")");
            break;
        }
    }
    if (!found) fail("the factory has no audio module class");

    lifeCycle(plug);
    state(plug);
    playback(plug);
    linked(plug);
    copy(plug);
    loadWhilePlaying(plug);

    setStep("unload the module");
    module.reset();
    std::printf("vst3host: ALL PASS (%d checks)\n", gChecks);
    return 0;
}
