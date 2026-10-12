# Third-party code

Header-only libraries vendored verbatim; nothing here is patched. To update one, replace the file and
refresh its hash below (`shasum -a 256 <file>`).

| Library | Version | License | File | Origin |
|---|---|---|---|---|
| nlohmann/json | 3.11.2 | MIT | `third_party/nlohmann/json.hpp` | Copied byte for byte from iPlug2's `Dependencies/Extras/nlohmann/json.hpp` (the single-include header of the upstream v3.11.2 release, <https://github.com/nlohmann/json>). The hash below matches that copy. |
| doctest | 2.4.11 | MIT | `tests/doctest.h` | The single header of the upstream v2.4.11 release, <https://github.com/doctest/doctest>. Kept next to the tests, not here, because only the test executable uses it. |

SHA-256 of the files as committed:

```
665fa14b8af3837966949e8eb0052d583e2ac105d3438baba9951785512cf921  third_party/nlohmann/json.hpp
44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb  tests/doctest.h
```

Visibility: `third_party` is a PRIVATE include directory of `chordlab_engine`. Only `src/json_api.cpp`
includes nlohmann, and no header under `include/chordlab/` does, so consumers of the library never see
it. `engine_tests` adds `third_party` on its own because `tests/test_json_api.cpp` parses the JSON it
checks.
