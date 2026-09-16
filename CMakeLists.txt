cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(ShowcaseBot VERSION 0.1.0)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK not set. Install the Geode SDK: https://docs.geode-sdk.org")
else()
    message(STATUS "Found Geode SDK: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# ImGui-in-Cocos backend, vendored under external/imgui-cocos (a patched copy of
# matcool/gd-imgui-cocos, see external/imgui-cocos/README.vendor.md). Upstream
# main no longer compiles against current Geode bindings, so we build it locally.
set(IMGUI_COCOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui-cocos)

CPMAddPackage("gh:ocornut/imgui#v1.88")

add_library(imgui-cocos INTERFACE)
target_include_directories(imgui-cocos INTERFACE ${imgui_SOURCE_DIR} ${IMGUI_COCOS_DIR}/include)
target_sources(imgui-cocos INTERFACE
    ${IMGUI_COCOS_DIR}/src/main.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
if (WIN32)
    target_link_libraries(imgui-cocos INTERFACE opengl32 ${IMGUI_COCOS_DIR}/glew32.lib)
elseif (APPLE)
    target_link_libraries(imgui-cocos INTERFACE "-framework OpenGL")
elseif (ANDROID)
    target_link_libraries(imgui-cocos INTERFACE GLESv2)
endif()

target_link_libraries(${PROJECT_NAME} imgui-cocos)

setup_geode_mod(${PROJECT_NAME})
