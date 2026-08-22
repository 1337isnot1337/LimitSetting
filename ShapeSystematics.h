#ifndef LIMITSETTING_SHAPE_SYSTEMATICS_H
#define LIMITSETTING_SHAPE_SYSTEMATICS_H

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace LimitSetting {
  namespace ShapeSystematics {

    using ParameterVector = std::vector<double>;
    using ParameterMatrix = std::vector<ParameterVector>;
    using ParameterCube = std::vector<ParameterMatrix>;

    // Linear asymmetric interpolation around the nominal value.  The up/down
    // inputs are absolute alternate values, matching FitFunction::addSystematic.
    inline double interpolate(double nominal, double up, double down, double delta) {
      if (delta >= 0.0) {
        return nominal + delta * (up - nominal);
      }
      return nominal + (-delta) * (down - nominal);
    }

    inline double combinedValue(double nominal,
                                const std::vector<double>& upValues,
                                const std::vector<double>& downValues,
                                const std::vector<double>& deltas) {
      if (upValues.size() != downValues.size() || upValues.size() != deltas.size()) {
        throw std::invalid_argument("Shape-systematic values and nuisance values have different sizes");
      }

      double result = nominal;
      for (std::size_t index = 0; index < deltas.size(); ++index) {
        result += interpolate(nominal, upValues[index], downValues[index], deltas[index]) - nominal;
      }
      return result;
    }

    inline void validateParameterMatrix(const ParameterMatrix& parameters,
                                        const std::string& label,
                                        std::size_t minimumRows = 7,
                                        std::size_t minimumCoefficients = 3) {
      if (parameters.size() < minimumRows) {
        throw std::invalid_argument(label + " has " + std::to_string(parameters.size()) +
                                    " parameter rows; expected at least " + std::to_string(minimumRows));
      }

      for (std::size_t row = 0; row < parameters.size(); ++row) {
        if (parameters[row].size() < minimumCoefficients) {
          throw std::invalid_argument(label + " row " + std::to_string(row) + " has " +
                                      std::to_string(parameters[row].size()) + " coefficients; expected at least " +
                                      std::to_string(minimumCoefficients));
        }
      }
    }

    inline void validateSystematicInputs(const ParameterMatrix& nominal,
                                         const std::vector<std::string>& names,
                                         const ParameterCube& up,
                                         const ParameterCube& down,
                                         std::size_t minimumRows = 7,
                                         std::size_t minimumCoefficients = 3) {
      validateParameterMatrix(nominal, "nominal parameters", minimumRows, minimumCoefficients);

      if (names.size() != up.size() || names.size() != down.size()) {
        throw std::invalid_argument("Systematic names and up/down parameter sets have different sizes");
      }

      for (std::size_t index = 0; index < names.size(); ++index) {
        validateParameterMatrix(up[index], "up variation for " + names[index], minimumRows, minimumCoefficients);
        validateParameterMatrix(down[index], "down variation for " + names[index], minimumRows, minimumCoefficients);

        if (up[index].size() != nominal.size() || down[index].size() != nominal.size()) {
          throw std::invalid_argument("Variation " + names[index] +
                                      " does not have the same number of rows as the nominal parameters");
        }

        for (std::size_t row = 0; row < nominal.size(); ++row) {
          if (up[index][row].size() != nominal[row].size() || down[index][row].size() != nominal[row].size()) {
            throw std::invalid_argument("Variation " + names[index] + " does not match nominal row " +
                                        std::to_string(row));
          }
        }
      }
    }

    inline ParameterMatrix apply(const ParameterMatrix& nominal,
                                 const ParameterCube& up,
                                 const ParameterCube& down,
                                 const std::vector<double>& deltas) {
      std::vector<std::string> placeholderNames;
      placeholderNames.reserve(deltas.size());
      for (std::size_t index = 0; index < deltas.size(); ++index) {
        placeholderNames.push_back("systematic_" + std::to_string(index));
      }
      validateSystematicInputs(nominal, placeholderNames, up, down);

      ParameterMatrix result = nominal;
      for (std::size_t row = 0; row < nominal.size(); ++row) {
        for (std::size_t coefficient = 0; coefficient < nominal[row].size(); ++coefficient) {
          std::vector<double> upValues;
          std::vector<double> downValues;
          upValues.reserve(deltas.size());
          downValues.reserve(deltas.size());
          for (std::size_t systematic = 0; systematic < deltas.size(); ++systematic) {
            upValues.push_back(up[systematic][row][coefficient]);
            downValues.push_back(down[systematic][row][coefficient]);
          }
          result[row][coefficient] = combinedValue(nominal[row][coefficient], upValues, downValues, deltas);
        }
      }
      return result;
    }

    inline std::string sanitizeName(const std::string& name) {
      std::string result;
      result.reserve(name.size());
      for (const unsigned char character : name) {
        const bool alphaNumeric = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                                  (character >= '0' && character <= '9');
        result.push_back(alphaNumeric || character == '_' ? static_cast<char>(character) : '_');
      }

      if (result.empty() || (result.front() >= '0' && result.front() <= '9')) {
        result.insert(result.begin(), '_');
      }
      return result;
    }

    inline std::string nuisanceName(const std::string& systematicName) {
      return "shape_" + sanitizeName(systematicName);
    }

  }  // namespace ShapeSystematics
}  // namespace LimitSetting

#endif
