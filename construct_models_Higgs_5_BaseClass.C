#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <typeinfo>

#include "TFile.h"
#include "TF1.h"
#include "TGraph.h"
#include "TCanvas.h"
#include "TROOT.h"
#include "RooArgSet.h"
#include "RooArgList.h"
#include "RooRealVar.h"
#include "RooFormulaVar.h"
#include "RooDataSet.h"
#include "RooPlot.h"
#include "RooWorkspace.h"
#include "RooProduct.h"
#include "RooDataHist.h"
#include "TTree.h"

#include "RooAbsReal.h"
#include "RooRealSumPdf.h"
#include "RooAddPdf.h"
#include "RooRealConstant.h"
#include "RooConstVar.h"
#include "RooClassFactory.h"
#include "RooPDF_HiggsAnalysis_Base.h"
#include "RooPDF_HiggsAnalysis_DSCB.h"
#include "RooPDF_HiggsAnalysis_BKG.h"
#include "RooArgList.h"
#include "RooGenericPdf.h"
#include "CMSAnalysis/Analysis/interface/FitFunction.hh"
#include "CMSAnalysis/Analysis/interface/FitFunctionCollection.hh"



struct Channel
{
	std::string name;
	std::map<std::string, FitFunction> fitFunctions;
	Channel(std::string channelName, std::vector<FitFunction> channelFunctions);
	std::vector<std::vector<double>> extractParameters();
};

void construct_models_Higgs_5();

std::vector<double> split(const std::string& line);
std::string replaceAll(std::string unmodifiedString, const std::string from, const std::string to);

std::vector<std::string> splitLine(const std::string& str);
std::vector<std::vector<double>> getParameters(std::vector<std::string> channelParameters);
TGraph makeGraph(double numCoords, std::vector<double>& xCoords, std::vector<double>& yCoords);

RooArgList create_bkg_pdf(std::string channel_name,
                          Channel backgroundChannel, 
                          RooRealVar& mass, RooRealVar& realHiggsMass, RooRealVar& branch_1, RooRealVar& branch_2, RooRealVar& norm_Systematic, RooRealVar& shape_Systematic);
RooArgList create_signal_pdf(std::string channel_name, std::vector<std::string> parameterSet, RooRealVar& mass, RooRealVar& realHiggsMass, RooRealVar& branch_1, RooRealVar& branch_2, RooRealVar& norm_Systematic, RooRealVar& shape_Systematic);
RooFormulaVar get_signal_norm(std::string channel_name, std::vector<std::string> parameterSet, RooRealVar& realHiggsMass);
std::map<std::string, std::vector<double>> getParameterValuesFromMap(std::string fileName, std::vector<string>* channelNames);
bool containsSubstring(std::string mainString, std::string subString);
std::map<std::string, std::vector<FitFunction>> getNominals(std::map<std::string, std::vector<FitFunction>>* originalSignal);
bool shouldSkipChannel(std::string channelName); // Temporary function for skipping certain channels like eeee_eeet
std::vector<std::string> splitByDelimiter(const std::string& str, char delimiter);

Channel::Channel(std::string channelName, std::vector<FitFunction> channelFunctions)
{
	name = channelName;
	for (auto channelFunction : channelFunctions)
	{
		std::string functionName = channelFunction.getParameterName();
		fitFunctions.insert({functionName,  channelFunction});
	}
}

std::vector<std::vector<double>> Channel::extractParameters()
{
	std::vector<std::vector<double>> extractedParameters = {};

	// Record names of functions and alphabetize them
	std::vector<std::string> functionNames = {};
	for (auto [functionName, function] : fitFunctions)
	{
		functionNames.push_back(functionName);
	}
	std::sort(functionNames.begin(), functionNames.end());
	

	// Print alphabetized parameters:
	std::cout << "Channel: " << name << "\n";
	std::cout << "Number of functions: " << functionNames.size() << "\n";
	// std::cout << "Alphabetized Parameters:" << "\n";
	// for (auto functionName : functionNames)
	// {
	// 	std::cout << functionName << "\n";
	// }
	// if (containsSubstring(name, "eeee"))
	// {
	// 	std::cout << "Channel: " << name << "\n";
	// 	std::cout << "Number of parameters: " << functionNames.size() << "\n";
	// 	for (auto functionName : functionNames)
	// 	{
	// 		std::cout << functionName << "\n";
	// 	}
	// }



	// Goes through each function (in alphabetical order) and extracts their parameters 
	for (auto functionName : functionNames)
	{
		auto fitFunc = fitFunctions.find(functionName);
		auto function = fitFunc->second.getFunction();
		
		std::vector<double> functionParameters = {};

		for (int i = 0; i < function->GetNpar(); i++)
		{
			functionParameters.push_back(function->GetParameter(i));
		}
		for (int i = 0; i < function->GetNpar(); i++)
		{
			functionParameters.push_back(function->GetParError(i));
		}

		extractedParameters.push_back(functionParameters);
	}

	return extractedParameters;
}



// Splits parameter lines by spaces, skipping the first entry (which I used as a name and not a value I actually need)
std::vector<double> split(const std::string& line)
{
	std::istringstream iss(line); 
	std::vector<double> tokens; 
	std::string token; 
	bool skip = true;
	while (iss >> token) 
	{ 
		if (skip)
		{
			skip = false;
		}
		else
		{
			tokens.push_back(std::stod(token)); 
		}
	} 
	return tokens;
}

std::vector<std::string> splitLine(const std::string& line)
{
	std::istringstream iss(line); 
	std::vector<std::string> tokens; 
	std::string token; 
	while (iss >> token) 
	{ 
		tokens.push_back(token); 
		
	} 
	// int i = 0;
	// for (std::string token : tokens)
	// {
	// 	std::cout << i << '	' << token << '\n';
	// 	i++;
	// }
	return tokens;
}

// Helper function for finding substring 
bool containsSubstring(std::string mainString, std::string subString)
{
	if (mainString.find(subString) != std::string::npos)
	{
		return true;
    } 
	else 
	{
		return false;
    }

}

bool shouldSkipChannel(std::string channelName)
{
	std::vector<std::string> splitString = splitByDelimiter(channelName, '_');
	if (splitString[0] == splitString[1])
	{
		std::cout << "False" << '\n';
		return false;
	}
	else
	{
		std::cout << "True" << '\n';
		return true;
	}
}


std::map<std::string, std::vector<FitFunction>> getNominals(std::map<std::string, std::vector<FitFunction>>* originalSignal)
{
	std::map<std::string, std::vector<FitFunction>> nominalsOnly;
	for (auto [channel, fitFunctions] : *originalSignal)
	{
		std::vector<FitFunction> onlyNominalFunctions;
		for (auto& fitFunction : fitFunctions)
		{
			std::string parameterName = fitFunction.getParameterName(); 
			if (containsSubstring(parameterName, "Nominal"))
			{
				onlyNominalFunctions.push_back(fitFunction);
			}
		}
		nominalsOnly[channel] = onlyNominalFunctions;
	}
	return nominalsOnly;
}

// Helper function for removing substrings
std::string removeSubstring(const std::string& original, const std::string& toRemove) 
{
    if (toRemove.empty()) return original;  // Nothing to remove

    std::string result = original;
    size_t pos;

    // Find and erase all occurrences
    while ((pos = result.find(toRemove)) != std::string::npos) {
        result.erase(pos, toRemove.length());
    }

    return result;
}

std::string replaceAll(std::string unmodifiedString, const std::string from, const std::string to)
{
    if (from.empty())
        return unmodifiedString; // avoid infinite loop if 'from' is empty

    std::string str = unmodifiedString;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // move past the replaced part
    }
    
    return str;

}


// Helper function for splitting strings
std::vector<std::string> splitByDelimiter(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    
	int i = 0;
	for (std::string token : tokens)
	{
		std::cout << i << '	' << token << '\n';
		i++;
	}
    return tokens;
}

// Gets the signal/background parameters from lines of a file; skips the first few lines, which are used as explanation for organization
std::vector<std::vector<double>> getParameters(std::vector<std::string> channelParameters)
{
	std::vector<std::vector<double>> parameterArrays;

	// std::cout << channelParameters[0] << '\n';
	// std::cout << channelParameters[1] << '\n';
	// Starts at i = 3 to skip first few lines
	for (size_t i = 3; i < channelParameters.size(); i++)
	{
		//std::cout << channelParameters[i] << '\n';
		parameterArrays.push_back(split(channelParameters[i]));
	}
	return parameterArrays;
}

// Creates a RooPDF_DSCB_test object for a channel (The naming stuff is for consistency, I don't think it matters since I am copying it anyway)
// THERE IS A MEMORY LEAK (USING THE *pdf)
RooArgList create_signal_pdf(std::string channel_name, Channel signalChannel, RooRealVar& mass, RooRealVar& realHiggsMass, RooRealVar& branch_1, RooRealVar& branch_2, RooRealVar& norm_Systematic, RooRealVar& shape_Systematic, std::string X_or_Y)
{
	std::vector<std::vector<double>> parameters = signalChannel.extractParameters();
	//std::vector<std::vector<double>> parameters = getParameters(parameterSet);
	// (The naming stuff is for consistency, I don't think it matters since I am copying the object out of the function anyway)
	auto* pdf = new RooPDF_HiggsAnalysis_DSCB((channel_name + "_signal_" + X_or_Y).c_str(), (channel_name + "_signal").c_str(), mass, realHiggsMass, branch_1, branch_2, norm_Systematic, shape_Systematic, parameters, false); 
	// std::cout << "Class Name Before Adding to List: ";
	// pdf->printClassName(std::cout);
	// std::cout << "\n";
	RooArgList list;
	list.add(*pdf);
	// std::cout << "Class Name After Adding to List: ";
	//list.at(0)->printClassName(std::cout);
	// std::cout << "\n";
	return list;
}

// Creates the Signal Normalization Object for a channel - The Normalization object must vary with mass when it is added to workspace, which is why it is a FormulaVar and not just be the integral over the signal pdf object 
RooFormulaVar get_signal_norm(std::string channel_name, std::vector<std::string> parameterSet, RooRealVar& realHiggsMass)
{
	std::cout << "signal_norm_error0" << "\n";
	std::vector<std::vector<double>> parameters = getParameters(parameterSet);

	//std::cout << "get_signal_norm: " << channel_name << '\n';
	// for (std::vector<double> parameterLine : parameters)
	// {
	// 	for (double parameterValue : parameterLine)
	// 	{
	// 		std::cout << parameterValue << ", ";
	// 	}
	// 	std::cout << '\n';
	// }

	std::cout << "signal_norm_error1" << "\n";

	std::string norm_string = std::to_string(parameters[6][0]) +  "* (@0 - " + std::to_string(parameters[6][1]) + ")^" + std::to_string(parameters[6][2]) + " + " + std::to_string(parameters[6][3]);
	
	std::cout << "signal_norm_error2" << "\n";

	// (The naming stuff is for consistency, I don't think it matters since I am copying the object out of the function anyway)
	RooFormulaVar norm((channel_name + "_signal_norm").c_str(), (channel_name + "_signal_norm").c_str(), norm_string.c_str(), RooArgList(realHiggsMass)); 

	std::cout << "signal_norm_error3" << "\n";
	return norm;
}

// Creates a RooPDF_HiggsAnalysis_BKG Object 
RooArgList create_bkg_pdf(std::string channel_name,
                          Channel backgroundChannel, 
                          RooRealVar& mass, RooRealVar& realHiggsMass, RooRealVar& branch_1, RooRealVar& branch_2, RooRealVar& norm_Systematic, RooRealVar& shape_Systematic, std::string X_or_Y)
{
    std::vector<std::vector<double>> bkg_types_params = backgroundChannel.extractParameters();

    auto* pdf = new RooPDF_HiggsAnalysis_BKG(
        (channel_name + "_bkg_" + X_or_Y).c_str(),
        (channel_name + "_bkg").c_str(),
        mass, 
		realHiggsMass, 
		branch_1, 
		branch_2, 
		norm_Systematic, 
		shape_Systematic,
        bkg_types_params,
        channel_name
    );

    //std::cout << "Class Name Before Adding to List: ";
    pdf->printClassName(std::cout);
    std::cout << "\n";

    RooArgList list;
    list.add(*pdf);

    //std::cout << "Class Name After Adding to List: ";
    list.at(0)->printClassName(std::cout);
    std::cout << "\n";

    return list;
}

// std::map<std::string, std::vector<std::vector<double>>> getParametersFromMap(std::map<std::string, std::vector<FitFunction>> sortedFunctions, std::vector<string>* channelNames)
// {
// 	std::map<std::string, std::vector<std::vector<double>>> parameters;
 
// 	for (const auto [channel, parameters] : sortedFunctions)
// 	{
// 		// Record channels
// 		if (std::find(channelNames->begin(), channelNames->end(), channel) == channelNames->end())
// 		{
// 			channelNames->push_back(channel);
// 		}
		
// 		// Retrieve parameters from a channel
// 		for (auto fitFunctionParameter : parameters)
// 		{
// 			auto parameter = fitFunctionParameter.getFunction();
// 			std::vector<double> parameterValues;
// 			for (int i = 0; i < parameter->GetNpar(); ++i)
// 			{
// 				parameterValues.push_back(parameter->GetParameter(i));
// 			}

// 			for (int i = 0; i < parameter->GetNpar(); ++i)
// 			{
// 				parameterValues.push_back(parameter->GetParError(i));
// 			}

// 			parameters[channel].push_back(parameterValues);
// 		}

// 	}
// 	return parameters;
// }


std::map<std::string, std::vector<FitFunction>> getFunctionsSortedByChannel(std::string fileName)
{
    std::map<std::string, std::vector<FitFunction>> sortedFunctions = {};
    std::vector<std::string> channelNames = {};

    std::map<std::string, std::string> channelNameModifiers = {};
    channelNameModifiers["mll1"] = "_X";
    channelNameModifiers["mll2"] = "_Y";
	channelNameModifiers["X projection"] = "_X";
	channelNameModifiers["Y projection"] = "_Y";


    std::vector<std::string> existingChannelNameModifiers = {};
    for (auto [modifier, replacement] : channelNameModifiers)
    {
        existingChannelNameModifiers.push_back(modifier);
    }


    FitFunctionCollection functionCollection;
    try {
        functionCollection = FitFunctionCollection::loadFunctions(fileName);
        std::cout << "Successfully read " << fileName << "\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to load functions: " << e.what() << std::endl;
        throw;
    }


    // Sorts functions by channel
    auto parameterFunctions = functionCollection.getFunctions();
    for (auto pair : parameterFunctions)
    {
        auto functionName = pair.first;
        auto function = pair.second;
        // std::string channel = function.getChannel();
		std::string channel = function.getName(); //temp change just so it builds, porbably breaks stuff
        //std::string channel = replaceAll(unformattedChannel, "m", "u");
        //std::cout << "DEBUG: channel name for function: " << channel << "    " << function.getName() << "\n";
		//std::cout << "DEBUG: nameOFfFunction: " << functionName << "\n \n";
        // Check whether channel name needs to be modified for internal processes
        for (auto modifier : existingChannelNameModifiers)
        {
            if (function.getParameterName().find(modifier) != std::string::npos)
            {
                channel += channelNameModifiers[modifier];
            }
        }

		sortedFunctions[channel].push_back(function);

        // Record channels
        // if (std::find(channelNames.begin(), channelNames.end(), channel) == channelNames.end())
        // {
        //     channelNames.push_back(channel);
        // }
    }

    // Alphabetize functions for each channel
    // for (std::string channelName : channelNames)
    // {
    //     std::vector<FitFunction> alphabetizedFunctions = alphabetizeParameters(sortedFunctions[channelName]);
    //     sortedFunctions[channelName] = alphabetizedFunctions;
    // }

    return sortedFunctions;
}



void construct_models_Higgs_5_BaseClass()
{
	gROOT->SetBatch(true); // I can't remember what this does exactly, but it needs to be here

	// Get the Signal Events from Monte Carlo
	// We will be fitting our model to these events
	// The X and Y refer to the sign (+ or -) of the leptons, which can be plotted on the X and Y axises

	TFile file("Usefulstuff/output900.root","READ");
	auto hist_X = file.Get<TTree>("Signal");
	// auto hist_Y = file.Get<TTree>("Signal");      <------ When We Get both data sets, uncomment this and fix mc_Y below

	RooRealVar mass("mass", "mass", 900, 50, 2000); // This is the invariant mass (energy) of the event and is the independent variable for the background and Signal PDFs

	// This converts the TTree to a RooDataSet correlated to / dependent on the mass RooRealVar.
	RooDataSet mc_X("Events900_X","Events900", RooArgSet(mass), RooFit::Import(*hist_X));
	RooDataSet mc_Y("Events900_Y","Events900", RooArgSet(mass), RooFit::Import(*hist_X)); // When we get both data sets, change to hist_Y

	// Define the RooRealVars we are fitting and Scanning over.
	// realHiggsMass represents mass of the Higgs we are looking for. The others are the branching ratio of the H++ decay channels.
	// Currently, we only scan over mass. Later, we will look for most likely Branching ratio values for a given mass through fits and other scans
	RooRealVar realHiggsMass("realHiggsMass", "realHiggsMass",1500, 200,2000);
	RooRealVar Bee("b_ee", "b_ee", 1, 0,1);
	RooRealVar Beu("b_eu", "b_eu",1, 0,1);
	//RooRealVar norm_Systematic("normalization_systematic", "normalization_systematic", 0, -5, 5);
	RooRealVar norm_Systematic("normalization_systematic", "normalization_systematic", 1, 0, 1);
	//RooRealVar shape_Systematic("shape_systematic", "shape_systeamtic", 1, 0, 1); 
	RooRealVar shape_Systematic("shape_systematic", "shape_systematic", 0, -5, 5);
	norm_Systematic.setConstant(true);
	shape_Systematic.setConstant(true);
	
	// For now, we don't need this variable to be able to change
	Bee.setConstant(true);
	Beu.setConstant(true);

	// ----------------------------------------------------------------------------------------------------------
	// The higgsworkspace is where all of the RooFit objects are stored and manipulated.
	// Each object used by the datacard must be imported
	// Some objects, like the realHiggsMass, are implicitly imported when the PDFs or other objects that depend on them are imported.
	// The original PDFs that we copied are not imported
	TFile f_out("higgsworkspace.root", "RECREATE");
	RooWorkspace w_sig("higgsworkspace","higgsworkspace");


	w_sig.import(mc_X);
	w_sig.import(mc_Y);


	//--------------------------------------------------------------------------------------------------
	// Building the Signal Models
	// Each signal decay channel is defined by a Double-Sided Crystal Ball Function using the above parameters.
	// RooPDF_DSCB_test is custom ; it was made using RooClassFactory::makePdf("RooPDF_DSCB_test"....)
	// We Currently have 6 Channels per sign, this may increase a lot later, so a new way of listing may be more efficient

	// Note on PDFs: RooAbsPdf is not explicitly normalized. To get normalized results, parameters to normalize over must be provided
	// For example, eeee_signal.getVal() will return the base value of the pdf (not normalized). eeee_signal.getVal(RooArgList(mass)) normalizes over the mass variable.	

	// Not entirely sure what this does, since I would get issues if i didn't run these files before this one anyway, but its here...

	


	// We will want to change how these lists are handled..

	// std::vector<std::string> channel_names {"eeee", "eeeu", "eeuu", "eueu", "euuu", "uuuu"};
	std::vector<std::string> signs {"X", "Y"};
	//std::vector<std::string> channelsToCheck = {"eeee_X", "eeee_Y", "uuuu_X", "uuuu_Y"};
	//std::vector<std::string> channelsToCheck = {"eeee", "uuuu"};
	//  std::vector<std::string> channelsToCheck = {"eeee"};
	//std::vector<std::string> channelsToCheck = {"eeee", "eeeu"};
	  std::vector<std::string> channelsToCheck = {"eeee", "eeeu", "eeuu"};
	//std::vector<std::string> channelsToCheck = {"eeee", "eeeu", "eeuu", "eueu"};
	// std::vector<std::string> channelsToCheck = {"eeee", "eeeu", "eeuu", "eueu", "euuu"};
	 // std::vector<std::string> channelsToCheck = {"eeee", "eeeu", "eeuu", "eueu", "euuu", "uuuu"};
	//std::vector<std::string> channelsToCheck = {"eeee", "eeeu", "eueu", "euuu", "uuuu"};

	// Get signal and background parameters from files - Note: Update file paths later
	// /uscms/home/bhobbs/Analysis/CMSSW_15_0_4/src/CMSAnalysis/Analysis/bin/fitting/H++SignalParameterFunctions
	std::string signalParamsFileName = "/uscms/home/jdavis1/analysis/CMSSW_15_0_10/src/CMSAnalysis/Analysis/bin/fitting/H++SignalParameterFunctions.txt";
	//std::string signalParamsFileName = "/home/chenh/lpc/analysis/CMSSW_15_0_4/src/CMSAnalysis/Analysis/bin/fitting/H++SignalParameterFunctions.txt";
	//std::string signalParamsFileName = "H++SignalParameterFunctions.txt";
	std::string backgroundParamsFileName = "H++BackgroundFunctions.txt";
	//std::string backgroundParamsFileName = "/uscms/home/kprasad/cmsReleaseArea/CMSSW_15_0_4/src/CMSAnalysis/Analysis/bin/fitting/H++BackgroundFunctions.txt";
	//std::string backgroundParamsFileName = "/uscms/home/bhobbs/Analysis/CMSSW_15_0_4/src/CMSAnalysis/Analysis/bin/fitting/OtherBackgroundFunctions.txt";
	std::map<std::string, std::vector<FitFunction>> allSignalParameters = getFunctionsSortedByChannel(signalParamsFileName);
	std::map<std::string, std::vector<FitFunction>> allBackgroundParameters = getFunctionsSortedByChannel(backgroundParamsFileName);  
	std::map<std::string, std::vector<FitFunction>> signalParameters = getNominals(&allSignalParameters);  
	std::map<std::string, std::vector<FitFunction>> backgroundParameters = getNominals(&allBackgroundParameters);


	std::vector<Channel> signalChannels = {};
	std::vector<Channel> backgroundChannels = {};
	
	std::map<std::string,std::string> unmodifiedNames;
	for (auto [unmodifiedChannelName, fitFunctions] : signalParameters)
	{

		std::string channel = replaceAll(unmodifiedChannelName, "m", "u");
		std::cout << "channel: " << channel << '\n';
		// Check if channel name Is valid and split channel name - NOTE: THIS IS TEMPORARY
		if (shouldSkipChannel(channel))
			continue;
		std::vector<std::string> channelNameSplit = splitByDelimiter(channel, '_');
		Channel signalChannel(channelNameSplit[0], fitFunctions);
		signalChannels.push_back(signalChannel);
	}

	// Create background Channel objects
	for (auto [unmodifiedChannelName, fitFunctions] : backgroundParameters)
	{
		std::string channel = replaceAll(unmodifiedChannelName, "m", "u");
		Channel backgroundChannel(channel, fitFunctions);
		backgroundChannels.push_back(backgroundChannel);
	}


	// Extract signal parameters
	std::cout << "Extracting signal parameters..." << "\n";
	for (auto channel : signalChannels)
	{
		std::vector<std::vector<double>> parameters = channel.extractParameters();
		//std::cout << "Channel: " << channel.name << "\n" << "Extracted Signal Parameters size: " << parameters.size() << "\n";
		// Print out signal parameter values
		// for (auto doubleVector : parameters)
		// {
		// 	for (auto value : doubleVector)
		// 	{
		// 		 std::cout << value << "\n";
		// 	}
		// }
		//std::cout << "\n \n \n";
		std::cout << "\n";
	}
	std::cout << "\n \n \n";

	// Extract background parameters
	std::cout << "Extracting background parameters..." << "\n";
	for (auto channel : backgroundChannels)
	{
		std::vector<std::vector<double>> parameters = channel.extractParameters();
		std::cout << "Channel: " << channel.name << "\n" << "Extracted Background Parameters size: " << parameters.size() << "\n";
		for (auto doubleVector : parameters)
		{
			for (auto value : doubleVector)
			{
				// std::cout << value << "\n";
			}
		}

		// Print out value of background parameters
		// for (auto doubleVector : parameters)
		// {
		// 	for (auto value : doubleVector)
		// 	{
		// 		std::cout << value << "\n";
		// 	}
		// }

		//std::cout << "\n \n \n";
		std::cout << "\n";
	}
	
	std::vector<std::vector<RooArgList>> signal_pdfs;
	std::vector<std::vector<std::string>> signal_pdfsNames;

	std::vector<std::vector<RooArgList>> bkg_pdfs;
	std::vector<std::vector<std::string>> bkg_pdfsNames;

	std::vector<std::vector<RooFormulaVar>> signal_Normalizations;
	std::vector<std::vector<std::string>> signal_NormalizationsNames;

	std::vector<std::vector<RooRealVar>> bkg_Normalizations;
	std::vector<std::vector<std::string>> bkg_NormalizationsNames;

	std::vector<std::string> completedChannels;

	for (std::string channel : channelsToCheck)
	{
		if (std::find(completedChannels.begin(), completedChannels.end(), channel) != completedChannels.end())
		{
			std::cout << channel << " has already been processed\n";
			continue;
		}

		std::cout << "Processing " << channel << " channels\n";

		std::vector<RooArgList> signal_X_and_Y;
		std::vector<std::string> signal_X_and_YNames;

		std::vector<RooArgList> bkg_X_and_Y;
		std::vector<std::string> bkg_X_and_YNames;

		std::vector<RooFormulaVar> signal_X_and_Y_Normalizations;
		std::vector<std::string> signal_X_and_Y_NormalizationsNames;

		std::vector<RooRealVar> bkg_X_and_Y_Normalizations;
		std::vector<std::string> bkg_X_and_Y_NormalizationsNames;

		for (std::string X_or_Y : signs)
		{
			std::string channelX_or_Y = channel + "_" + X_or_Y;

			// =====================================================
			// COLLECT ALL MATCHING SIGNAL CHANNELS
			// =====================================================
			std::vector<Channel*> matchedSignalChannels;

			for (auto& channelToProcess : signalChannels)
			{
				std::string outputChannelToCheck =
					channelToProcess.name.substr(0,4) + "_" + X_or_Y;

				std::cout << "SIG search: " << outputChannelToCheck
						<< " vs " << channelX_or_Y << "\n";

				if (outputChannelToCheck == channelX_or_Y)
				{
					matchedSignalChannels.push_back(&channelToProcess);
				}
			}

			// =====================================================
			// FIND BACKGROUND CHANNEL (single)
			// =====================================================
			Channel* currentBackgroundChannel = nullptr;

			for (auto& channelToProcess : backgroundChannels)
			{
				std::cout << "BKG search: " << channelToProcess.name
						<< " vs " << channelX_or_Y << "\n";


				//std::cout << "Name of BKG Channel Function Name Currently Being Searched For: " << channelToProcess.name << "\n";
				//std::cout << "Name of BKG Channel To Compare: " << channelX_or_Y << "\n";
				if (channelToProcess.name == channelX_or_Y)
				{
					//std::cout << "yay \n";
					currentBackgroundChannel = &channelToProcess;
					break;
				}
			}

			if (matchedSignalChannels.empty() || !currentBackgroundChannel)
			{
				throw std::runtime_error("Channel " + channelX_or_Y + " not found!");
			}

			// =====================================================
			// BUILD ALL SIGNAL COMPONENT PDFs
			// =====================================================
			RooArgList signal_components;

			for (size_t i = 0; i < matchedSignalChannels.size(); i++)
			{
				Channel* sig = matchedSignalChannels[i];

				std::string comp_name =
					channelX_or_Y + "_sig_comp_" + std::to_string(i);
			

				RooArgList comp_pdf(
					create_signal_pdf(
						comp_name,
						*sig,
						mass,
						realHiggsMass,
						Bee,
						Beu,
						norm_Systematic,
						shape_Systematic,
						X_or_Y
					),
					comp_name.c_str()
				);

				std::cout << "type of individual channel PDF " << comp_pdf.at(0)->ClassName() << "\n";

				// signal_X_and_Y_Normalizations.push_back(get_signal_norm(comp_name, comp_pdf.at(0).signal_params, realHiggsMass));

				signal_components.add(*comp_pdf.at(0));
				// w_sig.import(*comp_pdf.at(0));
			
				if (i == 0)
				{
					//USING THE 0TH INDEX FOR NORM, NOT THE SUMMED PDF
					auto signal_pdf_for_norm = static_cast<RooPDF_HiggsAnalysis_Base*>(comp_pdf.at(0));

			
					signal_X_and_Y_Normalizations.push_back(signal_pdf_for_norm->signal_norm(channelX_or_Y));
				}
			}

			// =====================================================
			// SUM SIGNAL PDFs
			// =====================================================
			std::string final_sig_name = channelX_or_Y;

			RooArgList coeffs;


			int n = signal_components.getSize();

			std::cout << "num components " << n << "\n";

			for (int i = 0; i < n - 1; i++)
			{
				std::string frac_name =
					final_sig_name + "_frac_" + std::to_string(i);

				RooRealVar* frac = new RooRealVar(
					frac_name.c_str(),
					frac_name.c_str(),
					1.0 / n,
					0.0, 1.0
				);

				coeffs.add(*frac);
			}

			RooAddPdf* signal_sum_pdf = new RooAddPdf(
				final_sig_name.c_str(),
				final_sig_name.c_str(),
				signal_components,
				coeffs,
				true   // now VALID
			);


			w_sig.import(*signal_sum_pdf);

			RooArgList signal_pdf(*signal_sum_pdf, final_sig_name.c_str());

			std::cout << "recently_created_pdf_type" << signal_pdf.at(0)->ClassName() << "\n";

			std::cout << "signal_pdf successfully created for "
					<< channelX_or_Y << "\n";

			signal_X_and_Y.push_back(signal_pdf);

			std::cout << "pdf_after_pushing_to_X_Y_type " << signal_X_and_Y.at(0).at(0)->ClassName() << "\n"; 
			signal_X_and_YNames.push_back(final_sig_name);

			// =====================================================
			// SIGNAL NORMALIZATION (use summed pdf!)
			// =====================================================

			std::cout << "pdf_after_pushing_to_X_Y_type " << __LINE__ << " " << signal_X_and_Y.at(0).at(0)->ClassName() << "\n"; 

			// =====================================================
			// BUILD BACKGROUND PDF (UNCHANGED)
			// =====================================================
			RooArgList bkg_pdf(
				create_bkg_pdf(
					channel,
					*currentBackgroundChannel,
					mass,
					realHiggsMass,
					Bee,
					Beu,
					norm_Systematic,
					shape_Systematic,
					X_or_Y
				),
				(channel + "_bkg_" + X_or_Y).c_str()
			);

			std::cout << "pdf_after_pushing_to_X_Y_type " << __LINE__ << " " << signal_X_and_Y.at(0).at(0)->ClassName() << "\n"; 


			std::cout << "background_pdf successfully created for "
					<< channelX_or_Y << "\n";

			bkg_X_and_Y.push_back(bkg_pdf);
			bkg_X_and_YNames.push_back(channel + "_bkg_" + X_or_Y);

			auto bkg_pdf_for_norm = static_cast<RooPDF_HiggsAnalysis_Base*>(bkg_pdf.at(0));
			std::cout << '\n' << "bkg_norm for " << channel << "_" << X_or_Y << " " << bkg_pdf_for_norm->getNorm(mass) << '\n';  
			RooRealVar bkg_norm((channel + "_bkg_" + X_or_Y + "_norm").c_str(), (channel + "_bkg_" + X_or_Y +"_norm").c_str(), bkg_pdf_for_norm->getNorm(mass));
			bkg_norm.setConstant(true);

			bkg_X_and_Y_Normalizations.push_back(bkg_norm);
			bkg_X_and_Y_NormalizationsNames.push_back(channel + "_bkg_" + X_or_Y + "_norm");
			std::cout << "Signal and background functions created for " << channel << "_" << X_or_Y << '\n';
			std::cout << "\n \n \n";
			w_sig.import(*signal_X_and_Y.at(0).at(0));
		}

		// =====================================================
		// STORE PER-CHANNEL RESULTS (UNCHANGED)
		// =====================================================
		std::cout << "signal_X_and_Y_ClassName " << __LINE__ << signal_X_and_Y.at(0).at(0)->ClassName() << "\n";
		signal_pdfs.push_back(signal_X_and_Y);
		signal_pdfsNames.push_back(signal_X_and_YNames);

		bkg_pdfs.push_back(bkg_X_and_Y);
		bkg_pdfsNames.push_back(bkg_X_and_YNames);

		signal_Normalizations.push_back(signal_X_and_Y_Normalizations);
		signal_NormalizationsNames.push_back(signal_X_and_Y_NormalizationsNames);

		bkg_Normalizations.push_back(bkg_X_and_Y_Normalizations);
		bkg_NormalizationsNames.push_back(bkg_X_and_Y_NormalizationsNames);

		completedChannels.push_back(channel);
	}

	// Debug statements


	// std::vector<RooPDF_DSCB_test> signal_X_and_Y;
	// 	std::vector<std::string> signal_X_and_YNames;


	// 	std::vector<RooPDF_HiggsAnalysis_BKG> bkg_X_and_Y;
	// 	std::vector<std::string> bkg_X_and_YNames;
		
	// 	std::vector<RooFormulaVar> signal_X_and_Y_Normalizations;
	// 	std::vector<std::string> signal_X_and_Y_NormalizationsNames;

	// 	std::vector<RooRealVar> bkg_X_and_Y_Normalizations;
	// 	std::vector<std::string> bkg_X_and_Y_NormalizationsNames;



	// std::vector<std::vector<RooPDF_DSCB_test>> signal_pdfs;
	// std::vector<std::vector<string>> signal_pdfsNames;

	// std::vector<std::vector<RooPDF_HiggsAnalysis_BKG>> bkg_pdfs;
	// std::vector<std::vector<string>> bkg_pdfsNames;

	// std::vector<std::vector<RooFormulaVar>> signal_Normalizations;
	// std::vector<std::vector<string>> signal_NormalizationsNames;

	// std::vector<std::vector<RooRealVar>> bkg_Normalizations;
	// std::vector<std::vector<string>> bkg_NormalizationsNames;	
	// std::cout << "signal_pdfs:" << "\n";
	// for (auto variable : signal_pdfsNames)
	// {
	// 	for (auto name : variable)
	// 	{
	// 		std::cout << name << "\n";

	// 	}
	// }
	// std::cout << "\n";

	// std::cout << "bkg_pdfs:" << "\n";
	// for (auto variable : bkg_pdfsNames) 
	// {
	// 	for (auto name : variable)
	// 	{
	// 		std::cout << name << "\n";
	// 	}		
	// }
	// std::cout << "\n";

	// std::cout << "signalNormalizations:" << "\n";
	// for (auto variable : signal_NormalizationsNames)
	// {
	// 	for (auto name : variable)
	// 	{
	// 		std::cout << name << "\n";
	// 	}

	// }
	// std::cout << "\n";

	// std::cout << "bkgNormalizations:" << "\n";
	// for (auto variable : bkg_NormalizationsNames)
	// {
	// 	for (auto name : variable)
	// 	{
	// 		std::cout << name << "\n";
	// 	}

	// }
	// std::cout << "\n";

	// std::cout << "signal_pdfs size: " << signal_pdfs.size() << "\n";
	// std::cout << "signal_pdfs[0] size: " << signal_pdfs[0].size() << "\n";
	// std::cout << "signal_pdfs[1] size: " << signal_pdfs[1].size() << "\n" << "\n";


	// std::cout << "bkg_pdfs size: " << bkg_pdfs.size() << "\n";
	// std::cout << "bkg_pdfs[0] size: " << bkg_pdfs[0].size() << "\n";
	// std::cout << "bkg_pdfs[1] size: " << bkg_pdfs[1].size() << "\n" << "\n";


	// std::cout << "signal_normalizations size: " << signal_Normalizations.size() << "\n"; 
	// std::cout << "signal_Normalizations[0] size: " << signal_Normalizations[0].size() << "\n";
	// std::cout << "signal_Normalizations[1] size: " << signal_Normalizations[1].size() << "\n" << "\n";




	// std::cout << "bkg_Normalizations size: " << bkg_Normalizations.size() << "\n";
	// std::cout << "bkg_Normalizations[0] size: " << bkg_Normalizations[0].size() << "\n";
	// std::cout << "bkg_Normalizations[1] size: " << bkg_Normalizations[1].size() << "\n" << "\n";

	// std::cout << "Channels to look for:" << "\n";
	// for (auto channel : signalChannels)
	// {
	// 	std::cout << channel.name << "\n";
	// }
	// std::cout << "\n \n \n";

	int dummy;
	std::cin >> dummy;
	
	for (size_t i=0; i < channelsToCheck.size(); i++)
	{
		std::cout << "Importing channel " << i << "\n";
		for (size_t j=0; j < signs.size(); j++)
		{
			std::cout << "hi" << "\n";
			std::cout << "Type of signalpdf " << signal_pdfs[i][j].at(0)->ClassName() << "\n";
			std::cout << "Importing Signal PDF " << dynamic_cast<RooAbsPdf*>(signal_pdfs[i][j].at(0)) -> GetName() << "\n";
			w_sig.import(*signal_pdfs[i][j].at(0));
			std::cout << "Import successful: " << w_sig.arg(dynamic_cast<RooAbsPdf*>(signal_pdfs[i][j].at(0)) -> GetName()) << "\n ____ \n";

			std::cout << "Importing BKG PDF " << dynamic_cast<RooAbsPdf*>(bkg_pdfs[i][j].at(0)) -> GetName() << "\n";
			w_sig.import(*bkg_pdfs[i][j].at(0));
			std::cout << "Import successful: " << w_sig.arg(dynamic_cast<RooAbsPdf*>(bkg_pdfs[i][j].at(0)) -> GetName()) << "\n ____ \n";

			std::cout << "Importing Signal Normalization " << signal_Normalizations[i][j].GetName() << " " << w_sig.arg(signal_Normalizations[i][j].GetName()) << "\n";
			w_sig.import(signal_Normalizations[i][j]);
			std::cout << "Import successful: " << w_sig.arg(signal_Normalizations[i][j].GetName()) << "\n ____ \n";

			// w_sig.import(signal_pdfs[i][j].signal_norm());
			std::cout << "Importing Background Normalization " << bkg_Normalizations[i][j].GetName() << "\n ____ \n";
			w_sig.import(bkg_Normalizations[i][j]);
			std::cout << "Import successful: " << w_sig.arg(bkg_Normalizations[i][j].GetName()) << "\n ____ \n";
		}

		std::cout << "Completed importing channel " << i << "\n \n \n ";
		// w_sig.Print("v");
		// std::cout << "\n";
	}
	w_sig.Print("v");
	std::cout << "\n";
	std::cout << "Writing to workspace \n";
	w_sig.Write();
	f_out.Close();


}



// make a graph using vectors since ROOT needs arrays
TGraph makeGraph(double numCoords, std::vector<double>& xCoords, std::vector<double>& yCoords)
{
	//Transfer the vector input to an array (used in TGraph)
	int arraySizeX = xCoords.size();
	int arraySizeY = yCoords.size();
	double xArray[arraySizeX], yArray[arraySizeY];
	for (int i=0; i < arraySizeX; i++)
	{
		xArray[i] = xCoords[i];
	}

	for (int i = 0; i < arraySizeY; i++)
	{
		yArray[i] = yCoords[i];
	}

	TGraph graph(numCoords, xArray, yArray);
	return graph;
}