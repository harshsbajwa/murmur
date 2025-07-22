find_package(Qt6 REQUIRED COMPONENTS Test Concurrent Network Multimedia Sql Quick Qml)

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
    test_video_processing_integration.cpp
    
    # Unit tests - Transcription
    unit/transcription/TestWhisperEngine.cpp
    
    # Unit tests - Media components
    test_ffmpeg_wrapper.cpp
    # ********* Start testing of TestFFmpegWrapper *********
    # Config: Using QtTest library 6.8.3, Qt 6.8.3 (arm64-little_endian-lp64 shared (dynamic) release build; by Apple LLVM 15.0.0 (clang-1500.3.9.4)), macos 15.4
    # PASS   : TestFFmpegWrapper::initTestCase()
    # [07:04:37] [info] [12320884] FFmpegWrapper initialized
    # [07:04:38] [info] [12320884] FFmpegWrapper destroyed
    # PASS   : TestFFmpegWrapper::testVideoAnalysis()
    # [07:04:38] [info] [12320884] FFmpegWrapper initialized
    # [07:04:38] [debug] [12320942] Conversion options validated successfully - Video: libx264, Audio: aac, VideoBitrate: 1000, AudioBitrate: 128
    # [07:04:38] [debug] [12320942] performVideoConversion called with operationId: 61b50178-ee78-4d32-944e-e863d9b1d672
    # [07:04:38] [debug] [12320942] Current active operations count: 1
    # [07:04:38] [debug] [12320942] Active operation: 61b50178-ee78-4d32-944e-e863d9b1d672
    # [07:04:38] [debug] [12320942] Opening input file: /private/var/folders/jr/kx5l9zwx0nz1vpy63ny6zjy00000gn/T/MurmurTests-vPaERB/test_video.mp4
    # [07:04:38] [debug] [12320942] Finding stream info for input file
    # [07:04:38] [debug] [12320942] Setting up output context for: /private/var/folders/jr/kx5l9zwx0nz1vpy63ny6zjy00000gn/T/MurmurTests-vPaERB/converted_video.mp4
    # [07:04:38] [debug] [12320942] Opening video encoder: libx264
    # [libx264 @ 0x121954ad0] using SAR=1/1
    # [libx264 @ 0x121954ad0] using cpu capabilities: ARMv8 NEON
    # [libx264 @ 0x121954ad0] profile High, level 3.0, 4:2:0, 8-bit
    # [07:04:38] [debug] [12320942] Opening audio encoder: aac
    # [07:04:38] [debug] [12320942] Setting up audio resampler
    # [07:04:38] [debug] [12320942] Initializing audio resampler
    # [07:04:38] [debug] [12320942] Opening output file for writing
    # [07:04:38] [debug] [12320942] Writing output header
    # [mp4 @ 0x121955770] Timestamps are unset in a packet for stream 1. This is deprecated and will stop working in the future. Fix your code to set the timestamps properly
    # [mp4 @ 0x121955770] Encoder did not produce proper pts, making some up.
    # [libx264 @ 0x121954ad0] forced frame type (5) at 147 was changed to frame type (3)
    # [libx264 @ 0x121954ad0] frame I:1     Avg QP:14.66  size:  4846
    # [libx264 @ 0x121954ad0] frame P:23    Avg QP:13.38  size:   365
    # [libx264 @ 0x121954ad0] frame B:69    Avg QP:14.08  size:    71
    # [libx264 @ 0x121954ad0] consecutive B-frames:  1.1%  0.0%  0.0% 98.9%
    # [libx264 @ 0x121954ad0] mb I  I16..4: 71.4% 18.4% 10.2%
    # [libx264 @ 0x121954ad0] mb P  I16..4:  2.0%  1.6%  0.4%  P16..4:  5.4%  1.4%  0.1%  0.0%  0.0%    skip:89.1%
    # [libx264 @ 0x121954ad0] mb B  I16..4:  0.2%  0.0%  0.0%  B16..8:  2.1%  0.0%  0.0%  direct: 0.0%  skip:97.6%  L0:46.8% L1:52.6% BI: 0.6%
    # [libx264 @ 0x121954ad0] 8x8 transform intra:28.0% inter:84.1%
    # [libx264 @ 0x121954ad0] coded y,uvDC,uvAC intra: 7.4% 2.6% 1.3% inter: 0.1% 0.1% 0.0%
    # [libx264 @ 0x121954ad0] i16 v,h,dc,p: 83% 10%  3%  5%
    # [libx264 @ 0x121954ad0] i8 v,h,dc,ddl,ddr,vr,hd,vl,hu: 48% 27% 21%  1%  0%  0%  0%  0%  2%
    # [libx264 @ 0x121954ad0] i4 v,h,dc,ddl,ddr,vr,hd,vl,hu: 37% 40% 16%  2%  1%  1%  0%  2%  0%
    # [libx264 @ 0x121954ad0] i8c dc,h,v,p: 93%  1%  6%  0%
    # [libx264 @ 0x121954ad0] Weighted P-Frames: Y:0.0% UV:0.0%
    # [libx264 @ 0x121954ad0] ref P L0: 81.6%  2.0% 14.2%  2.2%
    # [libx264 @ 0x121954ad0] ref B L0: 61.9% 36.8%  1.4%
    # [libx264 @ 0x121954ad0] ref B L1: 98.1%  1.9%
    # [libx264 @ 0x121954ad0] kb/s:46.81
    # [aac @ 0x1219896e0] Qavg: 204.028
    # [aac @ 0x1219896e0] 1 frames left in the queue on closing
    # FAIL!  : TestFFmpegWrapper::testVideoConversion() Caught unhandled exception
    # Loc: [/Users/qt/work/qt/qtbase/src/testlib/qtestcase.cpp(1982)]
    # Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 2703ms
    # ********* Finished testing of TestFFmpegWrapper *********
    # libc++abi: terminating due to uncaught exception of type std::runtime_error: Expected contains value, not error
    # zsh: abort      ./MurmurTests
    
    # Unit tests - Storage components
    test_storage_manager.cpp
    
    # Unit tests - Security components
    test_security_components.cpp
    
    # Real media tests with full functionality
    test_simple_real_media.cpp
    test_real_media_processing.cpp
    test_performance_benchmarks.cpp
    # UI flow tests
    test_ui_flows.cpp
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

# Main comprehensive test
add_test(NAME MurmurComprehensiveTests COMMAND MurmurTests)
set_tests_properties(MurmurComprehensiveTests PROPERTIES
    TIMEOUT 300
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Test data setup
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_data)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_data/media)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_data/databases)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test_data/torrents)

# Custom test targets
add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --parallel 4
    DEPENDS MurmurTests
    COMMENT "Running all tests"
)

# Code coverage support
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MurmurCore PRIVATE --coverage)
    target_link_options(MurmurCore PRIVATE --coverage)
    
    find_program(LCOV_EXECUTABLE lcov)
    find_program(GENHTML_EXECUTABLE genhtml)
    
    if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
        add_custom_target(coverage
            COMMAND ${LCOV_EXECUTABLE} --directory . --zerocounters
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV_EXECUTABLE} --directory . --capture --output-file coverage.info
            COMMAND ${LCOV_EXECUTABLE} --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage_filtered.info
            COMMAND ${GENHTML_EXECUTABLE} coverage_filtered.info --output-directory coverage_html
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report"
        )
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