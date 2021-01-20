QT += core gui widgets
CONFIG += c++17 vulkan vsg assimp
DESTDIR = bin

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include(third-party.pri)

DEFINES *= KHRONOS_STATIC LIBKTX
INCLUDEPATH *= third-party/stb third-party/ktx/include third-party/ktx/other_include third-party/ktx/other_include/KHR
DEPENDPATH *= data

SOURCES += \
    # libktx
    third-party/ktx/lib/checkheader.c \
    third-party/ktx/lib/filestream.c \
    third-party/ktx/lib/hashlist.c \
    third-party/ktx/lib/hashtable.c \
    third-party/ktx/lib/memstream.c \
    third-party/ktx/lib/swap.c \
    third-party/ktx/lib/texture.c \
    third-party/ktx/lib/vkloader.c \
#    third-party/ktx2/lib/checkheader.c \
#    third-party/ktx2/lib/filestream.c \
#    third-party/ktx2/lib/hashlist.c \
#    third-party/ktx2/lib/memstream.c \
#    third-party/ktx2/lib/swap.c \
#    third-party/ktx2/lib/texture.c \
#    third-party/ktx2/lib/texture1.c \
#    third-party/ktx2/lib/texture2.c \
#    third-party/ktx2/lib/vk_funcs.c \
#    third-party/ktx2/lib/vkloader.c \
#    third-party/ktx2/lib/zstddeclib.c \
#    third-party/ktx2/lib/dfdutils/vk2dfd.c \
#    third-party/ktx2/lib/dfdutils/interpretdfd.c \
#    third-party/ktx2/lib/dfdutils/createdfd.c \
#    third-party/ktx2/lib/dfdutils/colourspaces.c \
#    third-party/ktx2/lib/dfdutils/queries.c \
    # vsgQtViewer
    src/ReaderWriterKTX.cpp \
    src/ReaderWriterAssimp.cpp \
    src/ReaderWriterStbi.cpp \
    src/SkyBox.cpp \
    src/main.cpp \
    src/MainWindow.cpp \
    src/VulkanWindow.cpp

HEADERS += \
    src/MainWindow.h \
    src/ReaderWriterAssimp.h \
    src/ReaderWriterKTX.h \
    src/ReaderWriterStbi.h \
    src/SkyBox.h \
    src/VulkanWindow.h

FORMS += \
    src/MainWindow.ui

GLSL_SOURCES *= \
    $$PWD/data/assimp_vertex.vert \
    $$PWD/data/assimp_phong.frag \
    $$PWD/data/assimp_pbr.frag \
    $$PWD/data/marmorset_pbr.frag \
    $$PWD/data/skybox_vert.vert \
    $$PWD/data/skybox_frag.frag

SPIRV.commands = $$(VULKAN_SDK)/bin/glslangValidator --quiet --target-env vulkan1.1 --vn ${QMAKE_FILE_IN_BASE} -o ${QMAKE_FILE_OUT} ${QMAKE_FILE_IN}
SPIRV.output = $$OUT_PWD/data/${QMAKE_FILE_IN_BASE}.h
SPIRV.input = GLSL_SOURCES
SPIRV.name = glslangValidator ${QMAKE_FILE_IN}
SPIRV.depends = $$GLSL_SOURCES
SPIRV.variable_out = COMPILED_SHADERS
SPIRV.CONFIG = target_predeps
QMAKE_EXTRA_COMPILERS += SPIRV

win32 {
    QT_BIN_DIR = $$dirname(QMAKE_QMAKE)
    DEPLOY_TARGET = $$shell_quote($${DESTDIR}/$${TARGET}.exe)
    QMAKE_POST_LINK += $$QT_BIN_DIR\\windeployqt --no-compiler-runtime --dir $${DESTDIR} --plugindir $${DESTDIR}/plugins $${DEPLOY_ARGS} $${DEPLOY_TARGET} $$escape_expand(\\n\\t)
}
