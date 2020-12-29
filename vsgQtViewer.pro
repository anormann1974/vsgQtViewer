QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
DESTDIR = bin

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    MainWindow.cpp \
    vulkanwindow.cpp

HEADERS += \
    MainWindow.h \
    vulkanwindow.h

FORMS += \
    MainWindow.ui

win32 {
    DEFINES *= VK_USE_PLATFORM_WIN32_KHR
    INCLUDEPATH *= C:/Developer/vcpkg/installed/x64-windows/include
    CONFIG(debug, debug|release) {
        LIBS *= -L/Developer/vcpkg/installed/x64-windows/debug/lib -LC:/VulkanSDK/1.2.162.0/Lib -lvsgd -lvulkan-1 -lGenericCodeGend -lMachineIndependentd -lOGLCompilerd -lOSDependentd -lglslangd -lspirv-cross-cd -lspirv-cross-cored -lspirv-cross-cppd -lspirv-cross-glsld -lspirv-cross-reflectd -lspirv-cross-utild -lSPIRVd -lSPIRV-Tools-optd -lSPIRV-Toolsd
    } else {
        LIBS *= -L/Developer/vcpkg/installed/x64-windows/lib -LC:/VulkanSDK/1.2.162.0/Lib -lvsg -lvulkan-1 -lGenericCodeGen -lMachineIndependent -lOGLCompiler -lOSDependent -lglslang -lspirv-cross-c -lspirv-cross-core -lspirv-cross-cpp -lspirv-cross-glsl -lspirv-cross-reflect -lspirv-cross-util -lSPIRV -lSPIRV-Tools-opt -lSPIRV-Tools
    }

    QT_BIN_DIR = $$dirname(QMAKE_QMAKE)

    contains(TEMPLATE, app) {
        MY_TARGET_EXT = .exe
        MY_TARGET_DIR = $${DESTDIR}
    }

    DEPLOY_TARGET = $$shell_quote($${DESTDIR}/$${TARGET}$${MY_TARGET_EXT})
    #QMAKE_POST_LINK += windeployqt --no-opengl-sw --no-angle --no-system-d3d-compiler --no-compiler-runtime --dir $${MY_TARGET_DIR} --plugindir $${MY_TARGET_DIR}/plugins $${DEPLOY_ARGS} $${DEPLOY_TARGET} $$escape_expand(\n\t)
    QMAKE_POST_LINK += $$QT_BIN_DIR\\windeployqt --no-compiler-runtime --dir $${MY_TARGET_DIR} --plugindir $${MY_TARGET_DIR}/plugins $${DEPLOY_ARGS} $${DEPLOY_TARGET} $$escape_expand(\\n\\t)
}
