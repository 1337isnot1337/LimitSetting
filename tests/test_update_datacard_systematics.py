#!/usr/bin/env python3

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from update_datacard_systematics import merge  # noqa: E402


card = """imax 1
jmax 1
kmax 0 number of nuisance parameters
------------
bin ch1
observation -1
shape_ElectronScaleFactor param 9 9
"""

result = merge(
    card,
    [
        "shape_ElectronScaleFactor param 0 1  # ElectronScaleFactor",
        "shape_MuonRecoScaleFactor param 0 1  # MuonRecoScaleFactor",
    ],
)

assert "kmax * number of nuisance parameters" in result
assert result.count("shape_ElectronScaleFactor param") == 1
assert "shape_ElectronScaleFactor param 0 1" in result
assert "shape_MuonRecoScaleFactor param 0 1" in result
print("datacard systematics merge tests passed")
