# CMakeList.txt: проект CMake для lab4-vs_cmake-dx12; включите исходный код и определения,
# укажите здесь логику для конкретного проекта.
#

# Добавьте источник в исполняемый файл этого проекта.
add_executable (lab4-vs_cmake-dx12 "lab4-vs_cmake-dx12.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET lab4-vs_cmake-dx12 PROPERTY CXX_STANDARD 20)
endif()

# TODO: Добавьте тесты и целевые объекты, если это необходимо.
