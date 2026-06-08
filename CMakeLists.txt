cmake_minimum_required(VERSION 3.16)

project(Aitrainer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Network)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appAitrainer
    main.cpp
    learningagent.h
    learningagent.cpp
    loraadapter.h
    loraadapter.cpp
    agentcontroller.h
    agentcontroller.cpp
    Synapse.h
    Hippocampus.h
    Hippocampus.cpp
    Amygdala.h
    Amygdala.cpp
    Thalamus.h
    Thalamus.cpp
    TemporalLobe.h
    TemporalLobe.cpp
    Cerebellum.h
    Cerebellum.cpp
    BasalGanglia.h
    BasalGanglia.cpp
    FrontalLobe.h
    FrontalLobe.cpp
    Brain.h
    Brain.cpp
)

qt_add_qml_module(appAitrainer
    URI Aitrainer
    QML_FILES
        Main.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appAitrainer PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appAitrainer
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appAitrainer
    PRIVATE Qt6::Quick Qt6::Network
)

if(WIN32)
    target_compile_definitions(appAitrainer PRIVATE AITRAINER_D3D11_GPU=1)
    target_link_libraries(appAitrainer PRIVATE d3d11 d3dcompiler dxgi)
endif()

include(GNUInstallDirs)
install(TARGETS appAitrainer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
add_subdirectory(aimobile)
