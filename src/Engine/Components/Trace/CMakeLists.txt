cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

set(ENGINE_COMPONENTS_TRACE_SOURCES
        EngineTraceSimpleRecorder.cpp
        EngineTraceSimplePlayer.cpp
        EngineTraceStateAccessor.cpp
        EngineTracePlayer.cpp
        EngineTraceRecorder.cpp)

set(ENGINE_COMPONENTS_TRACE_HEADERS
        EngineTraceSimpleRecorder.h
        EngineTraceSimplePlayer.h
        EngineTraceStateAccessor.h
        EngineTraceEnums.h
        EngineTracePlayer.h
        EngineTraceRecorder.h)

add_library(engine_components_trace STATIC ${ENGINE_COMPONENTS_TRACE_SOURCES} ${ENGINE_COMPONENTS_TRACE_HEADERS})
target_check_style(engine_components_trace)

target_link_libraries(engine_components_trace
        PUBLIC
        engine
        engine_components_control
        engine_components_deterministic
        library_application
        library_random
        library_trace
        platform
        utility)
