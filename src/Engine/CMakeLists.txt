cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

set(ENGINE_SOURCES
        AssetsManager.cpp
        AttackList.cpp
        Conditions.cpp
        Engine.cpp
        EngineGlobals.cpp
        ErrorHandling.cpp
        EngineIocContainer.cpp
        LOD.cpp
        LodTextureCache.cpp
        LodSpriteCache.cpp
        Localization.cpp
        MapEnums.cpp
        MapInfo.cpp
        OurMath.cpp
        Party.cpp
        PriceCalculator.cpp
        SaveLoad.cpp
        SpellFxRenderer.cpp
        Time.cpp
        GameResourceManager.cpp
        mm7_data.cpp
        mm7text_ru.cpp)

set(ENGINE_HEADERS
        AssetsManager.h
        AttackList.h
        Conditions.h
        Engine.h
        EngineGlobals.h
        ErrorHandling.h
        EngineIocContainer.h
        LOD.h
        LodTextureCache.h
        LodSpriteCache.h
        Localization.h
        MM7.h
        MapEnums.h
        MapInfo.h
        OurMath.h
        Party.h
        PartyEnums.h
        Pid.h
        PriceCalculator.h
        SaveLoad.h
        SpellFxRenderer.h
        Time.h
        GameResourceManager.h
        mm7_data.h
        stru160.h
        stru314.h)

add_library(engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})
target_check_style(engine)

target_link_libraries(engine
        PUBLIC
        engine_graphics
        engine_objects
        engine_serialization
        engine_spells
        engine_tables
        engine_turnengine
        engine_events
        library_compression
        library_logger
        library_serialization
        library_color
        library_lodformats
        utility)

target_compile_definitions(engine PRIVATE
        PROJECT_VERSION="${PROJECT_VERSION}"
        BUILD_COMPILER="${BUILD_COMPILER}"
        BUILD_PLATFORM="${BUILD_PLATFORM}"
        BUILD_TYPE="${BUILD_TYPE}")

add_subdirectory(Graphics)
add_subdirectory(Objects)
add_subdirectory(Snapshots)
add_subdirectory(Spells)
add_subdirectory(Tables)
add_subdirectory(Components)
add_subdirectory(TurnEngine)
add_subdirectory(Events)
