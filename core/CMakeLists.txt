add_library(chopper_core STATIC
  src/WavIO.cpp
  src/Fft.cpp
  src/TransientDetector.cpp
  src/Slicer.cpp
  src/Naming.cpp
  src/Tempo.cpp
  src/Exporters.cpp
  src/ChopSession.cpp
)
target_include_directories(chopper_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(chopper_core PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(chopper_core PRIVATE /W4)
else()
  target_compile_options(chopper_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
