#include "RooPDF_HiggsAnalysis_DSCB.h"
#include "SignalSystematics.h"

#include "RooAddPdf.h"
#include "RooAddition.h"
#include "RooArgList.h"
#include "RooConstVar.h"
#include "RooFit.h"
#include "RooFormulaVar.h"
#include "RooRealVar.h"
#include "RooWorkspace.h"
#include "TFile.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

  std::string normalizedOutputPath(const char* path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(std::filesystem::path(path), error);
    if (error) {
      return std::filesystem::path(path).lexically_normal().string();
    }
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
    return (error ? absolute.lexically_normal() : canonical).string();
  }

  std::string resolveSignalParameterFile(const char* requested) {
    if (requested != nullptr && *requested != '\0') {
      std::ifstream input(requested);
      if (!input) {
        throw std::runtime_error("Signal parameter file does not exist: " + std::string(requested));
      }
      return requested;
    }

    std::vector<std::string> candidates;
    if (const char* base = std::getenv("CMSANALYSIS_BASE")) {
      candidates.push_back(std::string(base) + "/Analysis/bin/fitting/H++SignalParameterFunctions.txt");
      candidates.push_back(std::string(base) + "/bin/fitting/H++SignalParameterFunctions.txt");
    }
    if (const char* base = std::getenv("CMSSW_BASE")) {
      candidates.push_back(std::string(base) + "/src/CMSAnalysis/Analysis/bin/fitting/H++SignalParameterFunctions.txt");
    }
    for (const auto& candidate : candidates) {
      std::ifstream input(candidate);
      if (input) {
        return candidate;
      }
    }
    throw std::runtime_error(
        "No signal parameter file was supplied and H++SignalParameterFunctions.txt was not found. "
        "Pass its path as the first argument or set CMSANALYSIS_BASE.");
  }

  bool selected(const LimitSetting::SignalSystematics::ModelKey& key,
                const std::string& reco,
                const std::string& genSim,
                const std::string& projection) {
    return (reco.empty() || key.reco == reco) && (genSim.empty() || key.genSim == genSim) &&
           (projection.empty() || key.projection == projection);
  }

  std::string aggregateName(const LimitSetting::SignalSystematics::ModelKey& key) {
    std::string result = LimitSetting::ShapeSystematics::sanitizeName(key.reco) + "_signal";
    if (!key.projection.empty()) {
      result += "_" + LimitSetting::ShapeSystematics::sanitizeName(key.projection);
    }
    return result;
  }

}  // namespace

// Build all DSCB signal components and all complete named shape systematics.
// Empty filters mean "all".  The resulting nuisance names are shared across
// components/channels, so a single `param 0 1` line correlates each source.
void construct_models_Higgs_Systematics(const char* signalParameterFile = "",
                                        const char* outputWorkspaceFile = "higgsworkspace_systematics.root",
                                        const char* outputNuisanceLines = "shape_systematics.txt",
                                        double higgsMassValue = 900.0,
                                        const char* recoFilter = "",
                                        const char* genSimFilter = "",
                                        const char* projectionFilter = "") {
  using namespace LimitSetting;
  using namespace LimitSetting::SignalSystematics;

  if (outputWorkspaceFile == nullptr || *outputWorkspaceFile == '\0' || outputNuisanceLines == nullptr ||
      *outputNuisanceLines == '\0') {
    throw std::invalid_argument("Output workspace and nuisance-line paths must not be empty");
  }
  if (normalizedOutputPath(outputWorkspaceFile) == normalizedOutputPath(outputNuisanceLines)) {
    throw std::invalid_argument("Workspace and nuisance-line outputs must be different files");
  }
  if (!std::isfinite(higgsMassValue) || higgsMassValue < 200.0 || higgsMassValue > 2000.0) {
    throw std::invalid_argument("Higgs mass must be in the configured [200, 2000] range");
  }

  const std::string inputPath = resolveSignalParameterFile(signalParameterFile);
  FitFunctionCollection functions = FitFunctionCollection::loadFunctions(inputPath);
  ExtractionResult extraction = extractDSCBModels(functions);
  for (const auto& warning : extraction.warnings) {
    std::cerr << "Warning: " << warning << '\n';
  }

  const std::string reco = recoFilter == nullptr ? "" : recoFilter;
  const std::string genSim = genSimFilter == nullptr ? "" : genSimFilter;
  const std::string projection = projectionFilter == nullptr ? "" : projectionFilter;

  for (const auto& unsupported : extraction.unsupportedModels) {
    if (selected(unsupported.key, reco, genSim, projection)) {
      throw std::runtime_error("The requested model set contains unsupported non-DSCB parameterizations (for example " +
                               unsupported.key.label() +
                               "). Refusing to write a silently incomplete signal workspace.");
    }
  }

  std::vector<const SignalModel*> models;
  for (const auto& model : extraction.models) {
    if (selected(model.key, reco, genSim, projection)) {
      models.push_back(&model);
    }
  }
  if (models.empty()) {
    throw std::runtime_error("No complete DSCB models matched the requested filters");
  }

  std::set<std::string> originalSystematicNames;
  for (const auto* model : models) {
    originalSystematicNames.insert(model->systematicNames.begin(), model->systematicNames.end());
  }
  if (originalSystematicNames.empty()) {
    throw std::runtime_error("The selected DSCB models contain no complete shape-systematic variations");
  }

  std::map<std::string, std::unique_ptr<RooRealVar>> nuisances;
  std::map<std::string, std::string> nuisanceOwners;
  for (const auto& systematic : originalSystematicNames) {
    const std::string nuisanceName = ShapeSystematics::nuisanceName(systematic);
    const auto owner = nuisanceOwners.find(nuisanceName);
    if (owner != nuisanceOwners.end() && owner->second != systematic) {
      throw std::runtime_error("Systematic names '" + owner->second + "' and '" + systematic +
                               "' sanitize to the same RooFit name " + nuisanceName);
    }
    nuisanceOwners[nuisanceName] = systematic;
    auto nuisance = std::make_unique<RooRealVar>(nuisanceName.c_str(), systematic.c_str(), 0.0, -5.0, 5.0);
    nuisance->setConstant(false);
    nuisances.emplace(systematic, std::move(nuisance));
  }

  RooRealVar mass("mass", "four-lepton invariant mass", higgsMassValue, 50.0, 2000.0);
  RooRealVar higgsMass("realHiggsMass", "Higgs mass", higgsMassValue, 200.0, 2000.0);
  RooRealVar branchRatio1("b_ee", "b_ee", 1.0, 0.0, 1.0);
  RooRealVar branchRatio2("b_eu", "b_eu", 1.0, 0.0, 1.0);
  RooConstVar normalizationSystematic("normalization_Systematic", "normalization_Systematic", 1.0);
  higgsMass.setConstant(true);
  branchRatio1.setConstant(true);
  branchRatio2.setConstant(true);

  using AggregateKey = std::pair<std::string, std::string>;  // reco, projection
  std::map<AggregateKey, std::vector<const SignalModel*>> grouped;
  for (const auto* model : models) {
    grouped[{model->key.reco, model->key.projection}].push_back(model);
  }

  RooWorkspace workspace("higgsworkspace", "higgsworkspace");
  std::vector<std::string> aggregateObjects;
  for (const auto& [groupKey, componentsToBuild] : grouped) {
    const ModelKey namingKey{groupKey.first, "", groupKey.second};
    const std::string combinedName = aggregateName(namingKey);

    std::vector<std::unique_ptr<RooPDF_HiggsAnalysis_DSCB>> componentPdfs;
    std::vector<std::unique_ptr<RooFormulaVar>> componentNorms;
    RooArgList pdfList;
    RooArgList normList;

    for (const auto* model : componentsToBuild) {
      RooArgList modelNuisances;
      for (const auto& systematic : model->systematicNames) {
        modelNuisances.add(*nuisances.at(systematic));
      }

      const std::string componentName =
          combinedName + "_component_" + ShapeSystematics::sanitizeName(model->key.genSim);
      auto pdf = std::make_unique<RooPDF_HiggsAnalysis_DSCB>(componentName.c_str(),
                                                             model->key.label().c_str(),
                                                             mass,
                                                             higgsMass,
                                                             branchRatio1,
                                                             branchRatio2,
                                                             normalizationSystematic,
                                                             modelNuisances,
                                                             model->systematicNames,
                                                             model->nominal,
                                                             model->up,
                                                             model->down,
                                                             false);
      auto norm = std::make_unique<RooFormulaVar>(pdf->signal_norm(componentName));
      pdfList.add(*pdf);
      normList.add(*norm);
      componentPdfs.push_back(std::move(pdf));
      componentNorms.push_back(std::move(norm));
    }

    RooAddPdf combinedPdf(combinedName.c_str(), combinedName.c_str(), pdfList, normList, false);
    const std::string combinedNormName = combinedName + "_norm";
    RooAddition combinedNorm(combinedNormName.c_str(), combinedNormName.c_str(), normList);

    if (workspace.import(combinedPdf, RooFit::RecycleConflictNodes())) {
      throw std::runtime_error("Failed to import " + combinedName + " into the workspace");
    }
    if (workspace.import(combinedNorm, RooFit::RecycleConflictNodes())) {
      throw std::runtime_error("Failed to import " + combinedNormName + " into the workspace");
    }
    aggregateObjects.push_back(combinedName);
  }

  for (const auto& [systematic, nuisance] : nuisances) {
    (void)systematic;
    RooRealVar* workspaceNuisance = workspace.var(nuisance->GetName());
    if (workspaceNuisance == nullptr || workspaceNuisance->isConstant()) {
      throw std::runtime_error("Workspace nuisance " + std::string(nuisance->GetName()) + " is missing or constant");
    }
  }
  errno = 0;
  if (std::remove(outputWorkspaceFile) != 0 && errno != ENOENT) {
    throw std::runtime_error("Cannot replace existing workspace file " + std::string(outputWorkspaceFile));
  }
  const bool writeResult = workspace.writeToFile(outputWorkspaceFile, true);
  TFile writtenWorkspace(outputWorkspaceFile, "READ");
  if (writtenWorkspace.IsZombie() || writtenWorkspace.Get("higgsworkspace") == nullptr) {
    throw std::runtime_error("Failed to write a readable workspace file " + std::string(outputWorkspaceFile));
  }
  auto* writtenWorkspaceObject = dynamic_cast<RooWorkspace*>(writtenWorkspace.Get("higgsworkspace"));
  for (const auto& aggregate : aggregateObjects) {
    if (writtenWorkspaceObject->pdf(aggregate.c_str()) == nullptr ||
        writtenWorkspaceObject->function((aggregate + "_norm").c_str()) == nullptr) {
      throw std::runtime_error("Written workspace is missing signal object " + aggregate);
    }
  }
  for (const auto& [systematic, nuisance] : nuisances) {
    (void)systematic;
    RooRealVar* writtenNuisance = writtenWorkspaceObject->var(nuisance->GetName());
    if (writtenNuisance == nullptr || writtenNuisance->isConstant()) {
      throw std::runtime_error("Written workspace is missing floating nuisance " +
                               std::string(nuisance->GetName()));
    }
  }
  if (!writeResult) {
    std::cerr << "Warning: RooWorkspace::writeToFile returned false, but the workspace file is readable; "
                 "continuing.\n";
  }

  std::ofstream nuisanceOutput(outputNuisanceLines);
  if (!nuisanceOutput) {
    throw std::runtime_error("Failed to write nuisance lines to " + std::string(outputNuisanceLines));
  }
  nuisanceOutput << "# Add these lines to the Combine datacard.\n"
                 << "# The matching zero-centred variables are embedded in the workspace PDFs.\n";
  for (const auto& systematic : originalSystematicNames) {
    nuisanceOutput << ShapeSystematics::nuisanceName(systematic) << " param 0 1"
                   << "  # " << systematic << '\n';
  }
  nuisanceOutput << "\n# Signal PDF objects written to higgsworkspace:\n";
  for (const auto& name : aggregateObjects) {
    nuisanceOutput << "#   " << name << " (normalization: " << name << "_norm)\n";
  }

  std::cout << "Wrote " << aggregateObjects.size() << " signal PDF(s) with " << originalSystematicNames.size()
            << " correlated shape nuisance(s) to " << outputWorkspaceFile << '\n';
  std::cout << "Wrote Combine param lines to " << outputNuisanceLines << '\n';
}
