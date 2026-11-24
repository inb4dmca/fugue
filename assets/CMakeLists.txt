# Qt-free asset decoding and pixel operations; no scene/model ownership.
file(GLOB asset_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(fugue_assets STATIC ${asset_srcs})
target_include_directories(fugue_assets PUBLIC ${PROJECT_SOURCE_DIR})
find_package(JPEG REQUIRED)
target_link_libraries(fugue_assets PUBLIC JPEG::JPEG)
# Keep reference pixel arithmetic reproducible.
if(MSVC)
  target_compile_options(fugue_assets PRIVATE /fp:strict)
else()
  target_compile_options(fugue_assets PRIVATE -ffp-contract=off)
endif()
