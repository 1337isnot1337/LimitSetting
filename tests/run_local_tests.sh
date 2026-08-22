#!/usr/bin/env bash
set -euo pipefail

repository_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d "${repository_dir}/.limitsetting-systematics-tests.XXXXXX")"
trap 'rm -rf -- "${build_dir}"' EXIT
export LIMITSETTING_TEST_WORKDIR="${build_dir}"

analysis_candidates=()
if [[ -n "${CMSANALYSIS_BASE:-}" ]]; then
  analysis_candidates+=("${CMSANALYSIS_BASE}")
fi
if [[ -n "${CMSSW_BASE:-}" ]]; then
  analysis_candidates+=("${CMSSW_BASE}/src/CMSAnalysis")
fi
analysis_candidates+=("${repository_dir}/../../../CMSAnalysis")

analysis_dir=""
for candidate in "${analysis_candidates[@]}"; do
  if [[ -f "${candidate}/Analysis/interface/FitFunction.hh" ]]; then
    analysis_dir="$(realpath "${candidate}/Analysis")"
    break
  fi
  if [[ -f "${candidate}/interface/FitFunction.hh" ]]; then
    analysis_dir="$(realpath "${candidate}")"
    break
  fi
done
if [[ -z "${analysis_dir}" ]]; then
  echo "Could not find CMSAnalysis/Analysis; set CMSANALYSIS_BASE." >&2
  exit 1
fi

command -v root-config >/dev/null
command -v rootcling >/dev/null
cxx="${CXX:-g++}"
read -r -a root_cflags <<< "$(root-config --cflags)"
read -r -a root_libs <<< "$(root-config --libs)"
common_flags=(-std=c++20 -Wall -Wextra -Wpedantic "${root_cflags[@]}")
include_flags=(-I"${repository_dir}" -I"${build_dir}/interface" -I"${analysis_dir}/interface")

mkdir -p "${build_dir}/interface" "${build_dir}/src"
cp "${repository_dir}/RooPDF_HiggsAnalysis_Base.h" \
   "${repository_dir}/RooPDF_HiggsAnalysis_DSCB.h" \
   "${repository_dir}/ShapeSystematics.h" \
   "${build_dir}/interface/"
cp "${repository_dir}/RooPDF_HiggsAnalysis_Base.cxx" \
   "${repository_dir}/RooPDF_HiggsAnalysis_DSCB.cxx" \
   "${build_dir}/src/"

"${cxx}" -std=c++17 -Wall -Wextra -Wpedantic -I"${repository_dir}" \
  "${repository_dir}/tests/test_shape_systematics.cpp" \
  -o "${build_dir}/test_shape_systematics"
"${build_dir}/test_shape_systematics"

"${cxx}" "${common_flags[@]}" "${include_flags[@]}" -c \
  "${build_dir}/src/RooPDF_HiggsAnalysis_Base.cxx" -o "${build_dir}/RooPDF_HiggsAnalysis_Base.o"
"${cxx}" "${common_flags[@]}" "${include_flags[@]}" -c \
  "${build_dir}/src/RooPDF_HiggsAnalysis_DSCB.cxx" -o "${build_dir}/RooPDF_HiggsAnalysis_DSCB.o"

rootcling -f "${build_dir}/LimitSettingShapeDict.cxx" \
  -I"${build_dir}/interface" \
  "${build_dir}/interface/RooPDF_HiggsAnalysis_Base.h" \
  "${build_dir}/interface/RooPDF_HiggsAnalysis_DSCB.h" \
  "${repository_dir}/tests/RootLinkDef.h"
"${cxx}" "${common_flags[@]}" "${include_flags[@]}" -c \
  "${build_dir}/LimitSettingShapeDict.cxx" -o "${build_dir}/LimitSettingShapeDict.o"

"${cxx}" "${common_flags[@]}" "${include_flags[@]}" -c \
  "${analysis_dir}/src/FitFunction.cc" -o "${build_dir}/FitFunction.o"
"${cxx}" "${common_flags[@]}" "${include_flags[@]}" -c \
  "${analysis_dir}/src/FitFunctionCollection.cc" -o "${build_dir}/FitFunctionCollection.o"

link_objects=(
  "${build_dir}/RooPDF_HiggsAnalysis_Base.o"
  "${build_dir}/RooPDF_HiggsAnalysis_DSCB.o"
  "${build_dir}/LimitSettingShapeDict.o"
)
root_link=("${root_libs[@]}" -lRooFit -lRooFitCore)

"${cxx}" "${common_flags[@]}" -no-pie "${include_flags[@]}" \
  "${repository_dir}/tests/test_roofit_shape_systematics.cpp" \
  "${link_objects[@]}" "${root_link[@]}" \
  -o "${build_dir}/test_roofit_shape_systematics"
"${build_dir}/test_roofit_shape_systematics"

"${cxx}" "${common_flags[@]}" "${include_flags[@]}" \
  "${repository_dir}/tests/test_signal_systematics_extraction.cpp" \
  "${build_dir}/FitFunction.o" "${build_dir}/FitFunctionCollection.o" \
  "${root_link[@]}" -o "${build_dir}/test_signal_systematics_extraction"
"${build_dir}/test_signal_systematics_extraction"

"${cxx}" "${common_flags[@]}" "${include_flags[@]}" -c \
  "${repository_dir}/construct_models_Higgs_Systematics.C" \
  -o "${build_dir}/construct_models_Higgs_Systematics.o"
"${cxx}" "${common_flags[@]}" -no-pie "${include_flags[@]}" \
  "${repository_dir}/tests/test_systematics_workspace_builder.cpp" \
  "${build_dir}/construct_models_Higgs_Systematics.o" \
  "${build_dir}/FitFunction.o" "${build_dir}/FitFunctionCollection.o" \
  "${link_objects[@]}" "${root_link[@]}" \
  -o "${build_dir}/test_systematics_workspace_builder"
"${build_dir}/test_systematics_workspace_builder"

PYTHONDONTWRITEBYTECODE=1 python3 "${repository_dir}/tests/test_update_datacard_systematics.py"
echo "All local systematics tests passed"
