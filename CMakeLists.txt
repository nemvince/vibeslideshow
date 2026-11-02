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

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

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
    PkgConfig::MPV
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vibeslideshow PRIVATE -Wall -Wextra -Wno-unused-parameter)
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
