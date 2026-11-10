// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "glsl_es100.h"

#include <cctype>
#include <cstring>
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

// gl_VertexID with integer bit operations: neither exists in GLSL ES 1.00.
// gd01's full-screen passes use these exact expressions on vertex ids 0..3;
// each becomes float arithmetic on the vita_VertexID attribute (identical
// results for 0..3: x & 1 == mod(x, 2), x >> 1 == floor(x / 2), and
// x & 2 == floor(x / 2) * 2).
struct VertexIdForm {
    const char* glsl300;
    const char* glsl100;
};
constexpr VertexIdForm kVertexIdForms[] = {
    // renderer.cpp (quad, GL_TRIANGLE_STRIP 0..3)
    {"vec2((gl_VertexID & 1) * 2 - 1, (gl_VertexID >> 1) * 2 - 1)",
     "vec2(mod(vita_VertexID, 2.0) * 2.0 - 1.0, floor(vita_VertexID * 0.5) * 2.0 - 1.0)"},
    // renderer.cpp, post_fx.cpp (full-screen triangle, GL_TRIANGLES 0..2)
    {"vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0)",
     "vec2(mod(vita_VertexID, 2.0) * 4.0 - 1.0, floor(vita_VertexID * 0.5) * 4.0 - 1.0)"},
    // vk_upscaler.cpp (not in the Vita build; kept for completeness)
    {"vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2))",
     "vec2(mod(vita_VertexID, 2.0) * 2.0, floor(vita_VertexID * 0.5) * 2.0)"},
};

// Replaces the known gl_VertexID expressions; true if any gl_VertexID was in
// the source (whether or not every use could be converted).
bool convertVertexId(std::string& s, std::vector<std::string>& notes) {
    if (!containsWord(s, "gl_VertexID")) return false;
    for (const VertexIdForm& f : kVertexIdForms)
        for (size_t at = s.find(f.glsl300); at != std::string::npos; at = s.find(f.glsl300, at))
            s.replace(at, std::strlen(f.glsl300), f.glsl100), at += std::strlen(f.glsl100);
    if (containsWord(s, "gl_VertexID"))
        notes.push_back("gl_VertexID in an expression with no GLSL ES 1.00 form here (add it to kVertexIdForms)");
    // The attribute goes after the #version / #define lines.
    size_t at = 0;
    while (s.compare(at, 1, "#") == 0) {
        size_t eol = s.find('\n', at);
        if (eol == std::string::npos) break;
        at = eol + 1;
    }
    s.insert(at, std::string("attribute float ") + kVertexIdAttribute + ";\n");
    return true;
}

// Inserts `text` after the leading #-directives, precision statements and the
// vita_VertexID attribute, i.e. before the shader's own declarations.
void insertAfterPreamble(std::string& s, const std::string& text) {
    size_t at = 0;
    while (at < s.size()) {
        const size_t eol = s.find('\n', at);
        if (eol == std::string::npos) break;
        const std::string line = s.substr(at, eol - at);
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#' || line.compare(first, 10, "precision ") == 0 ||
            line.find(kVertexIdAttribute) != std::string::npos)
            at = eol + 1;
        else
            break;
    }
    s.insert(at, text);
}

// Splits "a, f(b, c), d" at its top-level commas.
std::vector<std::string> splitArgs(const std::string& args) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (char c : args) {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    for (std::string& a : out) {
        const size_t b = a.find_first_not_of(" \t"), e = a.find_last_not_of(" \t");
        a = b == std::string::npos ? std::string() : a.substr(b, e - b + 1);
    }
    return out;
}

// GLSL ES 1.00 has no array constructors: `[const] T name[N] = T[N](a, b, ...);`
// becomes `T name[N]; name[0] = a; name[1] = b; ...` (on one line, so line
// numbers in compiler messages still match the original).
void convertArrayConstructors(std::string& s, std::vector<std::string>& notes) {
    static const std::regex kArrayInit(
        R"((^|\n)([ \t]*)(?:const[ \t]+)?(\w+)[ \t]+(\w+)[ \t]*\[[ \t]*(\d+)[ \t]*\][ \t]*=[ \t]*\w+[ \t]*\[[ \t]*\d*[ \t]*\][ \t]*\(([^;]*)\)[ \t]*;)");
    std::string out;
    auto begin = s.cbegin();
    std::smatch m;
    while (std::regex_search(begin, s.cend(), m, kArrayInit)) {
        out.append(begin, m[0].first);
        const std::string type = m[3], name = m[4], count = m[5];
        const std::vector<std::string> values = splitArgs(m[6]);
        std::string line = m[1].str() + m[2].str() + type + " " + name + "[" + count + "];";
        for (size_t i = 0; i < values.size(); ++i) line += " " + name + "[" + std::to_string(i) + "] = " + values[i] + ";";
        if (std::to_string(values.size()) != count)
            notes.push_back("array " + name + ": " + std::to_string(values.size()) + " values for " + count + " elements");
        out += line;
        begin = m[0].second;
    }
    out.append(begin, s.cend());
    s = std::move(out);
}

// GLSL ES 1.00's abs() takes floats only: abs(<int variable>) becomes
// vita_iabs(), defined in the shader.
void convertIntAbs(std::string& s) {
    static const std::regex kIntDecl(R"(\bint[ \t]+(\w+))");
    bool used = false;
    for (std::sregex_iterator it(s.begin(), s.end(), kIntDecl), end; it != end; ++it) {
        const std::string call = "abs(" + (*it)[1].str() + ")";
        for (size_t at = s.find(call); at != std::string::npos; at = s.find(call, at + 1)) {
            if (at > 0 && isIdentChar(s[at - 1])) continue;  // e.g. fabs(
            s.replace(at, 3, "vita_iabs");
            used = true;
        }
    }
    if (used) insertAfterPreamble(s, "int vita_iabs(int x) { return x < 0 ? -x : x; }\n");
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
    if (stage == ShaderStage::Vertex) r.usesVertexId = convertVertexId(result, r.notes);
    result = replaceCall(result, "texture", "texture2D");
    result = replaceCall(result, "textureLod", "texture2DLod");
    result = replaceCall(result, "textureProj", "texture2DProj");
    if (stage == ShaderStage::Fragment && containsWord(result, "texture2DLod")) {
        // Fragment shaders have no texture2DLod in GLSL ES 1.00. The bias form
        // gives the same level where the UV is constant across the pixel
        // quad (base level 0, so level = bias), as in gd01's auto-exposure.
        result = replaceCall(result, "texture2DLod", "texture2D");
        r.notes.push_back("textureLod in a fragment shader -> texture2D with the level as bias (exact for constant UVs)");
    }
    convertArrayConstructors(result, r.notes);
    convertIntAbs(result);

    for (const char* unsupported : {"texelFetch", "textureSize", "uint", "uvec2", "uvec3", "uvec4", "isampler2D",
                                    "usampler2D", "sampler2DArray", "sampler3D", "gl_InstanceID"})
        if (containsWord(result, unsupported))
            r.notes.push_back(std::string("uses '") + unsupported + "', which GLSL ES 1.00 lacks");

    r.source = std::move(result);
    return r;
}

}  // namespace vita
