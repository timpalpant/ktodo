cmake_minimum_required(VERSION 3.28)

project(ktodo VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(QT_MIN_VERSION 6.8.0)
set(KF_MIN_VERSION 6.0.0)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Widgets Network NetworkAuth Sql Concurrent)

# The KF6 umbrella config is not shipped on every distribution, so each
# framework is located individually.
find_package(KF6CoreAddons ${KF_MIN_VERSION} REQUIRED)
find_package(KF6I18n ${KF_MIN_VERSION} REQUIRED)
find_package(KF6Config ${KF_MIN_VERSION} REQUIRED)
find_package(KF6Notifications ${KF_MIN_VERSION} REQUIRED)
find_package(KF6Wallet ${KF_MIN_VERSION} REQUIRED)
find_package(KF6IconThemes ${KF_MIN_VERSION} REQUIRED)
find_package(KF6WindowSystem ${KF_MIN_VERSION} REQUIRED)
find_package(KF6Kirigami ${KF_MIN_VERSION} REQUIRED)

include(GNUInstallDirs)
include(CTest)

qt_standard_project_setup(REQUIRES 6.8)
set(CMAKE_AUTORCC ON)

# QML lives in qml/pages and qml/components; generate a qmldir per directory.
# QTP0004 is a Qt policy, not a CMake one, so if(POLICY ...) never sees it.
qt_policy(SET QTP0004 NEW)

# Baked-in OAuth client credentials. A desktop app is a "public client" in
# RFC 8252 terms: anything compiled in can be extracted, so these are an
# identifier for the build, not a secret. Distribution packages set them so
# the app works out of the box; users can still override at runtime.
set(KTODO_OAUTH_CLIENT_ID "" CACHE STRING "Todoist OAuth client id compiled into the binary")
set(KTODO_OAUTH_CLIENT_SECRET "" CACHE STRING "Todoist OAuth client secret compiled into the binary")

# Development convenience: fall back to a .env in the source tree so a local
# build works without passing -D on every configure. Deliberately done here and
# not at runtime — this reads one file in the developer's own checkout at build
# time, whereas the app reading a .env would take credentials from whatever
# directory it happened to be started in.
if(NOT KTODO_OAUTH_CLIENT_ID AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.env")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.env" _ktodo_dotenv)
    foreach(_line IN LISTS _ktodo_dotenv)
        # KEY=VALUE, tolerating `export ` and surrounding quotes.
        if(_line MATCHES "^[ \t]*(export[ \t]+)?(KTODO_CLIENT_ID|KTODO_CLIENT_SECRET)[ \t]*=[ \t]*(.*)$")
            set(_key "${CMAKE_MATCH_2}")
            set(_value "${CMAKE_MATCH_3}")
            string(STRIP "${_value}" _value)
            string(REGEX REPLACE "^[\"'](.*)[\"']$" "\\1" _value "${_value}")
            if(_key STREQUAL "KTODO_CLIENT_ID")
                set(KTODO_OAUTH_CLIENT_ID "${_value}")
            else()
                set(KTODO_OAUTH_CLIENT_SECRET "${_value}")
            endif()
        endif()
    endforeach()
    unset(_line)
    unset(_key)
    unset(_value)
    unset(_ktodo_dotenv)
    if(KTODO_OAUTH_CLIENT_ID)
        message(STATUS "ktodo: using OAuth client credentials from .env")
    endif()
endif()

configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

qt_add_executable(ktodo
    src/main.cpp

    src/application.cpp          src/application.h
    src/notifier.cpp             src/notifier.h

    src/auth/authmanager.cpp     src/auth/authmanager.h
    src/auth/credentialstore.cpp src/auth/credentialstore.h

    src/api/apiclient.cpp        src/api/apiclient.h

    src/data/types.cpp           src/data/types.h
    src/data/database.cpp        src/data/database.h
    src/data/repository.cpp      src/data/repository.h

    src/sync/syncengine.cpp      src/sync/syncengine.h
    src/sync/commandqueue.cpp    src/sync/commandqueue.h

    src/query/filterquery.cpp    src/query/filterquery.h

    src/models/taskmodel.cpp          src/models/taskmodel.h
    src/models/projectsmodel.cpp      src/models/projectsmodel.h
    src/models/labelsmodel.cpp        src/models/labelsmodel.h
    src/models/collaboratorsmodel.cpp src/models/collaboratorsmodel.h
    src/models/filtersmodel.cpp       src/models/filtersmodel.h
    src/models/notesmodel.cpp         src/models/notesmodel.h
    src/models/sectionsmodel.cpp      src/models/sectionsmodel.h

    src/util/colors.cpp          src/util/colors.h
    src/util/duedate.cpp         src/util/duedate.h
    src/util/richtext.cpp        src/util/richtext.h
)

# The generated QML type registration includes headers by bare file name, so
# every directory holding a QML_ELEMENT header must be on the include path.
target_include_directories(ktodo PRIVATE
    src
    src/auth
    src/api
    src/data
    src/models
    src/query
    src/sync
    src/util
    ${CMAKE_CURRENT_BINARY_DIR}
)

qt_add_qml_module(ktodo
    URI io.github.timpalpant.ktodo
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/LoadingPage.qml
        qml/pages/LoginPage.qml
        qml/pages/TaskListPage.qml
        qml/pages/UpcomingPage.qml
        qml/pages/SearchPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/AboutPage.qml
        qml/pages/ProjectEditorPage.qml
        qml/components/TaskDelegate.qml
        qml/components/TaskEditor.qml
        qml/components/QuickAddSheet.qml
        qml/components/DueDatePicker.qml
        qml/components/PriorityPicker.qml
        qml/components/LabelPicker.qml
        qml/components/AssigneePicker.qml
        qml/components/ProjectPicker.qml
        qml/components/CommentsSheet.qml
        qml/components/SyncIndicator.qml
        qml/components/PriorityBadge.qml
        qml/components/DueChip.qml
        qml/components/TaskDragHandle.qml
        qml/components/RecurrencePicker.qml
        qml/components/UpcomingCalendar.qml
    RESOURCES
        data/icons/io.github.timpalpant.ktodo.svg
)

target_link_libraries(ktodo PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Qt6::Widgets Qt6::Network Qt6::NetworkAuth Qt6::Sql Qt6::Concurrent
    KF6::CoreAddons KF6::I18n KF6::I18nQml KF6::ConfigCore KF6::ConfigGui
    KF6::Notifications KF6::Wallet KF6::IconThemes KF6::WindowSystem
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS ktodo ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/io.github.timpalpant.ktodo.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES data/io.github.timpalpant.ktodo.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
install(FILES data/ktodo.notifyrc DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/knotifications6)
install(FILES data/icons/io.github.timpalpant.ktodo.svg DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
# AppStream's Flatpak compose step needs a raster icon even when a scalable
# icon is installed too.  The PNG is rendered from the SVG source artwork.
install(FILES data/icons/io.github.timpalpant.ktodo.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/128x128/apps)

if(TARGET KF6::I18n)
    ki18n_install(po)
endif()

# The CI and release workflows build native DEB and RPM packages from the
# normal install tree, so keep their metadata next to the install rules.
set(CPACK_PACKAGE_NAME "ktodo")
set(CPACK_PACKAGE_VENDOR "Timothy Palpant")
set(CPACK_PACKAGE_CONTACT "Timothy Palpant <tim@palpant.us>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native Todoist client for KDE Plasma")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
# Note what is deliberately absent here: qml6-module-org-kde-desktop (the
# "org.kde.desktop" QQC2 style main.cpp always selects, see the RPM comment
# below) is not listed because it does not need to be -- on Debian it already
# arrives as a transitive hard Depends of qml6-module-org-kde-kirigamiaddons-
# delegates (-> ...-treeview -> qml6-module-org-kde-desktop), confirmed by
# installing this .deb with --no-install-recommends in a bare debian:trixie
# container and finding the module still present and loading. Do not add it
# back without re-checking that chain.
#
# libqt6sql6-sqlite is the SQLite Qt driver plugin database.cpp loads via
# QSqlDatabase::addDatabase("QSQLITE"). dpkg-shlibdeps only captures
# libqt6sql6 itself (the library ktodo links against), not this plugin, and
# on a default `apt install` it happens to ride in via libqt6sql6's own
# Recommends -- but Recommends are not guaranteed (--no-install-recommends,
# minimal images) and without the plugin ktodo cannot open its local cache
# and exits immediately. Confirmed by installing this .deb with
# --no-install-recommends in a bare debian:trixie container:
# `ktodo: could not open the local cache: Driver not loaded`.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-qtcore, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-org-kde-kirigami, qml6-module-org-kde-kirigamiaddons-components, qml6-module-org-kde-kirigamiaddons-datetime, qml6-module-org-kde-kirigamiaddons-delegates, qml6-module-org-kde-kirigamiaddons-formcard, libqt6sql6-sqlite")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Utility")
# kf6-qqc2-desktop-style provides the "org.kde.desktop" QQC2 style main.cpp
# selects via QQuickStyle::setStyle() whenever QT_QUICK_CONTROLS_STYLE is not
# already set in the environment. Fedora's RPM auto-Requires only scans
# linked shared libraries, not QML-engine-loaded style modules, and nothing
# else in this dependency list pulls it in transitively (unlike Debian, where
# a kirigami-addons submodule happens to require it) -- without it the QML
# engine refuses to load at all. Confirmed by installing this .rpm in a bare
# Fedora container: `module "org.kde.desktop" is not installed`.
#
# No separate package is needed for the SQLite Qt driver here: Fedora bundles
# it directly inside qt6-qtbase (already pulled in transitively via
# qt6-qtdeclarative), unlike Debian which splits it into libqt6sql6-sqlite.
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, kf6-kirigami, kf6-kirigami-addons, kf6-qqc2-desktop-style")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
include(CPack)
