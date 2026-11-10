// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// PS Vita: display pacing (vita_platform.cpp).
#pragma once

namespace vita {

// vitaGL's display queue waits `vblanks` screen refreshes (60 Hz) per frame
// before showing the next: 2 is a steady 30 fps, 1 up to 60, 0 no vertical
// sync (tearing).
void setSwapInterval(int vblanks);

}  // namespace vita
