#include "SignalSystematics.h"

#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

  template <typename T>
  auto makePowerLawFunctionImpl(const std::string& name, int)
      -> decltype(T::createFunctionOfType(T::FunctionType::PowerLaw,
                                          std::declval<std::string>(),
                                          std::declval<std::string>(),
                                          0.0,
                                          2000.0)) {
    return T::createFunctionOfType(T::FunctionType::PowerLaw, name, "", 0.0, 2000.0);
  }

  template <typename T>
  auto makePowerLawFunctionImpl(const std::string& name, long)
      -> decltype(T::createFunctionOfType(T::FunctionType::PowerLaw,
                                          std::declval<std::string>(),
                                          std::declval<std::string>(),
                                          0.0,
                                          2000.0,
                                          std::declval<std::string>())) {
    return T::createFunctionOfType(T::FunctionType::PowerLaw, name, "", 0.0, 2000.0, "");
  }

  FitFunction makePowerLawFunction(const std::string& name) {
    return makePowerLawFunctionImpl<FitFunction>(name, 0);
  }

  const std::array<std::string, 7> parameterNames{
      "#alpha_{low}", "#alpha_{high}", "n_{low}", "n_{high}", "#mu", "#sigma", "norm"};

  FitFunction makeParameterFunction(const std::string& reco,
                                    const std::string& genSim,
                                    const std::string& projection,
                                    const std::string& parameter,
                                    const std::string& systematic,
                                    const std::vector<double>& values) {
    std::map<std::string, std::string> fields{
        {"Reco", reco}, {"GenSim", genSim}, {"Parameter", parameter + " 500"}, {"Systematic", systematic}};
    if (!projection.empty()) {
      fields["Projection"] = projection;
    }
    FitFunction result = makePowerLawFunction(LimitSetting::SignalSystematics::detail::encodeName(fields));
    for (std::size_t index = 0; index < values.size(); ++index) {
      result.getFunction()->SetParameter(static_cast<int>(index), values[index]);
    }
    return result;
  }

  void addCompleteNominal(FitFunctionCollection& collection,
                          const std::string& reco,
                          const std::string& genSim,
                          const std::string& projection,
                          bool withEmbeddedSystematic) {
    for (std::size_t row = 0; row < parameterNames.size(); ++row) {
      const std::vector<double> nominal{100.0 + row, 2.0 + row, 0.1 * row};
      FitFunction function = makeParameterFunction(reco, genSim, projection, parameterNames[row], "Nominal", nominal);
      if (withEmbeddedSystematic) {
        auto up = nominal;
        auto down = nominal;
        up[1] += 0.25;
        down[1] -= 0.5;
        if constexpr (LimitSetting::SignalSystematics::detail::hasModernFitFunctionApi<FitFunction>::value) {
          function.addSystematic("MuonRecoScaleFactor", up, down);
        }
      }
      collection.insert(function);
    }
  }

  void addSeparateVariation(FitFunctionCollection& collection,
                            const std::string& reco,
                            const std::string& genSim,
                            const std::string& projection,
                            const std::string& name,
                            const std::string& direction,
                            bool complete) {
    const std::size_t rows = complete ? parameterNames.size() : parameterNames.size() - 1;
    for (std::size_t row = 0; row < rows; ++row) {
      std::vector<double> values{100.0 + row, 2.0 + row, 0.1 * row};
      values[0] += direction == "Up" ? 1.0 : -2.0;
      FitFunction function =
          makeParameterFunction(reco, genSim, projection, parameterNames[row], name + " " + direction, values);
      collection.insert(function);
    }
  }

  void expect(bool condition, const std::string& message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  void expectNear(double actual, double expected, const std::string& message) {
    if (std::abs(actual - expected) > 1e-12) {
      throw std::runtime_error(message);
    }
  }

}  // namespace

int main() {
  using namespace LimitSetting::SignalSystematics;

  const auto legacyName = detail::decodeName("eeee_eeuu/#mu 500 ElectronScaleFactor Up X projection");
  expect(detail::field(legacyName, "Reco") == "eeee" && detail::field(legacyName, "GenSim") == "eeuu" &&
             detail::field(legacyName, "Parameter") == "#mu" &&
             detail::field(legacyName, "Systematic") == "ElectronScaleFactor Up" &&
             detail::field(legacyName, "Projection") == "X",
         "Legacy CMSAnalysis parameter name was not decoded");

  FitFunctionCollection collection;
  addCompleteNominal(collection, "eeee", "eeee", "X", true);
  addSeparateVariation(collection, "eeee", "eeee", "X", "ElectronScaleFactor", "Up", true);
  addSeparateVariation(collection, "eeee", "eeee", "X", "ElectronScaleFactor", "Down", true);
  FitFunction doubleGaussianRow = makeParameterFunction("eeuu", "eeee", "Y", "mul_{1}", "Nominal", {1.0, 0.0, 0.0});
  collection.insert(doubleGaussianRow);

  const ExtractionResult extracted = extractDSCBModels(collection);
  expect(extracted.models.size() == 1, "Expected one extracted DSCB model");
  expect(extracted.unsupportedModels.size() == 1 && extracted.unsupportedModels.front().key.reco == "eeuu",
         "Unsupported non-DSCB model was not reported");
  const SignalModel& model = extracted.models.front();
  expect(model.key.reco == "eeee" && model.key.genSim == "eeee" && model.key.projection == "X",
         "Model key was not preserved");
  const bool hasEmbeddedSystematics = detail::hasModernFitFunctionApi<FitFunction>::value;
  const std::vector<std::string> expectedSystematics =
      hasEmbeddedSystematics ? std::vector<std::string>({"ElectronScaleFactor", "MuonRecoScaleFactor"})
                              : std::vector<std::string>({"ElectronScaleFactor"});
  expect(model.systematicNames == expectedSystematics, "Systematics were not extracted in deterministic order");
  expectNear(model.nominal[Mean][0], 104.0, "Wrong nominal mean coefficient");
  expectNear(model.up[0][Mean][0], 105.0, "Wrong separate up coefficient");
  expectNear(model.down[0][Mean][0], 102.0, "Wrong separate down coefficient");
  if (hasEmbeddedSystematics) {
    expectNear(model.up[1][Mean][1], 6.25, "Wrong embedded up coefficient");
    expectNear(model.down[1][Mean][1], 5.5, "Wrong embedded down coefficient");
  }
  expect(uniqueSystematicNames(extracted.models) == expectedSystematics,
         "Unique systematic-name collection failed");

  FitFunctionCollection incomplete;
  addCompleteNominal(incomplete, "uuuu", "uuuu", "Y", false);
  addSeparateVariation(incomplete, "uuuu", "uuuu", "Y", "MuonTriggerScaleFactor", "Up", true);
  addSeparateVariation(incomplete, "uuuu", "uuuu", "Y", "MuonTriggerScaleFactor", "Down", false);

  bool rejectedIncomplete = false;
  try {
    (void)extractDSCBModels(incomplete);
  } catch (const std::invalid_argument&) {
    rejectedIncomplete = true;
  }
  expect(rejectedIncomplete, "Strict extraction accepted an incomplete systematic");

  ExtractionOptions relaxed;
  relaxed.requireCompleteSystematics = false;
  const ExtractionResult diagnostic = extractDSCBModels(incomplete, relaxed);
  expect(diagnostic.models.size() == 1 && diagnostic.models.front().systematicNames.empty(),
         "Relaxed extraction did not skip the incomplete systematic");
  expect(!diagnostic.warnings.empty(), "Relaxed extraction did not report its skipped systematic");

  std::cout << "FitFunction systematic extraction tests passed\n";
  return 0;
}
