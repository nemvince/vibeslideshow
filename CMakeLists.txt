cmake_minimum_required(VERSION 3.21)

project(vibeslideshow
    VERSION 1.0.0
    DESCRIPTION "Beat-synced slideshow editor for your own footage"
    HOMEPAGE_URL "https://github.com/nemvince/vibeslideshow"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Gui)
qt_standard_project_setup(REQUIRES 6.5)

# libmpv: pkg-config where its paths are usable (Linux, Homebrew), direct lookup
# otherwise. MSYS2's mpv.pc emits POSIX prefixes ("-I/ucrt64/include") that a
# native Windows CMake cannot resolve, so a result with non-existent include
# directories is treated as no result at all.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MPV QUIET IMPORTED_TARGET mpv)
    if(MPV_FOUND)
        foreach(_mpv_dir IN LISTS MPV_INCLUDE_DIRS)
            if(NOT EXISTS "${_mpv_dir}")
                message(STATUS "libmpv: pkg-config gave unusable path '${_mpv_dir}', using find_path/find_library")
                unset(MPV_FOUND)
                break()
            endif()
        endforeach()
    endif()
endif()

if(NOT MPV_FOUND)
    # MSYS2/MinGW is the case this exists for: hint the real installation root.
    set(_mpv_hints "${MPV_ROOT}" "${CMAKE_PREFIX_PATH}" "$ENV{MINGW_PREFIX}")
    find_path(MPV_INCLUDE_DIR mpv/client.h
              HINTS ${_mpv_hints} PATH_SUFFIXES include)
    find_library(MPV_LIBRARY NAMES mpv mpv-2 libmpv
                 HINTS ${_mpv_hints} PATH_SUFFIXES lib bin)
    if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
        message(FATAL_ERROR "libmpv not found. Install it (libmpv-dev, mpv-libs, brew install mpv), "
                            "or configure with -DMPV_ROOT=/path/to/libmpv.")
    endif()
    message(STATUS "libmpv: ${MPV_LIBRARY} (headers ${MPV_INCLUDE_DIR})")
endif()


qt_add_executable(vibeslideshow
    src/main.cpp
    src/core/Types.h
    src/core/Params.h
    src/core/Params.cpp
    src/core/Analyzer.h
    src/core/Analyzer.cpp
    src/core/Scheduler.h
    src/core/Scheduler.cpp
    src/core/Library.h
    src/core/Library.cpp
    src/player/MpvItem.h
    src/player/MpvItem.cpp
    src/player/Show.h
    src/player/Show.cpp
    src/ui/ClipModel.h
    src/ui/ClipModel.cpp
    src/ui/TimelineModel.h
    src/ui/TimelineModel.cpp
    src/ui/Studio.h
    src/ui/Studio.cpp
    qml.qrc
)

target_compile_definitions(vibeslideshow PRIVATE
    VIBESLIDESHOW_VERSION="${PROJECT_VERSION}"
)

target_include_directories(vibeslideshow PRIVATE src)

target_link_libraries(vibeslideshow PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Gui
)

if(TARGET PkgConfig::MPV)
    target_link_libraries(vibeslideshow PRIVATE PkgConfig::MPV)
else()
    target_include_directories(vibeslideshow PRIVATE ${MPV_INCLUDE_DIR})
    target_link_libraries(vibeslideshow PRIVATE ${MPV_LIBRARY})
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # The param/preset tables are aggregate initialisers that intentionally omit
    # the trailing `unit` field.
    target_compile_options(vibeslideshow PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# macOS: a real .app bundle, so the window gets a dock icon and a menu bar.
set_target_properties(vibeslideshow PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "vibeslideshow"
    MACOSX_BUNDLE_GUI_IDENTIFIER "garden.stanislav.vibeslideshow"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

# Linux/BSD: .desktop entry and icon alongside the binary.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS vibeslideshow RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/vibeslideshow.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/vibeslideshow.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
