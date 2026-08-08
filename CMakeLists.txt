cmake_minimum_required(VERSION 3.20)
project(CodeKlicker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_executable(CodeKlicker WIN32
    src/main.cpp
    src/console.cpp
    src/world.cpp
    src/round.cpp
    src/skills.cpp
    src/skilltree.cpp
    src/skillfile.cpp
    src/save.cpp
    src/ore.cpp
    src/craft.cpp
    src/alloy.cpp
    src/json.cpp
    src/codecheck.cpp
    src/native.cpp
    src/instrument.cpp
    src/format.cpp
    src/proc.cpp
    ${coloredit_SOURCE_DIR}/TextEditor.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)

target_include_directories(CodeKlicker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${coloredit_SOURCE_DIR})

# d3d11/dxgi sind Teil von Windows - keine externen Bibliotheken noetig.
target_link_libraries(CodeKlicker PRIVATE d3d11 dxgi)

if(MSVC)
    # /utf-8: Quelltext ist UTF-8 (wegen Umlauten in den Ausgabetexten)
    target_compile_options(CodeKlicker PRIVATE /utf-8 /W3)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                 PROPERTY VS_STARTUP_PROJECT CodeKlicker)
endif()
