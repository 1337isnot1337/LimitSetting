#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
combined_limit_dir="$(cd -- "${script_dir}/.." && pwd)"

cp "${script_dir}"/RooPDF_HiggsAnalysis_*.h "${combined_limit_dir}/interface/"
cp "${script_dir}"/RooPDF_HiggsAnalysis_*.cxx "${combined_limit_dir}/src/"
cp "${script_dir}/ShapeSystematics.h" "${combined_limit_dir}/interface/"
cp "${script_dir}/SignalSystematics.h" "${combined_limit_dir}/interface/"

link_analysis_file() {
  local source_file="$1"
  local destination_file="$2"

  if [[ ! -e "${source_file}" ]]; then
    echo "Missing source file for symlink: ${source_file}" >&2
    exit 1
  fi

  # Use an absolute target.  A relative link is interpreted from the
  # destination directory, which made the old CMSSW_15_0_4 links invalid.
  ln -sfn "$(realpath "${source_file}")" "${destination_file}"
}

cmsanalysis_candidates=()
if [[ -n "${CMSANALYSIS_BASE:-}" ]]; then
  cmsanalysis_candidates+=("${CMSANALYSIS_BASE}")
fi
if [[ -n "${CMSSW_BASE:-}" ]]; then
  cmsanalysis_candidates+=("${CMSSW_BASE}/src/CMSAnalysis")
fi
cmsanalysis_candidates+=("${script_dir}/../../../CMSAnalysis")

analysis_dir=""
for candidate in "${cmsanalysis_candidates[@]}"; do
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
  echo "Could not find CMSAnalysis/Analysis." >&2
  echo "Set CMSANALYSIS_BASE to a CMSAnalysis checkout, then rerun setup.sh." >&2
  exit 1
fi

link_analysis_file "${analysis_dir}/src/FitFunction.cc" "${combined_limit_dir}/src/FitFunction.cc"
link_analysis_file "${analysis_dir}/src/FitFunctionCollection.cc" "${combined_limit_dir}/src/FitFunctionCollection.cc"
link_analysis_file "${analysis_dir}/interface/FitFunction.hh" "${combined_limit_dir}/interface/FitFunction.hh"
link_analysis_file "${analysis_dir}/interface/FitFunctionCollection.hh" "${combined_limit_dir}/interface/FitFunctionCollection.hh"

for header in "${script_dir}"/RooPDF_HiggsAnalysis_*.h; do
  header_name="$(basename "${header}")"
  class_name="$(grep -m 1 -E '^[[:space:]]*class[[:space:]]+RooPDF_HiggsAnalysis_' "${header}" | awk '{print $2}' | sed 's/[:{].*//')"
  #class RooPDF_HiggsAnalysis_DBLGAUSS : public RooPDF_HiggsAnalysis_Base { -> RooPDF_HiggsAnalysis_DBLGAUSS
  include_line="#include \"HiggsAnalysis/CombinedLimit/interface/${header_name}\""

  grep -Fxq "${include_line}" "${combined_limit_dir}/src/classes.h" || printf '%s\n' "${include_line}" >> "${combined_limit_dir}/src/classes.h"
  grep -Fq "<class name=\"${class_name}\"" "${combined_limit_dir}/src/classes_def.xml" || sed -i "\#</lcgdict>#i\\  <class name=\"${class_name}\" />" "${combined_limit_dir}/src/classes_def.xml"
done

if [[ "${1:-}" != "--nobuild" ]]; then
  cd "${combined_limit_dir}"
  scram b -j "${SCRAM_JOBS:-4}"
fi
