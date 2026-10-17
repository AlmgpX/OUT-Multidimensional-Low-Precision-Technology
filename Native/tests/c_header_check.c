// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
#include "out_native_c.h"
_Static_assert(sizeof(OUTGuardMovementResult) == 88, "Unexpected C ABI result layout");
_Static_assert(sizeof(OUTGuardMovementLimits) == 64, "Unexpected C ABI limit layout");
int out_native_c_header_compiles(void) {
    return 1;
}
