// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "glsl_es100.h"

#include <cctype>
#include <regex>
#include <sstream>

namespace vita {
namespace {

bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Replaces calls `from(` with `to(` where `from` is a whole identifier.
std::string replaceCall(const std::string& s, const std::string& from, const std::string& to) {
    std::string out;
    out.reserve(s.size());
    const std::string pattern = from + "(";
    size_t pos = 0;
    while (true) {
        size_t at = s.find(pattern, pos);
        if (at == std::string::npos) break;
        if (at > 0 && isIdentChar(s[at - 1])) {  // part of a longer name
            out.append(s, pos, at + pattern.size() - pos);
        } else {
            out.append(s, pos, at - pos);
            out += to + "(";
        }
        pos = at + pattern.size();
    }
    out.append(s, pos, std::string::npos);
    return out;
}

bool containsWord(const std::string& s, const std::string& word) {
    for (size_t at = s.find(word); at != std::string::npos; at = s.find(word, at + 1)) {
        const bool startOk = at == 0 || !isIdentChar(s[at - 1]);
        const size_t end = at + word.size();
        const bool endOk = end >= s.size() || !isIdentChar(s[end]);
        if (startOk && endOk) return true;
    }
    return false;
}

}  // namespace

Es100Result toGlslEs100(const std::string& source, ShaderStage stage) {
    static const std::regex kVersion(R"(^\s*#\s*version\b.*$)");
    static const std::regex kLayoutIn(R"(^\s*layout\s*\(\s*location\s*=\s*(\d+)\s*\)\s*in\s+(\w+)\s+(\w+)\s*;(.*)$)");
    static const std::regex kLayoutOther(R"(^(\s*)layout\s*\([^)]*\)\s*(.*)$)");
    static const std::regex kInterp(R"(^(\s*)(flat|smooth|centroid)\s+(.*)$)");
    static const std::regex kGlobalIn(R"(^(\s*)in\s+(.*)$)");
    static const std::regex kGlobalOut(R"(^(\s*)out\s+(.*)$)");
    static const std::regex kFragOut(R"(^\s*out\s+(?:(?:highp|mediump|lowp)\s+)?vec4\s+(\w+)\s*;.*$)");

    if (stage == ShaderStage::Unknown)
        stage = source.find("gl_Position") != std::string::npos ? ShaderStage::Vertex : ShaderStage::Fragment;

    Es100Result r;
    std::istringstream in(source);
    std::ostringstream out;
    std::string line, fragOutName;
    int fragOuts = 0;
    int depth = 0;  // brace depth: in/out are only rewritten at global scope
    bool wroteVersion = false;
    while (std::getline(in, line)) {
        std::smatch m;
        if (std::regex_match(line, kVersion)) {
            out << "#version 100\n";
            wroteVersion = true;
            continue;
        }
        if (depth == 0) {
            if (std::regex_match(line, m, kLayoutIn)) {
                r.attributes.emplace_back(std::stoi(m[1].str()), m[3].str());
                line = "attribute " + m[2].str() + " " + m[3].str() + ";" + m[4].str();
            } else if (std::regex_match(line, m, kLayoutOther)) {
                r.notes.push_back("dropped layout qualifier: " + line);
                line = m[1].str() + m[2].str();
            }
            if (std::regex_match(line, m, kInterp)) {
                if (m[2].str() == "flat") r.notes.push_back("dropped 'flat' (no flat interpolation in GLSL ES 1.00): " + line);
                line = m[1].str() + m[3].str();
            }
            if (stage == ShaderStage::Fragment && std::regex_match(line, m, kFragOut)) {
                if (fragOuts++ == 0) fragOutName = m[1].str();
                else r.notes.push_back("extra fragment output (MRT) has no GLSL ES 1.00 equivalent: " + line);
                continue;  // replaced by the #define below
            }
            if (stage == ShaderStage::Vertex && std::regex_match(line, m, kGlobalOut)) {
                line = m[1].str() + "varying " + m[2].str();
            } else if (stage == ShaderStage::Fragment && std::regex_match(line, m, kGlobalIn)) {
                line = m[1].str() + "varying " + m[2].str();
            }
        }
        for (char c : line) {
            if (c == '{') ++depth;
            else if (c == '}' && depth > 0) --depth;
        }
        out << line << '\n';
    }
    (void)wroteVersion;  // no #version line: GLSL ES 1.00 is the default

    std::string result = out.str();
    if (!fragOutName.empty() && fragOutName != "gl_FragColor") {
        // The #define goes right after the #version line (or at the top).
        const std::string def = "#define " + fragOutName + " gl_FragColor\n";
        size_t at = result.rfind("#version 100\n", 0) == 0 ? std::string("#version 100\n").size() : 0;
        result.insert(at, def);
    }
    result = replaceCall(result, "texture", "texture2D");
    result = replaceCall(result, "textureLod", "texture2DLod");
    result = replaceCall(result, "textureProj", "texture2DProj");

    for (const char* unsupported : {"texelFetch", "textureSize", "uint", "uvec2", "uvec3", "uvec4", "isampler2D",
                                    "usampler2D", "sampler2DArray", "sampler3D", "gl_VertexID", "gl_InstanceID"})
        if (containsWord(result, unsupported))
            r.notes.push_back(std::string("uses '") + unsupported + "', which GLSL ES 1.00 lacks");

    r.source = std::move(result);
    return r;
}

}  // namespace vita
