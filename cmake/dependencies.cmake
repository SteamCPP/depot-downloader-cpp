set(BOOST_OPTIONAL_COMPONENTS system)
if(BUILD_TESTING)
    list(APPEND BOOST_COMPONENTS unit_test_framework)
endif()

find_package(Boost REQUIRED OPTIONAL_COMPONENTS ${BOOST_OPTIONAL_COMPONENTS} COMPONENTS ${BOOST_COMPONENTS})

if(NOT TARGET Boost::system)
    add_library(Boost::system ALIAS Boost::headers)
endif()
find_package(CURL REQUIRED)
find_library(CRYPTOPP_LIB cryptopp)
find_package(ZLIB REQUIRED)