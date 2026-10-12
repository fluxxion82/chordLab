#include "doctest.h"

#include "chordlab/applied.h"
#include "chordlab/chord.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/suggest.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

// Pitch classes by name, so the cases below read like chord names.
constexpr PitchClass C = 0, D = 2, E = 4, F = 5, G = 7, Ab = 8, A = 9, Bb = 10, B = 11;

const Key kCIonian = key('C', 0, Mode::Ionian);
const Key kCAeolian = key('C', 0, Mode::Aeolian);

const Chord kC = chord(C, "maj");
const Chord kDm = chord(D, "min");
const Chord kF = chord(F, "maj");
const Chord kG = chord(G, "maj");
const Chord kG7 = chord(G, "7");
const Chord kAm = chord(A, "min");
const Chord kBdim = chord(B, "dim");

/// The labels of `s`, in order.
std::vector<std::string> labels(const std::vector<Suggestion>& s) {
    std::vector<std::string> out;
    for (const Suggestion& x : s) {
        out.push_back(x.label);
    }
    return out;
}

/// The labels of the first `n` suggestions.
std::vector<std::string> top(const std::vector<Suggestion>& s, std::size_t n) {
    std::vector<std::string> out = labels(s);
    if (out.size() > n) {
        out.resize(n);
    }
    return out;
}

/// The suggestion for `chord`, or nullptr when it is not in `s`.
const Suggestion* find(const std::vector<Suggestion>& s, const Chord& chord) {
    for (const Suggestion& x : s) {
        if (x.chord == chord) {
            return &x;
        }
    }
    return nullptr;
}

bool has(const Suggestion& s, Reason reason) {
    return std::find(s.reasons.begin(), s.reasons.end(), reason) != s.reasons.end();
}

bool hasText(const Suggestion& s, std::string_view text) {
    return std::find(s.reasonTexts.begin(), s.reasonTexts.end(), text) != s.reasonTexts.end();
}

bool contains(const std::vector<std::string>& v, std::string_view x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

bool hasRelationReason(const Suggestion& s) {
    return has(s, Reason::Resolves) || has(s, Reason::Cadence) || has(s, Reason::Prepares) ||
           has(s, Reason::CommonTones) || has(s, Reason::Smooth);
}

/// The invariants every result must satisfy, whatever the input.
void checkWellFormed(const std::vector<Suggestion>& s, const Chord* previous, std::size_t maxCount) {
    CHECK(s.size() <= maxCount);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const Suggestion& x = s[i];
        CAPTURE(i);
        CAPTURE(x.label);
        CHECK(x.chord.type != nullptr);
        CHECK(x.chord.root >= 0);
        CHECK(x.chord.root < 12);
        CHECK_FALSE(x.label.empty());
        CHECK_FALSE(x.roman.empty());
        CHECK(x.reasons.size() == x.reasonTexts.size());
        CHECK(x.reasonTexts.size() >= 1);
        for (const std::string& text : x.reasonTexts) {
            CHECK_FALSE(text.empty());
        }
        if (previous != nullptr) {
            CHECK(x.chord != *previous);
        }
        if (i > 0) {
            const Suggestion& before = s[i - 1];
            // Sorted by score descending, then label ascending.
            CHECK((before.score > x.score || (before.score == x.score && before.label < x.label)));
            CHECK(before.chord != x.chord);
        }
    }
}

bool sameSuggestion(const Suggestion& a, const Suggestion& b) {
    return a.chord == b.chord && a.label == b.label && a.roman == b.roman && a.score == b.score &&
           a.reasons == b.reasons && a.reasonTexts == b.reasonTexts;
}

}  // namespace

// --- Plan cases in C major ------------------------------------------------------------------

TEST_CASE("after G7 the top suggestion is C, resolving with a cadence") {
    const auto s = suggestNext(&kG7, kCIonian, kCIonian);
    checkWellFormed(s, &kG7, 8);
    REQUIRE(s.size() == 8);
    CHECK(s[0].chord == kC);
    CHECK(s[0].label == "C");
    CHECK(s[0].roman == "I");
    CHECK(has(s[0], Reason::Resolves));
    CHECK(has(s[0], Reason::Cadence));
    CHECK(has(s[0], Reason::Diatonic));
    CHECK(has(s[0], Reason::Tonic));
    CHECK(hasText(s[0], "resolves (D→T)"));
    CHECK(hasText(s[0], "cadence (V7→I)"));
    CHECK(hasText(s[0], "diatonic"));
    CHECK(hasText(s[0], "tonic"));
    // D->T 1.0 + one common tone (G) 0.15 - pc cost 4 x 0.03 + cadence 0.3 + in key 0.4 + triad 0.25.
    CHECK(s[0].score == doctest::Approx(1.98));
    // iii (two common tones) and the tonic's own extensions follow; the extensions share more tones
    // with G7 but pay for their size.
    CHECK(top(s, 5) == std::vector<std::string>{"C", "Em", "CΔ7", "CΔ9", "Em7"});
}

TEST_CASE("after G7 the tonic seventh chords also carry the cadence and their common tones") {
    const auto s = suggestNext(&kG7, kCIonian, kCIonian);
    const Suggestion* cmaj7 = find(s, chord(C, "maj7"));
    REQUIRE(cmaj7 != nullptr);
    CHECK(cmaj7->roman == "IΔ7");
    CHECK(has(*cmaj7, Reason::Cadence));
    CHECK(hasText(*cmaj7, "cadence (V7→IΔ7)"));
    CHECK(has(*cmaj7, Reason::CommonTones));
    CHECK(hasText(*cmaj7, "2 common tones (G, B)"));
    CHECK_FALSE(has(*cmaj7, Reason::Smooth));  // cost 3
}

TEST_CASE("after C the top three are F, FΔ9 and G; Dm, Em and Am follow") {
    // T->PD 0.8 puts the predominants ahead of the dominant's 0.6 + one shared tone; FΔ9 holds the
    // whole C triad (three common tones, 0.45) and stays second despite the ninth's -0.2. The
    // tonic-group chords (T->T 0.3) trail even with two common tones, and Em (cost 1) edges Am
    // (cost 2), so vi cannot reach the top three under a function-level table.
    const auto s = suggestNext(&kC, kCIonian, kCIonian);
    checkWellFormed(s, &kC, 8);
    REQUIRE(s.size() == 8);
    CHECK(top(s, 3) == std::vector<std::string>{"F", "FΔ9", "G"});
    CHECK(contains(labels(s), "Am"));
    CHECK(contains(labels(s), "Em"));
    CHECK(top(s, 8) == std::vector<std::string>{"F", "FΔ9", "G", "Dm", "F5", "FΔ7", "Em", "Am"});
    // T->PD 0.8 + one common tone 0.15 - pc cost 3 x 0.03 + in key 0.4 + triad 0.25.
    CHECK(s[0].score == doctest::Approx(1.51));
    CHECK(s[0].roman == "IV");
    CHECK(s[0].reasons == std::vector<Reason>{Reason::Diatonic, Reason::Predominant});
    CHECK(s[0].reasonTexts == std::vector<std::string>{"diatonic", "predominant"});
    CHECK(hasText(s[1], "3 common tones (C, E, G)"));
    CHECK(has(s[2], Reason::Dominant));
    CHECK_FALSE(has(s[2], Reason::Prepares));  // T->D is not a preparation
}

TEST_CASE("after C the tonic-group chords are smooth and share two tones") {
    const auto s = suggestNext(&kC, kCIonian, kCIonian);
    const Suggestion* am = find(s, kAm);
    REQUIRE(am != nullptr);
    CHECK(am->reasons == std::vector<Reason>{Reason::CommonTones, Reason::Smooth, Reason::Diatonic, Reason::Tonic});
    CHECK(am->reasonTexts ==
          std::vector<std::string>{"2 common tones (C, E)", "smooth voice leading (cost 2)", "diatonic", "tonic"});
    const Suggestion* em = find(s, chord(E, "min"));
    REQUIRE(em != nullptr);
    CHECK(hasText(*em, "2 common tones (E, G)"));
    CHECK(hasText(*em, "smooth voice leading (cost 1)"));
    CHECK(em->score > am->score);
}

TEST_CASE("after Dm the dominant is prepared") {
    const auto s = suggestNext(&kDm, kCIonian, kCIonian);
    checkWellFormed(s, &kDm, 8);
    const std::vector<std::string> first3 = top(s, 3);
    CHECK((contains(first3, "G") || contains(first3, "G7")));
    const Suggestion* g = find(s, kG);
    REQUIRE(g != nullptr);
    CHECK(has(*g, Reason::Prepares));
    CHECK(hasText(*g, "prepares (PD→D)"));
    CHECK(has(*g, Reason::Dominant));
    CHECK_FALSE(has(*g, Reason::CommonTones));  // only D is shared
    const Suggestion* g7 = find(s, kG7);
    REQUIRE(g7 != nullptr);
    CHECK(has(*g7, Reason::Prepares));
    CHECK(hasText(*g7, "2 common tones (D, F)"));
    // The leading-tone chord shares two tones with ii and moves by one step, so it leads: a known
    // weakness of a function-level table (ii -> vii° is rare in pop).
    CHECK(top(s, 5) == std::vector<std::string>{"Bmb5", "G", "Bm7b5", "G9", "G7"});
}

TEST_CASE("after Am the top three include F") {
    const auto s = suggestNext(&kAm, kCIonian, kCIonian);
    checkWellFormed(s, &kAm, 8);
    CHECK(contains(top(s, 3), "F"));
    CHECK(top(s, 3) == std::vector<std::string>{"F", "Dm", "FΔ7"});
    const Suggestion* f = find(s, kF);
    REQUIRE(f != nullptr);
    CHECK(has(*f, Reason::Predominant));
    CHECK(has(*f, Reason::CommonTones));
    CHECK(hasText(*f, "2 common tones (C, A)"));  // ascending pitch classes from C
    CHECK(has(*f, Reason::Smooth));
    CHECK(hasText(*f, "smooth voice leading (cost 1)"));
    CHECK_FALSE(has(*f, Reason::Resolves));  // T->PD
}

TEST_CASE("vii° to I is a cadence too") {
    const auto s = suggestNext(&kBdim, kCIonian, kCIonian);
    checkWellFormed(s, &kBdim, 8);
    REQUIRE_FALSE(s.empty());
    CHECK(s[0].chord == kC);
    CHECK(has(s[0], Reason::Cadence));
    CHECK(hasText(s[0], "cadence (vii°→I)"));
    CHECK(has(s[0], Reason::Resolves));
}

TEST_CASE("a borrowed chord on the tonic root is not a cadence") {
    // G7 -> Cm: D->T, but Cm is not the tonic chord of C major.
    const auto s = suggestNext(&kG7, kCIonian, kCAeolian, Style::Pop, 32);
    const Suggestion* cm = find(s, chord(C, "min"));
    REQUIRE(cm != nullptr);
    CHECK(has(*cm, Reason::Resolves));
    CHECK_FALSE(has(*cm, Reason::Cadence));
    CHECK(has(*cm, Reason::Borrowed));
    CHECK_FALSE(has(*cm, Reason::Diatonic));
    // Its tonic sibling C5 is in key and does cadence.
    const Suggestion* c5 = find(s, chord(C, "5"));
    REQUIRE(c5 != nullptr);
    CHECK(has(*c5, Reason::Cadence));
}

// --- No previous chord ----------------------------------------------------------------------

TEST_CASE("with no previous chord the tonic triad leads and nothing relates") {
    const auto s = suggestNext(nullptr, kCIonian, kCIonian);
    checkWellFormed(s, nullptr, 8);
    REQUIRE(s.size() == 8);
    CHECK(s[0].chord == kC);
    CHECK(s[0].label == "C");
    // Tonic 1.0 + home 0.1 + in key 0.4 + triad 0.25.
    CHECK(s[0].score == doctest::Approx(1.75));
    CHECK(s[0].reasons == std::vector<Reason>{Reason::Diatonic, Reason::Tonic});
    // The other tonic-group triads follow, then the tonic group's extensions by label.
    CHECK(top(s, 3) == std::vector<std::string>{"C", "Am", "Em"});
    for (const Suggestion& x : s) {
        CAPTURE(x.label);
        CHECK_FALSE(hasRelationReason(x));
        CHECK(fullyInKey(x.chord, kCIonian));
    }
}

TEST_CASE("with no previous chord the functions rank T, PD, D, then applied and borrowed") {
    const auto s = suggestNext(nullptr, kCIonian, kCIonian, Style::Pop, 64);
    const Suggestion* f = find(s, kF);
    const Suggestion* g = find(s, kG);
    const Suggestion* d7 = find(s, chord(D, "7"));
    const Suggestion* eb = find(s, chord(3, "maj"));
    REQUIRE((f != nullptr && g != nullptr && d7 != nullptr && eb != nullptr));
    CHECK(f->score == doctest::Approx(1.25));  // PD 0.6 + in key 0.4 + triad 0.25
    CHECK(g->score == doctest::Approx(1.15));  // D 0.5 + in key 0.4 + triad 0.25
    CHECK(d7->score == doctest::Approx(0.35)); // D 0.5 - applied 0.05 - seventh 0.1
    CHECK(eb->score == doctest::Approx(1.25)); // T 1.0 + triad 0.25 (bIII is tonic-group, not in key)
    CHECK(has(*d7, Reason::AppliedDominant));
    CHECK(has(*eb, Reason::Borrowed));
}

// --- Applied and borrowed candidates --------------------------------------------------------

TEST_CASE("applied dominants are listed after a predominant, below the diatonic dominants") {
    const auto s = suggestNext(&kF, kCIonian, kCIonian, Style::Pop, 64);
    checkWellFormed(s, &kF, 64);
    const Suggestion* d7 = find(s, chord(D, "7"));
    REQUIRE(d7 != nullptr);
    CHECK(d7->label == "D7");
    CHECK(d7->roman == "V7/V");
    CHECK(has(*d7, Reason::AppliedDominant));
    CHECK(hasText(*d7, "secondary dominant of V (V7/V)"));
    CHECK(has(*d7, Reason::Dominant));
    CHECK(has(*d7, Reason::Prepares));  // scored as D after a PD
    CHECK(hasText(*d7, "2 common tones (C, A)"));
    CHECK_FALSE(has(*d7, Reason::Diatonic));
    CHECK_FALSE(has(*d7, Reason::Borrowed));
    // PD->D 0.9 + two common tones 0.3 - pc cost 4 x 0.03 - applied 0.05 - seventh 0.1: below every
    // diatonic dominant (in key +0.4) and the plain triads, so outside the default eight.
    CHECK(d7->score == doctest::Approx(0.93));
    CHECK(top(s, 8) == std::vector<std::string>{"Bmb5", "Bm7b5", "G", "Am", "G9", "Dm", "G7", "G5"});
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK_FALSE(has(s[i], Reason::AppliedDominant));
    }
}

TEST_CASE("applied leading-tone chords are labelled with their target") {
    const auto s = suggestNext(&kF, kCIonian, kCIonian, Style::Pop, 64);
    const Suggestion* fs = find(s, chord(6, "dim7"));
    REQUIRE(fs != nullptr);
    CHECK(fs->label == "F#dim7");  // Klimper style, spelled in its own key (G major): F#, not Gb
    CHECK(fs->roman == "vii°7/V");
    CHECK(has(*fs, Reason::AppliedDominant));
    CHECK(hasText(*fs, "leading-tone chord of V (vii°7/V)"));
    const Suggestion* cs = find(s, chord(1, "dim7"));
    REQUIRE(cs != nullptr);
    CHECK(cs->label == "C#dim7");
    CHECK(cs->roman == "vii°7/ii");
}

TEST_CASE("borrowed chords appear when browsing a parallel key") {
    const auto s = suggestNext(nullptr, kCIonian, kCAeolian, Style::Pop, 24);
    checkWellFormed(s, nullptr, 24);
    const Suggestion* ab = find(s, chord(Ab, "maj"));
    const Suggestion* bb = find(s, chord(Bb, "maj"));
    REQUIRE(ab != nullptr);
    REQUIRE(bb != nullptr);
    CHECK(ab->label == "Ab");
    CHECK(ab->roman == "bVI");
    CHECK(has(*ab, Reason::Borrowed));
    CHECK(hasText(*ab, "borrowed from C Aeolian"));
    CHECK(has(*ab, Reason::Predominant));
    CHECK_FALSE(has(*ab, Reason::Diatonic));
    CHECK(bb->roman == "bVII");
    CHECK(has(*bb, Reason::Borrowed));
    CHECK(hasText(*bb, "borrowed from C Aeolian"));
    CHECK(has(*bb, Reason::Tonic));
    // With no previous chord the one in-key tonic chord (C5) leads, then the borrowed tonic-group triads.
    CHECK(top(s, 4) == std::vector<std::string>{"C5", "Cm", "Bb", "Eb"});
}

TEST_CASE("the four common borrowed triads are candidates even when browsing the project key") {
    const auto s = suggestNext(&kC, kCIonian, kCIonian, Style::Pop, 64);
    const Suggestion* db = find(s, chord(1, "maj"));
    const Suggestion* eb = find(s, chord(3, "maj"));
    const Suggestion* ab = find(s, chord(Ab, "maj"));
    const Suggestion* bb = find(s, chord(Bb, "maj"));
    REQUIRE((db != nullptr && eb != nullptr && ab != nullptr && bb != nullptr));
    CHECK(db->label == "Db");
    CHECK(db->roman == "bII");
    CHECK(hasText(*db, "borrowed from C Phrygian"));
    CHECK(eb->label == "Eb");
    CHECK(hasText(*eb, "borrowed from C Aeolian"));
    CHECK(ab->label == "Ab");
    CHECK(hasText(*ab, "borrowed from C Aeolian"));
    CHECK(bb->label == "Bb");
    CHECK(hasText(*bb, "borrowed from C Aeolian"));
    CHECK(has(*ab, Reason::Smooth));  // C -> Ab: E -> Eb, G -> Ab
    CHECK(hasText(*ab, "smooth voice leading (cost 2)"));
}

TEST_CASE("the full candidate list is the palette, the applied chords and the borrowed triads, once each") {
    const auto s = suggestNext(nullptr, kCIonian, kCIonian, Style::Pop, 1000);
    std::set<std::string> seen;
    for (const Suggestion& x : s) {
        CHECK(seen.insert(x.label).second);
    }
    const Palette p = buildPalette(kCIonian, kCIonian);
    CHECK(s.size() == p.cells.size() + appliedChords(kCIonian).size() + 4);
    for (const PaletteCell& cell : p.cells) {
        CAPTURE(cell.symbol);
        const Suggestion* x = find(s, cell.chord);
        REQUIRE(x != nullptr);
        CHECK(x->label == cell.symbol);
        CHECK(x->roman == cell.roman);
    }
    for (const AppliedChord& a : appliedChords(kCIonian)) {
        CAPTURE(a.label);
        const Suggestion* x = find(s, a.chord);
        REQUIRE(x != nullptr);
        CHECK(x->roman == a.label);
    }
}

TEST_CASE("an applied chord that is also a palette cell is listed once, as the cell") {
    // In A Aeolian G7 is both the diatonic VII7 and V7/bIII.
    const Key aAeolian = key('A', 0, Mode::Aeolian);
    const auto s = suggestNext(nullptr, aAeolian, aAeolian, Style::Pop, 1000);
    int count = 0;
    for (const Suggestion& x : s) {
        if (x.chord == kG7) {
            ++count;
            CHECK(x.roman == "bVII7");
            CHECK(has(x, Reason::Diatonic));
            CHECK_FALSE(has(x, Reason::AppliedDominant));
        }
    }
    CHECK(count == 1);
}

// --- Contract -------------------------------------------------------------------------------

TEST_CASE("maxCount is respected") {
    CHECK(suggestNext(&kC, kCIonian, kCIonian, Style::Pop, 0).empty());
    CHECK(suggestNext(&kC, kCIonian, kCIonian, Style::Pop, 1).size() == 1);
    CHECK(suggestNext(&kC, kCIonian, kCIonian, Style::Pop, 3).size() == 3);
    CHECK(suggestNext(&kC, kCIonian, kCIonian).size() == 8);
    const auto all = suggestNext(&kC, kCIonian, kCIonian, Style::Pop, 1000);
    CHECK(all.size() > 8);
    CHECK(all.size() < 1000);
    // A prefix of the full list.
    CHECK(top(all, 3) == labels(suggestNext(&kC, kCIonian, kCIonian, Style::Pop, 3)));
}

TEST_CASE("the previous chord is never suggested") {
    for (const Chord* prev : {&kC, &kG7, &kDm, &kAm, &kBdim}) {
        const auto s = suggestNext(prev, kCIonian, kCIonian, Style::Pop, 1000);
        CHECK(find(s, *prev) == nullptr);
    }
}

TEST_CASE("the same input gives the identical list twice") {
    for (const Chord* prev : {static_cast<const Chord*>(nullptr), &kC, &kG7, &kDm}) {
        const auto a = suggestNext(prev, kCIonian, kCAeolian, Style::Pop, 64);
        const auto b = suggestNext(prev, kCIonian, kCAeolian, Style::Pop, 64);
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            CAPTURE(i);
            CHECK(sameSuggestion(a[i], b[i]));
        }
    }
}

TEST_CASE("every suggestion is well formed, with and without a previous chord, in and out of key") {
    for (const Chord* prev : {static_cast<const Chord*>(nullptr), &kC, &kG7, &kDm, &kAm, &kF}) {
        for (const Key& browsed : {kCIonian, kCAeolian, key('G', 0, Mode::Ionian), key('F', 1, Mode::Lydian)}) {
            CAPTURE(browsed.tonic.letter);
            const auto s = suggestNext(prev, kCIonian, browsed, Style::Pop, 12);
            checkWellFormed(s, prev, 12);
            CHECK(s.size() == 12);
        }
    }
}

TEST_CASE("the roman numeral reads against the project key, the label against the browsed key") {
    const Key gIonian = key('G', 0, Mode::Ionian);
    const auto s = suggestNext(&kC, kCIonian, gIonian, Style::Pop, 64);
    const Suggestion* d = find(s, chord(D, "maj"));
    REQUIRE(d != nullptr);
    CHECK(d->label == "D");
    CHECK(d->roman == "II");
    CHECK(has(*d, Reason::Borrowed));
    CHECK(hasText(*d, "borrowed from C Lydian"));
    const Suggestion* fs = find(s, chord(6, "dim"));
    REQUIRE(fs != nullptr);
    CHECK(fs->label == "F#mb5");
    CHECK(fs->roman == "#iv°");
}

TEST_CASE("Locrian: only the tonic chord is tonic, and the diatonic chords still lead") {
    const Key cLocrian = key('C', 0, Mode::Locrian);
    const auto s = suggestNext(nullptr, cLocrian, cLocrian);
    REQUIRE(s.size() == 8);
    CHECK(s[0].chord == diatonicTriad(cLocrian, 0));
    CHECK(has(s[0], Reason::Tonic));
    CHECK(s[1].chord == chord(C, "m7b5"));  // the tonic's seventh is the tonic chord too (rule 1)
    CHECK(has(s[1], Reason::Tonic));
    for (std::size_t i = 2; i < s.size(); ++i) {
        CAPTURE(s[i].label);
        CHECK(fullyInKey(s[i].chord, cLocrian));
        CHECK(s[i].reasons == std::vector<Reason>{Reason::Diatonic});  // Other has no function reason
    }
}

// --- Properties -----------------------------------------------------------------------------

TEST_CASE("every canonical key opens with its tonic triad and a fully diatonic list") {
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (int m = 0; m < kNumModes; ++m) {
            const Key k{canonicalTonic(pc, Mode(m)), Mode(m)};
            CAPTURE(pc);
            CAPTURE(m);
            const auto s = suggestNext(nullptr, k, k);
            checkWellFormed(s, nullptr, 8);
            REQUIRE(s.size() == 8);
            CHECK(s[0].chord == diatonicTriad(k, 0));
            CHECK(has(s[0], Reason::Tonic));
            for (const Suggestion& x : s) {
                CAPTURE(x.label);
                CHECK(fullyInKey(x.chord, k));
                CHECK(has(x, Reason::Diatonic));
                CHECK_FALSE(has(x, Reason::Borrowed));
                CHECK_FALSE(has(x, Reason::AppliedDominant));
                CHECK_FALSE(hasRelationReason(x));
            }
        }
    }
}

TEST_CASE("every canonical key: after each diatonic triad the list is well formed and never repeats it") {
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (int m = 0; m < kNumModes; ++m) {
            const Key k{canonicalTonic(pc, Mode(m)), Mode(m)};
            for (int d = 0; d < 7; ++d) {
                const Chord prev = diatonicTriad(k, d);
                CAPTURE(pc);
                CAPTURE(m);
                CAPTURE(d);
                const auto s = suggestNext(&prev, k, k);
                checkWellFormed(s, &prev, 8);
                CHECK(s.size() == 8);
            }
        }
    }
}
