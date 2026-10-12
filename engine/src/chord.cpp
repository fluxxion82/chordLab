#include "chordlab/chord.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace chordlab {

namespace {

/// The triad with exactly the intervals `third` and `fifth` above the root.
const ChordType& triadWith(int third, int fifth) {
    if (third == 4 && fifth == 7) {
        return chordType("maj");
    }
    if (third == 3 && fifth == 7) {
        return chordType("min");
    }
    if (third == 3 && fifth == 6) {
        return chordType("dim");
    }
    assert(third == 4 && fifth == 8 && "chordlab: stacked thirds do not form a dictionary triad");
    return chordType("aug");
}

}  // namespace

const std::vector<ChordType>& allChordTypes() {
    // Function-local static: initialised once, lives for the program, so references into it are stable.
    static const std::vector<ChordType> kTypes{
        {"5", "5", {0, 7}, Family::Other},
        {"maj", "", {0, 4, 7}, Family::Major},
        {"min", "m", {0, 3, 7}, Family::Minor},
        {"dim", "mb5", {0, 3, 6}, Family::Other},
        {"aug", "+", {0, 4, 8}, Family::Other},
        {"sus2", "sus2", {0, 2, 7}, Family::Other},
        {"sus4", "sus4", {0, 5, 7}, Family::Other},
        {"6", "6", {0, 4, 7, 9}, Family::Major},
        {"m6", "m6", {0, 3, 7, 9}, Family::Minor},
        {"maj7", "Δ7", {0, 4, 7, 11}, Family::Major},
        {"7", "7", {0, 4, 7, 10}, Family::Major},
        {"m7", "m7", {0, 3, 7, 10}, Family::Minor},
        {"m7b5", "m7b5", {0, 3, 6, 10}, Family::Other},
        {"dim7", "dim7", {0, 3, 6, 9}, Family::Other},
        {"mMaj7", "mΔ7", {0, 3, 7, 11}, Family::Minor},
        {"augMaj7", "+Δ7", {0, 4, 8, 11}, Family::Other},
        {"add9", "add9", {0, 4, 7, 14}, Family::Major},
        {"madd9", "madd9", {0, 3, 7, 14}, Family::Minor},
        {"9", "9", {0, 4, 7, 10, 14}, Family::Major},
        {"maj9", "Δ9", {0, 4, 7, 11, 14}, Family::Major},
        {"m9", "m9", {0, 3, 7, 10, 14}, Family::Minor},
        {"69", "6/9", {0, 4, 7, 9, 14}, Family::Major},
        {"7sus4", "7sus4", {0, 5, 7, 10}, Family::Other},
        // Phase 2 colour chords: appended, so every id above keeps its position.
        {"maj7#11", "Δ7#11", {0, 4, 7, 11, 18}, Family::Major},
        {"mMaj9", "mΔ9", {0, 3, 7, 11, 14}, Family::Minor},
        {"7#11", "7#11", {0, 4, 7, 10, 18}, Family::Major},
    };
    return kTypes;
}

const ChordType& chordType(std::string_view id) {
    for (const ChordType& type : allChordTypes()) {
        if (id == type.id) {
            return type;
        }
    }
    throw std::out_of_range("chordlab: unknown chord type id '" + std::string(id) + "'");
}

std::vector<PitchClass> pitchClasses(const Chord& chord) {
    assert(chord.type != nullptr);
    std::vector<PitchClass> result;
    result.reserve(chord.type->intervals.size());
    for (const int interval : chord.type->intervals) {
        result.push_back(pitchClassOf(chord.root + interval));
    }
    return result;
}

bool fullyInKey(const Chord& chord, const Key& key) {
    const PitchClass tonic = key.tonicPc();
    for (const PitchClass pc : pitchClasses(chord)) {
        if (!inScale(pc, tonic, key.mode)) {
            return false;
        }
    }
    return true;
}

Chord diatonicTriad(const Key& key, int degree) {
    const std::array<PitchClass, 7> scale = scalePitchClasses(key.tonicPc(), key.mode);
    const auto d = static_cast<std::size_t>(((degree % 7) + 7) % 7);
    const PitchClass root = scale[d];
    const int third = pitchClassOf(scale[(d + 2) % 7] - root);
    const int fifth = pitchClassOf(scale[(d + 4) % 7] - root);
    return Chord{root, &triadWith(third, fifth)};
}

}  // namespace chordlab
