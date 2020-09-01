#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include "passes/fuse-functions.h"

namespace
{

static constexpr char PluginName[] = "FuseFunctions";

// Pass registration
static llvm::PassPluginLibraryInfo getFuseFunctionsPluginInfo()
{
  return
  {
    LLVM_PLUGIN_API_VERSION,
    PluginName,
    LLVM_VERSION_STRING,
    [](llvm::PassBuilder& passBuilder)
    {      
      passBuilder.registerPipelineParsingCallback(
        [](llvm::StringRef name, llvm::ModulePassManager& mpm,
          llvm::ArrayRef<llvm::PassBuilder::PipelineElement>)
        {
          if (name.equals("fuse-functions"))
          {
            mpm.addPass(jvs::FuseFunctionsPass());
            return true;
          }
          else if (name.equals("fuse-functions<force>") ||
            name.equals("fuse-functions-force"))
          {
            mpm.addPass(jvs::FuseFunctionsPass(true));
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
  return getFuseFunctionsPluginInfo();
}
