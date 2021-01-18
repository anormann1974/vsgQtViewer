QT += core gui widgets
CONFIG += c++17 vulkan vsg assimp pvrtextool
DESTDIR = bin

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include(third-party.pri)

INCLUDEPATH *= third-party/stb

SOURCES += \
    src/ReaderWriterAssimp.cpp \
    src/ReaderWriterStbi.cpp \
    src/main.cpp \
    src/MainWindow.cpp \
    src/VulkanWindow.cpp

HEADERS += \
    src/MainWindow.h \
    src/ReaderWriterAssimp.h \
    src/ReaderWriterStbi.h \
    src/VulkanWindow.h

FORMS += \
    src/MainWindow.ui

RESOURCES += \
    data/shaders.qrc

win32 {
    QT_BIN_DIR = $$dirname(QMAKE_QMAKE)
    DEPLOY_TARGET = $$shell_quote($${DESTDIR}/$${TARGET}.exe)
    QMAKE_POST_LINK += $$QT_BIN_DIR\\windeployqt --no-compiler-runtime --dir $${DESTDIR} --plugindir $${DESTDIR}/plugins $${DEPLOY_ARGS} $${DEPLOY_TARGET} $$escape_expand(\\n\\t)
}
