# FFmpeg (avcodec, avutil, swresample) is a hard dependency of fooyin itself, so it has
# already been located by the top-level CMakeLists.txt.
if(NOT FFMPEG_FOUND)
    message(STATUS "FFmpeg not found; skipping sacdiso plugin.")
    return()
endif()

create_fooyin_plugin_internal(
    sacdiso
    DEPENDS Fooyin::Core
            Fooyin::Gui
            ${FFMPEG_LIBRARIES}
    SOURCES sacdimage.cpp
            sacdimage.h
            sacdinput.cpp
            sacdinput.h
            sacdisoplugin.cpp
            sacdisoplugin.h
            sacdisosettings.h
            sacdisosettingsdialog.cpp
            sacdisosettingsdialog.h
)

target_include_directories(sacdiso SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(sacdiso PRIVATE ${FFMPEG_LIBRARY_DIRS})
