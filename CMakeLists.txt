cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(Superior LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static runtime (/MT): no Visual C++ redistributable needed on the target PC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

include(FetchContent)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

# ---- core: pure C++ logic, testable on CI without a GPU --------------------
add_library(superior_core STATIC src/core/log.cpp src/core/config.cpp)
target_include_directories(superior_core PUBLIC src)
target_link_libraries(superior_core PUBLIC nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(superior_core PRIVATE /W4 /utf-8)
endif()

# ---- tests -------------------------------------------------------------------
enable_testing()
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
FetchContent_MakeAvailable(googletest)
add_executable(superior_tests tests/test_core.cpp)
target_link_libraries(superior_tests PRIVATE superior_core GTest::gtest_main)
add_test(NAME core COMMAND superior_tests)

# ---- the application (Windows only) -----------------------------------------
if(WIN32)
  # Embed the HLSL files in the exe so Superior.exe is a single file.
  set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
  file(READ ${CMAKE_SOURCE_DIR}/shaders/flow.hlsl FLOW_SRC)
  file(READ ${CMAKE_SOURCE_DIR}/shaders/render.hlsl RENDER_SRC)
  file(WRITE ${GEN_DIR}/shaders_gen.h
    "#pragma once\nstatic const char* const kFlowHlsl = R\"HLSL(${FLOW_SRC})HLSL\";\nstatic const char* const kRenderHlsl = R\"HLSL(${RENDER_SRC})HLSL\";\n")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/shaders/flow.hlsl ${CMAKE_SOURCE_DIR}/shaders/render.hlsl)

  add_executable(Superior WIN32 src/app/main.cpp src/app/capture.cpp src/app/renderer.cpp)
  set_target_properties(Superior PROPERTIES OUTPUT_NAME "Superior")
  target_include_directories(Superior PRIVATE src ${GEN_DIR})
  target_compile_definitions(Superior PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
                                              WINVER=0x0A00 _WIN32_WINNT=0x0A00 UNICODE _UNICODE)
  target_compile_options(Superior PRIVATE /W4 /utf-8 /permissive- /EHsc)
  target_link_libraries(Superior PRIVATE superior_core d3d11 dxgi d3dcompiler windowsapp shell32 user32)
endif()
