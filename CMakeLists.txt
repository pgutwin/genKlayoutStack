cmake_minimum_required(VERSION 3.20)
project(genKlayoutStack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# tl::expected — backport of std::expected for C++20
FetchContent_Declare(
  tl_expected
  GIT_REPOSITORY https://github.com/TartanLlama/expected.git
  GIT_TAG        v1.1.0
)
set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tl_expected)

# toml++
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

# pugixml
FetchContent_Declare(
  pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG        v1.14
)
FetchContent_MakeAvailable(pugixml)

# GoogleTest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# gks core library
add_library(gks_lib
  src/core/LayerStack.cpp
  src/core/buildStack.cpp
  src/core/Validator.cpp
  src/core/Defaulter.cpp
  src/io/TomlReader.cpp
  src/io/TomlWriter.cpp
  src/io/LypReader.cpp
  src/io/LypWriter.cpp
  src/io/ScriptReader.cpp
  src/io/ScriptWriter.cpp
)
target_include_directories(gks_lib PUBLIC include ${tl_expected_SOURCE_DIR}/include)
target_link_libraries(gks_lib PUBLIC tomlplusplus::tomlplusplus pugixml::pugixml)
target_compile_features(gks_lib PUBLIC cxx_std_20)

# gks executable
add_executable(gks
  src/cli/main.cpp
  src/cli/cmd_import.cpp
  src/cli/cmd_generate.cpp
  src/cli/cmd_validate.cpp
)
target_link_libraries(gks PRIVATE gks_lib)

# Tests
enable_testing()
include(GoogleTest)

# Unit tests
add_executable(unit_tests
  tests/unit/test_smoke.cpp
  tests/unit/test_buildStack.cpp
  tests/unit/test_validator.cpp
  tests/unit/test_defaulter.cpp
  tests/unit/test_tomlReader.cpp
  tests/unit/test_lypReader.cpp
  tests/unit/test_scriptReader.cpp
  tests/unit/test_scriptWriter.cpp
)
target_link_libraries(unit_tests PRIVATE gks_lib GTest::gtest_main)
gtest_discover_tests(unit_tests)

# Integration tests
add_executable(integration_tests
  tests/integration/test_roundtrip_toml_lyp.cpp
  tests/integration/test_roundtrip_toml_script.cpp
  tests/integration/test_bootstrap_import.cpp
  tests/integration/test_negative_z.cpp
  tests/integration/test_parallel_groups.cpp
)
target_link_libraries(integration_tests PRIVATE gks_lib GTest::gtest_main)
gtest_discover_tests(integration_tests)

# Pass fixtures path to tests
target_compile_definitions(unit_tests PRIVATE
  GKS_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures"
)
target_compile_definitions(integration_tests PRIVATE
  GKS_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures"
)
