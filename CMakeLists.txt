cmake_minimum_required(VERSION 3.16)
project(CashSloth LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cash-sloth
    src/main.cpp
    src/cash_sloth_json.cpp
    src/cash_sloth_style.cpp)

target_include_directories(cash-sloth PRIVATE include)

if (MSVC)
    target_compile_options(cash-sloth PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(cash-sloth PRIVATE UNICODE _UNICODE)
else()
    target_compile_options(cash-sloth PRIVATE -Wall -Wextra -Wpedantic)
    if (MINGW)
        target_compile_options(cash-sloth PRIVATE -municode)
    endif()
endif()

if (WIN32)
    target_link_libraries(cash-sloth PRIVATE comctl32 gdi32 uxtheme msimg32)
    add_custom_command(TARGET cash-sloth POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cash-sloth>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:cash-sloth>/assets")
endif()

