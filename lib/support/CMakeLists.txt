add_llvm_library(support
  metadata-util.cpp
  pass-pipeline.cpp
  value-util.cpp
  )

set_target_properties(support
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

if (MSVC)
  target_compile_definitions(support 
    PUBLIC _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
endif()
