#include "piglit_manifest.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "gl/gl.h"

namespace {

static const char *kExternalManifest =
    "/vol/external01/gx2gl/piglit_manifest.tsv";
static const char *kContentManifest =
    "/vol/content/gx2gl/piglit_manifest.tsv";
static const size_t kMaxShaderTestBytes = 512 * 1024;
static const int kTargetWidth = 256;
static const int kTargetHeight = 256;
static const int kMaxRunnerTextures = 32;
static const int kLegacyVertexAttribLocation = 15;

struct ManifestEntry {
    std::string name;
    std::string kind;
    std::string path;
};

struct ShaderTest {
    std::string requirements;
    std::string vertex_shader;
    std::string fragment_shader;
    std::string geometry_shader;
    std::string tess_control_shader;
    std::string tess_eval_shader;
    std::string compute_shader;
    std::string vertex_data;
    std::string test;
};

struct TestCommand {
    enum Type {
        ClearColor,
        Clear,
        Color,
        TextureRgbw,
        FramebufferTexture2D,
        Uniform,
        DrawRect,
        Ortho,
        ProbeAllRgba,
        ProbeRgba,
        ProbeRectRgba,
        LinkSuccess,
        LinkError,
    } type;
    std::string text;
    std::string uniform_type;
    std::string uniform_name;
    float values[8];
    GLint int_values[8];
    GLuint uint_values[8];
    int value_count;
    int compare_components;
    bool relative;
    bool force_ortho;
};

struct ProgramObjects {
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint fbo;
    GLuint texture;
    GLuint vao;
    GLuint vbo;
    GLuint generated_textures[kMaxRunnerTextures];
    int generated_texture_width[kMaxRunnerTextures];
    int generated_texture_height[kMaxRunnerTextures];
};

struct VaryingLocation {
    std::string name;
    int location;
};

struct TexCoordLocation {
    int index;
    int location;
};

static void add_result(PiglitRunStats *stats, PiglitResult result) {
    ++stats->total;
    if (result == PIGLIT_RESULT_PASS) {
        ++stats->pass;
    } else if (result == PIGLIT_RESULT_FAIL) {
        ++stats->fail;
    } else {
        ++stats->skip;
    }
}

static void publish_result(PiglitRunStats *stats, PiglitResultFunc result_func,
                           const char *name, PiglitResult result,
                           const char *detail, void *user_data) {
    add_result(stats, result);
    if (result_func) {
        result_func(name, result, detail, user_data);
    }
}

static std::string trim(const std::string &value) {
    size_t first = 0;
    size_t last = value.size();
    while (first < last &&
           isspace((unsigned char)value[first])) {
        ++first;
    }
    while (last > first &&
           isspace((unsigned char)value[last - 1])) {
        --last;
    }
    return value.substr(first, last - first);
}

static std::string lower_ascii(std::string value) {
    for (size_t i = 0; i < value.size(); ++i) {
        value[i] = (char)tolower((unsigned char)value[i]);
    }
    return value;
}

static bool starts_with(const std::string &value, const char *prefix) {
    const size_t prefix_len = strlen(prefix);
    return value.size() >= prefix_len &&
           memcmp(value.c_str(), prefix, prefix_len) == 0;
}

static bool contains(const std::string &value, const char *needle) {
    return value.find(needle) != std::string::npos;
}

static bool is_identifier_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static bool contains_identifier(const std::string &value,
                                const char *needle) {
    const size_t needle_len = strlen(needle);
    size_t pos = value.find(needle);
    while (pos != std::string::npos) {
        const bool left_ok =
            pos == 0 || !is_identifier_char(value[pos - 1]);
        const size_t end = pos + needle_len;
        const bool right_ok =
            end >= value.size() || !is_identifier_char(value[end]);
        if (left_ok && right_ok) {
            return true;
        }
        pos = value.find(needle, pos + 1);
    }
    return false;
}

static std::string replace_identifier(std::string value, const char *from,
                                      const char *to) {
    const size_t from_len = strlen(from);
    const size_t to_len = strlen(to);
    size_t pos = value.find(from);

    while (pos != std::string::npos) {
        const bool left_ok =
            pos == 0 || !is_identifier_char(value[pos - 1]);
        const size_t end = pos + from_len;
        const bool right_ok =
            end >= value.size() || !is_identifier_char(value[end]);
        if (left_ok && right_ok) {
            value.replace(pos, from_len, to);
            pos = value.find(from, pos + to_len);
        } else {
            pos = value.find(from, pos + 1);
        }
    }
    return value;
}

static std::vector<std::string> split_ws(const std::string &line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() &&
               isspace((unsigned char)line[i])) {
            ++i;
        }
        const size_t start = i;
        while (i < line.size() &&
               !isspace((unsigned char)line[i])) {
            ++i;
        }
        if (i > start) {
            out.push_back(line.substr(start, i - start));
        }
    }
    return out;
}

static std::vector<std::string> split_tabs(const std::string &line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= line.size()) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            out.push_back(line.substr(start));
            break;
        }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return out;
}

static bool parse_float(const std::string &token, float *value) {
    char *end = NULL;
    errno = 0;
    const float parsed = strtof(token.c_str(), &end);
    if (end == token.c_str() || errno == ERANGE) {
        return false;
    }
    while (end && *end) {
        if (!isspace((unsigned char)*end)) {
            return false;
        }
        ++end;
    }
    *value = parsed;
    return true;
}

static bool parse_uint32(const std::string &token, GLuint *value) {
    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(token.c_str(), &end, 0);
    if (end == token.c_str() || errno == ERANGE ||
        parsed > 0xFFFFFFFFul) {
        return false;
    }
    while (end && *end) {
        if (!isspace((unsigned char)*end)) {
            return false;
        }
        ++end;
    }
    *value = (GLuint)parsed;
    return true;
}

static bool parse_int32(const std::string &token, GLint *value) {
    if (token == "true") {
        *value = 1;
        return true;
    }
    if (token == "false") {
        *value = 0;
        return true;
    }

    char *end = NULL;
    errno = 0;
    const long parsed = strtol(token.c_str(), &end, 0);
    if (end == token.c_str() || errno == ERANGE ||
        parsed < (long)INT32_MIN || parsed > (long)INT32_MAX) {
        return false;
    }
    while (end && *end) {
        if (!isspace((unsigned char)*end)) {
            return false;
        }
        ++end;
    }
    *value = (GLint)parsed;
    return true;
}

static bool is_unsigned_uniform_type(const std::string &type) {
    return type == "uint" || type == "uvec2" ||
           type == "uvec3" || type == "uvec4";
}

static bool is_integer_uniform_type(const std::string &type) {
    return type == "int" || type == "bool" ||
           type == "ivec2" || type == "ivec3" ||
           type == "ivec4" || type == "bvec2" ||
           type == "bvec3" || type == "bvec4";
}

static GLenum parse_gl_format_name(const std::string &name) {
    if (name == "GL_RGBA8") return GL_RGBA8;
    if (name == "GL_RGB8") return GL_RGB8;
    if (name == "GL_RGBA") return GL_RGBA;
    if (name == "GL_RGB") return GL_RGB;
    if (name == "GL_R8") return GL_R8;
    if (name == "GL_RG8") return GL_RG8;
    if (name == "GL_RGBA16F") return GL_RGBA16F;
    if (name == "GL_RGBA32F") return GL_RGBA32F;
    return 0;
}

static std::vector<float> parse_float_values(std::string line) {
    std::vector<float> values;
    for (size_t i = 0; i < line.size(); ++i) {
        switch (line[i]) {
            case '(':
            case ')':
            case ',':
            case ';':
                line[i] = ' ';
                break;
            default:
                break;
        }
    }

    const std::vector<std::string> tokens = split_ws(line);
    for (size_t i = 0; i < tokens.size(); ++i) {
        float value = 0.0f;
        if (parse_float(tokens[i], &value)) {
            values.push_back(value);
        }
    }
    return values;
}

static std::string strip_comment(const std::string &line) {
    const size_t hash = line.find('#');
    if (hash == std::string::npos) {
        return line;
    }
    return line.substr(0, hash);
}

static bool read_text_file(const char *path, size_t max_bytes,
                           std::string *out) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const long size = ftell(file);
    if (size < 0 || (size_t)size > max_bytes) {
        fclose(file);
        return false;
    }
    rewind(file);

    out->assign((size_t)size, '\0');
    if (size > 0) {
        const size_t read_count =
            fread(&(*out)[0], 1, (size_t)size, file);
        if (read_count != (size_t)size) {
            fclose(file);
            return false;
        }
    }
    fclose(file);
    return true;
}

static void clear_gl_errors(void) {
    while (glGetError() != GL_NO_ERROR) {
    }
}

static bool expect_no_error(PiglitReportFunc report, const char *label) {
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) {
        return true;
    }
    report("[FAIL] Piglit manifest %s produced GL error 0x%04X\n",
           label, error);
    return false;
}

static std::string sanitize_info_log(const char *log, size_t max_len) {
    std::string out;

    for (size_t i = 0; i < max_len && log[i]; ++i) {
        const unsigned char c = (unsigned char)log[i];
        if (c == '\n' || c == '\r' || c == '\t') {
            out.push_back((char)c);
        } else if (c >= 32 && c < 127) {
            out.push_back((char)c);
        } else {
            out.push_back('?');
        }
        if (out.size() >= 320u) {
            out += "...";
            break;
        }
    }
    if (out.empty()) {
        out = "(empty log)";
    }
    return out;
}

static std::string add_fragment_output_location(const std::string &source) {
    std::string out;
    size_t pos = 0;
    bool output_seen = false;

    while (pos <= source.size()) {
        size_t next = source.find('\n', pos);
        bool has_newline = true;
        if (next == std::string::npos) {
            next = source.size();
            has_newline = false;
        }

        const std::string line = source.substr(pos, next - pos);
        const std::string stripped = trim(line);
        if (!output_seen && starts_with(stripped, "out ") &&
            !contains(stripped, "layout") &&
            (contains(stripped, " vec4 ") ||
             starts_with(stripped, "out vec4 "))) {
            const size_t indent = line.find_first_not_of(" \t");
            if (indent != std::string::npos) {
                out.append(line.substr(0, indent));
            }
            out += "layout(location = 0) ";
            out += stripped;
            output_seen = true;
        } else {
            out += line;
        }

        if (has_newline) {
            out += '\n';
            pos = next + 1;
        } else {
            break;
        }
    }
    return out;
}

static std::string remove_version_directives(const std::string &source) {
    std::string out;
    size_t pos = 0;

    while (pos <= source.size()) {
        size_t next = source.find('\n', pos);
        bool has_newline = true;
        if (next == std::string::npos) {
            next = source.size();
            has_newline = false;
        }

        const std::string line = source.substr(pos, next - pos);
        if (!starts_with(trim(line), "#version")) {
            out += line;
            if (has_newline) {
                out += '\n';
            }
        }

        if (!has_newline) {
            break;
        }
        pos = next + 1;
    }
    return out;
}

static bool parse_varying_declaration_name(const std::string &stripped,
                                           std::string *name) {
    if (!starts_with(stripped, "varying ")) {
        return false;
    }

    std::string decl = stripped.substr(strlen("varying "));
    const size_t semi = decl.find(';');
    if (semi == std::string::npos) {
        return false;
    }
    decl.resize(semi);
    const std::vector<std::string> tokens = split_ws(decl);
    if (tokens.size() < 2) {
        return false;
    }

    std::string parsed = tokens[tokens.size() - 1];
    const size_t comma = parsed.find(',');
    if (comma != std::string::npos) {
        parsed.resize(comma);
    }
    const size_t array = parsed.find('[');
    if (array != std::string::npos) {
        parsed.resize(array);
    }
    if (parsed.empty()) {
        return false;
    }
    *name = parsed;
    return true;
}

static int varying_location_for(const std::vector<VaryingLocation> &varyings,
                                const std::string &name) {
    for (size_t i = 0; i < varyings.size(); ++i) {
        if (varyings[i].name == name) {
            return varyings[i].location;
        }
    }
    return -1;
}

static void collect_varying_names(const std::string &source,
                                  std::vector<std::string> *names) {
    size_t pos = 0;
    while (pos <= source.size()) {
        size_t next = source.find('\n', pos);
        bool has_newline = true;
        if (next == std::string::npos) {
            next = source.size();
            has_newline = false;
        }

        std::string name;
        if (parse_varying_declaration_name(
                trim(source.substr(pos, next - pos)), &name)) {
            bool seen = false;
            for (size_t i = 0; i < names->size(); ++i) {
                if ((*names)[i] == name) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                names->push_back(name);
            }
        }

        if (!has_newline) {
            break;
        }
        pos = next + 1;
    }
}

static std::vector<VaryingLocation> build_varying_locations(
    const std::string &vertex_source, const std::string &fragment_source) {
    std::vector<std::string> names;
    std::vector<VaryingLocation> locations;
    int next_location = 0;

    if (contains_identifier(vertex_source, "gl_FrontColor") ||
        contains_identifier(fragment_source, "gl_Color")) {
        next_location = 1;
    }
    if (contains_identifier(vertex_source, "gl_FrontSecondaryColor") ||
        contains_identifier(fragment_source, "gl_SecondaryColor")) {
        next_location = next_location < 1 ? 1 : next_location + 1;
    }

    collect_varying_names(vertex_source, &names);
    collect_varying_names(fragment_source, &names);
    for (size_t i = 0; i < names.size(); ++i) {
        VaryingLocation varying;
        varying.name = names[i];
        varying.location = next_location++;
        locations.push_back(varying);
    }
    return locations;
}

static int front_color_location_for(const std::string &vertex_source,
                                    const std::string &fragment_source) {
    if (!contains_identifier(vertex_source, "gl_FrontColor") &&
        !contains_identifier(fragment_source, "gl_Color")) {
        return -1;
    }
    return 0;
}

static int secondary_color_location_for(const std::string &vertex_source,
                                        const std::string &fragment_source,
                                        int front_color_location) {
    if (!contains_identifier(vertex_source, "gl_FrontSecondaryColor") &&
        !contains_identifier(fragment_source, "gl_SecondaryColor")) {
        return -1;
    }
    return front_color_location >= 0 ? front_color_location + 1 : 0;
}

static bool parse_texcoord_reference(const std::string &source, size_t pos,
                                     int *index, size_t *end_pos) {
    static const char kName[] = "gl_TexCoord";
    const size_t name_len = strlen(kName);
    size_t cursor = pos + name_len;

    if (source.compare(pos, name_len, kName) != 0) {
        return false;
    }
    if (pos > 0 && is_identifier_char(source[pos - 1])) {
        return false;
    }
    if (cursor < source.size() && is_identifier_char(source[cursor])) {
        return false;
    }
    while (cursor < source.size() &&
           isspace((unsigned char)source[cursor])) {
        ++cursor;
    }
    if (cursor >= source.size() || source[cursor] != '[') {
        return false;
    }
    ++cursor;
    while (cursor < source.size() &&
           isspace((unsigned char)source[cursor])) {
        ++cursor;
    }
    if (cursor >= source.size() || !isdigit((unsigned char)source[cursor])) {
        return false;
    }

    int parsed = 0;
    while (cursor < source.size() &&
           isdigit((unsigned char)source[cursor])) {
        parsed = parsed * 10 + (source[cursor] - '0');
        ++cursor;
    }
    while (cursor < source.size() &&
           isspace((unsigned char)source[cursor])) {
        ++cursor;
    }
    if (cursor >= source.size() || source[cursor] != ']') {
        return false;
    }

    *index = parsed;
    *end_pos = cursor + 1;
    return true;
}

static bool texcoord_seen(const std::vector<int> &indices, int index) {
    for (size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] == index) {
            return true;
        }
    }
    return false;
}

static void collect_texcoord_indices(const std::string &source,
                                     std::vector<int> *indices) {
    size_t line_start = 0;
    while (line_start <= source.size()) {
        size_t line_end = source.find('\n', line_start);
        bool has_newline = true;
        if (line_end == std::string::npos) {
            line_end = source.size();
            has_newline = false;
        }

        const std::string line =
            source.substr(line_start, line_end - line_start);
        std::string varying_name;
        if (!parse_varying_declaration_name(trim(line), &varying_name) ||
            varying_name != "gl_TexCoord") {
            size_t pos = line.find("gl_TexCoord");
            while (pos != std::string::npos) {
                int index = 0;
                size_t end_pos = 0;
                if (parse_texcoord_reference(line, pos, &index, &end_pos) &&
                    index >= 0 && index < 8 &&
                    !texcoord_seen(*indices, index)) {
                    indices->push_back(index);
                }
                pos = line.find("gl_TexCoord", pos + 1);
            }
        }

        if (!has_newline) {
            break;
        }
        line_start = line_end + 1;
    }
}

static std::vector<TexCoordLocation> build_texcoord_locations(
    const std::string &vertex_source, const std::string &fragment_source,
    const std::vector<VaryingLocation> &varyings,
    int front_color_location, int secondary_color_location) {
    std::vector<int> indices;
    std::vector<TexCoordLocation> locations;
    int next_location = 0;

    if (front_color_location >= 0 &&
        next_location <= front_color_location) {
        next_location = front_color_location + 1;
    }
    if (secondary_color_location >= 0 &&
        next_location <= secondary_color_location) {
        next_location = secondary_color_location + 1;
    }
    for (size_t i = 0; i < varyings.size(); ++i) {
        if (next_location <= varyings[i].location) {
            next_location = varyings[i].location + 1;
        }
    }

    collect_texcoord_indices(vertex_source, &indices);
    collect_texcoord_indices(fragment_source, &indices);
    for (size_t i = 0; i < indices.size(); ++i) {
        TexCoordLocation texcoord;
        texcoord.index = indices[i];
        texcoord.location = next_location++;
        locations.push_back(texcoord);
    }
    return locations;
}

static int texcoord_location_for(
    const std::vector<TexCoordLocation> &texcoords, int index) {
    for (size_t i = 0; i < texcoords.size(); ++i) {
        if (texcoords[i].index == index) {
            return texcoords[i].location;
        }
    }
    return -1;
}

static std::string rewrite_legacy_texcoords(
    const std::string &source,
    const std::vector<TexCoordLocation> &texcoords) {
    std::string out;
    size_t pos = 0;

    while (pos < source.size()) {
        const size_t found = source.find("gl_TexCoord", pos);
        if (found == std::string::npos) {
            out.append(source.substr(pos));
            break;
        }

        int index = 0;
        size_t end_pos = 0;
        out.append(source.substr(pos, found - pos));
        if (parse_texcoord_reference(source, found, &index, &end_pos) &&
            texcoord_location_for(texcoords, index) >= 0) {
            out += "gx2gl_TexCoord";
            out += std::to_string(index);
            pos = end_pos;
        } else {
            out += "gl_TexCoord";
            pos = found + strlen("gl_TexCoord");
        }
    }
    return out;
}

static size_t find_main_closing_brace(const std::string &source) {
    size_t main_pos = source.find("void main");
    size_t open_brace;
    int depth = 0;
    bool line_comment = false;
    bool block_comment = false;

    if (main_pos == std::string::npos) return std::string::npos;
    open_brace = source.find('{', main_pos);
    if (open_brace == std::string::npos) return std::string::npos;

    for (size_t i = open_brace; i < source.size(); ++i) {
        const char c = source[i];
        const char next = i + 1u < source.size() ? source[i + 1u] : '\0';

        if (line_comment) {
            if (c == '\n') line_comment = false;
            continue;
        }
        if (block_comment) {
            if (c == '*' && next == '/') {
                block_comment = false;
                ++i;
            }
            continue;
        }
        if (c == '/' && next == '/') {
            line_comment = true;
            ++i;
            continue;
        }
        if (c == '/' && next == '*') {
            block_comment = true;
            ++i;
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

static std::string append_main_epilogue(const std::string &source,
                                        const std::string &epilogue) {
    const size_t closing_brace = find_main_closing_brace(source);
    if (closing_brace == std::string::npos || epilogue.empty()) {
        return source;
    }
    return source.substr(0, closing_brace) + epilogue +
           source.substr(closing_brace);
}

static std::string rewrite_legacy_varyings(
    const std::string &source, GLenum stage,
    const std::vector<VaryingLocation> &varyings) {
    std::string out;
    size_t pos = 0;

    while (pos <= source.size()) {
        size_t next = source.find('\n', pos);
        bool has_newline = true;
        if (next == std::string::npos) {
            next = source.size();
            has_newline = false;
        }

        const std::string line = source.substr(pos, next - pos);
        const std::string stripped = trim(line);
        std::string name;
        if (parse_varying_declaration_name(stripped, &name)) {
            if (name != "gl_TexCoord") {
                const int location = varying_location_for(varyings, name);
                const size_t indent = line.find_first_not_of(" \t");
                if (indent != std::string::npos) {
                    out.append(line.substr(0, indent));
                }
                out += "layout(location = ";
                out += std::to_string(location >= 0 ? location : 0);
                if (stage == GL_VERTEX_SHADER) {
                    out += ") out ";
                } else {
                    out += ") in ";
                }
                out += stripped.substr(strlen("varying "));
            }
        } else {
            out += line;
        }

        if (!has_newline) {
            break;
        }
        out += '\n';
        pos = next + 1;
    }
    return out;
}

static std::string make_source_330(
    GLenum stage, const std::string &source,
    const std::vector<VaryingLocation> &varyings,
    const std::vector<TexCoordLocation> &texcoords,
    int front_color_location, int secondary_color_location) {
    std::string out;
    std::string body = rewrite_legacy_varyings(
        remove_version_directives(source), stage, varyings);
    body = rewrite_legacy_texcoords(body, texcoords);

    out = "#version 330 core\n";
    if (stage == GL_FRAGMENT_SHADER) {
        const bool uses_gl_color = contains_identifier(body, "gl_Color");
        const bool uses_gl_secondary_color =
            contains_identifier(body, "gl_SecondaryColor");
        const bool uses_gl_frag_color =
            contains_identifier(body, "gl_FragColor");
        if (uses_gl_color) {
            body = replace_identifier(body, "gl_Color",
                                      "gx2gl_FrontColor");
        }
        if (uses_gl_secondary_color) {
            body = replace_identifier(body, "gl_SecondaryColor",
                                      "gx2gl_FrontSecondaryColor");
        }
        if (uses_gl_frag_color) {
            body = replace_identifier(body, "gl_FragColor",
                                      "gx2gl_FragColor");
        }
        body = add_fragment_output_location(body);
        if (uses_gl_color) {
            out += "layout(location = ";
            out += std::to_string(front_color_location >= 0
                                      ? front_color_location
                                      : 0);
            out += ") in vec4 gx2gl_FrontColor;\n";
        }
        if (uses_gl_secondary_color) {
            out += "layout(location = ";
            out += std::to_string(secondary_color_location >= 0
                                      ? secondary_color_location
                                      : 0);
            out += ") in vec4 gx2gl_FrontSecondaryColor;\n";
        }
        if (uses_gl_frag_color) {
            out += "layout(location = 0) out vec4 gx2gl_FragColor;\n";
        }
        for (size_t i = 0; i < texcoords.size(); ++i) {
            out += "layout(location = ";
            out += std::to_string(texcoords[i].location);
            out += ") in vec4 gx2gl_TexCoord";
            out += std::to_string(texcoords[i].index);
            out += ";\n";
        }
        out += "#define texture2D texture\n";
        out += "#define textureCube texture\n";
    } else if (stage == GL_VERTEX_SHADER) {
        std::string color_epilogue;
        const bool uses_gl_vertex = contains_identifier(body, "gl_Vertex") ||
                                    contains_identifier(body, "ftransform");
        const bool uses_gl_front_color =
            contains_identifier(body, "gl_FrontColor");
        const bool uses_gl_front_secondary_color =
            contains_identifier(body, "gl_FrontSecondaryColor");
        const bool uses_mvp =
            contains_identifier(body, "gl_ModelViewProjectionMatrix") ||
            contains_identifier(body, "ftransform");
        const bool uses_current_color =
            contains_identifier(body, "gl_CurrentColor");
        if (uses_gl_vertex) {
            body = replace_identifier(body, "gl_Vertex", "gx2gl_Vertex");
        }
        if (uses_gl_front_color) {
            body = replace_identifier(body, "gl_FrontColor",
                                      "gx2gl_FrontColor");
            color_epilogue +=
                "    gx2gl_FrontColor = clamp(gx2gl_FrontColor, 0.0, 1.0);\n";
        }
        if (uses_gl_front_secondary_color) {
            body = replace_identifier(body, "gl_FrontSecondaryColor",
                                      "gx2gl_FrontSecondaryColor");
            color_epilogue +=
                "    gx2gl_FrontSecondaryColor = "
                "clamp(gx2gl_FrontSecondaryColor, 0.0, 1.0);\n";
        }
        if (uses_mvp) {
            body = replace_identifier(body, "gl_ModelViewProjectionMatrix",
                                      "gx2gl_ModelViewProjectionMatrix");
        }
        if (uses_current_color) {
            body = replace_identifier(body, "gl_CurrentColor",
                                      "gx2gl_CurrentColor");
        }
        if (uses_gl_vertex) {
            out += "layout(location = ";
            out += std::to_string(kLegacyVertexAttribLocation);
            out += ") in vec4 gx2gl_Vertex;\n";
        }
        if (uses_gl_front_color) {
            out += "layout(location = ";
            out += std::to_string(front_color_location >= 0
                                      ? front_color_location
                                      : 0);
            out += ") out vec4 gx2gl_FrontColor;\n";
        }
        if (uses_gl_front_secondary_color) {
            out += "layout(location = ";
            out += std::to_string(secondary_color_location >= 0
                                      ? secondary_color_location
                                      : 0);
            out += ") out vec4 gx2gl_FrontSecondaryColor;\n";
        }
        for (size_t i = 0; i < texcoords.size(); ++i) {
            out += "layout(location = ";
            out += std::to_string(texcoords[i].location);
            out += ") out vec4 gx2gl_TexCoord";
            out += std::to_string(texcoords[i].index);
            out += ";\n";
        }
        if (uses_mvp) {
            out += "uniform mat4 gx2gl_ModelViewProjectionMatrix;\n";
        }
        if (uses_current_color) {
            out += "uniform vec4 gx2gl_CurrentColor;\n";
        }
        body = append_main_epilogue(body, color_epilogue);
        out += "#define attribute layout(location = 0) in\n";
        out += "#define ftransform() (gx2gl_ModelViewProjectionMatrix * gx2gl_Vertex)\n";
    }
    out += body;
    return out;
}

static GLuint compile_shader(PiglitReportFunc report, GLenum type,
                             const std::string &source,
                             const char *case_name,
                             const std::vector<VaryingLocation> &varyings,
                             const std::vector<TexCoordLocation> &texcoords,
                             int front_color_location,
                             int secondary_color_location) {
    const std::string upgraded = make_source_330(
        type, source, varyings, texcoords, front_color_location,
        secondary_color_location);
    const char *src = upgraded.c_str();
    GLuint shader = glCreateShader(type);
    GLint status = GL_FALSE;
    char log[1024];
    GLsizei log_len = 0;

    if (!shader || !expect_no_error(report, "glCreateShader")) {
        return 0;
    }
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    if (!expect_no_error(report, "glCompileShader")) {
        glDeleteShader(shader);
        return 0;
    }
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!expect_no_error(report, "glGetShaderiv")) {
        glDeleteShader(shader);
        return 0;
    }
    if (status == GL_TRUE) {
        return shader;
    }

    memset(log, 0, sizeof(log));
    glGetShaderInfoLog(shader, sizeof(log), &log_len, log);
    const std::string clean_log = sanitize_info_log(log, sizeof(log));
    report("[FAIL] Piglit manifest %s shader compile failed: %s\n",
           case_name, clean_log.c_str());
    glDeleteShader(shader);
    return 0;
}

static GLuint link_program(PiglitReportFunc report, GLuint vs, GLuint fs,
                           const char *case_name, bool *linked) {
    GLuint program = glCreateProgram();
    GLint status = GL_FALSE;
    char log[1024];
    GLsizei log_len = 0;

    *linked = false;
    if (!program || !expect_no_error(report, "glCreateProgram")) {
        return 0;
    }
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    if (!expect_no_error(report, "glLinkProgram")) {
        glDeleteProgram(program);
        return 0;
    }
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!expect_no_error(report, "glGetProgramiv(GL_LINK_STATUS)")) {
        glDeleteProgram(program);
        return 0;
    }
    if (status == GL_TRUE) {
        *linked = true;
        return program;
    }

    memset(log, 0, sizeof(log));
    glGetProgramInfoLog(program, sizeof(log), &log_len, log);
    const std::string clean_log = sanitize_info_log(log, sizeof(log));
    report("[INFO] Piglit manifest %s link failed: %s\n",
           case_name, clean_log.c_str());
    return program;
}

static std::string section_name(const std::string &line) {
    const std::string trimmed = trim(line);
    if (trimmed.size() < 3 || trimmed[0] != '[' ||
        trimmed[trimmed.size() - 1] != ']') {
        return std::string();
    }
    return lower_ascii(trim(trimmed.substr(1, trimmed.size() - 2)));
}

static void append_section(ShaderTest *test, const std::string &section,
                           const std::string &line) {
    std::string *target = NULL;
    if (section == "require") {
        target = &test->requirements;
    } else if (section == "vertex shader") {
        target = &test->vertex_shader;
    } else if (section == "fragment shader") {
        target = &test->fragment_shader;
    } else if (section == "geometry shader") {
        target = &test->geometry_shader;
    } else if (section == "tessellation control shader") {
        target = &test->tess_control_shader;
    } else if (section == "tessellation evaluation shader") {
        target = &test->tess_eval_shader;
    } else if (section == "compute shader") {
        target = &test->compute_shader;
    } else if (section == "vertex data") {
        target = &test->vertex_data;
    } else if (section == "test") {
        target = &test->test;
    }

    if (target) {
        target->append(line);
        target->push_back('\n');
    }
}

static ShaderTest parse_shader_test(const std::string &contents) {
    ShaderTest test;
    std::string section;
    size_t pos = 0;

    while (pos <= contents.size()) {
        size_t next = contents.find('\n', pos);
        if (next == std::string::npos) {
            next = contents.size();
        }
        std::string line = contents.substr(pos, next - pos);
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.resize(line.size() - 1);
        }

        const std::string new_section = section_name(line);
        if (!new_section.empty()) {
            section = new_section;
        } else {
            append_section(&test, section, line);
        }

        if (next == contents.size()) {
            break;
        }
        pos = next + 1;
    }
    return test;
}

static bool parse_gl_version(const std::string &token, int *scaled) {
    float value = 0.0f;
    if (!parse_float(token, &value)) {
        return false;
    }
    *scaled = (int)(value * 100.0f + 0.5f);
    return true;
}

static bool requirements_supported(const ShaderTest &test,
                                   std::string *skip_reason) {
    size_t pos = 0;
    while (pos <= test.requirements.size()) {
        size_t next = test.requirements.find('\n', pos);
        if (next == std::string::npos) {
            next = test.requirements.size();
        }
        const std::string line = trim(strip_comment(
            test.requirements.substr(pos, next - pos)));
        if (!line.empty()) {
            const std::vector<std::string> tokens = split_ws(line);
            if (tokens.size() >= 3 && tokens[0] == "GL" &&
                tokens[1] == ">=") {
                int version = 0;
                if (!parse_gl_version(tokens[2], &version) ||
                    version > 330) {
                    *skip_reason = "requires GL newer than 3.3";
                    return false;
                }
            } else if (tokens.size() >= 3 && tokens[0] == "GLSL" &&
                       tokens[1] == ">=") {
                int version = 0;
                if (!parse_gl_version(tokens[2], &version) ||
                    version > 330) {
                    *skip_reason = "requires GLSL newer than 3.30";
                    return false;
                }
            } else if (tokens.size() >= 1 &&
                       starts_with(tokens[0], "GL_")) {
                *skip_reason = "extension-gated shader_test";
                return false;
            } else if (tokens.size() >= 1 &&
                       (tokens[0] == "GLSL" || tokens[0] == "GL")) {
                *skip_reason = "unsupported version requirement";
                return false;
            }
        }

        if (next == test.requirements.size()) {
            break;
        }
        pos = next + 1;
    }
    return true;
}

static bool parse_command_line(const std::string &raw_line,
                               TestCommand *command,
                               std::string *skip_reason) {
    command->type = TestCommand::Clear;
    command->text.clear();
    command->uniform_type.clear();
    command->uniform_name.clear();
    command->value_count = 0;
    command->compare_components = 4;
    command->relative = false;
    command->force_ortho = false;
    for (int i = 0; i < 8; ++i) {
        command->values[i] = 0.0f;
        command->int_values[i] = 0;
        command->uint_values[i] = 0;
    }

    const std::string line = trim(strip_comment(raw_line));
    if (line.empty()) {
        return true;
    }

    const std::vector<std::string> tokens = split_ws(line);
    if (tokens.empty()) {
        return true;
    }

    command->text = line;

    if (tokens.size() == 6 && tokens[0] == "clear" &&
        tokens[1] == "color") {
        command->type = TestCommand::ClearColor;
        for (int i = 0; i < 4; ++i) {
            if (!parse_float(tokens[2 + i], &command->values[i])) {
                *skip_reason = "unsupported clear color";
                return false;
            }
        }
        command->value_count = 4;
        return true;
    }
    if (tokens.size() == 1 && tokens[0] == "clear") {
        command->type = TestCommand::Clear;
        return true;
    }
    if (tokens.size() == 5 && tokens[0] == "color") {
        command->type = TestCommand::Color;
        for (int i = 0; i < 4; ++i) {
            if (!parse_float(tokens[1 + i], &command->values[i])) {
                *skip_reason = "unsupported color";
                return false;
            }
        }
        command->value_count = 4;
        return true;
    }
    if (tokens.size() >= 4 && tokens[0] == "texture" &&
        tokens[1] == "rgbw") {
        const std::vector<float> floats = parse_float_values(line);
        GLenum internal_format = GL_RGBA8;

        if (floats.size() < 3) {
            *skip_reason = "unsupported texture rgbw";
            return false;
        }
        if (tokens.size() >= 6) {
            internal_format = parse_gl_format_name(tokens[tokens.size() - 1]);
            if (!internal_format) {
                *skip_reason = "unsupported texture format";
                return false;
            }
        }
        command->type = TestCommand::TextureRgbw;
        command->int_values[0] = (GLint)floats[0];
        command->int_values[1] = (GLint)floats[1];
        command->int_values[2] = (GLint)floats[2];
        command->uint_values[0] = (GLuint)internal_format;
        command->value_count = 3;
        return true;
    }
    if (tokens.size() >= 4 && tokens[0] == "fb" &&
        tokens[1] == "tex" && tokens[2] == "2d") {
        command->type = TestCommand::FramebufferTexture2D;
        command->value_count = (int)tokens.size() - 3;
        if (command->value_count > 8) {
            *skip_reason = "too many framebuffer attachments";
            return false;
        }
        for (int i = 0; i < command->value_count; ++i) {
            if (!parse_int32(tokens[3 + i], &command->int_values[i])) {
                *skip_reason = "unsupported framebuffer texture";
                return false;
            }
        }
        return true;
    }
    if (tokens.size() >= 4 && tokens[0] == "uniform") {
        command->type = TestCommand::Uniform;
        command->uniform_type = tokens[1];
        command->uniform_name = tokens[2];
        command->value_count = (int)tokens.size() - 3;
        if (command->value_count > 8) {
            *skip_reason = "uniform has too many values";
            return false;
        }
        for (int i = 0; i < command->value_count; ++i) {
            if (is_unsigned_uniform_type(command->uniform_type)) {
                if (!parse_uint32(tokens[3 + i],
                                  &command->uint_values[i])) {
                    *skip_reason = "unsupported uniform value";
                    return false;
                }
                command->values[i] = (float)command->uint_values[i];
            } else if (is_integer_uniform_type(command->uniform_type)) {
                if (!parse_int32(tokens[3 + i],
                                 &command->int_values[i])) {
                    *skip_reason = "unsupported uniform value";
                    return false;
                }
                command->values[i] = (float)command->int_values[i];
            } else if (!parse_float(tokens[3 + i], &command->values[i])) {
                *skip_reason = "unsupported uniform value";
                return false;
            }
        }
        return true;
    }
    if (tokens.size() == 6 && tokens[0] == "draw" &&
        tokens[1] == "rect") {
        command->type = TestCommand::DrawRect;
        for (int i = 0; i < 4; ++i) {
            if (!parse_float(tokens[2 + i], &command->values[i])) {
                *skip_reason = "unsupported draw rect";
                return false;
            }
        }
        command->value_count = 4;
        return true;
    }
    if (tokens.size() == 7 && tokens[0] == "draw" &&
        tokens[1] == "rect" && tokens[2] == "ortho") {
        command->type = TestCommand::DrawRect;
        command->force_ortho = true;
        for (int i = 0; i < 4; ++i) {
            if (!parse_float(tokens[3 + i], &command->values[i])) {
                *skip_reason = "unsupported draw rect ortho";
                return false;
            }
        }
        command->value_count = 4;
        return true;
    }
    if ((tokens.size() == 1 || tokens.size() == 5) && tokens[0] == "ortho") {
        command->type = TestCommand::Ortho;
        if (tokens.size() == 5) {
            for (int i = 0; i < 4; ++i) {
                if (!parse_float(tokens[1 + i], &command->values[i])) {
                    *skip_reason = "unsupported ortho";
                    return false;
                }
            }
            command->value_count = 4;
        }
        return true;
    }
    if (tokens.size() == 7 && tokens[0] == "probe" &&
        tokens[1] == "all" && tokens[2] == "rgba") {
        command->type = TestCommand::ProbeAllRgba;
        for (int i = 0; i < 4; ++i) {
            if (!parse_float(tokens[3 + i], &command->values[i])) {
                *skip_reason = "unsupported probe all rgba";
                return false;
            }
        }
        command->value_count = 4;
        return true;
    }
    if (tokens.size() == 6 && tokens[0] == "probe" &&
        tokens[1] == "all" && tokens[2] == "rgb") {
        command->type = TestCommand::ProbeAllRgba;
        command->compare_components = 3;
        for (int i = 0; i < 3; ++i) {
            if (!parse_float(tokens[3 + i], &command->values[i])) {
                *skip_reason = "unsupported probe all rgb";
                return false;
            }
        }
        command->value_count = 3;
        return true;
    }
    if (tokens.size() == 8 && tokens[0] == "probe" &&
        tokens[1] == "rgba") {
        command->type = TestCommand::ProbeRgba;
        for (int i = 0; i < 6; ++i) {
            if (!parse_float(tokens[2 + i], &command->values[i])) {
                *skip_reason = "unsupported probe rgba";
                return false;
            }
        }
        command->value_count = 6;
        return true;
    }
    if (tokens.size() >= 7 && tokens[0] == "probe" &&
        tokens[1] == "rgb") {
        command->type = TestCommand::ProbeRgba;
        command->compare_components = 3;
        for (int i = 0; i < 5; ++i) {
            if (!parse_float(tokens[2 + i], &command->values[i])) {
                *skip_reason = "unsupported probe rgb";
                return false;
            }
        }
        command->value_count = 5;
        return true;
    }
    if (tokens.size() >= 4 && tokens[0] == "probe" &&
        tokens[1] == "rect" &&
        (tokens[2] == "rgba" || tokens[2] == "rgb")) {
        const std::vector<float> floats = parse_float_values(line);
        const int components = tokens[2] == "rgba" ? 4 : 3;
        if (floats.size() < (size_t)(4 + components)) {
            *skip_reason = "unsupported probe rect";
            return false;
        }
        command->type = TestCommand::ProbeRectRgba;
        command->compare_components = components;
        command->value_count = 4 + components;
        for (int i = 0; i < command->value_count; ++i) {
            command->values[i] = floats[(size_t)i];
        }
        return true;
    }
    if (tokens.size() >= 4 && tokens[0] == "relative" &&
        tokens[1] == "probe") {
        const bool rect = tokens[2] == "rect";
        const bool rgba = (!rect && tokens[2] == "rgba") ||
                          (rect && tokens[3] == "rgba");
        const bool rgb = (!rect && tokens[2] == "rgb") ||
                         (rect && tokens[3] == "rgb");
        const std::vector<float> floats = parse_float_values(line);
        const int components = rgba ? 4 : (rgb ? 3 : 0);
        const int coordinate_count = rect ? 4 : 2;
        if (components == 0 ||
            floats.size() < (size_t)(coordinate_count + components)) {
            *skip_reason = "unsupported relative probe";
            return false;
        }

        command->type = rect ? TestCommand::ProbeRectRgba
                             : TestCommand::ProbeRgba;
        command->compare_components = components;
        command->relative = true;
        command->value_count = coordinate_count + components;
        for (int i = 0; i < command->value_count; ++i) {
            command->values[i] = floats[(size_t)i];
        }
        return true;
    }
    if (tokens.size() == 2 && tokens[0] == "link" &&
        tokens[1] == "success") {
        command->type = TestCommand::LinkSuccess;
        return true;
    }
    if (tokens.size() == 2 && tokens[0] == "link" &&
        tokens[1] == "error") {
        command->type = TestCommand::LinkError;
        return true;
    }

    *skip_reason = std::string("unsupported shader_test command: ") +
                   tokens[0];
    return false;
}

static bool parse_commands(const ShaderTest &test,
                           std::vector<TestCommand> *commands,
                           std::string *skip_reason) {
    size_t pos = 0;
    while (pos <= test.test.size()) {
        size_t next = test.test.find('\n', pos);
        if (next == std::string::npos) {
            next = test.test.size();
        }
        TestCommand command;
        if (!parse_command_line(test.test.substr(pos, next - pos), &command,
                                skip_reason)) {
            return false;
        }
        if (!command.text.empty()) {
            commands->push_back(command);
        }
        if (next == test.test.size()) {
            break;
        }
        pos = next + 1;
    }
    return true;
}

static bool command_list_has_draw_or_link(
    const std::vector<TestCommand> &commands) {
    for (size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].type == TestCommand::DrawRect ||
            commands[i].type == TestCommand::LinkSuccess ||
            commands[i].type == TestCommand::LinkError) {
            return true;
        }
    }
    return false;
}

static bool unsupported_shader_sections(const ShaderTest &test,
                                        std::string *skip_reason) {
    if (!test.geometry_shader.empty() ||
        !test.tess_control_shader.empty() ||
        !test.tess_eval_shader.empty() ||
        !test.compute_shader.empty()) {
        *skip_reason = "shader_test uses unsupported shader stage";
        return true;
    }
    if (!test.vertex_data.empty()) {
        *skip_reason = "shader_test uses vertex data";
        return true;
    }
    if (test.fragment_shader.empty()) {
        *skip_reason = "shader_test has no fragment shader";
        return true;
    }
    if (contains(test.vertex_shader, "switch") ||
        contains(test.fragment_shader, "switch")) {
        *skip_reason = "shader_test uses switch (CafeGLSL fatal guard)";
        return true;
    }
    return false;
}

static const char *fallback_vertex_shader(void) {
    return "#version 330 core\n"
           "layout(location = 0) in vec2 aPosition;\n"
           "uniform mat4 gx2gl_ModelViewProjectionMatrix;\n"
           "uniform vec4 gx2gl_CurrentColor;\n"
           "out vec4 gx2gl_FrontColor;\n"
           "void main() {\n"
           "    gx2gl_FrontColor = gx2gl_CurrentColor;\n"
           "    gl_Position = gx2gl_ModelViewProjectionMatrix * vec4(aPosition, 0.0, 1.0);\n"
           "}\n";
}

static bool setup_render_target(PiglitReportFunc report, ProgramObjects *obj) {
    glGenFramebuffers(1, &obj->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, obj->fbo);
    glGenTextures(1, &obj->texture);
    glBindTexture(GL_TEXTURE_2D, obj->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTargetWidth, kTargetHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, obj->texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        report("[FAIL] Piglit manifest framebuffer incomplete.\n");
        return false;
    }
    return expect_no_error(report, "manifest FBO setup");
}

static void rgbw_pixel(int x, int y, GLubyte *out) {
    const bool right = (x & 1) != 0;
    const bool top = (y & 1) != 0;
    out[0] = right ? 0 : 255;
    out[1] = top ? 0 : 255;
    out[2] = (right == top) ? 255 : 0;
    out[3] = 255;
}

static bool create_rgbw_texture(PiglitReportFunc report, ProgramObjects *obj,
                                const TestCommand &command) {
    const int unit = command.int_values[0];
    const int width = command.int_values[1];
    const int height = command.int_values[2];
    const GLenum internal_format = (GLenum)command.uint_values[0];
    GLuint texture = 0;

    if (unit < 0 || unit >= kMaxRunnerTextures ||
        width <= 0 || height <= 0) {
        report("[FAIL] Piglit manifest invalid texture rgbw command.\n");
        return false;
    }

    if (obj->generated_textures[unit]) {
        glDeleteTextures(1, &obj->generated_textures[unit]);
        obj->generated_textures[unit] = 0;
    }

    std::vector<GLubyte> pixels((size_t)width * (size_t)height * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            rgbw_pixel(x, y,
                       &pixels[((size_t)y * (size_t)width +
                                (size_t)x) * 4u]);
        }
    }

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal_format, width, height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);
    if (!expect_no_error(report, "manifest texture rgbw")) {
        if (texture) {
            glDeleteTextures(1, &texture);
        }
        return false;
    }

    obj->generated_textures[unit] = texture;
    obj->generated_texture_width[unit] = width;
    obj->generated_texture_height[unit] = height;
    return true;
}

static bool bind_framebuffer_texture_2d(PiglitReportFunc report,
                                        ProgramObjects *obj,
                                        const TestCommand &command) {
    GLenum attachments[8];

    if (command.value_count <= 0 || command.value_count > 8) {
        report("[FAIL] Piglit manifest invalid fb tex 2d command.\n");
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, obj->fbo);
    for (int i = 0; i < command.value_count; ++i) {
        const int unit = command.int_values[i];
        GLuint texture = 0;

        if (unit >= 0) {
            if (unit >= kMaxRunnerTextures ||
                !obj->generated_textures[unit]) {
                report("[FAIL] Piglit manifest fb tex 2d references "
                       "missing texture %d.\n", unit);
                return false;
            }
            texture = obj->generated_textures[unit];
        }
        attachments[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachments[i],
                               GL_TEXTURE_2D, texture, 0);
    }
    glDrawBuffers(command.value_count, attachments);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        report("[FAIL] Piglit manifest fb tex 2d produced incomplete FBO.\n");
        return false;
    }
    glViewport(0, 0, kTargetWidth, kTargetHeight);
    return expect_no_error(report, "manifest fb tex 2d");
}

static bool setup_rect_vertices(PiglitReportFunc report, ProgramObjects *obj,
                                const TestCommand &command) {
    GLint attrib_location;
    float x = command.values[0];
    float y = command.values[1];
    float w = command.values[2];
    float h = command.values[3];
    if (command.force_ortho) {
        x = -1.0f + 2.0f * (x / (float)kTargetWidth);
        y = -1.0f + 2.0f * (y / (float)kTargetHeight);
        w = 2.0f * (w / (float)kTargetWidth);
        h = 2.0f * (h / (float)kTargetHeight);
    }
    const GLfloat vertices[] = {
        x,     y,
        x + w, y,
        x + w, y + h,
        x,     y,
        x + w, y + h,
        x,     y + h,
    };

    if (!obj->vao) {
        glGenVertexArrays(1, &obj->vao);
    }
    if (!obj->vbo) {
        glGenBuffers(1, &obj->vbo);
    }
    glBindVertexArray(obj->vao);
    glBindBuffer(GL_ARRAY_BUFFER, obj->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                 GL_STREAM_DRAW);
    attrib_location = glGetAttribLocation(obj->program, "gx2gl_Vertex");
    if (attrib_location < 0) {
        attrib_location = glGetAttribLocation(obj->program, "aPosition");
    }
    if (attrib_location < 0) {
        attrib_location = 0;
    }
    if (attrib_location >= 16) {
        report("[FAIL] Piglit manifest vertex attribute location %d is "
               "outside the runner limit.\n", attrib_location);
        return false;
    }
    glVertexAttribPointer((GLuint)attrib_location, 2, GL_FLOAT, GL_FALSE,
                          2 * sizeof(GLfloat),
                          (const GLvoid *)0);
    glEnableVertexAttribArray((GLuint)attrib_location);
    return expect_no_error(report, "manifest vertex setup");
}

static bool set_mvp_uniform(PiglitReportFunc report, GLuint program,
                            bool ortho, const float *bounds = NULL) {
    GLfloat matrix[16];
    const GLint location =
        glGetUniformLocation(program, "gx2gl_ModelViewProjectionMatrix");

    if (location < 0) {
        return expect_no_error(report, "manifest MVP lookup");
    }

    memset(matrix, 0, sizeof(matrix));
    if (ortho) {
        const float left = bounds ? bounds[0] : 0.0f;
        const float right = bounds ? bounds[1] : (float)kTargetWidth;
        const float bottom = bounds ? bounds[2] : 0.0f;
        const float top = bounds ? bounds[3] : (float)kTargetHeight;
        if (right == left || top == bottom) {
            report("[FAIL] Piglit manifest invalid ortho bounds.\n");
            return false;
        }
        matrix[0] = 2.0f / (right - left);
        matrix[5] = 2.0f / (top - bottom);
        matrix[10] = 1.0f;
        matrix[12] = -(right + left) / (right - left);
        matrix[13] = -(top + bottom) / (top - bottom);
        matrix[15] = 1.0f;
    } else {
        matrix[0] = 1.0f;
        matrix[5] = 1.0f;
        matrix[10] = 1.0f;
        matrix[15] = 1.0f;
    }

    glUniformMatrix4fv(location, 1, GL_FALSE, matrix);
    return expect_no_error(report, "manifest MVP uniform");
}

static bool set_current_color_uniform(PiglitReportFunc report, GLuint program,
                                      const float *rgba) {
    const GLint location =
        glGetUniformLocation(program, "gx2gl_CurrentColor");

    if (location < 0) {
        return expect_no_error(report, "manifest current color lookup");
    }

    glUniform4f(location, rgba[0], rgba[1], rgba[2], rgba[3]);
    return expect_no_error(report, "manifest current color uniform");
}

static bool apply_uniform(PiglitReportFunc report, GLuint program,
                          const TestCommand &command) {
    const GLint location =
        glGetUniformLocation(program, command.uniform_name.c_str());
    const std::string type = command.uniform_type;
    if (location < 0) {
        return expect_no_error(report, "manifest inactive uniform");
    }
    if (type == "int" || type == "bool") {
        glUniform1i(location, command.int_values[0]);
    } else if (type == "uint") {
        glUniform1ui(location, command.uint_values[0]);
    } else if (type == "float") {
        glUniform1f(location, command.values[0]);
    } else if (type == "vec2") {
        glUniform2f(location, command.values[0], command.values[1]);
    } else if (type == "vec3") {
        glUniform3f(location, command.values[0], command.values[1],
                    command.values[2]);
    } else if (type == "vec4") {
        glUniform4f(location, command.values[0], command.values[1],
                    command.values[2], command.values[3]);
    } else if (type == "ivec2" || type == "bvec2") {
        glUniform2i(location, command.int_values[0],
                    command.int_values[1]);
    } else if (type == "ivec3" || type == "bvec3") {
        glUniform3i(location, command.int_values[0],
                    command.int_values[1],
                    command.int_values[2]);
    } else if (type == "ivec4" || type == "bvec4") {
        glUniform4i(location, command.int_values[0],
                    command.int_values[1],
                    command.int_values[2],
                    command.int_values[3]);
    } else if (type == "uvec2") {
        glUniform2ui(location, command.uint_values[0],
                     command.uint_values[1]);
    } else if (type == "uvec3") {
        glUniform3ui(location, command.uint_values[0],
                     command.uint_values[1],
                     command.uint_values[2]);
    } else if (type == "uvec4") {
        glUniform4ui(location, command.uint_values[0],
                     command.uint_values[1],
                     command.uint_values[2],
                     command.uint_values[3]);
    } else {
        report("[INFO] Piglit manifest unsupported uniform type %s.\n",
               type.c_str());
        return false;
    }
    return expect_no_error(report, "manifest uniform");
}

static int expected_u8(float value) {
    int converted = (int)(value * 255.0f + 0.5f);
    if (converted < 0) {
        converted = 0;
    } else if (converted > 255) {
        converted = 255;
    }
    return converted;
}

static bool probe_pixel(const GLubyte *pixel, const float *expected,
                        int components) {
    const int tolerance = 3;
    for (int c = 0; c < components; ++c) {
        int diff = (int)pixel[c] - expected_u8(expected[c]);
        if (diff < 0) {
            diff = -diff;
        }
        if (diff > tolerance) {
            return false;
        }
    }
    return true;
}

static bool run_probe_all(PiglitReportFunc report, const char *case_name,
                          const TestCommand &command) {
    std::vector<GLubyte> pixels(kTargetWidth * kTargetHeight * 4);
    glReadPixels(0, 0, kTargetWidth, kTargetHeight, GL_RGBA,
                 GL_UNSIGNED_BYTE, &pixels[0]);
    if (!expect_no_error(report, "manifest probe all rgba")) {
        return false;
    }
    for (int i = 0; i < kTargetWidth * kTargetHeight; ++i) {
        if (!probe_pixel(&pixels[(size_t)i * 4], command.values,
                         command.compare_components)) {
            const GLubyte *p = &pixels[(size_t)i * 4];
            report("[FAIL] Piglit manifest %s pixel %d returned "
                   "{%u,%u,%u,%u}, expected {%d,%d,%d,%d}.\n",
                   case_name, i, p[0], p[1], p[2], p[3],
                   expected_u8(command.values[0]),
                   expected_u8(command.values[1]),
                   expected_u8(command.values[2]),
                   expected_u8(command.values[3]));
            return false;
        }
    }
    return true;
}

static int clamped_relative_coord(float value, int extent) {
    int coord = (int)(value * (float)(extent - 1) + 0.5f);
    if (coord < 0) return 0;
    if (coord >= extent) return extent - 1;
    return coord;
}

static bool run_probe(PiglitReportFunc report, const char *case_name,
                      const TestCommand &command, bool relative) {
    GLubyte pixel[4] = {0, 0, 0, 0};
    const int x = relative ? clamped_relative_coord(command.values[0],
                                                    kTargetWidth)
                           : (int)command.values[0];
    const int y = relative ? clamped_relative_coord(command.values[1],
                                                    kTargetHeight)
                           : (int)command.values[1];
    if (x < 0 || y < 0 || x >= kTargetWidth || y >= kTargetHeight) {
        report("[FAIL] Piglit manifest %s probe outside target.\n",
               case_name);
        return false;
    }
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!expect_no_error(report, "manifest probe rgba")) {
        return false;
    }
    if (probe_pixel(pixel, &command.values[2], command.compare_components)) {
        return true;
    }
    report("[FAIL] Piglit manifest %s probe returned {%u,%u,%u,%u}, "
           "expected {%d,%d,%d,%d}.\n",
           case_name, pixel[0], pixel[1], pixel[2], pixel[3],
           expected_u8(command.values[2]), expected_u8(command.values[3]),
           expected_u8(command.values[4]), expected_u8(command.values[5]));
    return false;
}

static bool run_probe_rect(PiglitReportFunc report, const char *case_name,
                           const TestCommand &command, bool relative) {
    int x0;
    int y0;
    int x1;
    int y1;

    if (relative) {
        x0 = (int)(command.values[0] * (float)kTargetWidth);
        y0 = (int)(command.values[1] * (float)kTargetHeight);
        x1 = (int)((command.values[0] + command.values[2]) *
                   (float)kTargetWidth + 0.999f);
        y1 = (int)((command.values[1] + command.values[3]) *
                   (float)kTargetHeight + 0.999f);
    } else {
        x0 = (int)command.values[0];
        y0 = (int)command.values[1];
        x1 = x0 + (int)command.values[2];
        y1 = y0 + (int)command.values[3];
    }
    const float *expected = &command.values[4];
    if (x0 < 0 || y0 < 0 || x1 <= x0 || y1 <= y0 ||
        x1 > kTargetWidth || y1 > kTargetHeight) {
        report("[FAIL] Piglit manifest %s probe rect outside target.\n",
               case_name);
        return false;
    }

    std::vector<GLubyte> pixels((size_t)(x1 - x0) * (size_t)(y1 - y0) * 4u);
    glReadPixels(x0, y0, x1 - x0, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE,
                 &pixels[0]);
    if (!expect_no_error(report, "manifest probe rect rgba")) {
        return false;
    }
    for (size_t i = 0; i < pixels.size() / 4u; ++i) {
        if (!probe_pixel(&pixels[i * 4u], expected,
                         command.compare_components)) {
            const GLubyte *p = &pixels[i * 4u];
            report("[FAIL] Piglit manifest %s rect pixel returned "
                   "{%u,%u,%u,%u}, expected {%d,%d,%d,%d}.\n",
                   case_name, p[0], p[1], p[2], p[3],
                   expected_u8(expected[0]), expected_u8(expected[1]),
                   expected_u8(expected[2]),
                   command.compare_components == 4
                       ? expected_u8(expected[3])
                       : 255);
            return false;
        }
    }
    return true;
}

static void destroy_objects(ProgramObjects *obj) {
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int i = 0; i < kMaxRunnerTextures; ++i) {
        if (obj->generated_textures[i]) {
            glDeleteTextures(1, &obj->generated_textures[i]);
        }
    }
    if (obj->vbo) glDeleteBuffers(1, &obj->vbo);
    if (obj->vao) glDeleteVertexArrays(1, &obj->vao);
    if (obj->texture) glDeleteTextures(1, &obj->texture);
    if (obj->fbo) glDeleteFramebuffers(1, &obj->fbo);
    if (obj->program) glDeleteProgram(obj->program);
    if (obj->vertex_shader) glDeleteShader(obj->vertex_shader);
    if (obj->fragment_shader) glDeleteShader(obj->fragment_shader);
    memset(obj, 0, sizeof(*obj));
}

static PiglitResult run_shader_test(PiglitReportFunc report,
                                    const char *case_name,
                                    const std::string &contents,
                                    std::string *detail) {
    const ShaderTest test = parse_shader_test(contents);
    std::vector<TestCommand> commands;
    ProgramObjects obj = {};
    const std::string vertex_source =
        test.vertex_shader.empty() ? std::string(fallback_vertex_shader())
                                   : test.vertex_shader;
    const std::vector<VaryingLocation> varyings =
        build_varying_locations(vertex_source, test.fragment_shader);
    const int front_color_location =
        front_color_location_for(vertex_source, test.fragment_shader);
    const int secondary_color_location =
        secondary_color_location_for(vertex_source, test.fragment_shader,
                                     front_color_location);
    const std::vector<TexCoordLocation> texcoords =
        build_texcoord_locations(vertex_source, test.fragment_shader,
                                 varyings, front_color_location,
                                 secondary_color_location);
    bool linked = false;
    bool link_error_expected = false;

    if (unsupported_shader_sections(test, detail)) {
        return PIGLIT_RESULT_SKIP;
    }
    if (!requirements_supported(test, detail)) {
        return PIGLIT_RESULT_SKIP;
    }
    if (!parse_commands(test, &commands, detail)) {
        return PIGLIT_RESULT_SKIP;
    }
    if (!command_list_has_draw_or_link(commands)) {
        *detail = "shader_test has no supported draw/link command";
        return PIGLIT_RESULT_SKIP;
    }

    for (size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].type == TestCommand::LinkError) {
            link_error_expected = true;
        }
    }

    clear_gl_errors();
    obj.vertex_shader = compile_shader(report, GL_VERTEX_SHADER,
                                       vertex_source, case_name, varyings,
                                       texcoords, front_color_location,
                                       secondary_color_location);
    obj.fragment_shader = compile_shader(report, GL_FRAGMENT_SHADER,
                                         test.fragment_shader, case_name,
                                         varyings, texcoords,
                                         front_color_location,
                                         secondary_color_location);
    if (!obj.vertex_shader || !obj.fragment_shader) {
        *detail = "shader compile failed";
        destroy_objects(&obj);
        return link_error_expected ? PIGLIT_RESULT_PASS : PIGLIT_RESULT_FAIL;
    }

    obj.program = link_program(report, obj.vertex_shader, obj.fragment_shader,
                               case_name, &linked);
    if (!obj.program) {
        *detail = "program allocation failed";
        destroy_objects(&obj);
        return PIGLIT_RESULT_FAIL;
    }
    if (!linked) {
        *detail = link_error_expected ? "link failed as expected"
                                      : "program link failed";
        destroy_objects(&obj);
        return link_error_expected ? PIGLIT_RESULT_PASS : PIGLIT_RESULT_FAIL;
    }
    if (link_error_expected) {
        *detail = "link succeeded but Piglit expected an error";
        destroy_objects(&obj);
        return PIGLIT_RESULT_FAIL;
    }

    if (!setup_render_target(report, &obj)) {
        *detail = "render target setup failed";
        destroy_objects(&obj);
        return PIGLIT_RESULT_FAIL;
    }

    glViewport(0, 0, kTargetWidth, kTargetHeight);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(obj.program);
    if (!set_mvp_uniform(report, obj.program, false)) {
        *detail = "MVP setup failed";
        destroy_objects(&obj);
        return PIGLIT_RESULT_FAIL;
    }
    float current_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (!set_current_color_uniform(report, obj.program, current_color)) {
        *detail = "current color setup failed";
        destroy_objects(&obj);
        return PIGLIT_RESULT_FAIL;
    }

    for (size_t i = 0; i < commands.size(); ++i) {
        const TestCommand &command = commands[i];
        switch (command.type) {
            case TestCommand::ClearColor:
                glClearColor(command.values[0], command.values[1],
                             command.values[2], command.values[3]);
                if (!expect_no_error(report, "manifest clear color")) {
                    *detail = "clear color failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::Color:
                for (int c = 0; c < 4; ++c) {
                    current_color[c] = command.values[c];
                }
                if (!set_current_color_uniform(report, obj.program,
                                               current_color)) {
                    *detail = "current color failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::Clear:
                glClear(GL_COLOR_BUFFER_BIT);
                if (!expect_no_error(report, "manifest clear")) {
                    *detail = "clear failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::Ortho:
                if (!set_mvp_uniform(report, obj.program, true,
                                     command.value_count == 4
                                         ? command.values
                                         : NULL)) {
                    *detail = "ortho setup failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::TextureRgbw:
                if (!create_rgbw_texture(report, &obj, command)) {
                    *detail = "texture rgbw failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::FramebufferTexture2D:
                if (!bind_framebuffer_texture_2d(report, &obj, command)) {
                    *detail = "framebuffer texture failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::Uniform:
                if (!apply_uniform(report, obj.program, command)) {
                    *detail = "uniform setup failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::DrawRect:
                if (!setup_rect_vertices(report, &obj, command)) {
                    *detail = "draw rect vertex setup failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                glDrawArrays(GL_TRIANGLES, 0, 6);
                if (!expect_no_error(report, "manifest draw rect")) {
                    *detail = "draw rect failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::ProbeAllRgba:
                glFinish();
                if (!run_probe_all(report, case_name, command)) {
                    *detail = "probe all rgba failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::ProbeRgba:
                glFinish();
                if (!run_probe(report, case_name, command,
                               command.relative)) {
                    *detail = "probe rgba failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::ProbeRectRgba:
                glFinish();
                if (!run_probe_rect(report, case_name, command,
                                    command.relative)) {
                    *detail = "probe rect rgba failed";
                    destroy_objects(&obj);
                    return PIGLIT_RESULT_FAIL;
                }
                break;
            case TestCommand::LinkSuccess:
            case TestCommand::LinkError:
                break;
        }
    }

    *detail = "executed shader_test";
    destroy_objects(&obj);
    return PIGLIT_RESULT_PASS;
}

static bool load_manifest(const char *path,
                          std::vector<ManifestEntry> *entries) {
    std::string contents;
    if (!read_text_file(path, 4 * 1024 * 1024, &contents)) {
        return false;
    }

    size_t pos = 0;
    while (pos <= contents.size()) {
        size_t next = contents.find('\n', pos);
        if (next == std::string::npos) {
            next = contents.size();
        }
        std::string line = trim(contents.substr(pos, next - pos));
        if (!line.empty() && line[0] != '#') {
            const std::vector<std::string> fields = split_tabs(line);
            if (fields.size() >= 3 && fields[0] != "name") {
                ManifestEntry entry;
                entry.name = fields[0];
                entry.kind = fields[1];
                entry.path = fields[2];
                entries->push_back(entry);
            }
        }
        if (next == contents.size()) {
            break;
        }
        pos = next + 1;
    }
    return true;
}

static std::string source_path_for(const char *manifest_path,
                                   const ManifestEntry &entry) {
    std::string base = manifest_path;
    const size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) {
        base.resize(slash + 1);
    } else {
        base.clear();
    }
    return base + "piglit/" + entry.path;
}

static PiglitResult run_manifest_entry(PiglitReportFunc report,
                                       const char *manifest_path,
                                       const ManifestEntry &entry,
                                       std::string *detail) {
    if (entry.name == "shaders/glsl-predication-on-large-array.shader_test" ||
        contains(entry.name, "large-local-array")) {
        *detail = "large-array shader currently poisons CafeGLSL runner";
        return PIGLIT_RESULT_SKIP;
    }
    if (contains(entry.name, "const-array-indirect-access-128-elements") ||
        contains(entry.name, "const-array-indirect-access-256-elements")) {
        *detail = "large const-array shader currently poisons CafeGLSL runner";
        return PIGLIT_RESULT_SKIP;
    }
    if (contains(entry.name,
                 "fs-vec4-const-array-indirect-access-048-elements") ||
        contains(entry.name,
                 "fs-vec4-const-array-indirect-access-064-elements")) {
        *detail = "large const-array shader currently poisons CafeGLSL runner";
        return PIGLIT_RESULT_SKIP;
    }
    if (contains(entry.name, "uniform_buffer/fs-struct-copy") ||
        contains(entry.name, "uniform_buffer/vs-struct-copy")) {
        *detail = "UBO struct-copy shader currently poisons CafeGLSL runner";
        return PIGLIT_RESULT_SKIP;
    }
    if (contains(entry.name, "inline-explosion")) {
        *detail = "inline-explosion shader currently poisons CafeGLSL runner";
        return PIGLIT_RESULT_SKIP;
    }
    if (contains(entry.name, "glsl-fs-struct-equal") ||
        contains(entry.name, "glsl-fs-struct-notequal")) {
        *detail = "struct comparison shader currently hangs CafeGLSL runner";
        return PIGLIT_RESULT_SKIP;
    }
    if (entry.kind != "shader_test") {
        *detail = "non-shader Piglit case not ported to RPX runner yet";
        return PIGLIT_RESULT_SKIP;
    }

    const std::string source_path = source_path_for(manifest_path, entry);
    std::string contents;
    if (!read_text_file(source_path.c_str(), kMaxShaderTestBytes,
                        &contents)) {
        *detail = "shader_test source missing or too large";
        return PIGLIT_RESULT_FAIL;
    }
    return run_shader_test(report, entry.name.c_str(), contents, detail);
}

} // namespace

PiglitRunStats run_piglit_manifest_tests(PiglitReportFunc report,
                                         PiglitBeginFunc begin_func,
                                         PiglitResultFunc result_func,
                                         PiglitContinueFunc continue_func,
                                         void *user_data) {
    PiglitRunStats stats = {};
    std::vector<ManifestEntry> entries;
    const char *manifest_path = NULL;

    if (!report) {
        return stats;
    }
    if (load_manifest(kExternalManifest, &entries)) {
        manifest_path = kExternalManifest;
    } else if (load_manifest(kContentManifest, &entries)) {
        manifest_path = kContentManifest;
    } else {
        publish_result(&stats, result_func, "piglit/external-manifest",
                       PIGLIT_RESULT_SKIP,
                       "copy gx2gl/piglit_manifest.tsv to SD or content",
                       user_data);
        return stats;
    }

    report("[INFO] Piglit manifest loaded %u cases from %s\n",
           (unsigned int)entries.size(), manifest_path);

    for (size_t i = 0; i < entries.size(); ++i) {
        if (continue_func && !continue_func(user_data)) {
            break;
        }
        if (begin_func) {
            begin_func(entries[i].name.c_str(), user_data);
        }
        std::string detail;
        const PiglitResult result =
            run_manifest_entry(report, manifest_path, entries[i], &detail);
        publish_result(&stats, result_func, entries[i].name.c_str(), result,
                       detail.c_str(), user_data);
    }

    return stats;
}
