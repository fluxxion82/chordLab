#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"

#include <string>

namespace chordlab {

/// How chord qualities are written.
/// Standard uses the conventional glyphs for diminished and half-diminished qualities (`B°`, `Bø7`,
/// `B°7`, `vii°`); Klimper writes them the way the Klimper app does (`Bmb5`, `Bm7b5`, `Bdim7`,
/// `viib5`). Everything else is shared: `C+`, `CΔ7`, `C+Δ7`, `C6/9`. Accidentals are ASCII `b`/`#`
/// (the UI maps them to glyphs); `Δ`, `°` and `ø` are literal UTF-8 characters.
enum class SymbolStyle { Standard, Klimper };

/// The chord's symbol: the root spelled in `spellingKey` (`spell(root, spellingKey).text()`) followed
/// by the type's suffix in the given style, e.g. `Bm7b5` / `Bø7`, `Eb`, `CmΔ7`.
std::string chordSymbol(const Chord& chord, const Key& spellingKey, SymbolStyle style = SymbolStyle::Klimper);

/// The chord's roman numeral relative to `projectKey`, e.g. `vii°`, `V7`, `bVI`, `#iv°`, `IΔ7`.
///
/// Degree: a root in the project scale is that degree (I..VII) with no accidental, whatever the
/// quality (D major in C Ionian is `II`). A chromatic root takes the degree it holds in a parallel
/// mode of the key (b2, b3, b6, b7 above a major scale; #3, #6, #7 above a minor one; the tritone is
/// always #4 rather than b5) and is prefixed with that degree's alteration relative to the project
/// mode, always a single `b` or `#`: in C Ionian `Ab` is `bVI`, `F#` is `#IV`; in A Aeolian `G#` is
/// `#VII`; in C Lydian `F` is `bIV`.
/// Case: lowercase when the type has a minor third and no major third; uppercase otherwise,
/// including power chords and suspended chords (`I5`, `Isus4`).
/// Suffix: as `chordSymbol`, except that a minor quality is carried by the lowercase numeral
/// (`ii`, `ii7`, `iΔ7`, `iadd9`, `i6`).
std::string romanNumeral(const Chord& chord, const Key& projectKey, SymbolStyle style = SymbolStyle::Standard);

}  // namespace chordlab
