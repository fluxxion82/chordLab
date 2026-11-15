#include "chordlab/guide.h"

#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

namespace chordlab {

namespace {

/// Semitones of each major-scale degree above the tonic: the major or perfect interval a degree label is
/// measured against.
constexpr std::array<int, 7> kMajorScale{0, 2, 4, 5, 7, 9, 11};

/// The representative of `semitones` mod 12 in -6..5.
int signedMod12(int semitones) {
    return ((semitones % 12) + 12 + 6) % 12 - 6;
}

char letterSteps(char letter, int steps) {
    return static_cast<char>('A' + (letter - 'A' + steps) % 7);
}

/// `pc` on `letter`, unfolded (E#, B#, Fx stay as they are), or `fallback` when no single or double accidental
/// reaches it from that letter.
Spelling spellOn(char letter, PitchClass pc, const Spelling& fallback) {
    const int alter = signedMod12(pc - Spelling{letter, 0}.pitchClass());
    if (alter < -2 || alter > 2) {
        return fallback;
    }
    return Spelling{letter, alter};
}

/// "b", "bb", "#", "x" for an alteration of -1, -2, +1, +2.
std::string accidental(int alter) {
    switch (alter) {
        case -2: return "bb";
        case -1: return "b";
        case 1: return "#";
        case 2: return "x";
        default: return "";
    }
}

/// A chord tone's degree label: its structural degree with the alteration against that degree's major or
/// perfect interval, compound (9, 11, 13) when the type writes it at 12 semitones or more.
std::string chordToneLabel(const ChordType& type, int interval) {
    const int degree = chordToneDegree(type, interval);
    if (degree == 0) {
        return "R";
    }
    const int alter = signedMod12(interval - kMajorScale[static_cast<std::size_t>(degree)]);
    const int number = degree + 1 + (interval >= 12 ? 7 : 0);
    return accidental(alter) + std::to_string(number);
}

bool hasIntervalClass(const ChordType& type, int semitones) {
    return std::any_of(type.intervals.begin(), type.intervals.end(),
                       [semitones](int i) { return pitchClassOf(i) == semitones; });
}

/// A non-chord tone's degree label, `interval` (0..11) above the root (see `harmonicGuide`, step 6).
std::string otherToneLabel(int interval, bool hasSeventh, bool majorThird, bool perfectFifth) {
    switch (interval) {
        case 1: return hasSeventh ? "b9" : "b2";
        case 2: return hasSeventh ? "9" : "2";
        case 3: return majorThird ? (hasSeventh ? "#9" : "#2") : "b3";
        case 4: return "3";
        case 5: return hasSeventh ? "11" : "4";
        case 6: return perfectFifth ? (hasSeventh ? "#11" : "#4") : "b5";
        case 7: return "5";
        case 8: return perfectFifth ? (hasSeventh ? "b13" : "b6") : "#5";
        case 9: return hasSeventh ? "13" : "6";
        case 10: return "b7";
        case 11: return "7";
        default: return "R";  // unreachable: the root is a chord tone
    }
}

bool inMaskOf(std::uint16_t mask, PitchClass pc) {
    return ((mask >> pc) & 1u) != 0;
}

/// A tone of the chord scale: its spelling and whether it is a chord tone.
struct ScaleTone {
    Spelling spelling;
    bool chordTone;
};

}  // namespace

const char* guideRoleName(GuideRole role) {
    switch (role) {
        case GuideRole::Root: return "root";
        case GuideRole::ChordTone: return "chord";
        case GuideRole::Tension: return "tension";
        case GuideRole::Avoid: return "avoid";
        case GuideRole::Outside: return "outside";
    }
    assert(false && "chordlab: GuideRole outside the enum");
    return "outside";
}

Guide harmonicGuide(const Chord& chord, const Key& projectKey) {
    assert(chord.type != nullptr);
    assert(chord.root >= 0 && chord.root < 12);
    const ChordType& type = *chord.type;
    const PitchClass root = chord.root;

    // Step 1: the chord tones on their structural letters, the key's scale tones on the signature's.
    const char rootLetter = spell(root, projectKey).letter;
    std::array<bool, 12> inChord{};
    std::vector<Spelling> chordTones;  // in interval order, one per pitch class
    for (const int interval : type.intervals) {
        const PitchClass pc = pitchClassOf(root + interval);
        if (inChord[static_cast<std::size_t>(pc)]) {
            continue;
        }
        inChord[static_cast<std::size_t>(pc)] = true;
        const char letter = letterSteps(rootLetter, chordToneDegree(type, interval));
        chordTones.push_back(spellOn(letter, pc, spell(pc, projectKey)));
    }

    // Step 2: the chord scale. Index d holds the key's degree-d tone, on the d-th letter above the tonic's, until a
    // chord tone on that letter replaces it; a chord tone whose letter is taken is added after the seven.
    std::vector<ScaleTone> scale;
    std::array<bool, 7> replaced{};
    std::array<bool, 12> inKey{};
    for (int d = 0; d < 7; ++d) {
        const Spelling s = spellDegree(projectKey, d);
        scale.push_back(ScaleTone{s, inChord[static_cast<std::size_t>(s.pitchClass())]});
        inKey[static_cast<std::size_t>(s.pitchClass())] = true;
    }
    std::vector<Spelling> replacedTones;
    for (const Spelling& tone : chordTones) {
        if (inKey[static_cast<std::size_t>(tone.pitchClass())]) {
            continue;
        }
        const std::size_t d = static_cast<std::size_t>((tone.letter - projectKey.tonic.letter + 7) % 7);
        assert(scale[d].spelling.letter == tone.letter);
        if (!replaced[d] && !scale[d].chordTone) {
            replacedTones.push_back(scale[d].spelling);
            scale[d] = ScaleTone{tone, true};
            replaced[d] = true;
        } else {
            scale.push_back(ScaleTone{tone, true});
        }
    }

    Guide guide{};
    guide.role.fill(GuideRole::Outside);
    guide.scaleMask = 0;
    std::array<bool, 12> named{};
    for (const ScaleTone& tone : scale) {
        const PitchClass pc = tone.spelling.pitchClass();
        guide.scaleMask = static_cast<std::uint16_t>(guide.scaleMask | (1u << pc));
    }

    // Step 3: roles, and step 7: names, chord tones first so their structural letters win.
    for (const Spelling& tone : chordTones) {
        const std::size_t i = static_cast<std::size_t>(tone.pitchClass());
        guide.role[i] = static_cast<PitchClass>(i) == root ? GuideRole::Root : GuideRole::ChordTone;
        guide.name[i] = tone.text();
        named[i] = true;
    }
    for (const ScaleTone& tone : scale) {
        const std::size_t i = static_cast<std::size_t>(tone.spelling.pitchClass());
        if (named[i]) {
            continue;
        }
        const bool clash = inChord[static_cast<std::size_t>(pitchClassOf(static_cast<int>(i) - 1))];
        guide.role[i] = clash ? GuideRole::Avoid : GuideRole::Tension;
        guide.name[i] = tone.spelling.text();
        named[i] = true;
    }
    for (const Spelling& tone : replacedTones) {
        const std::size_t i = static_cast<std::size_t>(tone.pitchClass());
        if (named[i]) {
            continue;
        }
        guide.role[i] = GuideRole::Avoid;
        guide.name[i] = tone.text();
        named[i] = true;
    }

    const bool majorThird = hasIntervalClass(type, 4);
    const bool minorThird = hasIntervalClass(type, 3);
    const bool perfectFifth = hasIntervalClass(type, 7);
    const bool hasSeventh = hasIntervalClass(type, 10) || hasIntervalClass(type, 11) ||
                            std::any_of(type.intervals.begin(), type.intervals.end(),
                                        [&type](int i) { return pitchClassOf(i) == 9 && chordToneDegree(type, i) == 6; });

    // Step 4: on a dominant chord, an in-key b9, #9 or b13 the rules call Avoid is a Tension.
    if (majorThird && hasIntervalClass(type, 10)) {
        for (const int interval : {1, 3, 8}) {
            const std::size_t i = static_cast<std::size_t>(pitchClassOf(root + interval));
            if (inKey[i] && guide.role[i] == GuideRole::Avoid) {
                guide.role[i] = GuideRole::Tension;
            }
        }
    }

    // Step 5: on a suspended chord, the third the suspension replaces is Avoid.
    if (!majorThird && !minorThird && (hasIntervalClass(type, 2) || hasIntervalClass(type, 5))) {
        for (const int interval : {3, 4}) {
            const PitchClass pc = pitchClassOf(root + interval);
            if (inMaskOf(guide.scaleMask, pc) && !inChord[static_cast<std::size_t>(pc)]) {
                guide.role[static_cast<std::size_t>(pc)] = GuideRole::Avoid;
            }
        }
    }

    // Step 6: degrees; step 7 for Outside pitch classes.
    for (const int interval : type.intervals) {
        const std::size_t i = static_cast<std::size_t>(pitchClassOf(root + interval));
        if (guide.degree[i].empty()) {
            guide.degree[i] = chordToneLabel(type, interval);
        }
    }
    for (PitchClass pc = 0; pc < 12; ++pc) {
        const std::size_t i = static_cast<std::size_t>(pc);
        if (!inChord[i]) {
            guide.degree[i] = otherToneLabel(pitchClassOf(pc - root), hasSeventh, majorThird, perfectFifth);
        }
        if (!named[i]) {
            guide.name[i] = spell(pc, projectKey).text();
        }
    }
    return guide;
}

}  // namespace chordlab
