add_portable_llvm_plugin(demote-registers
  demote-registers.cpp
  pass-registration/pass-registration.cpp
  
  LINK_LIBS
  support
  )

add_llvm_library(demote-registers-lib
  demote-registers.cpp
  
  LINK_LIBS
  support
  )

set_target_properties(demote-registers-lib
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
if (MSVC)
  target_compile_definitions(demote-registers-lib
    PUBLIC _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
endif()
