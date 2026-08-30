QT += core gui widgets network

CONFIG += c++17 warn_on
TARGET   = SalerPromts
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

msvc {
    QMAKE_CXXFLAGS += /utf-8 /Zm400
}

win32: LIBS += -liphlpapi -lws2_32

SRC_DIR = $$PWD
SRC_DIR ~= s,\\\\,/,g
DEFINES += SALER_SOURCE_DIR=\\\"$$SRC_DIR\\\"

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/core/apppaths.cpp \
    src/core/appsettings.cpp \
    src/core/catalogs.cpp \
    src/core/sessionstore.cpp \
    src/core/childcleanup.cpp \
    src/services/localllm.cpp \
    src/services/dialogengine.cpp \
    src/services/analyzer.cpp \
    src/tabs/dialogtab.cpp \
    src/tabs/analysistab.cpp \
    src/tabs/resultstab.cpp \
    src/tabs/settingstab.cpp

HEADERS += \
    src/mainwindow.h \
    src/core/types.h \
    src/core/apppaths.h \
    src/core/appsettings.h \
    src/core/catalogs.h \
    src/core/sessionstore.h \
    src/core/childcleanup.h \
    src/services/localllm.h \
    src/services/dialogengine.h \
    src/services/analyzer.h \
    src/tabs/dialogtab.h \
    src/tabs/analysistab.h \
    src/tabs/resultstab.h \
    src/tabs/settingstab.h

DATA_SRC = $$shell_path($$PWD/data)
DATA_DST = $$shell_path($$OUT_PWD/data)
DATA_EXE = $$shell_path($$OUT_PWD/release/data)
QMAKE_POST_LINK += $$quote(if not exist \"$$DATA_DST\" mkdir \"$$DATA_DST\") & $$quote(xcopy /E /I /Y \"$$DATA_SRC\" \"$$DATA_DST\") & $$quote(if not exist \"$$DATA_EXE\" mkdir \"$$DATA_EXE\") & $$quote(xcopy /E /I /Y \"$$DATA_SRC\" \"$$DATA_EXE\")
