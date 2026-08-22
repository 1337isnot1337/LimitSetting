# LimitSetting

Parametric RooFit/Combine models for the IMSA-CMS Higgs analysis.

## CMSSW setup

Place both repositories under the same CMSSW release, for example:

```text
CMSSW_16_0_0/src/
├── CMSAnalysis/
└── HiggsAnalysis/CombinedLimit/
    └── LimitSetting/
```

Then run:

```bash
cd CMSSW_16_0_0/src
cmsenv
cd HiggsAnalysis/CombinedLimit/LimitSetting
./setup.sh
```

`setup.sh` finds CMSAnalysis from `CMSANALYSIS_BASE`, then
`$CMSSW_BASE/src/CMSAnalysis`, then the sibling checkout shown above. It no
longer contains a user-specific CMSSW path. Set `SCRAM_JOBS` to change the
default four build jobs.

## Shape systematics

The current systematic implementation and cluster workflow are documented in
[SYSTEMATICS.md](SYSTEMATICS.md). A standalone local test (ROOT plus a
CMSAnalysis checkout required) is:

```bash
CMSANALYSIS_BASE=/path/to/CMSAnalysis tests/run_local_tests.sh
```

The test covers interpolation, real `FitFunction` extraction, RooFit nuisance
propagation, workspace serialization, multi-component model construction, and
datacard updates.

Combine documentation: <https://cms-analysis.github.io/HiggsAnalysis-CombinedLimit/latest/>
