cmake_minimum_required(VERSION 3.16)

project(yamp VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(QT_QML_GENERATE_QMLLS_INI ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(LIBPULSE REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Sql DBus Network)

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
        qml/ScrollableViewHelper.qml
        qml/SmoothScrollBar.qml
        qml/DetailHeader.qml
        qml/Lyrics.qml
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
        src/LibraryDb.h
        src/LibraryDb.cpp
        src/ScannerHelpers.h
        src/ScannerHelpers.cpp
        src/SqlUtils.h
        src/AudioFormats.h
        src/LibraryWatcher.h
        src/LibraryWatcher.cpp
        src/ScanSession.h
        src/ScanSession.cpp
        src/ScanCoordinator.h
        src/ScanCoordinator.cpp
        src/CoverCacheService.h
        src/CoverCacheService.cpp
        src/AlsaDevicesService.h
        src/AlsaDevicesService.cpp
        src/PlayerBackend.h
        src/PlayerBackend.cpp
        src/AppController.h
        src/AppController.cpp
        src/PaVolumeController.h
        src/PaVolumeController.cpp
        src/MprisAdaptor.h
        src/MprisAdaptor.cpp
        src/LastFmScrobbler.h
        src/LastFmScrobbler.cpp
        src/Settings.h
        src/Settings.cpp
)

set_target_properties(appyamp PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    INSTALL_RPATH ""
)

target_link_libraries(appyamp
    PRIVATE
        Qt6::Quick
        Qt6::Sql
        Qt6::DBus
        Qt6::Network
        PkgConfig::TAGLIB
        PkgConfig::LIBPULSE
        PkgConfig::ALSA
        PkgConfig::MPV
)

include(GNUInstallDirs)
set(YAMP_LICENSE_INSTALL_DIR
    "${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}"
    CACHE STRING "Install directory for YAMP license notices")
install(TARGETS appyamp
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${CMAKE_SOURCE_DIR}/yamp.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)
foreach(size IN ITEMS 16 22 24 32 48 64 96 128 192 256 512)
    install(FILES ${CMAKE_SOURCE_DIR}/icons/yamp-${size}.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps
        RENAME yamp.png
    )
endforeach()
install(FILES ${CMAKE_SOURCE_DIR}/icons/yamp.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
)
install(FILES ${CMAKE_SOURCE_DIR}/icons/yamp-symbolic.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/symbolic/apps
)
install(FILES
    ${CMAKE_SOURCE_DIR}/LICENSE
    ${CMAKE_SOURCE_DIR}/COPYRIGHT
    ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
    ${CMAKE_SOURCE_DIR}/licenses/LGPL-2.1.txt
    DESTINATION ${YAMP_LICENSE_INSTALL_DIR}
)
