cmake_minimum_required(VERSION 3.21)

project(
    FluentSerialAssistant
    VERSION 0.1.5
    DESCRIPTION "A Fluent Qt serial assistant prototype"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Python3 COMPONENTS Interpreter)
find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE AND Python3_EXECUTABLE)
    add_custom_target(format
        COMMAND "${CMAKE_COMMAND}" -E env "CLANG_FORMAT=${CLANG_FORMAT_EXECUTABLE}"
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/format_code.py" .
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Format C/C++ sources"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND "${CMAKE_COMMAND}" -E env "CLANG_FORMAT=${CLANG_FORMAT_EXECUTABLE}"
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/format_code.py" --check .
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Check C/C++ source formatting"
        VERBATIM
    )
endif()

if(MINGW)
    string(APPEND CMAKE_CXX_FLAGS_MINSIZEREL " -ffunction-sections -fdata-sections")
    string(APPEND CMAKE_C_FLAGS_MINSIZEREL " -ffunction-sections -fdata-sections")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_MINSIZEREL " -Wl,--gc-sections -s")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets SerialPort Svg Network Core5Compat)

set(FLUENTQTWIDGETS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/FluentQtWidgets" CACHE PATH "Path to FluentQtWidgets checkout")
if(NOT EXISTS "${FLUENTQTWIDGETS_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "FLUENTQTWIDGETS_ROOT does not point to a FluentQtWidgets checkout: ${FLUENTQTWIDGETS_ROOT}")
endif()

set(CMAKE_DISABLE_FIND_PACKAGE_Qt6Multimedia ON CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Qt6MultimediaWidgets ON CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Qt6WebEngineWidgets ON CACHE BOOL "" FORCE)
set(FQW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FQW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FQW_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(FQW_ENABLE_NETWORK OFF CACHE BOOL "" FORCE)
set(FQW_COMPACT_BUILD ON CACHE BOOL "" FORCE)
add_subdirectory("${FLUENTQTWIDGETS_ROOT}" "${CMAKE_BINARY_DIR}/_deps/fluentqtwidgets")

add_executable(FluentSerialAssistant
    src/app/core/font_preferences.cpp
    src/app/core/font_preferences.h
    src/app/core/hex_utils.cpp
    src/app/core/hex_utils.h
    src/app/core/text_encoding.cpp
    src/app/core/text_encoding.h
    src/app/core/update_checker.cpp
    src/app/core/update_checker.h
    src/app/serial/serial_controller.cpp
    src/app/serial/serial_controller.h
    src/app/view/app_page.cpp
    src/app/view/app_page.h
    src/app/view/main_window.cpp
    src/app/view/main_window.h
    src/app/view/settings_page.cpp
    src/app/view/settings_page.h
    src/app/view/workbench_page.cpp
    src/app/view/workbench_page.h
    src/app/view/workbench/workbench_page_connection.cpp
    src/app/view/workbench/workbench_page_files.cpp
    src/app/view/workbench/workbench_page_internal.h
    src/app/view/workbench/workbench_page_layout.cpp
    src/app/view/workbench/workbench_page_packets.cpp
    src/app/view/workbench/workbench_page_side_sections.cpp
    src/app/view/workbench/workbench_page_state.cpp
    src/app/view/workbench/workbench_page_terminal.cpp
    src/app/view/workbench/workbench_page_terminal_sections.cpp
    src/app/resources/app.qrc
    src/main.cpp
)

if(WIN32)
    set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/logo.ico")
    configure_file(
        src/app/resources/app_icon.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc"
        @ONLY
    )
    target_sources(FluentSerialAssistant PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
endif()

if(APPLE)
    set(APP_ICON_PATH_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/resources/app_icon.icns")
    target_sources(FluentSerialAssistant PRIVATE "${APP_ICON_PATH_MACOS}")
    set_source_files_properties("${APP_ICON_PATH_MACOS}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

target_include_directories(FluentSerialAssistant PRIVATE src src/app)

target_link_libraries(FluentSerialAssistant
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::SerialPort
        Qt6::Network
        Qt6::Core5Compat
        FluentQtWidgets::Widgets
)

if(MSVC)
    target_compile_options(FluentSerialAssistant PRIVATE /utf-8)
endif()

if(MINGW)
    target_link_options(FluentSerialAssistant PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-s>
        $<$<CONFIG:MinSizeRel>:-Wl,--gc-sections>
    )
endif()

set_target_properties(FluentSerialAssistant PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Fluent Serial Assistant"
    MACOSX_BUNDLE_GUI_IDENTIFIER "tech.zhangshu.FluentSerialAssistant"
    MACOSX_BUNDLE_ICON_FILE "app_icon.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)
