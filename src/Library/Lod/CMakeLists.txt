cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

set(LIBRARY_LOD_SOURCES
        LodReader.cpp
        LodEnums.cpp
        LodSnapshots.cpp)

set(LIBRARY_LOD_HEADERS
        LodReader.h
        LodEnums.h
        LodSnapshots.h)

add_library(library_lod STATIC ${LIBRARY_LOD_SOURCES} ${LIBRARY_LOD_HEADERS})
target_link_libraries(library_lod PUBLIC library_lodformats library_serialization library_binary utility)
target_check_style(library_lod)
