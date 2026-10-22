cmake_minimum_required(VERSION 3.20)
project(chordlab_engine CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
file(GLOB ENGINE_SRC CONFIGURE_DEPENDS src/*.cpp)
add_library(chordlab_engine STATIC ${ENGINE_SRC})
target_include_directories(chordlab_engine PUBLIC include PRIVATE third_party)
target_compile_options(chordlab_engine PRIVATE -Wall -Wextra -Wpedantic)
enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*.cpp)
add_executable(engine_tests ${TEST_SRC})
find_package(Threads REQUIRED)
target_link_libraries(engine_tests PRIVATE chordlab_engine Threads::Threads)  # test_triple_buffer.cpp spawns a producer
target_include_directories(engine_tests PRIVATE tests third_party)  # test_json_api.cpp parses with nlohmann
add_test(NAME engine_tests COMMAND engine_tests)
target_compile_definitions(engine_tests PRIVATE CHORDLAB_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
                                                 CHORDLAB_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")  # test_fixtures.cpp: golden JSON for the UI contract
# Its own process: it replaces the global operator new/delete to count allocations on the real-time surface.
add_executable(engine_rt_alloc_test tests/rt_alloc/test_rt_alloc.cpp)
target_link_libraries(engine_rt_alloc_test PRIVATE chordlab_engine)
target_include_directories(engine_rt_alloc_test PRIVATE tests)
target_compile_options(engine_rt_alloc_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME engine_rt_alloc_test COMMAND engine_rt_alloc_test)
# Not a test: the weight grid search behind src/suggest.cpp's constants (see tools/suggest_tune.cpp).
add_executable(suggest_tune tools/suggest_tune.cpp)
target_link_libraries(suggest_tune PRIVATE chordlab_engine)
target_compile_options(suggest_tune PRIVATE -Wall -Wextra -Wpedantic)
