#include "FitFunctionCollection.hh"
#include "RooPDF_HiggsAnalysis_DSCB.h"

#include "RooRealVar.h"
#include "RooWorkspace.h"
#include "TFile.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

void construct_models_Higgs_Systematics(const char* signalParameterFile,
                                        const char* outputWorkspaceFile,
                                        const char* outputNuisanceLines,
                                        double higgsMassValue,
                                        const char* recoFilter,
                                        const char* genSimFilter,
                                        const char* projectionFilter);

namespace {

  const std::array<std::string, 7> parameterNames{
      "#alpha_{low}", "#alpha_{high}", "n_{low}", "n_{high}", "#mu", "#sigma", "norm"};

  std::vector<double> nominalRow(std::size_t row, double componentYield) {
    switch (row) {
      case 0:
        return {1.5, 0.0, 0.0};
      case 1:
        return {2.0, 0.0, 0.0};
      case 2:
        return {3.0, 0.0, 0.0};
      case 3:
        return {4.0, 0.0, 0.0};
      case 4:
        return {1.0, 0.0, 1.0};  // mean = MH
      case 5:
        return {20.0, 0.0, 0.0};
      default:
        return {componentYield, 0.0, 0.0};
    }
  }

  FitFunction makeFunction(const std::string& genSim,
                           const std::string& parameter,
                           const std::string& systematic,
                           const std::vector<double>& values) {
    const std::map<std::string, std::string> fields{{"Reco", "eeee"},
                                                    {"GenSim", genSim},
                                                    {"Projection", "X"},
                                                    {"Parameter", parameter + " 500"},
                                                    {"Systematic", systematic}};
    FitFunction function = FitFunction::createFunctionOfType(
        FitFunction::FunctionType::PowerLaw, FitFunction::encodeName(fields), "", 0.0, 2000.0);
    for (std::size_t index = 0; index < values.size(); ++index) {
      function.getFunction()->SetParameter(static_cast<int>(index), values[index]);
    }
    return function;
  }

  void addModel(FitFunctionCollection& collection, const std::string& genSim, double componentYield) {
    for (std::size_t row = 0; row < parameterNames.size(); ++row) {
      const auto nominal = nominalRow(row, componentYield);
      FitFunction nominalFunction = makeFunction(genSim, parameterNames[row], "Nominal", nominal);
      collection.insert(nominalFunction);

      auto up = nominal;
      auto down = nominal;
      if (row == 4) {
        up[0] = 1.01;
        down[0] = 0.99;
      }
      if (row == 6) {
        up[0] *= 1.1;
        down[0] *= 0.9;
      }
      FitFunction upFunction = makeFunction(genSim, parameterNames[row], "ElectronScaleFactor Up", up);
      FitFunction downFunction = makeFunction(genSim, parameterNames[row], "ElectronScaleFactor Down", down);
      collection.insert(upFunction);
      collection.insert(downFunction);
    }
  }

  void expect(bool condition, const std::string& message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  void expectNear(double actual, double expected, const std::string& message) {
    if (std::abs(actual - expected) > 1e-9) {
      throw std::runtime_error(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
  }

}  // namespace

int main() {
  const std::string parameterFile = "/tmp/limitsetting_builder_parameters.txt";
  const std::string workspaceFile = "/tmp/limitsetting_builder_workspace.root";
  const std::string nuisanceFile = "/tmp/limitsetting_builder_nuisances.txt";
  std::remove(parameterFile.c_str());
  std::remove(workspaceFile.c_str());
  std::remove(nuisanceFile.c_str());

  FitFunctionCollection functions;
  addModel(functions, "eeee", 10.0);
  addModel(functions, "eeuu", 5.0);
  functions.saveFunctions(parameterFile);

  construct_models_Higgs_Systematics(
      parameterFile.c_str(), workspaceFile.c_str(), nuisanceFile.c_str(), 900.0, "eeee", "", "X");

  TFile file(workspaceFile.c_str(), "READ");
  auto* workspace = dynamic_cast<RooWorkspace*>(file.Get("higgsworkspace"));
  expect(workspace != nullptr, "Workspace builder did not write higgsworkspace");
  expect(workspace->pdf("eeee_signal_X") != nullptr, "Missing aggregated signal PDF");
  expect(workspace->var("realHiggsMass") != nullptr && workspace->var("b_ee") != nullptr &&
             workspace->var("b_eu") != nullptr,
         "Workspace is missing variables used by the existing Combine runner");
  auto* norm = workspace->function("eeee_signal_X_norm");
  expect(norm != nullptr, "Missing aggregate _norm function");
  expectNear(norm->getVal(), 15.0, "Wrong nominal aggregate normalization");

  auto* nuisance = workspace->var("shape_ElectronScaleFactor");
  expect(nuisance != nullptr && !nuisance->isConstant(), "Nuisance is absent or frozen");
  auto* component = dynamic_cast<RooPDF_HiggsAnalysis_DSCB*>(workspace->pdf("eeee_signal_X_component_eeee"));
  expect(component != nullptr, "Missing custom DSCB component");
  nuisance->setVal(1.0);
  expectNear(component->effectiveCoefficient(4, 0), 1.01, "Workspace component did not receive nuisance shift");
  expectNear(norm->getVal(), 16.5, "Aggregate normalization did not receive nuisance shift");

  std::ifstream nuisanceInput(nuisanceFile);
  const std::string nuisanceText((std::istreambuf_iterator<char>(nuisanceInput)), std::istreambuf_iterator<char>());
  expect(nuisanceText.find("shape_ElectronScaleFactor param 0 1") != std::string::npos,
         "Generated Combine param line is missing");

  file.Close();
  std::remove(parameterFile.c_str());
  std::remove(workspaceFile.c_str());
  std::remove(nuisanceFile.c_str());
  std::cout << "systematics workspace builder tests passed\n";
  return 0;
}
