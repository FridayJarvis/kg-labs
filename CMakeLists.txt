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
)

target_include_directories(Lab4 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_definitions(Lab4 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(Lab4 PROPERTIES WIN32_EXECUTABLE TRUE)

target_link_libraries(Lab4 PRIVATE
    user32 gdi32
    dxgi d3d12 d3dcompiler dxguid
)

# Копирование шейдера в выходную директорию сборки
add_custom_command(TARGET Lab4 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/CubeShader.hlsl"
        "$<TARGET_FILE_DIR:Lab4>/CubeShader.hlsl"
    COMMENT "Copying CubeShader.hlsl to output directory"
)