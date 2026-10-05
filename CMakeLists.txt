cmake_minimum_required(VERSION 3.28)
project(omanta VERSION 0.1.23 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 DBus Svg)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)
# Full-text search: SPARQL client for the localsearch index.
pkg_check_modules(TINYSPARQL REQUIRED IMPORTED_TARGET tinysparql-3.0)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

set(OMANTA_SOURCES
    src/Application.cpp
    src/Application.h
    src/Applications.cpp
    src/Applications.h
    src/ArchiveEngine.cpp
    src/ArchiveEngine.h
    src/BatchRenamer.cpp
    src/BatchRenamer.h
    src/Clipboard.cpp
    src/Clipboard.h
    src/DBusAdaptors.cpp
    src/DBusAdaptors.h
    src/DirectoryModel.cpp
    src/DirectoryModel.h
    src/DirectoryTreeModel.cpp
    src/DirectoryTreeModel.h
    src/FileEntry.cpp
    src/FileEntry.h
    src/FileOperations.cpp
    src/FileOperations.h
    src/FileOperationTypes.h
    src/FileOperationWorker.cpp
    src/FileOperationWorker.h
    src/FileProperties.cpp
    src/FileProperties.h
    src/FileSortFilterModel.cpp
    src/FileSortFilterModel.h
    src/IconImageProvider.cpp
    src/IconImageProvider.h
    src/KineticScroll.cpp
    src/KineticScroll.h
    src/Location.cpp
    src/Location.h
    src/Mounter.cpp
    src/Mounter.h
    src/NavigationHistory.cpp
    src/NavigationHistory.h
    src/DefaultFileManager.cpp
    src/DefaultFileManager.h
    src/Platform.cpp
    src/Platform.h
    src/PlacesModel.cpp
    src/PlacesModel.h
    src/SearchModel.cpp
    src/StarredStore.cpp
    src/StarredStore.h
    src/StarredModel.cpp
    src/StarredModel.h
    src/ServerStore.cpp
    src/ServerStore.h
    src/NetworkModel.cpp
    src/NetworkModel.h
    src/Previewer.cpp
    src/Previewer.h
    src/SearchModel.h
    src/Settings.cpp
    src/Settings.h
    src/ThumbnailProvider.cpp
    src/ThumbnailProvider.h
    src/SystemTheme.cpp
    src/SystemTheme.h
    src/UserActions.cpp
    src/UserActions.h
)

set_source_files_properties(qml/Colors.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(OMANTA_QML_FILES
    qml/Main.qml
    qml/Tab.qml
    qml/TabPanes.qml
    qml/OpsPie.qml
    qml/PathBar.qml
    qml/Sidebar.qml
    qml/FileListView.qml
    qml/FileIconView.qml
    qml/FileDrag.qml
    qml/ToolbarButton.qml
    qml/DialogCloseButton.qml
    qml/PromptDialog.qml
    qml/PreferencesDialog.qml
    qml/VisibleColumnsDialog.qml
    qml/ShortcutsDialog.qml
    qml/AboutDialog.qml
    qml/BatchRenameDialog.qml
    qml/CompressDialog.qml
    qml/FolderPickerDialog.qml
    qml/ConfirmDialog.qml
    qml/ConflictDialog.qml
    qml/CredentialDialog.qml
    qml/PropertiesDialog.qml
    qml/Colors.qml
)

qt_add_executable(omanta src/main.cpp ${OMANTA_SOURCES})
qt_add_qml_module(omanta URI Omanta VERSION 1.0 QML_FILES ${OMANTA_QML_FILES})

# GIO's gdbusintrospection.h declares a struct member named `signals`, which
# Qt's unprefixed keyword macros turn into a syntax error. Any Qt+GIO project
# must drop the unprefixed keywords and use Q_SIGNALS/Q_SLOTS/Q_EMIT.
target_compile_definitions(omanta PRIVATE QT_NO_KEYWORDS)

# Qt generates a type-registration file that includes each QML_ELEMENT header
# by bare name (<DirectoryModel.h>), so src/ has to be on the include path or
# the registrations silently compile to nothing.
target_include_directories(omanta PRIVATE src)

set(OMANTA_LIBRARIES
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
    Qt6::Svg
    PkgConfig::GIO
    PkgConfig::LIBARCHIVE
    PkgConfig::TINYSPARQL
)

target_link_libraries(omanta PRIVATE ${OMANTA_LIBRARIES})

option(OMANTA_BUILD_TESTS "Build the model test suite" ON)
if(OMANTA_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()
    # Exercise the real QML and generated type registrations in a separate
    # executable, using the same source lists as the application.
    qt_add_executable(tst_qmlviews tests/tst_qmlviews.cpp ${OMANTA_SOURCES})
    qt_add_qml_module(tst_qmlviews URI Omanta VERSION 1.0
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests/qml/Omanta
        QML_FILES ${OMANTA_QML_FILES})
    target_compile_definitions(tst_qmlviews PRIVATE QT_NO_KEYWORDS)
    target_include_directories(tst_qmlviews PRIVATE src)
    target_link_libraries(tst_qmlviews PRIVATE ${OMANTA_LIBRARIES})
    add_subdirectory(tests)
endif()

install(TARGETS omanta RUNTIME DESTINATION bin)
install(FILES LICENSE DESTINATION share/licenses/omanta)
install(FILES packaging/omanta.desktop DESTINATION share/applications)
install(FILES packaging/omanta.svg DESTINATION share/icons/hicolor/scalable/apps)
install(DIRECTORY packaging/actions/ DESTINATION share/omanta/actions FILES_MATCHING PATTERN "*.toml")
install(PROGRAMS packaging/bin/omanta-dropbox-link DESTINATION bin)
install(PROGRAMS packaging/bin/omanta-switch packaging/bin/omanta-launch
        packaging/bin/omanta-launch-cwd DESTINATION bin)
