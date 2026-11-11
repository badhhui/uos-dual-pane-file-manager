cmake_minimum_required(VERSION 3.10)
project(DualPaneFileManager VERSION 2.9.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5 5.11 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Test)
find_package(ZLIB REQUIRED)
find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
find_library(SQLITE3_LIBRARY sqlite3)
if (NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_LIBRARY)
    message(FATAL_ERROR "SQLite3 development files not found")
endif()
include_directories(${SQLITE3_INCLUDE_DIR})

set(UI_SOURCES
    src/mainwindow.cpp
    src/filepane.cpp
    src/filedelegate.cpp
    src/filetableview.cpp
    src/fileheaderview.cpp
    src/directorymodel.cpp
    src/iconprovider.cpp
    src/theme.cpp
    src/formatters.cpp
    src/fileoperations.cpp
    src/fulltextsearch.cpp
    src/singleinstance.cpp
    src/ejecttask.cpp
    src/desktopentry.cpp
    src/uidialogs.cpp
    src/moderndialog.cpp
    src/transfertask.cpp
    src/transfermanager.cpp
    src/transferdialog.cpp
    src/trashdialog.cpp
    src/openwithdialog.cpp
    src/propertiesdialog.cpp
    src/previewdialog.cpp
    src/ziparchive.cpp
)

add_executable(dual-pane-file-manager
    src/main.cpp
    ${UI_SOURCES}
    resources/resources.qrc
)

target_include_directories(dual-pane-file-manager PRIVATE src)
target_compile_definitions(dual-pane-file-manager PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)
target_compile_options(dual-pane-file-manager PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(dual-pane-file-manager PRIVATE Qt5::Widgets Qt5::Gui Qt5::Core Qt5::Network Qt5::Concurrent ZLIB::ZLIB ${SQLITE3_LIBRARY})
set_target_properties(dual-pane-file-manager PROPERTIES OUTPUT_NAME uos-dual-pane-file-manager)

add_executable(ui_smoke
    tests/ui_smoke.cpp
    ${UI_SOURCES}
    resources/resources.qrc
)
target_include_directories(ui_smoke PRIVATE src)
target_compile_definitions(ui_smoke PRIVATE APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(ui_smoke PRIVATE Qt5::Widgets Qt5::Gui Qt5::Core Qt5::Network Qt5::Concurrent ZLIB::ZLIB ${SQLITE3_LIBRARY} Qt5::Test)

include(GNUInstallDirs)
install(TARGETS dual-pane-file-manager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(transfer_smoke
    tests/transfer_smoke.cpp
    src/transfertask.cpp
    src/formatters.cpp
)
target_include_directories(transfer_smoke PRIVATE src)
target_link_libraries(transfer_smoke PRIVATE Qt5::Core)

add_executable(screenshot_tool
    tests/screenshot_tool.cpp
    ${UI_SOURCES}
    resources/resources.qrc
)
target_include_directories(screenshot_tool PRIVATE src ${SQLITE3_INCLUDE_DIR})
target_compile_definitions(screenshot_tool PRIVATE APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(screenshot_tool PRIVATE Qt5::Widgets Qt5::Gui Qt5::Core Qt5::Network Qt5::Concurrent ZLIB::ZLIB ${SQLITE3_LIBRARY} Qt5::Test)

add_executable(dialog_gallery
    tests/dialog_gallery.cpp
    src/moderndialog.cpp
    src/iconprovider.cpp
    src/theme.cpp
    src/formatters.cpp
    resources/resources.qrc
)
target_include_directories(dialog_gallery PRIVATE src)
target_link_libraries(dialog_gallery PRIVATE Qt5::Widgets Qt5::Gui Qt5::Core Qt5::Test)

add_executable(singleinstance_smoke
    tests/singleinstance_smoke.cpp
    src/singleinstance.cpp
)
target_include_directories(singleinstance_smoke PRIVATE src)
target_link_libraries(singleinstance_smoke PRIVATE Qt5::Core Qt5::Network)

add_executable(fulltext_smoke
    tests/fulltext_smoke.cpp
    src/fulltextsearch.cpp
)
target_include_directories(fulltext_smoke PRIVATE src ${SQLITE3_INCLUDE_DIR})
target_link_libraries(fulltext_smoke PRIVATE Qt5::Core ${SQLITE3_LIBRARY})

add_executable(fileops_smoke
    tests/fileops_smoke.cpp
    src/fileoperations.cpp
    src/formatters.cpp
)
target_include_directories(fileops_smoke PRIVATE src)
target_link_libraries(fileops_smoke PRIVATE Qt5::Core)

add_executable(desktopentry_smoke
    tests/desktopentry_smoke.cpp
    src/desktopentry.cpp
)
target_include_directories(desktopentry_smoke PRIVATE src)
target_link_libraries(desktopentry_smoke PRIVATE Qt5::Gui Qt5::Core Qt5::Test)
