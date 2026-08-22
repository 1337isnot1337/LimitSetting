#ifndef LIMITSETTING_SIGNAL_SYSTEMATICS_H
#define LIMITSETTING_SIGNAL_SYSTEMATICS_H

#include "FitFunctionCollection.hh"
#include "ShapeSystematics.h"

#include "TF1.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace LimitSetting {
  namespace SignalSystematics {

    constexpr std::size_t dscbParameterCount = 7;

    enum DSCBParameter : std::size_t {
      AlphaLow = 0,
      AlphaHigh = 1,
      NLow = 2,
      NHigh = 3,
      Mean = 4,
      Sigma = 5,
      Normalization = 6
    };

    struct ModelKey {
      std::string reco;
      std::string genSim;
      std::string projection;

      bool operator<(const ModelKey& other) const {
        return std::tie(reco, genSim, projection) < std::tie(other.reco, other.genSim, other.projection);
      }

      std::string label() const {
        std::string result = genSim + " -> " + reco;
        if (!projection.empty()) {
          result += " (" + projection + " projection)";
        }
        return result;
      }

      std::string objectName() const {
        std::string result = ShapeSystematics::sanitizeName(reco) + "_" + ShapeSystematics::sanitizeName(genSim);
        if (!projection.empty()) {
          result += "_" + ShapeSystematics::sanitizeName(projection);
        }
        return result;
      }
    };

    struct SignalModel {
      ModelKey key;
      ShapeSystematics::ParameterMatrix nominal;
      std::vector<std::string> systematicNames;
      ShapeSystematics::ParameterCube up;
      ShapeSystematics::ParameterCube down;
    };

    struct ExtractionOptions {
      // Missing one side or one DSCB row is dangerous because it would silently
      // turn a requested nuisance into a partial/no-op variation.  Keep strict
      // mode enabled for production; relaxed mode is useful only for diagnosis.
      bool requireCompleteSystematics = true;
    };

    struct ExtractionResult {
      std::vector<SignalModel> models;
      struct UnsupportedModel {
        ModelKey key;
        std::vector<std::string> parameterNames;
      };
      std::vector<UnsupportedModel> unsupportedModels;
      std::vector<std::string> warnings;
    };

    namespace detail {

      using OptionalRow = std::optional<ShapeSystematics::ParameterVector>;
      using OptionalMatrix = std::array<OptionalRow, dscbParameterCount>;

      struct PendingModel {
        OptionalMatrix nominal;
        std::map<std::string, OptionalMatrix> up;
        std::map<std::string, OptionalMatrix> down;
      };

      enum class VariationDirection { Nominal, Up, Down };

      struct Variation {
        VariationDirection direction = VariationDirection::Nominal;
        std::string name;
      };

      template <typename T, typename = void>
      struct hasModernFitFunctionApi : std::false_type {};

      template <typename T>
      struct hasModernFitFunctionApi<
          T,
          std::void_t<decltype(T::encodeName(std::declval<std::map<std::string, std::string>>()))>>
          : std::true_type {};

      inline std::string trim(std::string value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
          return {};
        }
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
      }

      // Newer CMSAnalysis releases expose FitFunction::encodeName/decodeName.
      // Keep the parser local so this package also works with the older
      // FitFunction API used by the CMSSW_15_0_4 checkout.  The older fitter
      // encoded parameterized names as reco_genSim/<parameter> <mass> <desc>.
      inline std::string encodeName(const std::map<std::string, std::string>& fields) {
        std::string result;
        for (const auto& [key, value] : fields) {
          result += key + " - " + value + " | ";
        }
        return result;
      }

      inline std::map<std::string, std::string> decodeName(std::string name) {
        std::map<std::string, std::string> result;
        std::istringstream encoded(name);
        std::string token;
        while (std::getline(encoded, token, '|')) {
          const auto dash = token.find(" - ");
          if (dash == std::string::npos) {
            continue;
          }
          result[trim(token.substr(0, dash))] = trim(token.substr(dash + 3));
        }
        if (result.count("Reco") != 0 && result.count("GenSim") != 0) {
          return result;
        }

        // Compatibility with parameter files written before encodeName was
        // added to CMSAnalysis.
        const auto slash = name.find('/');
        if (slash == std::string::npos) {
          return {};
        }
        const std::string channel = trim(name.substr(0, slash));
        const auto separator = channel.find('_');
        if (separator == std::string::npos) {
          return {};
        }
        result.clear();
        result["Reco"] = channel.substr(0, separator);
        result["GenSim"] = channel.substr(separator + 1);

        std::istringstream legacy(trim(name.substr(slash + 1)));
        std::string parameter;
        std::string mass;
        if (!(legacy >> parameter >> mass)) {
          return {};
        }
        result["Parameter"] = parameter;
        std::string descriptor;
        std::getline(legacy, descriptor);
        descriptor = trim(descriptor);
        for (const auto& [suffix, projection] :
             std::array<std::pair<std::string, std::string>, 2>{{{" X projection", "X"}, {" Y projection", "Y"}}}) {
          if (descriptor.size() >= suffix.size() &&
              descriptor.compare(descriptor.size() - suffix.size(), suffix.size(), suffix) == 0) {
            result["Projection"] = projection;
            descriptor = trim(descriptor.substr(0, descriptor.size() - suffix.size()));
            break;
          }
        }
        result["Systematic"] = descriptor.empty() ? "Nominal" : descriptor;
        return result;
      }

      inline std::string field(const std::map<std::string, std::string>& decoded, const std::string& name) {
        const auto found = decoded.find(name);
        return found == decoded.end() ? std::string{} : trim(found->second);
      }

      inline std::optional<std::size_t> parameterIndex(std::string parameterName) {
        parameterName = trim(std::move(parameterName));
        // Current Fitter output appends a histogram/mass token after the TF1
        // parameter name.  It is metadata, not part of the DSCB row identity.
        const auto whitespace = parameterName.find_first_of(" \t");
        if (whitespace != std::string::npos) {
          parameterName.erase(whitespace);
        }

        static const std::map<std::string, std::size_t> exactNames{{"#alpha_{low}", AlphaLow},
                                                                   {"alpha_low", AlphaLow},
                                                                   {"#alpha_{high}", AlphaHigh},
                                                                   {"alpha_high", AlphaHigh},
                                                                   {"n_{low}", NLow},
                                                                   {"n_low", NLow},
                                                                   {"n_{high}", NHigh},
                                                                   {"n_high", NHigh},
                                                                   {"#mu", Mean},
                                                                   {"mean", Mean},
                                                                   {"#sigma", Sigma},
                                                                   {"sigma", Sigma},
                                                                   {"norm", Normalization},
                                                                   {"normalization", Normalization}};
        const auto exact = exactNames.find(parameterName);
        if (exact != exactNames.end()) {
          return exact->second;
        }

        // A few older files suffix the projection directly on the parameter.
        for (const auto& [prefix, index] : exactNames) {
          if (parameterName.rfind(prefix + "_mll", 0) == 0) {
            return index;
          }
        }
        return std::nullopt;
      }

      inline Variation parseVariation(std::string descriptor) {
        descriptor = trim(std::move(descriptor));
        if (descriptor.empty() || descriptor == "Nominal") {
          return {};
        }

        constexpr const char* upSuffix = " Up";
        constexpr const char* downSuffix = " Down";
        if (descriptor.size() > 3 && descriptor.compare(descriptor.size() - 3, 3, upSuffix) == 0) {
          return {VariationDirection::Up, trim(descriptor.substr(0, descriptor.size() - 3))};
        }
        if (descriptor.size() > 5 && descriptor.compare(descriptor.size() - 5, 5, downSuffix) == 0) {
          return {VariationDirection::Down, trim(descriptor.substr(0, descriptor.size() - 5))};
        }
        throw std::invalid_argument("Unrecognized systematic descriptor '" + descriptor +
                                    "' (expected Nominal, '<name> Up', or '<name> Down')");
      }

      inline ShapeSystematics::ParameterVector coefficients(const TF1& function) {
        if (function.GetNpar() < 3) {
          throw std::invalid_argument("Parameterization '" + std::string(function.GetName()) +
                                      "' has fewer than three PowerLaw coefficients");
        }
        ShapeSystematics::ParameterVector result;
        result.reserve(static_cast<std::size_t>(function.GetNpar()));
        for (int index = 0; index < function.GetNpar(); ++index) {
          const double value = function.GetParameter(index);
          if (!std::isfinite(value)) {
            throw std::invalid_argument("Parameterization '" + std::string(function.GetName()) +
                                        "' contains a non-finite coefficient at index " + std::to_string(index));
          }
          result.push_back(value);
        }
        return result;
      }

      inline bool equivalent(const ShapeSystematics::ParameterVector& left,
                             const ShapeSystematics::ParameterVector& right) {
        if (left.size() != right.size()) {
          return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
          const double scale = std::max({1.0, std::abs(left[index]), std::abs(right[index])});
          if (std::abs(left[index] - right[index]) > 1e-12 * scale) {
            return false;
          }
        }
        return true;
      }

      inline void assign(OptionalRow& destination,
                         ShapeSystematics::ParameterVector values,
                         const std::string& description) {
        if (destination && !equivalent(*destination, values)) {
          throw std::invalid_argument("Conflicting parameterizations for " + description);
        }
        destination = std::move(values);
      }

      inline bool complete(const OptionalMatrix& matrix) {
        return std::all_of(matrix.begin(), matrix.end(), [](const OptionalRow& row) { return row.has_value(); });
      }

      inline ShapeSystematics::ParameterMatrix materialize(const OptionalMatrix& matrix) {
        ShapeSystematics::ParameterMatrix result;
        result.reserve(dscbParameterCount);
        for (const auto& row : matrix) {
          result.push_back(*row);
        }
        return result;
      }

      inline std::string missingRows(const OptionalMatrix& matrix) {
        static const std::array<const char*, dscbParameterCount> names{
            "alpha_low", "alpha_high", "n_low", "n_high", "mean", "sigma", "norm"};
        std::ostringstream result;
        bool first = true;
        for (std::size_t index = 0; index < matrix.size(); ++index) {
          if (!matrix[index]) {
            result << (first ? "" : ", ") << names[index];
            first = false;
          }
        }
        return result.str();
      }

    }  // namespace detail

    inline ExtractionResult extractDSCBModels(FitFunctionCollection& collection,
                                              const ExtractionOptions& options = {}) {
      std::map<ModelKey, detail::PendingModel> pending;
      std::map<ModelKey, std::set<std::string>> unsupportedParameters;
      ExtractionResult result;

      for (auto& [storedName, fitFunction] : collection.getFunctions()) {
        (void)storedName;
        const auto decoded = detail::decodeName(fitFunction.getName());
        const std::string reco = detail::field(decoded, "Reco");
        const std::string genSim = detail::field(decoded, "GenSim");
        if (reco.empty() || genSim.empty()) {
          continue;  // Background/differently encoded functions.
        }
        const ModelKey key{reco, genSim, detail::field(decoded, "Projection")};
        const std::string rawParameter = detail::field(decoded, "Parameter");
        const auto parameter = detail::parameterIndex(rawParameter);
        if (!parameter) {
          unsupportedParameters[key].insert(rawParameter);
          continue;
        }
        if (fitFunction.getFunctionType() != FitFunction::FunctionType::PowerLaw) {
          result.warnings.push_back("Ignoring non-PowerLaw DSCB parameter row in " + fitFunction.getName());
          continue;
        }

        auto& model = pending[key];
        const auto variation = detail::parseVariation(detail::field(decoded, "Systematic"));
        const auto nominalValues = detail::coefficients(*fitFunction.getFunction());
        const std::string rowDescription = key.label() + " row " + std::to_string(*parameter);

        if (variation.direction == detail::VariationDirection::Nominal) {
          detail::assign(model.nominal[*parameter], nominalValues, "nominal " + rowDescription);

          // Also support the alternate representation where each nominal
          // FitFunction owns absolute up/down TF1 alternatives.  A few older
          // CMSAnalysis checkouts declare these methods but do not link their
          // definitions; the modern-name API is a safe compatibility marker.
          if constexpr (detail::hasModernFitFunctionApi<FitFunction>::value) {
            for (const auto& systematic : fitFunction.listSystematics()) {
              const TF1* upFunction = fitFunction.getSystematic(systematic, true);
              const TF1* downFunction = fitFunction.getSystematic(systematic, false);
              if (upFunction == nullptr || downFunction == nullptr) {
                throw std::invalid_argument("Embedded systematic '" + systematic + "' on " + rowDescription +
                                            " is missing an up or down function");
              }
              detail::assign(model.up[systematic][*parameter],
                             detail::coefficients(*upFunction),
                             systematic + " up " + rowDescription);
              detail::assign(model.down[systematic][*parameter],
                             detail::coefficients(*downFunction),
                             systematic + " down " + rowDescription);
            }
          }
        } else if (variation.direction == detail::VariationDirection::Up) {
          detail::assign(model.up[variation.name][*parameter], nominalValues, variation.name + " up " + rowDescription);
        } else {
          detail::assign(
              model.down[variation.name][*parameter], nominalValues, variation.name + " down " + rowDescription);
        }
      }

      for (const auto& [key, parameters] : unsupportedParameters) {
        ExtractionResult::UnsupportedModel unsupported;
        unsupported.key = key;
        unsupported.parameterNames.assign(parameters.begin(), parameters.end());
        result.unsupportedModels.push_back(unsupported);

        std::ostringstream message;
        message << "Found unsupported non-DSCB parameterization for " << key.label() << ": ";
        bool first = true;
        for (const auto& parameter : parameters) {
          message << (first ? "" : ", ") << parameter;
          first = false;
        }
        result.warnings.push_back(message.str());
      }

      for (auto& [key, source] : pending) {
        if (!detail::complete(source.nominal)) {
          result.warnings.push_back("Skipping incomplete/non-DSCB model " + key.label() +
                                    "; missing nominal rows: " + detail::missingRows(source.nominal));
          continue;
        }

        SignalModel model;
        model.key = key;
        model.nominal = detail::materialize(source.nominal);

        std::set<std::string> names;
        for (const auto& [name, rows] : source.up) {
          (void)rows;
          names.insert(name);
        }
        for (const auto& [name, rows] : source.down) {
          (void)rows;
          names.insert(name);
        }

        for (const auto& name : names) {
          const auto up = source.up.find(name);
          const auto down = source.down.find(name);
          const bool isComplete = up != source.up.end() && down != source.down.end() && detail::complete(up->second) &&
                                  detail::complete(down->second);
          if (!isComplete) {
            std::string message = "Systematic '" + name + "' is incomplete for " + key.label();
            if (up == source.up.end()) {
              message += "; missing all up rows";
            } else if (!detail::complete(up->second)) {
              message += "; missing up rows: " + detail::missingRows(up->second);
            }
            if (down == source.down.end()) {
              message += "; missing all down rows";
            } else if (!detail::complete(down->second)) {
              message += "; missing down rows: " + detail::missingRows(down->second);
            }

            if (options.requireCompleteSystematics) {
              throw std::invalid_argument(message);
            }
            result.warnings.push_back("Skipping " + message);
            continue;
          }

          model.systematicNames.push_back(name);
          model.up.push_back(detail::materialize(up->second));
          model.down.push_back(detail::materialize(down->second));
        }

        ShapeSystematics::validateSystematicInputs(model.nominal, model.systematicNames, model.up, model.down);
        result.models.push_back(std::move(model));
      }

      return result;
    }

    inline std::vector<std::string> uniqueSystematicNames(const std::vector<SignalModel>& models) {
      std::set<std::string> names;
      for (const auto& model : models) {
        names.insert(model.systematicNames.begin(), model.systematicNames.end());
      }
      return {names.begin(), names.end()};
    }

  }  // namespace SignalSystematics
}  // namespace LimitSetting

#endif
