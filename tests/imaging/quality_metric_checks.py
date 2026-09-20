"""Hand-computable checks of the reporting math, independent of the model."""
import numpy as np
from quality_report import disk_band, reference_metrics

thresholds = {"alpha_mae_max": .05, "boundary_mae_max": .12, "foreground_recall_min": .97,
              "background_mean_alpha_max": .02, "iou_min": .9, "boundary_f1_min": .85}
reference = np.zeros((8, 8))
reference[2:6, 2:6] = 1
same = reference_metrics(reference, reference, thresholds)
assert same["screening"] == "pass" and same["alpha_mae"] == 0 and same["iou"] == 1 and same["boundary_f1"] == 1
shifted = np.zeros((8, 8)); shifted[2:6, 3:7] = 1
result = reference_metrics(shifted, reference, thresholds)
assert result["screening"] == "fail"
assert result["alpha_mae"] == .125 and result["boundary_mae"] == .125
assert result["foreground_recall"] == .75 and result["iou"] == .6
assert abs(result["background_mean_alpha"] - 4/48) < 1e-12
assert result["boundary_f1"] == 1  # one-pixel displacement lies within the fixed two-pixel tolerance
point = np.zeros((9, 9), dtype=bool); point[4, 4] = True
assert disk_band(point, 2).sum() == 13  # exact disk, not a 5x5 square
assert reference_metrics(np.zeros((8, 8)), reference, thresholds)["boundary_f1"] == 0
print("PASS hand-computable alpha/coverage/IoU/boundary/F1 metrics and Euclidean disk")
