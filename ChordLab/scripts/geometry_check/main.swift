// Checks of the UI's pure logic (ChordLab has no XCTest target): `run.sh` compiles this with plain swiftc
// against ../../UI/SequenceGeometry.swift, HarmonyText.swift, ChordSymbol.swift and Snapshot.swift and
// runs it. Prints one line per expectation and exits non-zero if any failed. Add a case here whenever the
// sequence bar's geometry rules or the harmony text formatting change.
import CoreGraphics
import Foundation

var failures = 0
func expect(_ condition: Bool, _ what: String, file: String = #file, line: Int = #line) {
  if condition {
    print("ok    \(what)")
  } else {
    failures += 1
    print("FAIL  \(what)  (line \(line))")
  }
}

let px: CGFloat = 24

// --- 32 quarter-beat blocks: the case Codex raised ---
let quarters = SequenceGeometry.layout(lengths: Array(repeating: 0.25, count: 32), pxPerBeat: px)
expect(quarters.frames.count == 32, "32 frames")
expect(quarters.endX == 192, "32 quarter-beat blocks span 192 pt (not 320): endX == \(quarters.endX)")
expect(quarters.frames.allSatisfy { $0.width == 6 }, "every quarter-beat block is 6 pt wide")
expect(quarters.frames.enumerated().allSatisfy { CGFloat($0.offset) * 6 == $0.element.x }, "blocks abut at multiples of 6")
expect(quarters.frames[0].centre == 3 && quarters.frames[1].centre == 9, "centres at 3, 9, ...")
expect(SequenceGeometry.x(ofBeat: 8, pxPerBeat: px) == quarters.endX, "the end marker x equals the ruler's beat 8")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 2, in: quarters) == 0, "dragging block 0 by 2 pt: stays at 0 (centre 5 < 9)")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 5.9, in: quarters) == 0, "by 5.9 pt: still 0 (centre 8.9 < 9)")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 7, in: quarters) == 1, "by 7 pt: index 1, and the block is drawn 7 pt right, past block 1's centre")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 12.5, in: quarters) == 2, "by 12.5 pt: index 2 (centre 15.5 > 9, 15)")
expect(SequenceGeometry.targetIndex(from: 31, droppedBy: -1000, in: quarters) == 0, "the last block dragged far left: 0")
expect(SequenceGeometry.targetIndex(from: 5, droppedBy: 0, in: quarters) == 5, "no drag: itself")
expect(SequenceGeometry.targetIndex(from: 40, droppedBy: 10, in: quarters) == 40, "index outside the layout: itself")

// --- mixed lengths: 4, 0.25, 2 beats ---
let mixed = SequenceGeometry.layout(lengths: [4, 0.25, 2], pxPerBeat: px)
expect(mixed.frames.map(\.x) == [0, 96, 102], "x: 0, 96, 102")
expect(mixed.frames.map(\.width) == [96, 6, 48], "widths: 96, 6, 48")
expect(mixed.endX == 150, "endX 150")
expect(mixed.frames.map(\.centre) == [48, 99, 126], "centres 48, 99, 126")
expect(SequenceGeometry.targetIndex(from: 2, droppedBy: -100, in: mixed) == 0, "block 2 dropped at 26: before everything")
expect(SequenceGeometry.targetIndex(from: 2, droppedBy: -30, in: mixed) == 1, "block 2 dropped at 96: after block 0, before block 1 (centre 99)")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 60, in: mixed) == 1, "block 0 dropped at 108: after block 1 (99), before block 2 (126)")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 100, in: mixed) == 2, "block 0 dropped at 148: last")
expect(SequenceGeometry.targetIndex(from: 1, droppedBy: -60, in: mixed) == 0, "the quarter-beat block dropped at 39: first")
expect(SequenceGeometry.targetIndex(from: 1, droppedBy: -40, in: mixed) == 1, "the quarter-beat block dropped at 59: after block 0's centre, still 1")

// --- rendering and the ruler agree: the x of beat b equals the frame of the block starting there ---
let starts: [Double] = [0, 4, 4.25]
expect(zip(starts, mixed.frames).allSatisfy { SequenceGeometry.x(ofBeat: $0, pxPerBeat: px) == $1.x }, "beat -> x matches every block start")

// --- degenerate input ---
let empty = SequenceGeometry.layout(lengths: [], pxPerBeat: px)
expect(empty.frames.isEmpty && empty.endX == 0, "empty layout")
let odd = SequenceGeometry.layout(lengths: [1, .nan, -2, .infinity], pxPerBeat: px)
expect(odd.frames.map(\.width) == [24, 0, 0, 0] && odd.endX == 24, "non-finite or negative lengths render as 0 wide")

// --- resize snapping and clamping ---
let range = 0.25...64.0
expect(SequenceGeometry.resizedLength(4, by: 0, pxPerBeat: px, snap: 0.25, range: range) == 4, "no drag: unchanged")
expect(SequenceGeometry.resizedLength(4, by: 24, pxPerBeat: px, snap: 0.25, range: range) == 5, "+24 pt: +1 beat")
expect(SequenceGeometry.resizedLength(4, by: 5, pxPerBeat: px, snap: 0.25, range: range) == 4.25, "+5 pt (0.21 beat) snaps to 4.25")
expect(SequenceGeometry.resizedLength(4, by: 2, pxPerBeat: px, snap: 0.25, range: range) == 4, "+2 pt (0.08 beat) snaps back to 4")
expect(SequenceGeometry.resizedLength(0.5, by: -1000, pxPerBeat: px, snap: 0.25, range: range) == 0.25, "clamped to 0.25")
expect(SequenceGeometry.resizedLength(60, by: 1000, pxPerBeat: px, snap: 0.25, range: range) == 64, "clamped to 64")

// --- section spans and boundary snapping (the sections header) ---
let spans = SequenceGeometry.sectionSpans(starts: [0, 2], in: mixed)
expect(spans == [SequenceGeometry.SpanFrame(x: 0, width: 102), SequenceGeometry.SpanFrame(x: 102, width: 48)],
       "two sections over 4, 0.25, 2 beats: spans 0–102 and 102–150")
expect(SequenceGeometry.sectionSpans(starts: [0], in: mixed) == [SequenceGeometry.SpanFrame(x: 0, width: 150)], "one section spans everything")
expect(SequenceGeometry.sectionSpans(starts: [], in: mixed).isEmpty, "no sections, no spans")
expect(SequenceGeometry.sectionSpans(starts: [0, 7], in: mixed) == [SequenceGeometry.SpanFrame(x: 0, width: 150), SequenceGeometry.SpanFrame(x: 150, width: 0)],
       "a start outside the layout: the earlier span runs to the end, the stale one is empty")
expect(SequenceGeometry.sectionSpans(starts: [2, 1], in: mixed) == [SequenceGeometry.SpanFrame(x: 102, width: 48), SequenceGeometry.SpanFrame(x: 150, width: 0)],
       "an unsorted start is empty")
expect(SequenceGeometry.sectionSpans(starts: [0], in: empty) == [SequenceGeometry.SpanFrame(x: 0, width: 0)], "a section on no blocks is empty")
expect(SequenceGeometry.boundaryIndex(nearestTo: 0, in: mixed) == 0 && SequenceGeometry.boundaryIndex(nearestTo: 40, in: mixed) == 0,
       "x 0 and 40 snap to block 0 (starts 0, 96, 102)")
expect(SequenceGeometry.boundaryIndex(nearestTo: 97, in: mixed) == 1 && SequenceGeometry.boundaryIndex(nearestTo: 99, in: mixed) == 1,
       "97 and 99 (a tie) snap to block 1, the lower index")
expect(SequenceGeometry.boundaryIndex(nearestTo: 100, in: mixed) == 2 && SequenceGeometry.boundaryIndex(nearestTo: 1000, in: mixed) == 2,
       "100 and far right snap to block 2")
expect(SequenceGeometry.boundaryIndex(nearestTo: -50, in: mixed) == 0, "far left snaps to block 0")
expect(SequenceGeometry.boundaryIndex(nearestTo: 10, in: empty) == 0, "no blocks: 0")

// --- section names: the engine's 64-byte bound at a character boundary ---
expect(HarmonyText.boundedSectionName("Verse") == "Verse", "a short name is untouched")
expect(HarmonyText.boundedSectionName(String(repeating: "a", count: 64)) == String(repeating: "a", count: 64), "64 ASCII bytes pass")
expect(HarmonyText.boundedSectionName(String(repeating: "a", count: 65)) == String(repeating: "a", count: 64), "65 cut to 64")
let twoByte = String(repeating: "é", count: 33)   // 66 bytes
expect(HarmonyText.boundedSectionName(twoByte) == String(repeating: "é", count: 32), "33 two-byte characters cut to 32 (64 bytes)")
expect(HarmonyText.boundedSectionName(String(repeating: "a", count: 63) + "é") == String(repeating: "a", count: 63),
       "a character straddling the bound is dropped whole")
expect(HarmonyText.boundedSectionName("") == "", "empty stays empty")

// --- HarmonyText: function words, tension, "why" lines ---
expect(HarmonyText.functionWord("T") == "Tonic" && HarmonyText.functionWord("PD") == "Predominant" &&
       HarmonyText.functionWord("D") == "Dominant" && HarmonyText.functionWord("O") == "Other", "function words")
expect(HarmonyText.functionWord("X") == "Other" && HarmonyText.functionWord(nil) == nil, "unknown code is Other, nil is nil")
expect(HarmonyText.tensionFraction(42) == 0.42 && HarmonyText.tensionFraction(0) == 0 && HarmonyText.tensionFraction(100) == 1,
       "tension fraction")
expect(HarmonyText.tensionFraction(-5) == 0 && HarmonyText.tensionFraction(250) == 1 && HarmonyText.tensionFraction(nil) == 0,
       "tension fraction clamps; nil is 0")
let why = WhyJSON(commonTones: 2, commonPcs: [0, 4], commonNames: ["C", "E"], rootMotion: "down a fifth",
                  voiceLeadingCost: 4, borrowedFrom: nil, borrowedFromName: nil,
                  text: "G7 → C: down a fifth, resolves (D→T), 2 common tones (C, E), smooth voice leading (cost 4)")
expect(HarmonyText.whyHeadline(why) == why.text, "headline is the engine's sentence")
expect(HarmonyText.whyHeadline(nil) == "first chord", "the first block says \"first chord\"")
expect(HarmonyText.whyChips(why) == ["common C", "common E", "down a fifth", "cost 4"], "chips: common tones, motion, cost")
let borrowed = WhyJSON(commonTones: 0, commonPcs: [], commonNames: [], rootMotion: "up a third", voiceLeadingCost: 6,
                       borrowedFrom: KeyJSON(tonic: TonicJSON(letter: "C", alter: 0), mode: "Aeolian"),
                       borrowedFromName: "C Aeolian", text: "C → Eb: up a third, borrowed from C Aeolian")
expect(HarmonyText.whyChips(borrowed) == ["up a third", "cost 6", "borrowed from C Aeolian"], "chips: borrowed-from last, no common tones")
expect(HarmonyText.whyChips(nil).isEmpty, "no chips for the first block")
expect(HarmonyText.voicingText([60, 64, 67]) == "C4  E4  G4" && HarmonyText.voicingText([59, 62, 67, 70]) == "B3  D4  G4  A♯4",
       "voicing text, C4 = 60")
expect(HarmonyText.reasonsLine(["resolves (D→T)", "1 common tone (G)"]) == "resolves (D→T) · 1 common tone (G)", "reasons joined")

// --- Snapshot decoding: the Phase 2 fields are optional, so a Phase 1 snapshot still decodes ---
let phase1 = """
{"schema":1,"projectKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},"browsedKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},
 "projectKeyName":"C Ionian","browsedKeyName":"C Ionian","progressionLoop":true,"followHost":true,"endBeat":4.0,
 "events":[{"id":1,"root":0,"type":"maj","symbol":"C","roman":"I","startBeat":0.0,"lengthBeats":4.0,"velocity":100,"voicing":[60,64,67]}],
 "palette":{"tiers":1,"cells":[{"degree":0,"tier":0,"root":0,"type":"5","symbol":"C5","roman":"I","family":"Other","filled":true,"voicing":[48,55]}]}}
"""
let decoded1 = try? JSONDecoder().decode(Snapshot.self, from: Data(phase1.utf8))
expect(decoded1 != nil, "a Phase 1 snapshot decodes")
expect(decoded1?.events[0].function == nil && decoded1?.events[0].why == nil && decoded1?.suggestions == nil &&
       decoded1?.sections == nil && decoded1?.selectedEventId == nil && decoded1?.autoVoiceLead == nil, "its Phase 2 fields are nil")
let phase2 = """
{"schema":1,"projectKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},"browsedKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},
 "projectKeyName":"C Ionian","browsedKeyName":"C Ionian","progressionLoop":true,"followHost":true,"autoVoiceLead":true,"endBeat":8.0,
 "selectedEventId":null,
 "events":[{"id":1,"root":7,"type":"7","symbol":"G7","roman":"V7","family":"Major","function":"D","tension":30,"startBeat":0.0,"lengthBeats":4.0,
            "velocity":100,"inversion":0,"octave":0,"manualVoicing":false,"voicing":[55,59,62,65],"outOfKey":[],"why":null},
           {"id":2,"root":0,"type":"maj","symbol":"C","roman":"I","family":"Major","function":"T","tension":3,"startBeat":4.0,"lengthBeats":4.0,
            "velocity":100,"inversion":1,"octave":0,"manualVoicing":true,"voicing":[64,67,72],"outOfKey":[],
            "why":{"commonTones":1,"commonPcs":[7],"commonNames":["G"],"rootMotion":"down a fifth","voiceLeadingCost":4,
                   "borrowedFrom":null,"borrowedFromName":null,"text":"G7 → C: down a fifth"}}],
 "sections":[{"id":1,"name":"Verse","colourIndex":2,"firstEventIndex":0}],
 "suggestions":[{"root":5,"type":"maj","symbol":"F","roman":"IV","function":"PD","score":2.35,"reasons":["commonTones"],"reasonTexts":["1 common tone (C)"]}],
 "palette":{"tiers":1,"cells":[{"degree":0,"tier":0,"root":0,"type":"5","symbol":"C5","roman":"I","family":"Other","function":"T","tension":0,"filled":true,"voicing":[48,55],"outOfKey":[]}]}}
"""
let decoded2 = try? JSONDecoder().decode(Snapshot.self, from: Data(phase2.utf8))
expect(decoded2 != nil, "a Phase 2 snapshot decodes")
expect(decoded2?.selectedEventId == nil && decoded2?.autoVoiceLead == true, "null selection reads as nil; autoVoiceLead true")
expect(decoded2?.events[0].why == nil && decoded2?.events[1].why?.commonNames == ["G"], "why: null on the first event, data on the second")
expect(decoded2?.events[1].manualVoicing == true && decoded2?.events[1].inversion == 1, "voicing fields")
expect(decoded2?.sections?.first?.name == "Verse" && decoded2?.sections?.first?.colourIndex == 2, "sections")
expect(decoded2?.suggestions?.first?.symbol == "F" && decoded2?.suggestions?.first?.reasonTexts == ["1 common tone (C)"], "suggestions")
expect(decoded2?.palette.cells[0].function == "T" && decoded2?.palette.cells[0].tension == 0, "cell function and tension")

print(failures == 0 ? "ALL OK" : "\(failures) FAILED")
exit(failures == 0 ? 0 : 1)
