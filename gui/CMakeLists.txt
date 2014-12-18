set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

# Needs QtWidgets
find_package(Qt5Widgets 5.3 REQUIRED)

if(${PORTABLE})
    add_definitions(
        -DPORTABLE
        -DLOCAL_DATA_DIR="../data"
    )
else()
    add_definitions(
        -DDATA_DIR="${DATA_INSTALL_DIR}"
    )
endif()

add_definitions(
    -DQT_NO_CAST_FROM_ASCII
    -DQT_NO_CAST_TO_ASCII
    -DQT_NO_CAST_FROM_BYTEARRAY
)

# Allow libmbp headers to be found
include_directories(${CMAKE_SOURCE_DIR})

set(GUI_SOURCES
    main.cpp
    mainwindow.cpp
    qcombocheckbox.cpp
    qcheckboxlistdelegate.cpp
)

if(WIN32)
    set(GUI_SOURCES
        ${GUI_SOURCES}
        main.rc
    )
endif()

set(GUI_PRIVATE_HEADERS
    mainwindow_p.h
    qcombocheckbox_p.h
)

set(GUI_HEADERS
    mainwindow.h
    qcombocheckbox.h
    qcheckboxlistdelegate.h
)

set(GUI_RESOURCES
    resources.qrc
)

qt5_add_resources(GUI_RESOURCES_RCC ${GUI_RESOURCES})

add_executable(gui WIN32 ${GUI_SOURCES} ${GUI_RESOURCES_RCC})

target_link_libraries(gui
    Qt5::Core
    Qt5::Widgets
    mbp
)

# Set rpath for portable build
if (${PORTABLE})
    set_target_properties(gui
        PROPERTIES
        BUILD_WITH_INSTALL_RPATH OFF
        INSTALL_RPATH "\$ORIGIN/../lib"
    )
endif()

install(
    TARGETS gui
    RUNTIME DESTINATION "${BIN_INSTALL_DIR}/"
    COMPONENT Applications
)