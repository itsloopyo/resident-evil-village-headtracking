cmake_minimum_required(VERSION 3.20)
project(RE8HeadTracking VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# cameraunlock-core shared library (static) with REFramework utils.
# No hook manager: this plugin detours nothing natively - every hook it
# installs goes through REFramework's own plugin API - so CAMERAUNLOCK_BUILD_HOOKS
# would only pull MinHook off GitHub at configure time and break a firewalled
# build for a library nothing links against.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_REFRAMEWORK ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern" CACHE STRING "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# Plugin sources
add_library(RE8HeadTracking SHARED
    src/pch.cpp
    src/plugin_main.cpp
    src/camera/gui_compensation.cpp
    src/camera/gui_diagnostics.cpp
    src/camera/game_state_detector.cpp
)

target_precompile_headers(RE8HeadTracking PRIVATE src/pch.h)

set_target_properties(RE8HeadTracking PROPERTIES
    OUTPUT_NAME "RE8HeadTracking"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_link_libraries(RE8HeadTracking PRIVATE
    cameraunlock cameraunlock_reframework ws2_32)

target_include_directories(RE8HeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern
)

# Opt-in tests for the config: the committed HeadTracking.ini against the canonical table, the
# toggles' saves, and the differential test that holds the import of an old HeadTracking.ini to
# the published build that read it. OFF by default so neither the normal build nor the release
# packager ever drops a test binary into bin/. Enable with -DRE8HT_BUILD_TESTS=ON, or run
# 'pixi run test'.
option(RE8HT_BUILD_TESTS "Build RE8HeadTracking tests" OFF)
if(RE8HT_BUILD_TESTS)
    enable_testing()

    # Every test is named for its target and hangs off this aggregate, so run-tests.ps1 builds
    # them all with one --target and selects them all with one ctest filter.
    add_custom_target(re8ht_tests)

    function(re8ht_add_test name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(${name} PRIVATE cameraunlock cameraunlock_reframework)
        set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        if(MSVC)
            target_compile_options(${name} PRIVATE /W4 /WX /permissive-)
        endif()
        add_test(NAME ${name} COMMAND ${name})
        add_dependencies(re8ht_tests ${name})
    endfunction()

    # The committed HeadTracking.ini against the table, and the toggles' saves.
    # `--render-config <path>` writes the committed file instead (pixi run render-config).
    re8ht_add_test(re8ht_config_tests tests/config_tests.cpp)
    target_compile_definitions(re8ht_config_tests PRIVATE
        RE8HT_COMMITTED_CONFIG=L"${CMAKE_CURRENT_SOURCE_DIR}/HeadTracking.ini")

    # The config differential test. The oracle is the reader of the dev pre-release at 56c327d,
    # the newest published build, copied byte for byte into tests/config_differential/oracle;
    # renaming its namespace lets it link beside core's. oracle_adapter.cpp copies its Config out
    # for the test.
    set(RE8HT_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle)
    add_library(re8ht_config_oracle STATIC
        ${RE8HT_ORACLE_DIR}/src/core/config.cpp
        ${RE8HT_ORACLE_DIR}/src/core/logger.cpp
        tests/config_differential/oracle_adapter.cpp
    )
    target_include_directories(re8ht_config_oracle PRIVATE
        ${RE8HT_ORACLE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(re8ht_config_oracle PRIVATE RE8HT=re8ht_oracle)
    target_link_libraries(re8ht_config_oracle PRIVATE cameraunlock)

    re8ht_add_test(re8ht_config_differential tests/config_differential/differential_tests.cpp)
    target_include_directories(re8ht_config_differential PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(re8ht_config_differential PRIVATE
        RE8HT_DIFFERENTIAL_INPUTS="${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/inputs"
        RE8HT_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/HeadTracking.ini")
    target_link_libraries(re8ht_config_differential PRIVATE re8ht_config_oracle)

    # Core's canonical config lint over the committed file and every file the differential test
    # migrated, which it writes beside itself under migrated\.
    add_test(NAME re8ht_config_lint COMMAND node
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/lint-migrated.mjs
        $<TARGET_FILE_DIR:re8ht_config_differential>/migrated)
    set_tests_properties(re8ht_config_lint PROPERTIES DEPENDS re8ht_config_differential)

    # The oracle, the inputs and the core sources the oracle compiles, against the hashes
    # tests/config_differential/frozen.tsv records.
    add_test(NAME re8ht_config_frozen COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/check-frozen.ps1)
endif()
