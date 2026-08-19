cmake_minimum_required(VERSION 3.28)
project(omanta VERSION 0.1.3 LANGUAGES CXX)

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

qt_add_executable(omanta
    src/main.cpp
    src/Application.cpp
    src/Application.h
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
    src/Location.cpp
    src/Location.h
    src/Mounter.cpp
    src/Mounter.h
    src/NavigationHistory.cpp
    src/NavigationHistory.h
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

qt_add_qml_module(omanta
    URI Omanta
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Tab.qml
        qml/TabPanes.qml
        qml/OpsPie.qml
        qml/PathBar.qml
        qml/Sidebar.qml
        qml/FileListView.qml
        qml/FileIconView.qml
        qml/ToolbarButton.qml
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

# GIO's gdbusintrospection.h declares a struct member named `signals`, which
# Qt's unprefixed keyword macros turn into a syntax error. Any Qt+GIO project
# must drop the unprefixed keywords and use Q_SIGNALS/Q_SLOTS/Q_EMIT.
target_compile_definitions(omanta PRIVATE QT_NO_KEYWORDS)

# Qt generates a type-registration file that includes each QML_ELEMENT header
# by bare name (<DirectoryModel.h>), so src/ has to be on the include path or
# the registrations silently compile to nothing.
target_include_directories(omanta PRIVATE src)

target_link_libraries(omanta PRIVATE
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

option(OMANTA_BUILD_TESTS "Build the model test suite" ON)
if(OMANTA_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
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
