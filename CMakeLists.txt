cmake_minimum_required(VERSION 3.16)

project(yamp VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(QT_QML_GENERATE_QMLLS_INI ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Multimedia Sql DBus)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appyamp
    src/main.cpp
)

qt_add_qml_module(appyamp
    URI yamp
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/BottomBar.qml
        qml/SideBar.qml
        qml/Tracklist.qml
        qml/Albums.qml
        qml/AlbumDetail.qml
        qml/Artists.qml
        qml/ArtistDetail.qml
        qml/QueuePanel.qml
        qml/SortableHeader.qml
        qml/SettingsWindow.qml
    SOURCES
        src/Track.h
        src/TrackQueue.h
        src/TrackQueue.cpp
        src/TrackModel.h
        src/TrackModel.cpp
        src/AlbumModel.h
        src/AlbumModel.cpp
        src/ArtistModel.h
        src/ArtistModel.cpp
        src/QueueModel.h
        src/QueueModel.cpp
        src/CoverExtractor.h
        src/CoverExtractor.cpp
        src/CoverImageProvider.h
        src/CoverImageProvider.cpp
        src/MusicLibrary.h
        src/MusicLibrary.cpp
        src/LibraryWatcher.h
        src/LibraryWatcher.cpp
        src/PlayerBackend.h
        src/PlayerBackend.cpp
        src/MprisAdaptor.h
        src/MprisAdaptor.cpp
        src/settings.h
        src/settings.cpp
)

set_target_properties(appyamp PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appyamp
    PRIVATE
        Qt6::Quick
        Qt6::Multimedia
        Qt6::Sql
        Qt6::DBus
        PkgConfig::TAGLIB
)

include(GNUInstallDirs)
install(TARGETS appyamp
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${CMAKE_SOURCE_DIR}/yamp.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)
