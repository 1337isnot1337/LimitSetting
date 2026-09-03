#include "RooPDF_HiggsAnalysis_DSCB.h"
#include "CMSAnalysis/Analysis/interface/FitFunction.hh"

#include "TF1.h"
#include "RooAbsReal.h"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using ParameterTable = std::vector<std::vector<double>>;

ParameterTable getNominalParameters(std::map<std::string, FitFunction>& fitFunctions)
{
    ParameterTable parameters;

    for (auto& [name, fitFunction] : fitFunctions)
    {
        TF1* function = fitFunction.getFunction();
        std::vector<double> values;

        for (int i = 0; i < function->GetNpar(); i++)
        {
            values.push_back(function->GetParameter(i));
        }

        parameters.push_back(values);
    }

    return parameters;
}

ParameterTable getSystematicParameters(const std::map<std::string, FitFunction>& fitFunctions,
                                       const std::string& systematicName,
                                       bool up)
{
    ParameterTable parameters;

    for (const auto& [name, fitFunction] : fitFunctions)
    {
        const TF1* function = fitFunction.getSystematic(systematicName, up);

        if (function == nullptr)
        {
            throw std::runtime_error("Missing systematic '" + systematicName + "' for " + name);
        }

        std::vector<double> values;
        for (int i = 0; i < function->GetNpar(); i++)
        {
            values.push_back(function->GetParameter(i));
        }

        parameters.push_back(values);
    }

    return parameters;
}

RooPDF_HiggsAnalysis_DSCB createSignalPdfWithSystematic(
    const std::string& pdfName,
    std::map<std::string, FitFunction>& fitFunctions,
    const std::string& systematicName,
    RooAbsReal& mass,
    RooAbsReal& realHiggsMass,
    RooAbsReal& branchRatio1,
    RooAbsReal& branchRatio2,
    RooAbsReal& normalizationSystematic,
    RooAbsReal& delta,
    bool multiplyBy2)
{
    ParameterTable nominal = getNominalParameters(fitFunctions);
    ParameterTable up = getSystematicParameters(fitFunctions, systematicName, true);
    ParameterTable down = getSystematicParameters(fitFunctions, systematicName, false);

    return RooPDF_HiggsAnalysis_DSCB(
        pdfName.c_str(),
        pdfName.c_str(),
        mass,
        realHiggsMass,
        branchRatio1,
        branchRatio2,
        normalizationSystematic,
        delta,
        nominal,
        up,
        down,
        multiplyBy2);
}
