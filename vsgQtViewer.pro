QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
DESTDIR = bin

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/VulkanWindow.cpp

HEADERS += \
    src/MainWindow.h \
    src/VulkanWindow.h

FORMS += \
    src/MainWindow.ui

win32 {
    DEFINES *= VK_USE_PLATFORM_WIN32_KHR
    INCLUDEPATH *= $$(VSG_ROOT)/include
    CONFIG(debug, debug|release) {
        LIBS *= -L$$(VSG_ROOT)/debug/lib -L$$(VULKAN_SDK)/Lib -lvsgd -lvulkan-1 -lGenericCodeGend -lMachineIndependentd -lOGLCompilerd -lOSDependentd -lglslangd -lspirv-cross-cd -lspirv-cross-cored -lspirv-cross-cppd -lspirv-cross-glsld -lspirv-cross-reflectd -lspirv-cross-utild -lSPIRVd -lSPIRV-Tools-optd -lSPIRV-Toolsd
    } else {
        LIBS *= -L$$(VSG_ROOT)/lib -L$$(VULKAN_SDK)/Lib -lvsg -lvulkan-1 -lGenericCodeGen -lMachineIndependent -lOGLCompiler -lOSDependent -lglslang -lspirv-cross-c -lspirv-cross-core -lspirv-cross-cpp -lspirv-cross-glsl -lspirv-cross-reflect -lspirv-cross-util -lSPIRV -lSPIRV-Tools-opt -lSPIRV-Tools
    }

    #QT_BIN_DIR = $$dirname(QMAKE_QMAKE)
    #DEPLOY_TARGET = $$shell_quote($${DESTDIR}/$${TARGET}.exe)
    #QMAKE_POST_LINK += $$QT_BIN_DIR\\windeployqt --no-compiler-runtime --dir $${DESTDIR} --plugindir $${DESTDIR}/plugins $${DEPLOY_ARGS} $${DEPLOY_TARGET} $$escape_expand(\\n\\t)
}

unix {
    DEFINES *= VK_USE_PLATFORM_XLIB_KHR
    INCLUDEPATH *= $$(VSG_ROOT)/include $$(VULKAN_SDK)/include
    LIBS *= -L$$(VSG_ROOT)/lib -L$$(VULKAN_SDK)/lib -lvsg -lvulkan-1 -lGenericCodeGen -lMachineIndependent -lOGLCompiler -lOSDependent -lglslang -lspirv-cross-c -lspirv-cross-core -lspirv-cross-cpp -lspirv-cross-glsl -lspirv-cross-reflect -lspirv-cross-util -lSPIRV -lSPIRV-Tools-opt -lSPIRV-Tools
}
