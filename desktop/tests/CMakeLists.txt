find_package(Qt6 REQUIRED COMPONENTS Test Concurrent Network Multimedia Sql Quick Qml QuickTest)

# Enable testing
enable_testing()

# Test utilities library for shared test functionality
add_library(MurmurTestUtils STATIC
    utils/TestUtils.hpp
    utils/TestUtils.cpp
    utils/MockComponents.hpp
    utils/MockComponents.cpp
    utils/TestDatabase.hpp
    utils/TestDatabase.cpp
)

target_link_libraries(MurmurTestUtils PUBLIC
    MurmurCore
    Qt6::Test
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Quick
    Qt6::Qml
)

# Test executable with existing test suites
add_executable(MurmurTests
    # Test runner
    test_main.cpp
    
    # Unit tests - Core components
    test_expected.cpp
    test_retry_manager.cpp
    test_input_validator.cpp
    
    # Unit tests - Torrent functionality
    test_torrent_engine.cpp
    
    # Integration tests
    # test_video_processing_integration.cpp
    
    # Unit tests - Transcription
    unit/transcription/TestWhisperEngine.cpp
    
    # Unit tests - Media components
    test_ffmpeg_wrapper.cpp
    
    # Unit tests - Storage components
    test_storage_manager.cpp
    
    # Unit tests - Security components
    test_security_components.cpp
    
    # Real media tests with full functionality
    test_simple_real_media.cpp
    # test_real_media_processing.cpp
    # test_performance_benchmarks.cpp
    # UI flow tests
    # test_ui_flows.cpp
    # test_ui_integration.cpp
    # End-to-end integration tests
    test_end_to_end_integration.cpp
)

target_link_libraries(MurmurTests PRIVATE
    MurmurTestUtils
    MurmurCore
    MurmurUI
    Qt6::Test
    Qt6::Core
    Qt6::Concurrent
    Qt6::Network
    Qt6::Multimedia
    Qt6::Sql
    Qt6::Quick
    Qt6::Qml
)

# QML Test executable for controller readiness tests
add_executable(MurmurQMLTests
    test_qml_controller_readiness.cpp
)

target_link_libraries(MurmurQMLTests PRIVATE
    MurmurCore
    MurmurUI
    Qt6::QuickTest
    Qt6::Quick
    Qt6::Qml
)

# Set the QML import path for the test
set_target_properties(MurmurQMLTests PROPERTIES
    QT_QML_IMPORT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

# Main comprehensive test
add_test(NAME MurmurComprehensiveTests COMMAND MurmurTests)
set_tests_properties(MurmurComprehensiveTests PROPERTIES
    TIMEOUT 300
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# QML Controller Readiness Test
add_test(NAME MurmurQMLControllerReadinessTest COMMAND MurmurQMLTests -input ${CMAKE_CURRENT_SOURCE_DIR}/qml)
set_tests_properties(MurmurQMLControllerReadinessTest PROPERTIES
    TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML2_IMPORT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/../src/qml"
)

# Test data setup
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_data)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_data/media)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_data/databases)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_data/torrents)

# Copy test assets for QFINDTESTDATA
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/assets)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets/)

# Custom test targets
add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --parallel 4
    DEPENDS MurmurTests MurmurQMLTests
    COMMENT "Running all tests"
)

# Custom target to run only QML tests
add_custom_target(run_qml_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -R "QML" --output-on-failure
    DEPENDS MurmurQMLTests
    COMMENT "Running QML tests"
)

# Code coverage support with gcovr integration
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MurmurCore PRIVATE --coverage)
    target_link_options(MurmurCore PRIVATE --coverage)
    target_compile_options(MurmurTestUtils PRIVATE --coverage)
    target_link_options(MurmurTestUtils PRIVATE --coverage)
    target_compile_options(MurmurTests PRIVATE --coverage)
    target_link_options(MurmurTests PRIVATE --coverage)
    
    # Find coverage tools - prefer gcovr, fallback to lcov
    find_program(GCOVR_EXECUTABLE gcovr)
    find_program(LCOV_EXECUTABLE lcov)
    find_program(GENHTML_EXECUTABLE genhtml)
    
    if(GCOVR_EXECUTABLE)
        # gcovr-based coverage reporting
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${GCOVR_EXECUTABLE} --root ${CMAKE_SOURCE_DIR} 
                    --exclude '${CMAKE_SOURCE_DIR}/tests/.*' 
                    --exclude '${CMAKE_SOURCE_DIR}/build/.*'
                    --exclude '${CMAKE_SOURCE_DIR}/external/.*'
                    --print-summary
                    --html --html-details coverage_report.html
                    --xml coverage.xml
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report with gcovr (aiming for ≥85%)"
        )
        
        # Quick coverage check target
        add_custom_target(coverage-check
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${GCOVR_EXECUTABLE} --root ${CMAKE_SOURCE_DIR} 
                    --exclude '${CMAKE_SOURCE_DIR}/tests/.*' 
                    --exclude '${CMAKE_SOURCE_DIR}/build/.*'
                    --exclude '${CMAKE_SOURCE_DIR}/external/.*'
                    --print-summary
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Quick coverage summary"
        )
    elseif(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
        # lcov-based coverage reporting (fallback)
        add_custom_target(coverage
            COMMAND ${LCOV_EXECUTABLE} --directory . --zerocounters
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV_EXECUTABLE} --directory . --capture --output-file coverage.info
            COMMAND ${LCOV_EXECUTABLE} --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage_filtered.info
            COMMAND ${GENHTML_EXECUTABLE} coverage_filtered.info --output-directory coverage_html
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report with lcov"
        )
        
        add_custom_target(coverage-check
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV_EXECUTABLE} --directory . --capture --output-file coverage.info
            COMMAND ${LCOV_EXECUTABLE} --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage_filtered.info
            COMMAND ${LCOV_EXECUTABLE} --list coverage_filtered.info
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Quick coverage summary with lcov"
        )
    else()
        message(STATUS "Coverage enabled but neither gcovr nor lcov found. Install gcovr with: pip install gcovr")
    endif()
endif()

# Memory checking with Valgrind (Linux)
if(UNIX AND NOT APPLE)
    find_program(VALGRIND_EXECUTABLE valgrind)
    if(VALGRIND_EXECUTABLE)
        add_custom_target(memcheck
            COMMAND ${CMAKE_CTEST_COMMAND} -T memcheck --output-on-failure
            DEPENDS MurmurTests
            COMMENT "Running memory check with Valgrind"
        )
    endif()
endif()