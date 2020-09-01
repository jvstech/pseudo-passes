add_portable_llvm_plugin(stack-to-global
  stack-to-global.cpp
  
  LINK_LIBS
  demote-registers-lib
  support
  )
