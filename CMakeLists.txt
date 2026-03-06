cmake_minimum_required(VERSION 3.16)
project(ds-sd-setup VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find raylib on system, or fetch it automatically
find_package(raylib QUIET)
if(NOT raylib_FOUND)
    include(FetchContent)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raylib)
endif()

# Find libcurl
find_package(CURL REQUIRED)

# Find libarchive (handles .7z extraction)
find_package(LibArchive QUIET)
if(NOT LibArchive_FOUND AND APPLE)
    # macOS: brew's libarchive is keg-only, help CMake find it
    execute_process(COMMAND brew --prefix libarchive
        OUTPUT_VARIABLE _brew_la OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_brew_la)
        list(APPEND CMAKE_PREFIX_PATH "${_brew_la}")
        find_package(LibArchive REQUIRED)
    endif()
endif()
if(NOT LibArchive_FOUND)
    find_package(LibArchive REQUIRED)
endif()

add_executable(ds-sd-setup src/main.c src/setup.c src/platform.c)
target_include_directories(ds-sd-setup PRIVATE src)
target_link_libraries(ds-sd-setup raylib CURL::libcurl LibArchive::LibArchive)

if(APPLE)
    target_link_libraries(ds-sd-setup
        "-framework Cocoa"
        "-framework IOKit"
        "-framework OpenGL"
        "-framework CoreVideo"
    )
elseif(WIN32)
    target_link_libraries(ds-sd-setup ole32 shell32 winmm gdi32 opengl32)
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(ds-sd-setup Threads::Threads m dl)
endif()
