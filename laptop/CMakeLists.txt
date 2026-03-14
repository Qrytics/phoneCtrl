cmake_minimum_required(VERSION 3.20)
project(phoneCtrl_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type default ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/capture/screen_capture.cpp
    src/encoder/video_encoder.cpp
    src/network/websocket_server.cpp
    src/input/input_handler.cpp
)

add_executable(phoneCtrl_host ${SOURCES})

target_include_directories(phoneCtrl_host PRIVATE src)

# ── Windows-specific settings ─────────────────────────────────────────────────
if(WIN32)
    # Unicode support
    target_compile_definitions(phoneCtrl_host PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    # DXGI / D3D11 (system libraries on Windows)
    target_link_libraries(phoneCtrl_host PRIVATE
        d3d11
        dxgi
        user32
    )
else()
    message(FATAL_ERROR
        "phoneCtrl_host only supports Windows (DXGI Desktop Duplication + SendInput).")
endif()

# ── FFmpeg ────────────────────────────────────────────────────────────────────
# Find via pkg-config (vcpkg, conan, or manual install all expose this)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavutil
    libswscale
)
target_link_libraries(phoneCtrl_host PRIVATE PkgConfig::FFMPEG)

# ── Boost (Beast + Asio) ──────────────────────────────────────────────────────
find_package(Boost 1.81 REQUIRED COMPONENTS system)
target_link_libraries(phoneCtrl_host PRIVATE Boost::system)
target_compile_definitions(phoneCtrl_host PRIVATE
    BOOST_BEAST_USE_STD_STRING_VIEW
    BOOST_ASIO_NO_DEPRECATED
)

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(phoneCtrl_host PRIVATE /W4 /WX /wd4100)
else()
    target_compile_options(phoneCtrl_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Installation ──────────────────────────────────────────────────────────────
install(TARGETS phoneCtrl_host RUNTIME DESTINATION bin)
