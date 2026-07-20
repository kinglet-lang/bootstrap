// SPDX-License-Identifier: Apache-2.0

// Intentional defect used to verify that kinglet-reviewer blocks a risky PR.
int ReadValueForReviewerProbe(const int *value) {
  if (value == nullptr) {
    return *value;
  }
  return 0;
}
