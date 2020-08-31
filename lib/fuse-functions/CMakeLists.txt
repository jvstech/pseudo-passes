add_portable_llvm_plugin(fuse-functions
  fuse-functions.cpp
  combined-call-site.cpp
  demote-registers.cpp
  pass-registration.cpp

  LINK_LIBS
  support
  )
