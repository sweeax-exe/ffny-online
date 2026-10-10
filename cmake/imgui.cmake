# Dear ImGui, fetched and built statically for the launcher and the in-game overlay.
#
#   include(imgui)   # defines the target defjam_imgui
#
# The release is pinned by URL and hash and is MIT licensed (thirdparty/imgui-LICENSE.txt). Only the
# core and the backends the host needs are built: Win32 and Direct3D 11 on Windows; elsewhere the
# SDL renderer (the launcher's own window) and Vulkan (the overlay drawn in the game's window), with
# the SDL events fed in by src/hooks/pc_ui.cpp. The Win32 backend's own XInput polling is switched
# off: the host input layer feeds the menu from every pad (SDL or XInput) instead. Elsewhere SDL3
# and Vulkan must have been found (find_package) before this is included.
include_guard(GLOBAL)
include(FetchContent)

set(DEFJAM_IMGUI_VERSION "1.92.9b")
set(DEFJAM_IMGUI_SHA256 "21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99")

FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v${DEFJAM_IMGUI_VERSION}.tar.gz
    URL_HASH SHA256=${DEFJAM_IMGUI_SHA256})
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

set(DEFJAM_IMGUI_CORE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)

if(WIN32)
    add_library(defjam_imgui STATIC
        ${DEFJAM_IMGUI_CORE}
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
    target_include_directories(defjam_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(defjam_imgui PUBLIC IMGUI_IMPL_WIN32_DISABLE_GAMEPAD IMGUI_DISABLE_DEBUG_TOOLS)
    target_link_libraries(defjam_imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)
else()
    add_library(defjam_imgui STATIC
        ${DEFJAM_IMGUI_CORE}
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
    target_include_directories(defjam_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(defjam_imgui PUBLIC IMGUI_DISABLE_DEBUG_TOOLS)
    target_link_libraries(defjam_imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)
endif()
