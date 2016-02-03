#-------------------------------------------------
#
# Project created by QtCreator 2015-03-13T11:19:13
#
#-------------------------------------------------

DESTDIR = ../bin

QT       += core xml
QT       -= gui


TARGET = progen
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app

macx {
CONFIG += c++11
DEFINES += HAVE_PTHREAD
QMAKE_CFLAGS += -gdwarf-2
QMAKE_CXXFLAGS += -gdwarf-2
debug { DEFINES += _DEBUG }
}


SOURCES += main.cpp
