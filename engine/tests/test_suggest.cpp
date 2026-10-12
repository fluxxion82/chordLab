#include "doctest.h"

#include "chordlab/applied.h"
#include "chordlab/chord.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/relations.h"
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
        CHECK(has(x, Reason::Diatonic) + has(x, Reason::Borrowed) + has(x, Reason::Chromatic) <= 1);
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
    // D->T 1.0 + one common tone (G) 0.1 - pc cost 4 x 0.05 + cadence 0.3 + in key 0.2 + triad 0.25
    // + prior I 1.0.
    CHECK(s[0].score == doctest::Approx(2.65));
    // The deceptive cadence is in the top four: vi's prior (0.8) outweighs iii's two common tones.
    CHECK(contains(top(s, 4), "Am"));
    CHECK(has(*find(s, kAm), Reason::Resolves));
    CHECK(top(s, 5) == std::vector<std::string>{"C", "CΔ7", "Am", "Em", "G"});
}

TEST_CASE("after G7 the tonic seventh resolves and shares tones, but the cadence is the triad's alone") {
    const auto s = suggestNext(&kG7, kCIonian, kCIonian);
    const Suggestion* cmaj7 = find(s, chord(C, "maj7"));
    REQUIRE(cmaj7 != nullptr);
    CHECK(cmaj7->roman == "IΔ7");
    CHECK(has(*cmaj7, Reason::Resolves));
    CHECK_FALSE(has(*cmaj7, Reason::Cadence));
    CHECK(has(*cmaj7, Reason::CommonTones));
    CHECK(hasText(*cmaj7, "2 common tones (G, B)"));
    CHECK_FALSE(has(*cmaj7, Reason::Smooth));  // cost 3
}

TEST_CASE("after C the top three are F, G and Am; Em stays below Am and vii° out of the top five") {
    // T->PD 0.8 and the IV/V priors (0.9) put F and G first; vi's prior (0.8) and two common tones
    // lift Am over Dm (prior 0.6, no common tone) and over iii (prior 0.35), which the table alone
    // could not do.
    const auto s = suggestNext(&kC, kCIonian, kCIonian);
    checkWellFormed(s, &kC, 8);
    REQUIRE(s.size() == 8);
    const std::vector<std::string> first3 = top(s, 3);
    CHECK(contains(first3, "F"));
    CHECK(contains(first3, "G"));
    CHECK(contains(first3, "Am"));
    CHECK(top(s, 5) == std::vector<std::string>{"F", "G", "Am", "Dm", "FΔ7"});
    const auto all = suggestNext(&kC, kCIonian, kCIonian, Style::Pop, 64);  // keeps the pointers below alive
    const Suggestion* am = find(s, kAm);
    const Suggestion* em = find(all, chord(E, "min"));
    REQUIRE((am != nullptr && em != nullptr));
    CHECK(em->score < am->score);
    CHECK_FALSE(contains(top(s, 5), "Bmb5"));
    // T->PD 0.8 + one common tone 0.1 - pc cost 3 x 0.05 + in key 0.2 + triad 0.25 + prior IV 0.9.
    CHECK(s[0].score == doctest::Approx(2.1));
    CHECK(s[0].roman == "IV");
    CHECK(s[0].reasons == std::vector<Reason>{Reason::Diatonic, Reason::Predominant});
    CHECK(s[0].reasonTexts == std::vector<std::string>{"diatonic", "predominant"});
    CHECK(has(s[1], Reason::Dominant));
    CHECK_FALSE(has(s[1], Reason::Prepares));  // T->D is not a preparation
    // The score counts at most two common tones; the sentence still reports the real number.
    const Suggestion* fmaj9 = find(all, chord(F, "maj9"));
    REQUIRE(fmaj9 != nullptr);
    CHECK(hasText(*fmaj9, "3 common tones (C, E, G)"));
    CHECK(fmaj9->score < find(s, kF)->score);
}

TEST_CASE("after C the tonic-group chords are smooth and share two tones") {
    const auto s = suggestNext(&kC, kCIonian, kCIonian, Style::Pop, 64);
    const Suggestion* am = find(s, kAm);
    REQUIRE(am != nullptr);
    CHECK(am->reasons == std::vector<Reason>{Reason::CommonTones, Reason::Smooth, Reason::Diatonic, Reason::Tonic});
    CHECK(am->reasonTexts ==
          std::vector<std::string>{"2 common tones (C, E)", "smooth voice leading (cost 2)", "diatonic", "tonic"});
    const Suggestion* em = find(s, chord(E, "min"));
    REQUIRE(em != nullptr);
    CHECK(hasText(*em, "2 common tones (E, G)"));
    CHECK(hasText(*em, "smooth voice leading (cost 1)"));
}

TEST_CASE("after Dm the dominant is prepared and leads; vii° stays out of the top three") {
    const auto s = suggestNext(&kDm, kCIonian, kCIonian);
    checkWellFormed(s, &kDm, 8);
    CHECK((s[0].label == "G" || s[0].label == "G7"));
    CHECK_FALSE(contains(top(s, 3), "Bmb5"));
    CHECK(top(s, 5) == std::vector<std::string>{"G", "F", "G7", "Am", "C"});
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
}

TEST_CASE("after Am a predominant leads and the dominant is in the top four") {
    const auto s = suggestNext(&kAm, kCIonian, kCIonian);
    checkWellFormed(s, &kAm, 8);
    CHECK((s[0].label == "F" || s[0].label == "Dm"));
    CHECK(contains(top(s, 4), "G"));
    CHECK(top(s, 5) == std::vector<std::string>{"F", "C", "Dm", "G", "FΔ7"});
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
    // The in-key power chord on the tonic resolves but is not the cadence chord either: that is the
    // triad's alone.
    const Suggestion* c5 = find(s, chord(C, "5"));
    REQUIRE(c5 != nullptr);
    CHECK(has(*c5, Reason::Resolves));
    CHECK_FALSE(has(*c5, Reason::Cadence));
}

// --- No previous chord ----------------------------------------------------------------------

TEST_CASE("with no previous chord the tonic triad leads and nothing relates") {
    const auto s = suggestNext(nullptr, kCIonian, kCIonian);
    checkWellFormed(s, nullptr, 8);
    REQUIRE(s.size() == 8);
    CHECK(s[0].chord == kC);
    CHECK(s[0].label == "C");
    // Tonic 1.0 + home 0.1 + in key 0.2 + triad 0.25 + prior I 1.0.
    CHECK(s[0].score == doctest::Approx(2.55));
    CHECK(s[0].reasons == std::vector<Reason>{Reason::Diatonic, Reason::Tonic});
    // One of the pop staples follows within the next three.
    const std::vector<std::string> next3{s[1].label, s[2].label, s[3].label};
    CHECK((contains(next3, "F") || contains(next3, "G") || contains(next3, "Am")));
    CHECK(top(s, 5) == std::vector<std::string>{"C", "Am", "C6", "CΔ7", "F"});
    for (const Suggestion& x : s) {
        CAPTURE(x.label);
        CHECK_FALSE(hasRelationReason(x));
        CHECK(fullyInKey(x.chord, kCIonian));
    }
}

TEST_CASE("with no previous chord the opening ranks by function, diatonicity and the Pop degree prior") {
    const auto s = suggestNext(nullptr, kCIonian, kCIonian, Style::Pop, 64);
    struct Expected {
        Chord chord;
        double score;
    };
    // function base + in key 0.2 + simplicity + prior (I 1.0, ii .6, iii .35, IV .9, V .9, vi .8,
    // vii° .1, bII .2, bIII .4, bVI .5, bVII .5); the tonic chord adds the 0.1 home bonus.
    const std::vector<Expected> expected{
        {kC, 2.55},               // T 1.0 + .1 + .2 + .25 + 1.0
        {kDm, 1.65},              // PD .6 + .2 + .25 + .6
        {chord(E, "min"), 1.8},   // T 1.0 + .2 + .25 + .35
        {kF, 1.95},               // PD .6 + .2 + .25 + .9
        {kG, 1.85},               // D .5 + .2 + .25 + .9
        {kAm, 2.25},              // T 1.0 + .2 + .25 + .8
        {kBdim, 1.05},            // D .5 + .2 + .25 + .1
        {chord(1, "maj"), 1.05},  // bII: PD .6 + .25 + .2
        {chord(3, "maj"), 1.65},  // bIII: T 1.0 + .25 + .4
        {chord(Ab, "maj"), 1.35}, // bVI: PD .6 + .25 + .5
        {chord(Bb, "maj"), 1.75}, // bVII: T 1.0 + .25 + .5
        {chord(C, "5"), 1.8},     // T 1.0 + .1 + .2 - power .5 + 1.0
        {chord(C, "maj7"), 2.05}, // T 1.0 + .1 + .2 - seventh .25 + 1.0
        {chord(C, "maj9"), 1.8},  // T 1.0 + .1 + .2 - ninth .5 + 1.0
        {chord(D, "7"), 0.9},     // applied: D .5 - seventh .25 + applied .2 + half of V's prior .45
    };
    for (const Expected& e : expected) {
        const Suggestion* x = find(s, e.chord);
        REQUIRE(x != nullptr);
        CAPTURE(x->label);
        CHECK(x->score == doctest::Approx(e.score));
    }
    CHECK(has(*find(s, chord(D, "7")), Reason::AppliedDominant));
    CHECK(has(*find(s, chord(3, "maj")), Reason::Borrowed));
}

// --- Applied and borrowed candidates --------------------------------------------------------

TEST_CASE("after F the dominant leads, the tonic is in the top three and V7/V is in the strip") {
    const auto s = suggestNext(&kF, kCIonian, kCIonian);
    checkWellFormed(s, &kF, 8);
    CHECK(contains(top(s, 2), "G"));
    CHECK(contains(top(s, 3), "C"));
    CHECK(top(s, 5) == std::vector<std::string>{"G", "Am", "C", "G7", "D7"});  // Am and C tie at 1.8
    const Suggestion* d7 = find(s, chord(D, "7"));
    REQUIRE(d7 != nullptr);  // within the default eight
    CHECK(d7->label == "D7");
    CHECK(d7->roman == "V7/V");
    CHECK(has(*d7, Reason::AppliedDominant));
    CHECK(hasText(*d7, "secondary dominant of V (V7/V)"));
    CHECK(has(*d7, Reason::Dominant));
    CHECK(has(*d7, Reason::Prepares));  // scored as D after a PD
    CHECK(hasText(*d7, "2 common tones (C, A)"));
    CHECK_FALSE(has(*d7, Reason::Diatonic));
    CHECK_FALSE(has(*d7, Reason::Borrowed));
    // PD->D 0.9 + two common tones 0.2 - pc cost 4 x 0.05 - seventh 0.25 + applied 0.2 + the ii-V
    // setup 0.15 (after a T or PD chord, targeting V or ii) + half of V's prior 0.45.
    CHECK(d7->score == doctest::Approx(1.45));
}

TEST_CASE("applied leading-tone chords are labelled with their target") {
    const auto s = suggestNext(&kF, kCIonian, kCIonian, Style::Pop, 64);
    const Suggestion* fs = find(s, chord(6, "dim7"));
    REQUIRE(fs != nullptr);
    CHECK(fs->label == "F#dim7");  // Klimper style, spelled in its own key (G major): F#, not Gb
    CHECK(fs->roman == "vii°7/V");
    CHECK(has(*fs, Reason::AppliedDominant));
    CHECK(hasText(*fs, "leading-tone chord of V (vii°7/V)"));
    // The ii-V setup bonus is the secondary dominant's, not the leading-tone chord's:
    // PD->D 0.9 + two common tones 0.2 - pc cost 3 x 0.05 - seventh 0.25 + applied 0.2 + 0.45.
    CHECK(fs->score == doctest::Approx(1.35));
    const Suggestion* cs = find(s, chord(1, "dim7"));
    REQUIRE(cs != nullptr);
    CHECK(cs->label == "C#dim7");
    CHECK(cs->roman == "vii°7/ii");
}

TEST_CASE("borrowed chords appear when browsing a parallel key") {
    // bVI is a predominant and bVII a tonic-group chord, so no one previous chord favours both:
    // after the tonic, Ab (T->PD) is in the strip; opening, Bb (T) is.
    const auto afterC = suggestNext(&kC, kCIonian, kCAeolian);
    checkWellFormed(afterC, &kC, 8);
    const Suggestion* ab = find(afterC, chord(Ab, "maj"));
    REQUIRE(ab != nullptr);
    CHECK(ab->label == "Ab");
    CHECK(ab->roman == "bVI");
    CHECK(has(*ab, Reason::Borrowed));
    CHECK(hasText(*ab, "borrowed from C Aeolian"));
    CHECK(has(*ab, Reason::Predominant));
    CHECK(has(*ab, Reason::Smooth));  // C -> Ab: E -> Eb, G -> Ab
    CHECK_FALSE(has(*ab, Reason::Diatonic));

    const auto opening = suggestNext(nullptr, kCIonian, kCAeolian);
    checkWellFormed(opening, nullptr, 8);
    const Suggestion* bb = find(opening, chord(Bb, "maj"));
    REQUIRE(bb != nullptr);
    CHECK(bb->label == "Bb");
    CHECK(bb->roman == "bVII");
    CHECK(has(*bb, Reason::Borrowed));
    CHECK(hasText(*bb, "borrowed from C Aeolian"));
    CHECK(has(*bb, Reason::Tonic));
    // Every hollow cell of the browsed palette is borrowed from C Aeolian; the in-key cells are not.
    for (const Suggestion& x : opening) {
        CAPTURE(x.label);
        CHECK(has(x, Reason::Borrowed) != fullyInKey(x.chord, kCIonian));
    }
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

TEST_CASE("borrowed triads are spelled in the project key, whatever key is browsed") {
    // bII of C is Db; spelled in the browsed G major it would come out as C#.
    const auto s = suggestNext(&kC, kCIonian, key('G', 0, Mode::Ionian), Style::Pop, 64);
    const Suggestion* db = find(s, chord(1, "maj"));
    REQUIRE(db != nullptr);
    CHECK(db->label == "Db");
    CHECK(db->roman == "bII");
    CHECK(find(s, chord(Ab, "maj"))->label == "Ab");
    CHECK(find(s, chord(Bb, "maj"))->label == "Bb");
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

TEST_CASE("a chord outside the key that no parallel mode holds is a colour chord") {
    // Browsing E major from C major: E and B are dominants of vi and iii by function and F#m has
    // none; none of the three is in any parallel mode of C, and none is an appliedChords entry.
    // G#m (Ab, B, Eb) is C harmonic minor's and stays borrowed.
    const auto s = suggestNext(nullptr, kCIonian, key('E', 0, Mode::Ionian), Style::Pop, 64);
    const Suggestion* e = find(s, chord(E, "maj"));
    const Suggestion* b = find(s, chord(B, "maj"));
    const Suggestion* fsm = find(s, chord(6, "min"));
    const Suggestion* gsm = find(s, chord(Ab, "min"));
    REQUIRE((e != nullptr && b != nullptr && fsm != nullptr && gsm != nullptr));
    CHECK(e->reasons == std::vector<Reason>{Reason::Chromatic, Reason::Dominant});
    CHECK(e->reasonTexts == std::vector<std::string>{"outside the key — colour chord", "dominant"});
    CHECK(b->reasons == std::vector<Reason>{Reason::Chromatic, Reason::Dominant});
    CHECK(fsm->reasons == std::vector<Reason>{Reason::Chromatic});
    CHECK(fsm->reasonTexts == std::vector<std::string>{"outside the key — colour chord"});
    CHECK(gsm->reasons == std::vector<Reason>{Reason::Borrowed});
    CHECK(hasText(*gsm, "borrowed from C harmonic minor"));
    // A cell of the browsed key that is in the project key is just diatonic, and an applied chord is
    // explained by its target rather than called chromatic.
    const Suggestion* a5 = find(s, chord(A, "5"));
    const Suggestion* d7 = find(s, chord(D, "7"));
    REQUIRE((a5 != nullptr && d7 != nullptr));
    CHECK(a5->reasons == std::vector<Reason>{Reason::Diatonic, Reason::Tonic});
    CHECK_FALSE(has(*d7, Reason::Chromatic));
    CHECK_FALSE(has(*d7, Reason::Borrowed));
}

TEST_CASE("regression: F+ when browsing D harmonic minor from C Dorian has a reason") {
    // F A C#: not in C Dorian, in no parallel mode of C, not an applied chord, and functionOf says
    // Other (its would-be target Bb is the subtonic), so before Reason::Chromatic it had none.
    const Key cDorian = key('C', 0, Mode::Dorian);
    const auto s = suggestNext(nullptr, cDorian, key('D', 0, Mode::HarmonicMinor));
    checkWellFormed(s, nullptr, 8);
    REQUIRE(s.size() == 8);
    const Suggestion* faug = find(s, chord(F, "aug"));
    REQUIRE(faug != nullptr);
    CHECK(faug->label == "F+");
    CHECK(faug->reasons == std::vector<Reason>{Reason::Chromatic});
    CHECK(faug->reasonTexts == std::vector<std::string>{"outside the key — colour chord"});
}

TEST_CASE("reasonName: every enumerator has a unique non-empty name") {
    std::set<std::string> names;
    for (int i = 0; i < kNumReasons; ++i) {
        const char* name = reasonName(Reason(i));
        REQUIRE(name != nullptr);
        CAPTURE(i);
        CHECK_FALSE(std::string(name).empty());
        CHECK(names.insert(name).second);
    }
    CHECK(names.size() == static_cast<std::size_t>(kNumReasons));
    CHECK(std::string(reasonName(Reason::Resolves)) == "resolves");
    CHECK(std::string(reasonName(Reason::CommonTones)) == "commonTones");
    CHECK(std::string(reasonName(Reason::AppliedDominant)) == "appliedDominant");
    CHECK(std::string(reasonName(Reason::Chromatic)) == "chromatic");
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

TEST_CASE("every project key x every browsed key: every suggestion carries at least one reason") {
    std::vector<Key> keys;
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (int m = 0; m < kNumModes; ++m) {
            keys.push_back(Key{canonicalTonic(pc, Mode(m)), Mode(m)});
        }
    }
    std::vector<Key> ionian;
    for (PitchClass pc = 0; pc < 12; ++pc) {
        ionian.push_back(Key{canonicalTonic(pc, Mode::Ionian), Mode::Ionian});
    }
    const auto check = [](const Chord* previous, const Key& project, const Key& browsed) {
        CAPTURE(project.tonic.letter);
        CAPTURE(static_cast<int>(project.mode));
        CAPTURE(browsed.tonic.letter);
        CAPTURE(static_cast<int>(browsed.mode));
        for (const Suggestion& x : suggestNext(previous, project, browsed)) {
            CAPTURE(x.label);
            CHECK(x.reasons.size() >= 1);
            CHECK(x.reasons.size() == x.reasonTexts.size());
            CHECK(has(x, Reason::Diatonic) + has(x, Reason::Borrowed) + has(x, Reason::Chromatic) <= 1);
            CHECK_FALSE(x.label.empty());
            CHECK_FALSE(x.roman.empty());
        }
    };
    for (const Key& project : keys) {
        // The guarantee is structural (a candidate that is not applied has exactly one of Diatonic, Borrowed
        // and Chromatic whatever precedes it; a previous chord only adds reasons), so the browsed keys need
        // only cover the candidate sources: the palette on the project tonic (its parallel modes) and on
        // every other tonic (the twelve Ionian keys), rather than all 108, which took fifteen seconds.
        const Chord tonic = diatonicTriad(project, 0);
        std::vector<Key> browsedKeys = parallelModes(project);
        browsedKeys.insert(browsedKeys.end(), ionian.begin(), ionian.end());
        for (const Key& browsed : browsedKeys) {
            check(nullptr, project, browsed);
            check(&tonic, project, browsed);
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
