cmake_minimum_required(VERSION 3.21)

project(DepotDL VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

## == Build Options == ##
option(BUILD_BINARY "Build depot downloader CLI tool" OFF)

## == Dependencies == ##
include(cmake/dependencies.cmake)

## == Sources == ##
include(cmake/sources.cmake)

add_library(DepotDL
    ${DEPOTDL_SOURCES}
)

## == Including / Linking == ##
target_include_directories(DepotDL
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external
)

target_link_libraries(DepotDL
    PUBLIC
        Boost::system
    PRIVATE
        CURL::libcurl
        ${CRYPTOPP_LIB}
        ZLIB::ZLIB
)

## == CLI == ##
if(BUILD_BINARY)
    add_subdirectory(cli)
endif()

## == PACKAGING == ##
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/DepotDLConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    cmake/DepotDLConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/DepotDLConfig.cmake
    INSTALL_DESTINATION lib/cmake/DepotDL
)

install(TARGETS DepotDL
    EXPORT DepotDLTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT DepotDLTargets
    FILE DepotDLTargets.cmake
    NAMESPACE DepotDL::
    DESTINATION lib/cmake/DepotDL
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/DepotDLConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/DepotDLConfigVersion.cmake
    DESTINATION lib/cmake/DepotDL
)

add_library(DepotDL::DepotDL ALIAS DepotDL)