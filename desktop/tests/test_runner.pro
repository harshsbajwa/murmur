QT += core testlib concurrent
CONFIG += console c++20
TARGET = test_runner
TEMPLATE = app

SOURCES += test_runner_standalone.cpp

# Test configuration
CONFIG += testcase
CONFIG += no_testcase_installs