QT += core gui widgets

CONFIG += c++17

QMAKE_PROJECT_DEPTH = 0


SOURCES += main.cpp mainwindow.cpp metrology.cpp
HEADERS += mainwindow.h metrology.h

isEmpty(OPENCV_DIR): OPENCV_DIR = D:/librerie/opencv_4_11_0/build
INCLUDEPATH += $$OPENCV_DIR/include
win32-msvc {
    CONFIG(debug, debug|release) {
        LIBS += $$OPENCV_DIR/x64/vc16/lib/opencv_world4110d.lib
        OPENCV_DLL = $$OPENCV_DIR/x64/vc16/bin/opencv_world4110d.dll
        OPENCV_DEST = $$OUT_PWD/debug
    } else {
        LIBS += $$OPENCV_DIR/x64/vc16/lib/opencv_world4110.lib
        OPENCV_DLL = $$OPENCV_DIR/x64/vc16/bin/opencv_world4110.dll
        OPENCV_DEST = $$OUT_PWD/release
    }
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$OPENCV_DLL)) $$shell_quote($$shell_path($$OPENCV_DEST))
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += opencv4
}
