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
add_library(superior_core STATIC src/core/log.cpp src/core/config.cpp src/core/files.cpp src/core/profiles.cpp src/core/settings_bundle.cpp src/core/diagnostics.cpp src/benchmark/quality.cpp src/present/session_stats.cpp)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE SUPERIOR_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT SUPERIOR_REVISION MATCHES "^[0-9a-fA-F]+$")
  set(SUPERIOR_REVISION "unknown")
endif()
target_compile_definitions(superior_core PRIVATE SUPERIOR_BUILD_REVISION="${SUPERIOR_REVISION}")
target_include_directories(superior_core PUBLIC src)
target_link_libraries(superior_core PUBLIC nlohmann_json::nlohmann_json)
if(WIN32)
  target_compile_definitions(superior_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
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
if(MSVC)
  target_compile_options(superior_tests PRIVATE /W4 /utf-8)
endif()

# ---- the application (Windows only) -----------------------------------------
if(WIN32)
  FetchContent_Declare(ort_c_api
    URL https://github.com/microsoft/onnxruntime/raw/refs/tags/v1.20.1/include/onnxruntime/core/session/onnxruntime_c_api.h
    URL_HASH SHA256=573a117ae6b83ead7f53da6cedaa0d7b7d9cdb45c0ba4726bac9081e67696a82
    DOWNLOAD_NO_EXTRACT TRUE)
  FetchContent_MakeAvailable(ort_c_api)
  FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.zip)
  FetchContent_MakeAvailable(imgui)
  add_library(superior_ui STATIC src/ui/panel.cpp src/ui/filter_pages.cpp src/ui/setup_page.cpp src/ui/comparison_page.cpp src/ui/diagnostics_page.cpp src/ui/localized.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
  target_include_directories(superior_ui PUBLIC src PRIVATE ${imgui_SOURCE_DIR})
  target_compile_options(superior_ui PRIVATE /utf-8)
  target_compile_definitions(superior_ui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(superior_ui PRIVATE d3d11 dxgi d3dcompiler dwmapi comdlg32)
  # Compile application shaders on Windows and embed bytecode, never compile
  # image-processing HLSL on the user's machine.
  file(GLOB FXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe")
  list(SORT FXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
  find_program(FXC_EXECUTABLE fxc HINTS ENV WindowsSdkVerBinPath PATH_SUFFIXES x64)
  if(NOT FXC_EXECUTABLE AND FXC_CANDIDATES)
    list(GET FXC_CANDIDATES 0 FXC_EXECUTABLE)
  endif()
  if(NOT FXC_EXECUTABLE)
    message(FATAL_ERROR "Windows SDK fxc.exe is required to build embedded shaders")
  endif()
  set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
  file(MAKE_DIRECTORY ${GEN_DIR})
  set(SHADER_HEADERS)
  set(SHADER_INCLUDES "#pragma once\n")
  foreach(ENTRY IN ITEMS VS PSMid PSAA PSUp PSFinal CSFlow)
    if(ENTRY STREQUAL "VS")
      set(PROFILE vs_5_0)
    elseif(ENTRY STREQUAL "CSFlow")
      set(PROFILE cs_5_0)
    else()
      set(PROFILE ps_5_0)
    endif()
    if(ENTRY STREQUAL "CSFlow")
      set(SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/shaders/flow.hlsl)
    else()
      set(SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/shaders/render.hlsl)
    endif()
    set(HEADER ${GEN_DIR}/${ENTRY}.h)
    add_custom_command(OUTPUT ${HEADER}
      COMMAND "${FXC_EXECUTABLE}" /nologo /O3 /T ${PROFILE} /E ${ENTRY}
              /Vn kShader${ENTRY} /Fh ${HEADER} ${SHADER_SOURCE}
      DEPENDS ${SHADER_SOURCE} VERBATIM)
    list(APPEND SHADER_HEADERS ${HEADER})
    string(APPEND SHADER_INCLUDES "#include \"${ENTRY}.h\"\n")
  endforeach()
  file(WRITE ${GEN_DIR}/shaders_gen.h "${SHADER_INCLUDES}")
  add_custom_target(superior_shaders DEPENDS ${SHADER_HEADERS})

  add_executable(Superior WIN32 src/app/main.cpp src/app/capture.cpp src/app/renderer.cpp src/hardware/monitor.cpp src/neural/offline.cpp)
  add_dependencies(Superior superior_shaders)
  set_target_properties(Superior PROPERTIES OUTPUT_NAME "Superior" CXX_STANDARD 20)
  target_include_directories(Superior PRIVATE src ${GEN_DIR} ${ort_c_api_SOURCE_DIR})
  target_compile_definitions(Superior PRIVATE _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
                                              WINVER=0x0A00 _WIN32_WINNT=0x0A00 UNICODE _UNICODE)
  target_compile_options(Superior PRIVATE /W4 /utf-8 /permissive- /EHsc)
  target_link_libraries(Superior PRIVATE superior_core superior_ui d3d11 dxgi d3dcompiler windowsapp shell32 user32)
endif()
