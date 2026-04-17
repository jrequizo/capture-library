cmake_minimum_required(VERSION 3.16)
project(capture_library)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CAPTURE_BUILD_PYTHON_BINDINGS "Build Python bindings module" OFF)

# Common sources
set(COMMON_SOURCES
    src/common/error.cpp
    src/common/frame_validation.cpp
    src/common/monotonic_time.cpp
    src/common/png_writer.cpp
    src/common/factory.cpp
)

# Platform-specific sources
if(WIN32)
    set(PLATFORM_SOURCES
        src/windows/monitor_enum.cpp
        src/windows/d3d11_helpers.cpp
        src/windows/wgc_interop.cpp
        src/windows/backend_windows_wgc.cpp
    )
    set(PLATFORM_LIBS d3d11 dxgi windowsapp)
elseif(APPLE)
    set(PLATFORM_SOURCES
        src/macos/backend_macos_screencapturekit.cpp
    )
    enable_language(OBJCXX)
    find_library(SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit)
    set_source_files_properties(src/macos/backend_macos_screencapturekit.cpp
        PROPERTIES LANGUAGE OBJCXX)
    set(PLATFORM_LIBS
        "-framework CoreGraphics"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework Foundation"
    )
    if(SCREENCAPTUREKIT_FRAMEWORK)
        list(APPEND PLATFORM_LIBS "-weak_framework ScreenCaptureKit")
    endif()
else()
    set(PLATFORM_SOURCES)
    set(PLATFORM_LIBS)
endif()

# Library
add_library(capture_lib
    ${COMMON_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(capture_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(capture_lib
    PRIVATE
        ${PLATFORM_LIBS}
)

if(APPLE)
    target_compile_options(capture_lib PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fblocks>
    )
endif()

# Example executable: single capture
add_executable(capture_once
    examples/capture_once.cpp
)

target_include_directories(capture_once
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(capture_once
    PRIVATE
        capture_lib
)

set_target_properties(capture_once PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Example executable: sustained capture loop
add_executable(capture_loop
    examples/capture_loop.cpp
)

target_include_directories(capture_loop
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(capture_loop
    PRIVATE
        capture_lib
)

set_target_properties(capture_loop PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Example executable: repeated lifecycle stress test
add_executable(capture_stress
    examples/capture_stress.cpp
)

target_include_directories(capture_stress
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(capture_stress
    PRIVATE
        capture_lib
)

set_target_properties(capture_stress PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Example executable: benchmarking
add_executable(benchmark_fps
    examples/benchmark_fps.cpp
)

target_include_directories(benchmark_fps
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(benchmark_fps
    PRIVATE
        capture_lib
)

set_target_properties(benchmark_fps PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Set startup project on Visual Studio
if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT capture_once)
endif()

if(CAPTURE_BUILD_PYTHON_BINDINGS)
    find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(pybind11 CONFIG QUIET)
    if(NOT pybind11_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            pybind11
            URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.tar.gz
        )
        FetchContent_MakeAvailable(pybind11)
    endif()

    pybind11_add_module(capture MODULE
        python/bindings.cpp
    )

    target_include_directories(capture
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(capture
        PRIVATE
            capture_lib
    )

    set_target_properties(capture PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
endif()
