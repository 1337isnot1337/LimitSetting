# Parametric signal shape systematics

## What is implemented

Each fitted DSCB PowerLaw coefficient can now depend on any number of named,
zero-centred RooFit nuisances. For a nominal absolute coefficient `beta`,
absolute alternate fits `up` and `down`, and nuisance `delta`, the interpolation
is

```text
beta_eff(delta) = beta + max(delta, 0) * (up - beta)
                       + max(-delta, 0) * (down - beta)
```

Therefore `delta = 0`, `+1`, and `-1` reproduce nominal, up, and down exactly.
Effects from independent nuisances are added as shifts about the same nominal.
The implementation applies this to all three PowerLaw coefficients for all
seven DSCB rows (both tails, both powers, mean, width, and normalization).

The data flow is now:

```text
FitFunction nominal + absolute up/down fits
    -> SignalSystematics extraction
    -> shared floating RooRealVar delta_systematic
    -> nuisance-dependent DSCB coefficients and component yields
    -> combined per-reconstruction-channel RooAddPdf + <pdf>_norm
    -> RooWorkspace
    -> Combine `param 0 1` Gaussian constraint
```

Modern CMSAnalysis builds accept both FitFunction formats:

- embedded alternatives from `FitFunction::addSystematic`, and
- separate records whose encoded `Systematic` field is `Nominal`, `<name> Up`,
  or `<name> Down` (the current `HiggsSignalFit.C` output).

Older CMSAnalysis checkouts with the legacy six-argument
`createFunctionOfType` API use the separate-record path. This avoids
unresolved embedded-systematic symbols in those releases; use a matching
modern CMSAnalysis checkout if embedded `addSystematic` alternatives are
required.

Extraction is strict. Once a systematic is present for a DSCB model, every one
of its seven rows must have both an up and a down fit with the same coefficient
layout. Conflicting or incomplete input raises an error instead of silently
dropping part of an uncertainty.

## Build a signal workspace on the cluster

Use a CMSAnalysis checkout from the same CMSSW release as the active shell.
`setup.sh` rejects cross-release combinations before changing the
`CombinedLimit` checkout. Then run:

```bash
cd "$CMSSW_BASE/src/HiggsAnalysis/CombinedLimit/LimitSetting"
export CMSANALYSIS_BASE="$CMSSW_BASE/src/CMSAnalysis"
./setup.sh
```

Build every supported channel from the current parameter file:

```bash
root -l -b -q 'construct_models_Higgs_Systematics.C+("/path/to/H++SignalParameterFunctions.txt","higgsworkspace_systematics.root","shape_systematics.txt",900,"","","")'
```

The last three strings are exact `Reco`, `GenSim`, and `Projection` filters;
empty means all. For a small first cluster check, for example:

```bash
root -l -b -q 'construct_models_Higgs_Systematics.C+("/path/to/H++SignalParameterFunctions.txt","higgsworkspace_systematics.root","shape_systematics.txt",900,"eeee","eeee","X")'
```

The workspace is named `higgsworkspace`. Aggregated objects follow the existing
datacard convention, such as `eeee_signal_X` and
`eeee_signal_X_norm`. All GenSim components for the same Reco/projection are
combined using their nuisance-dependent fitted yields. A nuisance with the same
physics name is represented by the same RooRealVar across all components and
channels, giving the intended correlation. The mass and legacy branching
variables retain the existing names `realHiggsMass`, `b_ee`, and `b_eu` so the
current Combine command runner can still set/freeze them.

The generated signal workspace does not duplicate the existing data and
background workspace. A datacard can reference different files: point only its
signal `shapes` entries at `higgsworkspace_systematics.root`, while leaving
`data_obs` and background entries on the existing workspace. For example:

```text
shapes eeee_X     analysis_1 higgsworkspace_systematics.root higgsworkspace:eeee_signal_X
shapes bkg_eeee_X analysis_1 higgsworkspace.root             higgsworkspace:eeee_bkg_X
shapes data_obs   analysis_1 higgsworkspace.root             higgsworkspace:Events900_X
```

Merge the generated Gaussian constraints into a copy of the datacard:

```bash
python3 update_datacard_systematics.py \
  Signal_datacards/datacard_Higgs_Combined.txt \
  shape_systematics.txt \
  --output Signal_datacards/datacard_Higgs_Combined_Systematics.txt
```

This changes a fixed `kmax` to `kmax *`, removes stale duplicate lines, and adds
entries such as:

```text
shape_ElectronScaleFactor param 0 1
```

These are `param`, not template `shape`, lines: the custom PDF already performs
the analytic shape morph and directly depends on the nuisance.

## Validation

Run the repository tests before transfer or on the cluster, from the matching
CMSSW environment:

```bash
CMSANALYSIS_BASE="$CMSSW_BASE/src/CMSAnalysis" tests/run_local_tests.sh
```

Then perform the environment-specific Combine checks:

```bash
text2workspace.py Signal_datacards/datacard_Higgs_Combined_Systematics.txt \
  -o Signal_datacards/datacard_Higgs_Combined_Systematics.root

combine -M FitDiagnostics \
  Signal_datacards/datacard_Higgs_Combined_Systematics.root \
  --setParameters shape_ElectronScaleFactor=0
```

Useful endpoint checks set one nuisance to `-1`, `0`, and `+1` with the others at
zero and compare the resulting component parameters/yields. The local tests do
this exactly and also prove that the dependencies survive a workspace file
round trip.

## Intentional boundary

This implementation targets the DSCB signal family identified in the mentor
notes. The current CMSAnalysis fitter can also choose DoubleGaussian for some
Reco/GenSim combinations. Those parameter names are detected and reported, and
the production builder refuses to write a silently incomplete workspace if an
unsupported model matches the requested filters. DoubleGaussian systematic
morphing should be implemented as a separate follow-up because the existing
`RooPDF_HiggsAnalysis_DBLGAUSS` source is currently a DSCB-like prototype rather
than a functioning double-Gaussian implementation.
