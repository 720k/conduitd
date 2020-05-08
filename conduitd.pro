TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        Conduit.c \
        Service.c \
        __main.c \
        output-queue.c

HEADERS += \
    Conduit.h \
    Service.h \
    output-queue.h

DISTFILES += \
    info.txt \
    runConduit.ps1


CONFIG(debug, debug|release) {
    BUILDTYPE_STR="debug"
} else {
    BUILDTYPE_STR="release"
}
BUILD_BIN = "$$OUT_PWD/$${BUILDTYPE_STR}"

# COPY FILES to BUILD BIN DIRECTORY
defineTest(copyToBin) {
    files=$$1
    source_dir=$$2
    dest_dir=$$3
    for(file, files) {
        source_file = $$shell_path($$source_dir/$$file)
        dest_file = $$shell_path($$dest_dir/$$file)
        exists($$source_file) {
            message(\$ $$QMAKE_COPY  $$shell_quote($$source_file)  $$shell_quote($$dest_file))
            QMAKE_POST_LINK += $$QMAKE_COPY  $$shell_quote($$source_file)  $$shell_quote($$dest_file) $$escape_expand(\\n\\t)
        }
    }
    export(QMAKE_POST_LINK)
}

win32 {
    message(**** ConduitD: win32 $${BUILDTYPE_STR} ****)

    VCPKG_GLIB_PATH="C:/Developer/Tools/vcpkg-master/packages/glib_x64-windows"
    WINKIT_LIB_PATH="C:/Windows Kits/10/Lib/10.0.18362.0/um/x64"

    # compiling
    INCLUDEPATH += " $$VCPKG_GLIB_PATH/include"
    #linking
    LIBS += "$$VCPKG_GLIB_PATH/lib/glib-2.0.lib" \
            "$$VCPKG_GLIB_PATH/lib/gio-2.0.lib" \
            "$$VCPKG_GLIB_PATH/lib/gobject-2.0.lib" \
            "$$VCPKG_GLIB_PATH/lib/gthread-2.0.lib" \
            "$$VCPKG_GLIB_PATH/lib/gmodule-2.0.lib" \
            "$$WINKIT_LIB_PATH/mpr.lib" \
            "$$WINKIT_LIB_PATH/Advapi32.lib"

    #
    DLL_FILES = gio-2.dll glib-2.dll gmodule-2.dll gobject-2.dll gthread-2.dll libcharset.dll libiconv.dll libintl.dll pcre.dll zlib1.dll
    exists($${BUILD_BIN}) {
        copyToBin($$DLL_FILES, $$VCPKG_GLIB_PATH/tools/glib, $${BUILD_BIN})
        copyToBin(runConduit.ps1, $$PWD, $${BUILD_BIN})
    }
}





