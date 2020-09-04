add_executable(resize-malloc-test
  resize-malloc-test.cpp
  )

set_target_properties(resize-malloc-test
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

if (MSVC)
  target_compile_definitions(resize-malloc-test 
    PUBLIC _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
endif()
