cmake_minimum_required(VERSION 3.16)
project(CashSloth LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cash-sloth WIN32
    src/main.cpp
    src/cash_sloth_diagnostics.cpp
    src/cash_sloth_json.cpp
    src/cash_sloth_style.cpp
    src/ui/layout/grid.cpp
    src/ui/layout/hbox.cpp
    src/ui/layout/layout_engine.cpp
    src/ui/layout/layout_node.cpp
    src/ui/layout/leaf_box.cpp
    src/ui/layout/spacer.cpp
    src/ui/layout/vbox.cpp
)

target_include_directories(cash-sloth PRIVATE include src)

add_executable(layout-tests
    src/ui/layout/layout_tests.cpp
    src/ui/layout/grid.cpp
    src/ui/layout/hbox.cpp
    src/ui/layout/layout_engine.cpp
    src/ui/layout/layout_node.cpp
    src/ui/layout/leaf_box.cpp
    src/ui/layout/spacer.cpp
    src/ui/layout/vbox.cpp
)

target_include_directories(layout-tests PRIVATE include src)

if (WIN32)
    target_compile_definitions(cash-sloth PRIVATE UNICODE _UNICODE)
    target_link_libraries(cash-sloth PRIVATE comctl32 gdi32 uxtheme msimg32)
    set_target_properties(cash-sloth PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if (MSVC)
    target_compile_options(cash-sloth PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(layout-tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(cash-sloth PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(layout-tests PRIVATE -Wall -Wextra -Wpedantic)
    if (MINGW)
        target_compile_options(cash-sloth PRIVATE -municode)
    endif()
endif()

if (MSVC)
    add_custom_command(TARGET cash-sloth POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cash-sloth>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:cash-sloth>/assets")
endif()
