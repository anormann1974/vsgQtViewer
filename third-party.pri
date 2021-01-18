isEmpty(THIRDPARTY_PRI_INCLUDED) {

THIRDPARTY_PRI_INCLUDED = 1

!exists(third-party.pri.user) {
    error("No user third-party include was found!")
}

include(third-party.pri.user)

vulkan {
    INCLUDEPATH *= $$VULKAN_INCLUDE
    win32 {
        CONFIG(debug, debug|release) {
            LIBS *= -L$$VULKAN_LIBRARY_DBG
            LIBS *= -lvulkan-1 -lGenericCodeGend -lMachineIndependentd -lOGLCompilerd -lOSDependentd -lglslangd -lspirv-cross-cd -lspirv-cross-cored -lspirv-cross-cppd -lspirv-cross-glsld -lspirv-cross-reflectd -lspirv-cross-utild -lSPIRVd -lSPIRV-Tools-optd -lSPIRV-Toolsd
        } else {
            LIBS *= -L$$VULKAN_LIBRARY_REL
            LIBS *= -lvulkan-1 -lGenericCodeGen -lMachineIndependent -lOGLCompiler -lOSDependent -lglslang -lspirv-cross-c -lspirv-cross-core -lspirv-cross-cpp -lspirv-cross-glsl -lspirv-cross-reflect -lspirv-cross-util -lSPIRV -lSPIRV-Tools-opt -lSPIRV-Tools
        }
    }
}

vsg {
    win32 {
        DEFINES *= VK_USE_PLATFORM_WIN32_KHR NOMINMAX
        INCLUDEPATH *= $$VSG_INCLUDE

        CONFIG(debug, debug|release) {
            LIBS *= -L$$VSG_LIBRARY_DBG
            LIBS *= -lvsgd
        } else {
            LIBS *= -L$$VSG_LIBRARY_REL
            LIBS *= -lvsg
        }
    }
}

pvrtextool {
    win32 {
        PVR_DIR = $$quote(C:/Imagination Technologies/PowerVR_Graphics/PowerVR_Tools/PVRTexTool/Library)
        INCLUDEPATH *= $${PVR_DIR}/include
        #DEFINES *= PVR_DLL=__declspec(dllimport)
        LIBS *= -L$${PVR_DIR}/Windows_x86_64 -lPVRTexLib
    }
}

assimp {
    win32 {
        INCLUDEPATH *= $$ASSIMP_INCLUDE

        CONFIG(debug, debug|release) {
            LIBS *= -L$$ASSIMP_LIBRARY_DBG -L$$ASSIMP_BINARY_DBG
            LIBS *= -lassimp-vc142-mtd -lzlibstaticd -lIrrXMLd
        } else {
            LIBS *= -L$$ASSIMP_LIBRARY_REL -L$$ASSIMP_BINARY_REL
            LIBS *= -lassimp-vc142-mt -lzlibstatic -lIrrXML
        }
    }
}

}
