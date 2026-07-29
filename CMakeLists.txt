cmake_minimum_required(VERSION 3.21)

project(
    FluentSerialAssistant
    VERSION 0.1.11
    DESCRIPTION "A Fluent Qt serial assistant prototype"
    LANGUAGES CXX
)

include(GNUInstallDirs)

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

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets SerialPort Svg Network Core5Compat Qml)

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

add_library(FluentSerialControlClient STATIC
    src/app/control/control_protocol.cpp
    src/app/control/control_protocol.h
    src/app/control/local_control_client.cpp
    src/app/control/local_control_client.h
)
target_include_directories(FluentSerialControlClient PUBLIC src)
target_link_libraries(FluentSerialControlClient PUBLIC Qt6::Core Qt6::Network)

add_executable(FluentSerialAssistant
    src/app/control/control_protocol.h
    src/app/control/local_control_server.cpp
    src/app/control/local_control_server.h
    src/app/control/session_control.h
    src/app/control/workbench_control_service.cpp
    src/app/control/workbench_control_service.h
    src/app/core/app_i18n.cpp
    src/app/core/app_i18n.h
    src/app/core/app_settings.cpp
    src/app/core/app_settings.h
    src/app/core/font_preferences.cpp
    src/app/core/font_preferences.h
    src/app/core/checksum_utils.cpp
    src/app/core/checksum_utils.h
    src/app/core/hex_utils.cpp
    src/app/core/hex_utils.h
    src/app/core/modbus_utils.cpp
    src/app/core/modbus_utils.h
    src/app/core/protocol_template.cpp
    src/app/core/protocol_template.h
    src/app/core/script_runner.cpp
    src/app/core/script_runner.h
    src/app/core/text_encoding.cpp
    src/app/core/text_encoding.h
    src/app/core/update_checker.cpp
    src/app/core/update_checker.h
    src/app/serial/serial_controller.cpp
    src/app/serial/serial_controller.h
    src/app/view/app_page.cpp
    src/app/view/app_page.h
    src/app/view/data_table_window.cpp
    src/app/view/data_table_window.h
    src/app/view/main_window.cpp
    src/app/view/main_window.h
    src/app/view/quick_plot_window.cpp
    src/app/view/quick_plot_window.h
    src/app/view/settings_page.cpp
    src/app/view/settings_page.h
    src/app/view/workbench_page.cpp
    src/app/view/workbench_page.h
    src/app/view/workbench_sessions_page.cpp
    src/app/view/workbench_sessions_page.h
    src/app/view/workbench/workbench_page_checksum.cpp
    src/app/view/workbench/workbench_page_auto_reply.cpp
    src/app/view/workbench/workbench_page_connection.cpp
    src/app/view/workbench/workbench_page_control.cpp
    src/app/view/workbench/workbench_page_data_table.cpp
    src/app/view/workbench/workbench_page_files.cpp
    src/app/view/workbench/workbench_page_internal.h
    src/app/view/workbench/workbench_page_layout.cpp
    src/app/view/workbench/workbench_page_macros.cpp
    src/app/view/workbench/workbench_page_modbus.cpp
    src/app/view/workbench/workbench_page_packets.cpp
    src/app/view/workbench/workbench_page_plot.cpp
    src/app/view/workbench/workbench_page_protocol_templates.cpp
    src/app/view/workbench/workbench_page_scripts.cpp
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
target_compile_definitions(FluentSerialAssistant PRIVATE
    FLUENT_SERIAL_ASSISTANT_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(FluentSerialAssistant
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::SerialPort
        Qt6::Network
        Qt6::Core5Compat
        Qt6::Qml
        FluentQtWidgets::Widgets
        FluentSerialControlClient
)

add_executable(fluentserial-cli
    src/cli/main.cpp
)
target_compile_definitions(fluentserial-cli PRIVATE
    FLUENT_SERIAL_ASSISTANT_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(fluentserial-cli PRIVATE FluentSerialControlClient)

add_executable(fluentserial-mcp
    src/mcp/main.cpp
    src/mcp/mcp_server.cpp
    src/mcp/mcp_server.h
)
target_include_directories(fluentserial-mcp PRIVATE src)
target_compile_definitions(fluentserial-mcp PRIVATE
    FLUENT_SERIAL_ASSISTANT_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(fluentserial-mcp PRIVATE FluentSerialControlClient)

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

if(UNIX AND NOT APPLE)
    set_target_properties(FluentSerialAssistant PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    )
endif()

if(WIN32)
    set(FLUENT_SERIAL_ASSISTANT_RUNTIME_INSTALL_DIR ".")
else()
    set(FLUENT_SERIAL_ASSISTANT_RUNTIME_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}")
endif()

install(TARGETS FluentSerialAssistant
    RUNTIME DESTINATION "${FLUENT_SERIAL_ASSISTANT_RUNTIME_INSTALL_DIR}" COMPONENT Runtime
    BUNDLE DESTINATION "." COMPONENT Runtime
)

if(APPLE)
    set(FLUENT_SERIAL_ASSISTANT_CONTROL_INSTALL_DIR "FluentSerialAssistant.app/Contents/MacOS")
    set_target_properties(fluentserial-cli fluentserial-mcp PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
else()
    set(FLUENT_SERIAL_ASSISTANT_CONTROL_INSTALL_DIR "${FLUENT_SERIAL_ASSISTANT_RUNTIME_INSTALL_DIR}")
    if(UNIX)
        set_target_properties(fluentserial-cli fluentserial-mcp PROPERTIES
            INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
        )
    endif()
endif()

install(TARGETS fluentserial-cli fluentserial-mcp
    RUNTIME DESTINATION "${FLUENT_SERIAL_ASSISTANT_CONTROL_INSTALL_DIR}" COMPONENT Runtime
)

if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/logo.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
        RENAME "tech.zhangshu.FluentSerialAssistant.png"
        COMPONENT Runtime
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/tech.zhangshu.FluentSerialAssistant.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        COMPONENT Runtime
    )
endif()

# Windows deployment is handled by scripts/package_windows.ps1 so the staged
# executable can be signed before Inno Setup builds the final installer.
# macOS explicitly deploys only the Qt plugins used by this application;
# Homebrew can otherwise expose unrelated plugins with unavailable frameworks.
if(APPLE)
    set(FLUENT_SERIAL_ASSISTANT_MACOS_QT_PLUGINS
        Qt6::QCocoaIntegrationPlugin
        Qt6::QICOPlugin
        Qt6::QSvgPlugin
        Qt6::QSvgIconPlugin
    )
    set(FLUENT_SERIAL_ASSISTANT_MACOS_OPTIONAL_QT_PLUGINS
        Qt6::QAppleNetworkInformationPlugin
        Qt6::QSecureTransportBackendPlugin
        Qt6::QTlsBackendCertOnlyPlugin
    )
    set(FLUENT_SERIAL_ASSISTANT_MACOS_DEPLOY_TOOL_OPTIONS "")
    foreach(plugin_target IN LISTS FLUENT_SERIAL_ASSISTANT_MACOS_OPTIONAL_QT_PLUGINS)
        if(TARGET ${plugin_target})
            list(APPEND FLUENT_SERIAL_ASSISTANT_MACOS_QT_PLUGINS ${plugin_target})
        endif()
    endforeach()
    foreach(plugin_target IN LISTS FLUENT_SERIAL_ASSISTANT_MACOS_QT_PLUGINS)
        if(NOT TARGET ${plugin_target})
            message(FATAL_ERROR "Required macOS Qt plugin target is unavailable: ${plugin_target}")
        endif()
        get_target_property(plugin_type ${plugin_target} QT_PLUGIN_TYPE)
        get_target_property(plugin_path ${plugin_target} IMPORTED_LOCATION_RELEASE)
        if(NOT plugin_path)
            get_target_property(plugin_path ${plugin_target} IMPORTED_LOCATION)
        endif()
        if(NOT plugin_type OR NOT plugin_path)
            message(FATAL_ERROR "Qt plugin target is missing deployment metadata: ${plugin_target}")
        endif()
        file(REAL_PATH "${plugin_path}" plugin_real_path)
        get_filename_component(plugin_file_name "${plugin_real_path}" NAME)
        install(FILES "${plugin_real_path}"
            DESTINATION "FluentSerialAssistant.app/Contents/PlugIns/${plugin_type}"
            COMPONENT Runtime
        )
        list(APPEND FLUENT_SERIAL_ASSISTANT_MACOS_DEPLOY_TOOL_OPTIONS
            "-executable=FluentSerialAssistant.app/Contents/PlugIns/${plugin_type}/${plugin_file_name}"
        )
    endforeach()
    list(APPEND FLUENT_SERIAL_ASSISTANT_MACOS_DEPLOY_TOOL_OPTIONS
        "-executable=FluentSerialAssistant.app/Contents/MacOS/fluentserial-cli"
        "-executable=FluentSerialAssistant.app/Contents/MacOS/fluentserial-mcp"
    )
    qt_generate_deploy_app_script(
        TARGET FluentSerialAssistant
        OUTPUT_SCRIPT FLUENT_SERIAL_ASSISTANT_DEPLOY_SCRIPT
        NO_PLUGINS
        NO_TRANSLATIONS
        NO_UNSUPPORTED_PLATFORM_ERROR
        DEPLOY_TOOL_OPTIONS ${FLUENT_SERIAL_ASSISTANT_MACOS_DEPLOY_TOOL_OPTIONS}
    )
    install(SCRIPT "${FLUENT_SERIAL_ASSISTANT_DEPLOY_SCRIPT}" COMPONENT Runtime)
elseif(UNIX)
    qt_generate_deploy_app_script(
        TARGET FluentSerialAssistant
        OUTPUT_SCRIPT FLUENT_SERIAL_ASSISTANT_DEPLOY_SCRIPT
        NO_TRANSLATIONS
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT "${FLUENT_SERIAL_ASSISTANT_DEPLOY_SCRIPT}" COMPONENT Runtime)
endif()

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/FluentSerialAssistant-version.txt"
    CONTENT "${PROJECT_VERSION}\n"
)
