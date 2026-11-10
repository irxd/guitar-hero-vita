// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Desktop check for the GLSL ES 3.00 -> 1.00 rewrite (no Vita needed):
//   glsl_es100_check <vert|frag> <in.glsl> <out.glsl>
// Writes the rewritten source and prints the bound attributes and the notes;
// tools/check-shaders.sh then validates the output with glslangValidator.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "glsl_es100.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <vert|frag> <in.glsl> <out.glsl>\n", argv[0]);
        return 2;
    }
    const std::string stageArg = argv[1];
    const vita::ShaderStage stage = stageArg == "vert" ? vita::ShaderStage::Vertex : vita::ShaderStage::Fragment;
    std::ifstream in(argv[2]);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    const vita::Es100Result r = vita::toGlslEs100(buf.str(), stage);
    std::ofstream(argv[3]) << r.source;
    for (const auto& [location, name] : r.attributes) std::printf("  attribute %d = %s\n", location, name.c_str());
    for (const std::string& note : r.notes) std::printf("  note: %s\n", note.c_str());
    return 0;
}
