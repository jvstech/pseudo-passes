#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"


#include "passes/demote-registers.h"

namespace
{

static constexpr char PluginName[] = "DemoteRegisters";

// Pass registration
static llvm::PassPluginLibraryInfo getDemoteRegistersPluginInfo()
{
  return
  {
    LLVM_PLUGIN_API_VERSION,
    PluginName,
    LLVM_VERSION_STRING,
    [](llvm::PassBuilder& passBuilder)
    {
      passBuilder.registerPipelineParsingCallback(
        [](llvm::StringRef name, llvm::FunctionPassManager& fpm,
          llvm::ArrayRef<llvm::PassBuilder::PipelineElement>)
        {
          if (name.equals("demote-registers"))
          {
            fpm.addPass(jvs::DemoteRegistersPass());
            return true;
          }

          return false;
        });
    }
  };
}

} // namespace

// This function is required for `opt` to be able to recognize this pass when
// requested in the pass pipeline.
extern "C" LLVM_ATTRIBUTE_WEAK auto llvmGetPassPluginInfo()
-> ::llvm::PassPluginLibraryInfo
{
  return getDemoteRegistersPluginInfo();
}
