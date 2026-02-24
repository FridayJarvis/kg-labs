cmake_minimum_required(VERSION 3.16)
project(SoftwareRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SRC_FILES
    src/main.cpp
    src/image.cpp
    src/model.cpp
    src/rasterizer.cpp
    src/shader.cpp
)

add_executable(software_renderer ${SRC_FILES})
target_include_directories(software_renderer PRIVATE include)

# Добавляем макрос с путем к папке проекта
target_compile_definitions(software_renderer PRIVATE 
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}/"
)

set_target_properties(software_renderer PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_custom_command(TARGET software_renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/models"
    "$<TARGET_FILE_DIR:software_renderer>/models"
)

# Вспомогательный скрипт для условного копирования
file(WRITE "${CMAKE_BINARY_DIR}/copy_if_exists.cmake"
"if(EXISTS \"\${SRC}\")
    execute_process(COMMAND \${CMAKE_COMMAND} -E copy \"\${SRC}\" \"\${DST}\")
endif()
")

# Копируем output.png из папки сборки в корень проекта (если файл существует)
add_custom_command(TARGET software_renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSRC="$<TARGET_FILE_DIR:software_renderer>/output.png"
        -DDST="${CMAKE_SOURCE_DIR}/output.png"
        -P "${CMAKE_BINARY_DIR}/copy_if_exists.cmake"
)