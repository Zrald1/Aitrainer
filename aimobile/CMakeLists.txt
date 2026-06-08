cmake_minimum_required(VERSION 3.16)

project(aimobile VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appaimobile
    main.cpp
    mobileagentcontroller.h
    mobileagentcontroller.cpp
    ../learningagent.h
    ../learningagent.cpp
    ../loraadapter.h
    ../loraadapter.cpp
    ../Synapse.h
    ../Hippocampus.h
    ../Hippocampus.cpp
    ../Amygdala.h
    ../Amygdala.cpp
    ../Thalamus.h
    ../Thalamus.cpp
    ../TemporalLobe.h
    ../TemporalLobe.cpp
    ../Cerebellum.h
    ../Cerebellum.cpp
    ../BasalGanglia.h
    ../BasalGanglia.cpp
    ../FrontalLobe.h
    ../FrontalLobe.cpp
    ../Brain.h
    ../Brain.cpp
)

qt_add_qml_module(appaimobile
    URI aimobile
    QML_FILES
        Main.qml
)

qt_add_resources(appaimobile "bundled_student_model"
    PREFIX "/"
    FILES
        student_agent.ai
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appaimobile PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appaimobile
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appaimobile
    PRIVATE Qt6::Quick
)

target_include_directories(appaimobile
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

install(TARGETS appaimobile
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
