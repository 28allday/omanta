# QmlIntegration provides the QML_ELEMENT macros the model headers use, without
# dragging in the QML engine — these stay headless model tests.
find_package(Qt6 6.5 REQUIRED COMPONENTS Test QmlIntegration Quick DBus)

# Each suite is its own executable so a crash in one cannot take the rest down,
# and so ctest reports them separately.
#
# The sources are compiled into the tests rather than linked from a library:
# the app target is a QML module, and pulling that in would drag the whole QML
# runtime into what are meant to be headless model tests.
function(omanta_test name)
    qt_add_executable(${name} ${name}.cpp ${ARGN})
    target_compile_definitions(${name} PRIVATE QT_NO_KEYWORDS)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Gui Qt6::Test Qt6::QmlIntegration Qt6::Quick PkgConfig::GIO PkgConfig::LIBARCHIVE PkgConfig::TINYSPARQL)
    add_test(NAME ${name} COMMAND ${name})

    # QTemporaryDir lands in /tmp, and the tests watch it with inotify.
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

omanta_test(tst_directorymodel
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
    ${CMAKE_SOURCE_DIR}/src/DirectoryModel.cpp
    ${CMAKE_SOURCE_DIR}/src/FileEntry.cpp
)

omanta_test(tst_sortfilter
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
    ${CMAKE_SOURCE_DIR}/src/DirectoryModel.cpp
    ${CMAKE_SOURCE_DIR}/src/FileEntry.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSortFilterModel.cpp
)

omanta_test(tst_navigation
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
    ${CMAKE_SOURCE_DIR}/src/NavigationHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/Platform.cpp
    ${CMAKE_SOURCE_DIR}/src/ArchiveEngine.cpp
)

omanta_test(tst_fileoperations
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
    ${CMAKE_SOURCE_DIR}/src/FileOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/FileOperationWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/ArchiveEngine.cpp
)

# Source-tree scan, not a running window: the rule it enforces is a property
# of the QML text, and a headless suite can check it in milliseconds.
omanta_test(tst_qmltext)
target_compile_definitions(tst_qmltext PRIVATE OMANTA_QML_DIR="${CMAKE_SOURCE_DIR}/qml")

omanta_test(tst_thumbnails
    ${CMAKE_SOURCE_DIR}/src/ThumbnailProvider.cpp
)

omanta_test(tst_properties
    ${CMAKE_SOURCE_DIR}/src/FileProperties.cpp
)

omanta_test(tst_theme
    ${CMAKE_SOURCE_DIR}/src/SystemTheme.cpp
)
# The portal client is D-Bus; the test runs fine without a bus (calls just
# error out) but the symbols still have to link.
target_link_libraries(tst_theme PRIVATE Qt6::DBus)

omanta_test(tst_places
    ${CMAKE_SOURCE_DIR}/src/PlacesModel.cpp
    ${CMAKE_SOURCE_DIR}/src/Mounter.cpp
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
)

omanta_test(tst_actions
    ${CMAKE_SOURCE_DIR}/src/UserActions.cpp
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
)

omanta_test(tst_starred
    ${CMAKE_SOURCE_DIR}/src/StarredStore.cpp
    ${CMAKE_SOURCE_DIR}/src/StarredModel.cpp
    ${CMAKE_SOURCE_DIR}/src/DirectoryModel.cpp
    ${CMAKE_SOURCE_DIR}/src/FileEntry.cpp
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
)

omanta_test(tst_servers
    ${CMAKE_SOURCE_DIR}/src/ServerStore.cpp
    ${CMAKE_SOURCE_DIR}/src/NetworkModel.cpp
    ${CMAKE_SOURCE_DIR}/src/FileEntry.cpp
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
)

omanta_test(tst_batchrename
    ${CMAKE_SOURCE_DIR}/src/BatchRenamer.cpp
    ${CMAKE_SOURCE_DIR}/src/FileOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/FileOperationWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/ArchiveEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
)

omanta_test(tst_archives
    ${CMAKE_SOURCE_DIR}/src/ArchiveEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/FileOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/FileOperationWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
)

omanta_test(tst_settings
    ${CMAKE_SOURCE_DIR}/src/Settings.cpp
)

omanta_test(tst_tree
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
    ${CMAKE_SOURCE_DIR}/src/DirectoryModel.cpp
    ${CMAKE_SOURCE_DIR}/src/DirectoryTreeModel.cpp
    ${CMAKE_SOURCE_DIR}/src/FileEntry.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSortFilterModel.cpp
)

omanta_test(tst_search
    ${CMAKE_SOURCE_DIR}/src/SearchModel.cpp
    ${CMAKE_SOURCE_DIR}/src/DirectoryModel.cpp
    ${CMAKE_SOURCE_DIR}/src/FileEntry.cpp
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
)

omanta_test(tst_fts
    ${CMAKE_SOURCE_DIR}/src/SearchModel.cpp
    ${CMAKE_SOURCE_DIR}/src/DirectoryModel.cpp
    ${CMAKE_SOURCE_DIR}/src/FileEntry.cpp
    ${CMAKE_SOURCE_DIR}/src/Location.cpp
)
