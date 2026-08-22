#include "ShapeSystematics.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

  void expectNear(double actual, double expected, const std::string& label) {
    if (std::abs(actual - expected) > 1e-12) {
      throw std::runtime_error(label + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
  }

}  // namespace

int main() {
  using namespace LimitSetting::ShapeSystematics;

  // The exact proof of concept from the mentor's whiteboard.
  expectNear(interpolate(5.0, 5.1, 4.9, 0.0), 5.0, "delta=0");
  expectNear(interpolate(5.0, 5.1, 4.9, 1.0), 5.1, "delta=+1");
  expectNear(interpolate(5.0, 5.1, 4.9, -1.0), 4.9, "delta=-1");
  expectNear(interpolate(5.0, 5.2, 4.7, 0.5), 5.1, "positive interpolation");
  expectNear(interpolate(5.0, 5.2, 4.7, -0.5), 4.85, "negative interpolation");

  // Independent nuisance effects add as shifts relative to the same nominal.
  expectNear(combinedValue(5.0, {5.1, 5.3}, {4.9, 4.8}, {1.0, -0.5}), 5.0, "combined asymmetric systematics");

  ParameterMatrix nominal(7, ParameterVector{1.0, 2.0, 3.0});
  ParameterCube up(2, nominal);
  ParameterCube down(2, nominal);
  up[0][4][0] = 1.1;
  down[0][4][0] = 0.9;
  up[1][5][1] = 2.4;
  down[1][5][1] = 1.8;

  const auto effective = apply(nominal, up, down, {1.0, -1.0});
  expectNear(effective[4][0], 1.1, "matrix up variation");
  expectNear(effective[5][1], 1.8, "matrix down variation");
  expectNear(effective[0][0], 1.0, "unaffected matrix coefficient");

  bool rejectedInvalidInput = false;
  try {
    validateSystematicInputs(nominal, {"missing"}, {}, {});
  } catch (const std::invalid_argument&) {
    rejectedInvalidInput = true;
  }
  if (!rejectedInvalidInput) {
    throw std::runtime_error("Mismatched systematic inputs were not rejected");
  }

  bool rejectedNonFinite = false;
  try {
    auto invalid = nominal;
    invalid[0][0] = std::numeric_limits<double>::quiet_NaN();
    validateParameterMatrix(invalid, "non-finite test");
  } catch (const std::invalid_argument&) {
    rejectedNonFinite = true;
  }
  if (!rejectedNonFinite) {
    throw std::runtime_error("Non-finite parameter values were not rejected");
  }

  bool rejectedDuplicateNames = false;
  try {
    validateSystematicInputs(nominal, {"duplicate", "duplicate"}, ParameterCube(2, nominal), ParameterCube(2, nominal));
  } catch (const std::invalid_argument&) {
    rejectedDuplicateNames = true;
  }
  if (!rejectedDuplicateNames) {
    throw std::runtime_error("Duplicate systematic names were not rejected");
  }

  if (nuisanceName("Electron scale factor") != "shape_Electron_scale_factor") {
    throw std::runtime_error("Unexpected nuisance-name sanitization");
  }

  std::cout << "shape-systematic interpolation tests passed\n";
  return 0;
}
