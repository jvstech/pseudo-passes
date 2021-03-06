#if !defined(JVS_PSEUDO_PASSES_SUPPORT_PASS_PIPELINE_H_)
#define JVS_PSEUDO_PASSES_SUPPORT_PASS_PIPELINE_H_

#include <memory>
#include <string>
#include <string_view>
#include <tuple>

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"

// forward declarations
namespace llvm
{

class Function;
class Module;

} // namespace llvm


namespace jvs
{

class PassPipeline
{
  llvm::PassBuilder pass_builder_;
  llvm::LoopAnalysisManager loop_analysis_manager_;
  llvm::FunctionAnalysisManager function_analysis_manager_;
  llvm::CGSCCAnalysisManager cgscc_analysis_manager_;
  llvm::ModuleAnalysisManager module_analysis_manager_;
  llvm::FunctionPassManager function_pass_manager_;
  llvm::ModulePassManager module_pass_manager_;
  std::string parse_error_{};

  PassPipeline(std::string_view passes, bool functionPipeline);
public:

  const llvm::PassBuilder& pass_builder() const;
  llvm::PassBuilder& pass_builder();

  const llvm::FunctionAnalysisManager& function_analysis_manager() const;
  llvm::FunctionAnalysisManager& function_analysis_manager();
  const llvm::LoopAnalysisManager& loop_analysis_manager() const;
  llvm::LoopAnalysisManager& loop_analysis_manager();
  const llvm::CGSCCAnalysisManager& cgscc_analysis_manager() const;
  llvm::CGSCCAnalysisManager& cgscc_analysis_manager();
  const llvm::ModuleAnalysisManager& module_analysis_manager() const;
  llvm::ModuleAnalysisManager& module_analysis_manager();
  const llvm::FunctionPassManager& function_pass_manager() const;
  llvm::FunctionPassManager& function_pass_manager();
  const llvm::ModulePassManager& module_pass_manager() const;
  llvm::ModulePassManager& module_pass_manager();
  const std::string& parse_error() const;

  llvm::PreservedAnalyses run(llvm::Module& m);
  llvm::PreservedAnalyses run(llvm::Function& f);

  static PassPipeline create_function_pipeline(std::string_view passes);
  static PassPipeline create_module_pipeline(std::string_view passes);
};

std::tuple<llvm::PreservedAnalyses, std::string> run_pass_pipeline(
  llvm::Module& m, std::string_view passes);
std::tuple<llvm::PreservedAnalyses, std::string> run_pass_pipeline(
  llvm::Function& f, std::string_view passes);

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_SUPPORT_PASS_PIPELINE_H_
