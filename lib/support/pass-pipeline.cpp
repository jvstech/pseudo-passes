#include "support/pass-pipeline.h"

#include <utility>

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"


jvs::PassPipeline::PassPipeline(std::string_view passes)
  : pass_builder_(),
  loop_analysis_manager_(llvm::DebugFlag),
  function_analysis_manager_(llvm::DebugFlag),
  cgscc_analysis_manager_(llvm::DebugFlag),
  module_analysis_manager_(llvm::DebugFlag),
  module_pass_manager_(llvm::DebugFlag),
  parse_error_()
{
  pass_builder_.registerModuleAnalyses(module_analysis_manager_);
  pass_builder_.registerCGSCCAnalyses(cgscc_analysis_manager_);
  pass_builder_.registerFunctionAnalyses(function_analysis_manager_);
  pass_builder_.registerLoopAnalyses(loop_analysis_manager_);
  pass_builder_.crossRegisterProxies(loop_analysis_manager_,
    function_analysis_manager_, cgscc_analysis_manager_,
    module_analysis_manager_);
  if (auto err = pass_builder_.parsePassPipeline(module_pass_manager_,
    passes, true, llvm::DebugFlag))
  {
    parse_error_ = llvm::toString(std::move(err));
  }
}

const llvm::PassBuilder& jvs::PassPipeline::pass_builder() const
{
  return pass_builder_;
}

llvm::PassBuilder& jvs::PassPipeline::pass_builder()
{
  return pass_builder_;
}

auto jvs::PassPipeline::function_analysis_manager() const
-> const llvm::FunctionAnalysisManager&
{
  return function_analysis_manager_;
}

llvm::FunctionAnalysisManager& jvs::PassPipeline::function_analysis_manager()
{
  return function_analysis_manager_;
}

auto jvs::PassPipeline::loop_analysis_manager() const
-> const llvm::LoopAnalysisManager&
{
  return loop_analysis_manager_;
}

llvm::LoopAnalysisManager& jvs::PassPipeline::loop_analysis_manager()
{
  return loop_analysis_manager_;
}

auto jvs::PassPipeline::cgscc_analysis_manager() const
-> const llvm::CGSCCAnalysisManager&
{
  return cgscc_analysis_manager_;
}

llvm::CGSCCAnalysisManager& jvs::PassPipeline::cgscc_analysis_manager()
{
  return cgscc_analysis_manager_;
}

auto jvs::PassPipeline::module_analysis_manager() const
-> const llvm::ModuleAnalysisManager&
{
  return module_analysis_manager_;
}

llvm::ModuleAnalysisManager& jvs::PassPipeline::module_analysis_manager()
{
  return module_analysis_manager_;
}

auto jvs::PassPipeline::module_pass_manager() const
-> const llvm::ModulePassManager&
{
  return module_pass_manager_;
}

llvm::ModulePassManager& jvs::PassPipeline::module_pass_manager()
{
  return module_pass_manager_;
}

const std::string& jvs::PassPipeline::parse_error() const
{
  return parse_error_;
}

llvm::PreservedAnalyses jvs::PassPipeline::run(llvm::Module& m)
{
  return module_pass_manager_.run(m, module_analysis_manager_);
}

std::tuple<llvm::PreservedAnalyses, std::string> jvs::run_pass_pipeline(
  llvm::Module& m, std::string_view passes)
{
  PassPipeline passPipeline(passes);
  llvm::PreservedAnalyses result = passPipeline.run(m);
  return std::make_tuple(result, passPipeline.parse_error());
}
