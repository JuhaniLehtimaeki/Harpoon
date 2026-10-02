add_library(harpoon-core STATIC
    src/model/error.h
    src/model/release.h
    src/model/appsettings.h
    src/model/appsettings.cpp
    src/version/versioncompare.h
    src/version/versioncompare.cpp
    src/version/rpmversion.h
    src/version/rpmversion.cpp
    src/version/versionextractor.h
    src/version/versionextractor.cpp
    src/net/httptransport.h
    src/net/networktransport.h
    src/net/networktransport.cpp
    src/net/ratelimit.h
    src/net/ratelimit.cpp
    src/sources/source.h
    src/sources/source.cpp
    src/sources/githubsource.h
    src/sources/githubsource.cpp
    src/sources/forgejosource.h
    src/sources/forgejosource.cpp
    src/sources/sourceregistry.h
    src/sources/sourceregistry.cpp
    src/pipeline/deviceinfo.h
    src/pipeline/deviceinfo.cpp
    src/pipeline/assetfilter.h
    src/pipeline/assetfilter.cpp
    src/pipeline/releaseselector.h
    src/pipeline/releaseselector.cpp
    src/pipeline/releasepipeline.h
    src/pipeline/releasepipeline.cpp
)
target_include_directories(harpoon-core PUBLIC src)
target_link_libraries(harpoon-core PUBLIC Qt5::Core Qt5::Network)
target_compile_definitions(harpoon-core PUBLIC
    HARPOON_VERSION="${PROJECT_VERSION}")
