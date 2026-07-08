cmake_minimum_required(VERSION 3.21)

project(
    FluentSerialAssistant
    VERSION 0.1.1
    DESCRIPTION "A Fluent Qt serial assistant prototype"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MINGW)
    string(APPEND CMAKE_CXX_FLAGS_MINSIZEREL " -ffunction-sections -fdata-sections")
    string(APPEND CMAKE_C_FLAGS_MINSIZEREL " -ffunction-sections -fdata-sections")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_MINSIZEREL " -Wl,--gc-sections -s")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets SerialPort Svg)

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
    src/app/core/hex_utils.cpp
    src/app/core/hex_utils.h
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

target_include_directories(FluentSerialAssistant PRIVATE src src/app)

target_link_libraries(FluentSerialAssistant
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::SerialPort
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
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)
