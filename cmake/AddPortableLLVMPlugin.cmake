function(add_portable_llvm_plugin plugin_target_name)
  set(extraArgs)
  if (WIN32 OR CYGWIN)
    configure_file(
      ${CMAKE_SOURCE_DIR}/cmake/windows-llvm-plugin.def.in
      ${plugin_target_name}.def)
    set(extraArgs ${CMAKE_CURRENT_BINARY_DIR}/${plugin_target_name}.def)
  endif()

  add_llvm_pass_plugin(${plugin_target_name} 
    ${extraArgs}
    ${ARGN})
endfunction()
