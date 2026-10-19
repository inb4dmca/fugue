# QRhi has limited compatibility guarantees. Keep its dependencies out of the CPU renderer and normal builds.
find_package(Qt6 6.7 REQUIRED COMPONENTS Gui ShaderTools Test)
# Older Qt SDKs export GuiPrivate from Gui; newer SDKs provide it as a separate component.
if(NOT TARGET Qt6::GuiPrivate)
  find_package(Qt6 6.7 REQUIRED COMPONENTS GuiPrivate)
endif()

fugue_module(fugue_rendergpu)
target_link_libraries(fugue_rendergpu PUBLIC Qt6::GuiPrivate)
target_compile_definitions(fugue_rendergpu INTERFACE FUGUE_GPU=1)
# Scalar filter kernels/geometry and inverse transforms follow the CPU reference's float64 preprocessing.
if(MSVC)
  target_compile_options(fugue_rendergpu PRIVATE /fp:strict)
else()
  target_compile_options(fugue_rendergpu PRIVATE -ffp-contract=off)
endif()
qt_add_shaders(fugue_rendergpu fugue_gpu_shaders
  PREFIX "/fugue/gpu"
  BASE "shaders"
  GLSL "150,300es"
  HLSL 50
  MSL 12
  FILES shaders/composite.vert shaders/composite.frag shaders/present.frag shaders/sprites.vert shaders/sprites.frag)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/executor_test.cpp)
  set(CMAKE_AUTOMOC ON)
  add_executable(gpu_executor_test tests/executor_test.cpp)
  target_link_libraries(gpu_executor_test PRIVATE fugue_rendergpu fugue_render fugue_model fugue_net fugue_proto
                                                 fugue_canon Qt6::Test)
  add_test(NAME gpu_executor COMMAND gpu_executor_test -o -,txt)
  set_tests_properties(gpu_executor PROPERTIES LABELS "gpu" TIMEOUT 120)
endif()
