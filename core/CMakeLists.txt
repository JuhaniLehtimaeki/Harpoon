# zlib inflates rpm-md primary.xml.gz (RpmMdRepoSource).
find_package(ZLIB REQUIRED)
# libcrypto verifies Sigstore signatures on GitHub attestations.
find_package(OpenSSL REQUIRED)

# Sigstore's public-good trusted root, compiled in. Update it from
# https://github.com/sigstore/root-signing (targets/trusted_root.json).
set(HARPOON_SIGSTORE_ROOT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/data/sigstore-trusted-root.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HARPOON_SIGSTORE_ROOT_FILE})
file(READ ${HARPOON_SIGSTORE_ROOT_FILE} HARPOON_SIGSTORE_TRUSTED_ROOT)
configure_file(src/verify/sigstoretrustedroot.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/sigstoretrustedroot.cpp @ONLY)

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
    src/net/downloader.h
    src/net/downloader.cpp
    src/net/ratelimit.h
    src/net/ratelimit.cpp
    src/sources/source.h
    src/sources/source.cpp
    src/sources/githubsource.h
    src/sources/githubsource.cpp
    src/sources/githubattestation.h
    src/sources/githubattestation.cpp
    src/sources/forgejosource.h
    src/sources/forgejosource.cpp
    src/sources/sourceutil.h
    src/sources/sourceutil.cpp
    src/sources/gitlabsource.h
    src/sources/gitlabsource.cpp
    src/sources/sourcehutsource.h
    src/sources/sourcehutsource.cpp
    src/sources/sourceforgesource.h
    src/sources/sourceforgesource.cpp
    src/sources/jenkinssource.h
    src/sources/jenkinssource.cpp
    src/sources/htmlsource.h
    src/sources/htmlsource.cpp
    src/sources/directlinksource.h
    src/sources/directlinksource.cpp
    src/sources/rpmmdreposource.h
    src/sources/rpmmdreposource.cpp
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
    src/pkg/processrunner.h
    src/pkg/processrunner.cpp
    src/pkg/rpminspector.h
    src/pkg/rpminspector.cpp
    src/pkg/packagebackend.h
    src/pkg/packagekitbackend.h
    src/pkg/packagekitbackend.cpp
    src/pkg/installhandlerbackend.h
    src/pkg/installhandlerbackend.cpp
    src/app/app.h
    src/app/app.cpp
    src/app/appstore.h
    src/app/appstore.cpp
    src/app/updatestatus.h
    src/app/updatestatus.cpp
    src/app/appchecker.h
    src/app/appchecker.cpp
    src/app/appinstaller.h
    src/app/appinstaller.cpp
    src/app/harpoonsettings.h
    src/app/harpoonsettings.cpp
    src/app/updatenotifications.h
    src/app/updatenotifications.cpp
    src/app/backgroundscheduler.h
    src/app/backgroundscheduler.cpp
    src/app/backup.h
    src/app/backup.cpp
    src/app/addlink.h
    src/app/addlink.cpp
    src/app/autoupdate.h
    src/app/autoupdate.cpp
    src/app/installedmatch.h
    src/app/installedmatch.cpp
    src/app/appidentity.h
    src/app/appidentity.cpp
    src/app/appservice.h
    src/app/appservice.cpp
    src/notify/notifier.h
    src/notify/notifier.cpp
    src/verify/sigstore.h
    src/verify/sigstore.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/sigstoretrustedroot.cpp
)
target_include_directories(harpoon-core PUBLIC src)
target_link_libraries(harpoon-core PUBLIC Qt5::Core Qt5::Network Qt5::DBus)
target_link_libraries(harpoon-core PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
target_compile_definitions(harpoon-core PUBLIC
    HARPOON_VERSION="${PROJECT_VERSION}")
