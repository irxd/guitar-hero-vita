// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// GLSL ES 3.00 -> GLSL ES 1.00 source rewrite for vitaGL's GLSL translator,
// which compiles 3.00 sources to null GXM programs (crash in glLinkProgram,
// found on hardware 2026-11-10). Covers the subset gd01's shaders use; what
// 1.00 cannot express is reported in `notes` and left for the compiler to flag.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace vita {

enum class ShaderStage { Unknown, Vertex, Fragment };

struct Es100Result {
    std::string source;
    // Vertex shaders: (location, name) of every layout(location = N) input,
    // to bind with glBindAttribLocation before the program links.
    std::vector<std::pair<int, std::string>> attributes;
    std::vector<std::string> notes;  // things dropped or not convertible
    // The source used gl_VertexID (full-screen passes drawn without vertex
    // data): it now reads the float attribute kVertexIdAttribute instead,
    // which the caller binds and feeds 0, 1, 2, ... at draw time.
    bool usesVertexId = false;
};

inline constexpr const char* kVertexIdAttribute = "vita_VertexID";

Es100Result toGlslEs100(const std::string& source, ShaderStage stage);

}  // namespace vita
