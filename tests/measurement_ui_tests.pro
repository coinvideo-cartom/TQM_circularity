QT += core gui widgets
CONFIG += console c++17 release
CONFIG -= app_bundle debug
TARGET = measurement_ui_tests
INCLUDEPATH += .. D:/librerie/opencv_4_11_0/build/include
SOURCES += measurement_ui_tests.cpp ../mainwindow.cpp ../metrology.cpp
HEADERS += ../mainwindow.h ../metrology.h
LIBS += D:/librerie/opencv_4_11_0/build/x64/vc16/lib/opencv_world4110.lib
