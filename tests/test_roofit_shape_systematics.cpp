#include "RooPDF_HiggsAnalysis_DSCB.h"

#include "RooArgList.h"
#include "RooArgSet.h"
#include "RooRealVar.h"
#include "RooWorkspace.h"
#include "TFile.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

  void expectNear(double actual, double expected, const std::string& label, double tolerance = 1e-10) {
    if (std::abs(actual - expected) > tolerance) {
      throw std::runtime_error(label + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
  }

  void expect(bool condition, const std::string& message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

}  // namespace

int main() {
  using namespace LimitSetting::ShapeSystematics;

  RooRealVar mass("mass", "four-lepton mass", 500.0, 200.0, 1200.0);
  RooRealVar higgsMass("MH", "Higgs mass", 500.0, 200.0, 1200.0);
  RooRealVar branch1("Bee", "Bee", 1.0);
  RooRealVar branch2("Beu", "Beu", 1.0);
  RooRealVar normalization("normalization_Systematic", "normalization_Systematic", 1.0);
  RooRealVar electronScale("shape_ElectronScaleFactor", "shape_ElectronScaleFactor", 0.0, -5.0, 5.0);
  RooRealVar resolution("shape_Resolution", "shape_Resolution", 0.0, -5.0, 5.0);

  higgsMass.setConstant(true);
  branch1.setConstant(true);
  branch2.setConstant(true);
  normalization.setConstant(true);
  electronScale.setConstant(false);
  resolution.setConstant(false);

  // p0 * (MH - p1)^p2.  p1=p2=0 makes these rows constants,
  // which keeps this test focused on nuisance interpolation.
  ParameterMatrix nominal{
      {1.5, 0.0, 0.0},    // alpha low
      {2.0, 0.0, 0.0},    // alpha high
      {3.0, 0.0, 0.0},    // n low
      {4.0, 0.0, 0.0},    // n high
      {500.0, 0.0, 0.0},  // mean
      {20.0, 0.0, 0.0},   // sigma
      {10.0, 0.0, 0.0}    // normalization
  };
  ParameterCube up(2, nominal);
  ParameterCube down(2, nominal);
  up[0][4][0] = 510.0;
  down[0][4][0] = 490.0;
  up[0][6][0] = 11.0;
  down[0][6][0] = 9.0;
  up[1][5][0] = 22.0;
  down[1][5][0] = 18.0;

  RooArgList nuisances(electronScale, resolution);
  RooPDF_HiggsAnalysis_DSCB pdf("signal",
                                "signal",
                                mass,
                                higgsMass,
                                branch1,
                                branch2,
                                normalization,
                                nuisances,
                                {"ElectronScaleFactor", "Resolution"},
                                nominal,
                                up,
                                down,
                                false);

  expectNear(pdf.effectiveCoefficient(4, 0), 500.0, "nominal mean coefficient");
  electronScale.setVal(1.0);
  expectNear(pdf.effectiveCoefficient(4, 0), 510.0, "mean coefficient at delta=+1");
  electronScale.setVal(-1.0);
  expectNear(pdf.effectiveCoefficient(4, 0), 490.0, "mean coefficient at delta=-1");
  electronScale.setVal(0.5);
  resolution.setVal(-1.0);
  expectNear(pdf.effectiveCoefficient(4, 0), 505.0, "interpolated mean coefficient");
  expectNear(pdf.effectiveCoefficient(5, 0), 18.0, "independent resolution coefficient");

  electronScale.setVal(0.0);
  resolution.setVal(0.0);
  RooArgSet normalizationSet(mass);
  const double nominalPdfValue = pdf.getVal(normalizationSet);
  electronScale.setVal(1.0);
  const double shiftedPdfValue = pdf.getVal(normalizationSet);
  expect(std::isfinite(nominalPdfValue) && std::isfinite(shiftedPdfValue), "DSCB returned a non-finite value");
  expect(std::abs(nominalPdfValue - shiftedPdfValue) > 1e-8, "Changing a nuisance did not trigger a PDF shape change");

  RooFormulaVar signalNorm = pdf.signal_norm("signal");
  electronScale.setVal(0.0);
  expectNear(signalNorm.getVal(), 10.0, "nominal signal normalization");
  electronScale.setVal(1.0);
  expectNear(signalNorm.getVal(), 11.0, "up signal normalization");
  electronScale.setVal(-1.0);
  expectNear(signalNorm.getVal(), 9.0, "down signal normalization");

  // Existing callers still compile, but the old generic placeholder cannot
  // invent an uncertainty without absolute up/down values.  Extra entries
  // historically appended to each row are fit errors and must be ignored.
  ParameterMatrix legacyParameters = nominal;
  for (auto& row : legacyParameters) {
    row.insert(row.end(), {123.0, 456.0, 789.0});
  }
  RooRealVar legacyShape("shape_systematic", "shape_systematic", 0.0, -5.0, 5.0);
  RooPDF_HiggsAnalysis_DSCB legacyPdf("legacy_signal",
                                      "legacy_signal",
                                      mass,
                                      higgsMass,
                                      branch1,
                                      branch2,
                                      normalization,
                                      legacyShape,
                                      legacyParameters,
                                      false);
  expectNear(legacyPdf.parameterizationValue(4, 500.0), 500.0, "legacy nominal parameterization");
  legacyShape.setVal(2.0);
  expectNear(legacyPdf.parameterizationValue(4, 500.0), 500.0, "legacy placeholder unexpectedly changed the PDF");

  // Prove that the nuisance dependency and custom class survive a workspace
  // round trip, which is what Combine will consume on the cluster.
  electronScale.setVal(0.0);
  RooWorkspace workspace("w", "w");
  expect(!workspace.import(pdf), "Could not import DSCB into workspace");
  expect(!workspace.import(signalNorm, RooFit::RecycleConflictNodes()),
         "Could not import signal normalization into workspace");

  const std::string workspacePath = "/tmp/limitsetting_shape_systematics_test.root";
  expect(workspace.writeToFile(workspacePath.c_str(), true), "Could not write test workspace");

  TFile input(workspacePath.c_str(), "READ");
  auto* loadedWorkspace = dynamic_cast<RooWorkspace*>(input.Get("w"));
  expect(loadedWorkspace != nullptr, "Could not reload test workspace");
  auto* loadedPdf = dynamic_cast<RooPDF_HiggsAnalysis_DSCB*>(loadedWorkspace->pdf("signal"));
  auto* loadedNuisance = loadedWorkspace->var("shape_ElectronScaleFactor");
  expect(loadedPdf != nullptr && loadedNuisance != nullptr, "Reloaded workspace is missing the PDF or nuisance");
  expect(!loadedNuisance->isConstant(), "Shape nuisance became constant in the workspace");
  loadedNuisance->setVal(1.0);
  expectNear(loadedPdf->effectiveCoefficient(4, 0), 510.0, "reloaded workspace mean coefficient at delta=+1");

  input.Close();
  std::remove(workspacePath.c_str());
  std::cout << "RooFit DSCB shape-systematic tests passed\n";
  return 0;
}
