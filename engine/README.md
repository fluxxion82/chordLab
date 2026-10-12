# chordlab engine

The plugin's theory, document and playback core: a static library with no UI and no host dependency,
exercised by a doctest suite.

## Layout

```
include/chordlab/   public headers, one per module (pitch, scale, chord, symbol, function, relations, voicing,
                    palette, document, compiler, note_event, limits, scheduler, triple_buffer,
                    publication, smf, json_api)
src/                the matching implementations
tests/              doctest suite (tests/doctest.h is the vendored framework), one file per module;
                    tests/rt_alloc/ is a second executable that counts allocations (see below)
fixtures/           golden files the tests compare against (cmaj-1bar.mid)
third_party/        vendored headers; see third_party/README.md for provenance and hashes
build/              CMake output, ignored by git
```

## Build and test

```
cmake -S engine -B engine/build && cmake --build engine/build && ctest --test-dir engine/build
```

That runs two executables, `engine_tests` (the suite) and `engine_rt_alloc_test` (below); either can be run
directly, e.g. `timeout 300 engine/build/engine_tests`. C++17, `-Wall -Wextra -Wpedantic`, zero warnings
expected. Both must report `0 failed`.

## Error handling

Three mechanisms, chosen by who can get it wrong:

- **Status returns for expected failures.** Input from outside the process, or a resource that may be
  missing, is reported, never thrown: `applyCommandJson` / `loadStateJson` return false with a message and
  leave the store untouched; `appendChord` returns `kNoEventId` when the document is full or the ids
  are exhausted; `writeSmf0` returns an empty vector and `writeFile` false; `pushPreview` returns false
  on a full queue and `process` returns false for a block it could not complete.
- **Assertions for programmer preconditions.** A `Chord` with a null type, a `Key` with a bad letter, a
  `Mode` outside the enum, an unsorted `Snapshot`: these are contract violations, checked with `assert`
  in debug builds and documented on each function. They are not validated at runtime in release.
- **Exceptions only in allocating, non-real-time work.** `chordType(id)` throws `std::out_of_range`
  for an unknown id; the JSON readers are `noexcept`, catch everything (including allocation failure)
  and report `"internal error"` with the store unchanged; `DocumentStore` mutations give the strong
  exception guarantee (see the class comment in `document.h`). Nothing on the audio-thread path throws.

## Real-time rules

What may run on the audio thread is exactly the `Scheduler` surface: `process`, `reset`, `adopt`,
`panic`, `ownersOf`, `sounding`, plus the consumer side of `TripleBuffer` (`acquire`, `current`,
`currentGeneration`). After construction neither allocates, throws, locks or logs; the scheduler's only
headers are `limits.h` and `note_event.h` plus `<array>`, `<atomic>`, `<cstddef>`, `<cstdint>`, and
`triple_buffer.h` adds `<type_traits>`, so a translation unit that includes them pulls in nothing that
allocates. `engine_rt_alloc_test` checks it from the outside: a separate process whose global
`operator new`/`delete` are replaced by counting versions runs the scheduler through ten thousand random
blocks (starts, stops, seeks, tempo changes, previews, panics, resets, adopts) and both sides of the triple
buffer, and asserts zero intercepted C++ allocations on the exercised paths. It cannot see a direct
`malloc` or a path the random walk never reaches, so it is evidence, not a proof.

Everything else (`DocumentStore`, `compile`, the palette and symbols, `json_api`, `smf`) is ordinary
allocating code for the UI/message thread. The hand-over is a `Snapshot`: `compile` produces the
`NoteEvent` array off the audio thread, and the caller keeps it alive and unchanged from `adopt` until
the next `adopt`, which never reads it (see `Snapshot` in `scheduler.h`). `kMaxChords` (`document.h`) bounds the document so
that a compiled snapshot always fits `kMaxSnapshotEvents` (`limits.h`). `TripleBuffer<T>`
(`triple_buffer.h`) is the handoff that satisfies that lifetime rule without a lock: the producer fills
and publishes preallocated slots, the consumer acquires the latest one per block, and the slot it holds
is never written until its next acquire. `publication.h` is the producer's half as the plug-in uses it:
`SnapshotSlot` (the compiled content plus `followHost`, the plug-in's transport gate, which travels with
the progression it belongs to) and `fillSlot`, which compiles, compares with the last publication and
fills a slot only when something changed, bumping the generation for content and keeping it for a
follow-only change.

## Threading contract

- `scheduler.h`, "Real time" paragraph: which calls belong to the audio thread, that `pushPreview` may
  come from one other thread (SPSC queue), and when `reset` / `adopt` may be called from elsewhere.
- `document.h`, `DocumentStore` class comment: single-threaded, strong exception guarantee, what is
  undoable and what is live state.
- `json_api.h`, "Robustness of the two readers": transactional parsing, the size and depth guards, and
  that additive snapshot fields do not bump the schema.
- `triple_buffer.h`, class comment: one producer thread and one consumer thread, which slot each owns,
  coalescing, and that an acquired slot stays unmodified until the consumer's next acquire.
