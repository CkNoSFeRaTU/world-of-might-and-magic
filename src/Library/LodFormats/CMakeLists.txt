cmake_minimum_required(VERSION 3.20.4 FATAL_ERROR)

set(LIBRARY_LOD_FORMATS_SOURCES
        LodFormats.cpp)

set(LIBRARY_LOD_FORMATS_HEADERS
        LodFormats.h
        LodFormatEnums.h
        LodFormatSnapshots.h)

add_library(library_lodformats STATIC ${LIBRARY_LOD_FORMATS_SOURCES} ${LIBRARY_LOD_FORMATS_HEADERS})
target_link_libraries(library_lodformats PUBLIC library_serialization library_binary library_snapshots utility)
target_check_style(library_lodformats)
