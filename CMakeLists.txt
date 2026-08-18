cmake_minimum_required(VERSION 3.20)
project(CodeMiner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ohne Angabe wird mit Optimierung gebaut. Unter Windows entscheidet Visual
# Studio das pro Konfiguration, unter Linux gibt es nur diese eine Einstellung -
# und ein Debug-Build waere dort eine unangenehme Ueberraschung.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# Oberflaeche: verschiebbare Fenster im Fenster, Knoepfe
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE)

# Code-Editor: C++-Syntax-Highlighting, Zeilennummern, Zeilenmarkierung
FetchContent_Declare(coloredit
    GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
    GIT_TAG        master)

FetchContent_MakeAvailable(imgui coloredit)

# ---- das Spiel selbst: auf beiden Systemen dasselbe ------------------------
set(GAME_SOURCES
    src/main.cpp
    src/console.cpp
    src/world.cpp
    src/market.cpp
    src/round.cpp
    src/quest.cpp
    src/skills.cpp
    src/wiki.cpp
    src/skilltree.cpp
    src/skillfile.cpp
    src/save.cpp
    src/ore.cpp
    src/oregen.cpp
    src/craft.cpp
    src/alloy.cpp
    src/json.cpp
    src/codecheck.cpp
    src/native.cpp
    src/instrument.cpp
    src/format.cpp
    src/datapath.cpp
    ${coloredit_SOURCE_DIR}/TextEditor.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)

# ---- und die Teile, die am System haengen ---------------------------------
if(WIN32)
    # Fenster + Grafik + Prozesse ueber Windows selbst - nichts zu installieren.
    set(PLATFORM_SOURCES
        src/platform_win32.cpp
        src/proc_win.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)

    # WIN32: startet ohne Konsolenfenster dahinter.
    add_executable(CodeMiner WIN32 ${GAME_SOURCES} ${PLATFORM_SOURCES})

    # d3d11/dxgi sind Teil von Windows - keine externen Bibliotheken noetig.
    target_link_libraries(CodeMiner PRIVATE d3d11 dxgi)

    # Das Spiel hat ein ganz normales main(), soll aber ohne Konsolenfenster
    # starten. Ohne diese Zeile sucht der Linker im Windows-Subsystem ein
    # WinMain und findet keines.
    if(MSVC)
        target_link_options(CodeMiner PRIVATE /ENTRY:mainCRTStartup)
    endif()
else()
    # Linux: GLFW fuers Fenster, OpenGL fuers Malen. Beides sind Pakete -
    # libglfw3-dev und libgl1-mesa-dev (bzw. wie sie bei der Distribution
    # heissen).
    find_package(OpenGL REQUIRED)

    find_package(glfw3 QUIET)
    if(NOT glfw3_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW REQUIRED glfw3)
    endif()

    set(PLATFORM_SOURCES
        src/platform_glfw.cpp
        src/proc_posix.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

    add_executable(CodeMiner ${GAME_SOURCES} ${PLATFORM_SOURCES})

    if(glfw3_FOUND)
        target_link_libraries(CodeMiner PRIVATE glfw)
    else()
        target_include_directories(CodeMiner PRIVATE ${GLFW_INCLUDE_DIRS})
        target_link_libraries(CodeMiner PRIVATE ${GLFW_LIBRARIES})
        target_link_directories(CodeMiner PRIVATE ${GLFW_LIBRARY_DIRS})
    endif()

    target_link_libraries(CodeMiner PRIVATE OpenGL::GL ${CMAKE_DL_LIBS})

    # Der Spielstand und das Uebersetzen laufen ueber Threads.
    find_package(Threads REQUIRED)
    target_link_libraries(CodeMiner PRIVATE Threads::Threads)
endif()

target_include_directories(CodeMiner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${coloredit_SOURCE_DIR})

if(MSVC)
    # /utf-8: Quelltext ist UTF-8 (wegen Umlauten in den Ausgabetexten)
    target_compile_options(CodeMiner PRIVATE /utf-8 /W3)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                 PROPERTY VS_STARTUP_PROJECT CodeMiner)
else()
    target_compile_options(CodeMiner PRIVATE -Wall)

    # TextEditor.h (Fremdcode) benutzt uint8_t, ohne <cstdint> einzubinden.
    # MSVC zieht den Header nebenbei mit herein, gcc seit Version 13 nicht mehr -
    # deshalb hier davorgehaengt, statt fremden Quelltext zu aendern.
    target_compile_options(CodeMiner PRIVATE -include cstdint)
endif()
