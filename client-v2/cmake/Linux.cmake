# Linux deployment and public regression checks. The Windows private suite is separate.
foreach(config_file update-public.der installer-keys.json)
    configure_file(config/${config_file} "${CMAKE_CURRENT_BINARY_DIR}/app/config/${config_file}" COPYONLY)
endforeach()
install(FILES config/update-public.der config/installer-keys.json DESTINATION bin/app/config)
install(FILES assets/License.txt LICENSE NOTICE DESTINATION share/licenses/rutool)
install(DIRECTORY licenses/ DESTINATION share/licenses/rutool/third-party)
install(FILES packaging/linux/rutool.desktop DESTINATION share/applications)
install(FILES packaging/linux/rutool.svg DESTINATION share/icons/hicolor/scalable/apps)
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    qt_add_executable(rutool_linux_tests linux-tests/LinuxTests.cpp)
    target_link_libraries(rutool_linux_tests PRIVATE Qt6::Test Qt6::Network rutool_core rutool_security rutool_network rutool_portable_crypto)
    target_compile_definitions(rutool_linux_tests PRIVATE RUTOOL_LINUX_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/linux-tests/fixtures"
        RUTOOL_PYTHON="${Python3_EXECUTABLE}" RUTOOL_DELTA_GENERATOR="${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-addon-delta.py")
    rutool_quality(rutool_linux_tests)
    qt_add_executable(rutool_linux_ui_tests linux-tests/UiTests.cpp
        src/app/ClientController.cpp src/app/ClientOperations.cpp ${rutool_updates}
        src/app/ClientPremium.cpp src/app/ClientController.hpp)
    target_include_directories(rutool_linux_ui_tests PRIVATE src/app)
    target_link_libraries(rutool_linux_ui_tests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2
        Qt6::Concurrent rutool_network rutool_ui_support)
    qt_add_qml_module(rutool_linux_ui_tests URI RuTool.App VERSION 1.0 NO_PLUGIN
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml-targets/rutool_linux_ui_tests/RuTool/App"
        QML_FILES ${rutool_qml_files})
    qt_add_resources(rutool_linux_ui_tests ui_test_artwork PREFIX /assets BASE assets
        FILES assets/Background.png assets/BackgroundDark.jpg assets/Arona.gif assets/RuTool.ico assets/License.txt)
    rutool_quality(rutool_linux_ui_tests)
    add_test(NAME linux_platform_and_crypto COMMAND rutool_linux_tests)
    set_tests_properties(linux_platform_and_crypto PROPERTIES TIMEOUT 60)
    add_test(NAME linux_qml_workflows COMMAND rutool_linux_ui_tests)
    set_tests_properties(linux_qml_workflows PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 45)
    add_test(NAME linux_production_qml COMMAND RuTool --smoke-test --smoke-screenshot "${CMAKE_CURRENT_BINARY_DIR}/linux-smoke.png")
    set_tests_properties(linux_production_qml PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 20)
endif()
