add_executable(depotdl cli.cpp)

target_include_directories(depotdl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)

target_link_libraries(depotdl PRIVATE DepotDL::DepotDL)