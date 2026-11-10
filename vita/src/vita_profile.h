// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// PS Vita: frame phase timing for gd01.log.
//
// profilePhase("name") ends the current phase and starts the named one (the
// name must be a string literal: it is kept by pointer). The swap hook in
// vita_platform.cpp closes the last phase of each frame and starts "loop"
// (the main loop's event handling until the next mark). Every 5 s the log
// gets each phase's milliseconds per frame, its draw calls, and the time
// spent inside vitaGL's draw calls during it ("submit"). Main thread only.
//
// Off unless asked for: GD01_VITA_PHASES=1 (args.txt) times profilePhase
// marks, =2 also profilePush/Pop and every draw call. Each clock read costs
// 1.3 us on the Vita, so level 2 slows mesh-heavy frames by 3-4 ms.
#pragma once

namespace vita {

void profilePhase(const char* name);

// A nested phase: push switches to `name`, pop returns to the phase that was
// current at the push (for a function called from several phases).
void profilePush(const char* name);
void profilePop();

// A mesh the scene renderer skipped as outside the view (perf line: culled
// per frame).
void countCulledDraw();

}  // namespace vita
