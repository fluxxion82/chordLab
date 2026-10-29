#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chordlab {

/// Which kind of key a preset is written for (P3): `Major` presets fit Ionian, Lydian and Mixolydian,
/// `Minor` presets the other six modes (Dorian, Phrygian, Aeolian, Locrian, harmonic and melodic minor),
/// and `Any` presets (the blues) fit all nine. A preset that does not fit still loads; it is only shown
/// greyed, because its chords will mostly lie outside the key.
enum class Tonality { Major, Minor, Any };

/// The wire name of `tonality`, as the snapshot's `presets[].tonality` spells it: "Major", "Minor",
/// "Any". The pointer is to a string literal and never goes stale.
const char* tonalityName(Tonality tonality);

/// One chord of a preset (P1), written as a numeral against the PARALLEL MAJOR of whatever tonic the
/// preset is loaded in, the engine's numeral convention (symbol.h): `degree` 1..7 of the major scale,
/// `alter` -1..+1 semitones on it (so `bVII` is {7, -1} and the minor key's own seventh), `typeId` an id
/// of `allChordTypes()` (chord.h), never inferred from the numeral's case, and `beats` the length the
/// chord gets, within the store's [kMinLengthBeats, kMaxLengthBeats] (document.h). `typeId` points at a
/// string literal.
struct PresetStep {
    int degree;
    int alter;
    const char* typeId;
    double beats;
};

/// A named progression from the built-in library (P2). `id` is stable ASCII, the handle the `loadPreset`
/// command names (json_api.h); it is persisted nowhere. `name` (also the name of the section a load
/// creates, so at most `kMaxSectionNameBytes` bytes) and `genre` are display text; the UI groups by
/// `genre` in table order. Every pointer is to a string literal. `steps` is never empty.
struct Preset {
    const char* id;
    const char* name;
    const char* genre;
    Tonality tonality;
    std::vector<PresetStep> steps;
};

/// The library: the 29 preset records, grouped by
/// genre (Pop, Rock, Blues, Jazz, Folk / Country, EDM / Dance, R&B / Neo-soul, Film / Epic, Gospel,
/// Classical) in that order. Constant for a build; the vector and every record in it are stable until
/// static destruction, so `findPreset`'s pointers into it never go stale.
const std::vector<Preset>& allPresets();

/// The record whose `id` is exactly `id` (case-sensitive), or nullptr.
const Preset* findPreset(std::string_view id);

/// Whether `preset`'s tonality suits `key`'s mode (P3, see `Tonality`). `key.mode` must be a declared
/// `Mode`; the tonic does not matter.
bool presetFits(const Preset& preset, const Key& key);

/// `preset`'s chords in `key`, with their lengths, in order (P1): each root is the key's tonic plus the
/// major-scale interval of the step's degree plus its `alter`, modulo 12; each type is
/// `chordType(typeId)`. Only the tonic of `key` matters (numerals read against the parallel major), so
/// `bVII` is Bb in C Ionian and in C Aeolian, G in A Aeolian. `key` must satisfy the `Key`
/// preconditions of pitch.h. Allocates; not for the audio thread.
std::vector<std::pair<Chord, double>> resolvePreset(const Preset& preset, const Key& key);

/// The display numerals of `preset` (P6): `romanNumeral` (symbol.h, Standard style) of each step
/// resolved in C Ionian (a Major or Any preset) or A Aeolian (a Minor one), joined with " – " (an en
/// dash, U+2013, between spaces). In the engine's own spelling, the one the sequence bar shows:
/// "I – V – vi – IV", "iiø7 – V7 – i7 – i7", "IΔ7 – vi7 – ii7 – V7". Consecutive identical steps are
/// kept (the 12-bar blues reads as twelve numerals). Constant per build. Allocates.
std::string presetNumerals(const Preset& preset);

}  // namespace chordlab
