# LimitSetting

## Installation
```bash 
cmsrel CMSSW_16_0_0
cd CMSSW_16_0_0/src
cmsenv
git -c advice.detachedHead=false clone --depth 1 --branch v11.0.0 https://github.com/cms-analysis/HiggsAnalysis-CombinedLimit.git HiggsAnalysis/CombinedLimit
cd HiggsAnalysis/CombinedLimit
git clone git@github.com:IMSA-CMS/LimitSetting.git
cd LimitSetting/
./setup.sh
```
NOTE: `./setup.sh` uses `../../../../../CMSSW_15_0_4` hardcoded
 
Partially from https://cms-analysis.github.io/HiggsAnalysis-CombinedLimit/v11.0.0/
