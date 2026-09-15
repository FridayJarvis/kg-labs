cmake_minimum_required(VERSION 3.21)
project(Lab4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(Lab4
    "src/main.cpp"
    "src/App.cpp"
    "src/Window.cpp"
    "src/KeyboardMouse.cpp"
    "src/D3D12Context.cpp"
    "src/GBuffer.cpp"
)

set(KG_LABS_IMGUI_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui"
    CACHE PATH "Dear ImGui source directory")
if(NOT EXISTS "${KG_LABS_IMGUI_ROOT}/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui was not found. Set KG_LABS_IMGUI_ROOT.")
endif()

target_sources(Lab4 PRIVATE
    "${KG_LABS_IMGUI_ROOT}/imgui.cpp"
    "${KG_LABS_IMGUI_ROOT}/imgui_draw.cpp"
    "${KG_LABS_IMGUI_ROOT}/imgui_tables.cpp"
    "${KG_LABS_IMGUI_ROOT}/imgui_widgets.cpp"
    "${KG_LABS_IMGUI_ROOT}/backends/imgui_impl_win32.cpp"
    "${KG_LABS_IMGUI_ROOT}/backends/imgui_impl_dx12.cpp"
)

target_include_directories(Lab4 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${KG_LABS_IMGUI_ROOT}"
    "${KG_LABS_IMGUI_ROOT}/backends"
)

set(KG_LABS_ASSIMP_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp"
    CACHE PATH "Directory containing Assimp include/, lib/ and the runtime DLL")
find_path(KG_LABS_ASSIMP_INCLUDE assimp/Importer.hpp
    HINTS "${KG_LABS_ASSIMP_ROOT}/include")
find_library(KG_LABS_ASSIMP_LIBRARY
    NAMES assimp-vc143-mt assimp-vc142-mt assimp
    HINTS "${KG_LABS_ASSIMP_ROOT}/lib/x64")

if(NOT KG_LABS_ASSIMP_INCLUDE OR NOT KG_LABS_ASSIMP_LIBRARY)
    message(FATAL_ERROR
        "Assimp was not found. Set KG_LABS_ASSIMP_ROOT to an Assimp distribution.")
endif()

target_include_directories(Lab4 PRIVATE "${KG_LABS_ASSIMP_INCLUDE}")

target_compile_definitions(Lab4 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(Lab4 PROPERTIES WIN32_EXECUTABLE TRUE)

target_link_libraries(Lab4 PRIVATE
    user32 gdi32
    dxgi d3d12 d3dcompiler dxguid
    windowscodecs ole32
    "${KG_LABS_ASSIMP_LIBRARY}"
)

set(KG_LABS_ASSIMP_DLL "${KG_LABS_ASSIMP_ROOT}/assimp-vc143-mt.dll"
    CACHE FILEPATH "Assimp runtime DLL")
set(KG_LABS_SPONZA_TEXTURE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/textures"
    CACHE PATH "Directory containing the Sponza material textures")

add_custom_command(TARGET Lab4 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Deferred.hlsl"
        "$<TARGET_FILE_DIR:Lab4>/Deferred.hlsl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/sponza.obj"
        "$<TARGET_FILE_DIR:Lab4>/sponza.obj"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/sponza.mtl"
        "$<TARGET_FILE_DIR:Lab4>/sponza.mtl"
    COMMENT "Copying assets to output directory"
)

if(EXISTS "${KG_LABS_ASSIMP_DLL}")
    add_custom_command(TARGET Lab4 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${KG_LABS_ASSIMP_DLL}" "$<TARGET_FILE_DIR:Lab4>"
        COMMENT "Copying Assimp runtime")
endif()

if(EXISTS "${KG_LABS_SPONZA_TEXTURE_DIR}")
    add_custom_command(TARGET Lab4 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${KG_LABS_SPONZA_TEXTURE_DIR}" "$<TARGET_FILE_DIR:Lab4>/textures"
        COMMENT "Copying Sponza textures")
else()
    message(WARNING "Sponza textures were not found; materials will use fallback colors")
endif()
