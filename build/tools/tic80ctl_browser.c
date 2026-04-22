#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <emscripten.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct
{
    char* data;
    size_t len;
    size_t cap;
} StringBuilder;

typedef struct
{
    bool ok;
    bool running;
    bool initialized;
    bool owned;
    bool closed;
    bool is_error;
    bool stopped;
    char target_kind[32];
    char origin[256];
    char error[1024];
    char verb[64];
    char last_load_target[PATH_MAX];
    char stderr_tail[1024];
    char mcp_raw[262144];
} BrowserResponse;

#define BROWSER_TEXT_BUFFER 8192

typedef struct
{
    const char* ext;
    const char* comment;
} ScriptCartCommentRule;

typedef struct
{
    const char* tag;
    int row_count;
    int payload_hex_chars;
    int max_bank;
} ScriptCartSectionRule;

typedef struct
{
    bool ok;
    int line;
    char message[512];
} LintResult;

static const char* usage_text =
    "usage: tic80ctl [--json] <command> [args...]\n"
    "\n"
    "run `tic80ctl --help` for a fuller guide.\n";

static const char* help_text =
    "tic80ctl\n"
    "\n"
    "Operate a live TIC-80 session from the browser.\n"
    "\n"
    "usage:\n"
    "  tic80ctl [--json] <command> [args...]\n"
    "  tic80ctl --help\n"
    "  tic80ctl help [command]\n"
    "\n"
    "session commands:\n"
    "  start             bind and initialize the currently configured TIC-80 target\n"
    "  status            report whether the browser session target is alive\n"
    "  stop              close an owned target, or detach from a non-owned one\n"
    "\n"
    "runtime commands:\n"
    "  cmd \"...\"        send a raw TIC-80 console command\n"
    "  lint-cart <file>  validate TIC-80 script-cart structure before load\n"
    "  lint-playtest-script <file>\n"
    "                    validate a Lua playtest episode script before use\n"
    "  load <cart>       load a cart or script cart into the active session\n"
    "  run               start the loaded cart\n"
    "  eval \"<expr>\"    run a short Lua expression in the active runtime\n"
    "  screenshot [path] capture one frame; path is relative to the TIC filesystem root\n"
    "\n"
    "playtest:\n"
    "  playtest --script-file <file> [--timeout <seconds>] [--input-overlay|--no-input-overlay]\n"
    "\n"
    "editor commands:\n"
    "  sfx ...\n"
    "  music ...\n"
    "  sprite ...\n"
    "  map ...\n";

static const ScriptCartCommentRule ScriptCartComments[] =
{
    {".lua", "--"},
    {".moon", "--"},
    {".wasmp", "--"},
    {".js", "//"},
    {".nut", "//"},
    {".wren", "//"},
    {".py", "#"},
    {".rb", "#"},
    {".janet", "#"},
    {".scm", ";;"},
    {".fnl", ";;"},
};

static const ScriptCartSectionRule ScriptCartSections[] =
{
    {"TILES", 256, 64, 7},
    {"SPRITES", 256, 64, 7},
    {"MAP", 136, 480, 7},
    {"WAVES", 16, 32, 7},
    {"SFX", 64, 132, 7},
    {"PATTERNS", 60, 384, 7},
    {"TRACKS", 8, 102, 7},
    {"FLAGS", 2, 512, 7},
    {"SCREEN", 136, 240, 7},
    {"PALETTE", 2, 96, 7},
    {"LANG", 1, 2, 0},
};

static const size_t ScriptCartCodeCapacity = 64u * 1024u * 8u;
static const char* PlaytestScriptMarker = "tic80ctl: playtest-script";

static StringBuilder StdoutBuffer;
static StringBuilder StderrBuffer;
static StringBuilder ResultBuffer;
static jmp_buf FailJump;
static bool FailJumpActive = false;

EM_ASYNC_JS(char*, browserHostInvokeRaw, (const char* request_json), {
    function cloneString(text) {
        var normalized = typeof text === "string" ? text : String(text || "");
        var size = lengthBytesUTF8(normalized) + 1;
        var ptr = _malloc(size);
        stringToUTF8(normalized, ptr, size);
        return ptr;
    }

    if(typeof Module === "undefined" || !Module.tic80ctlBrowserHost || typeof Module.tic80ctlBrowserHost.invoke !== "function")
        return cloneString("{\"ok\":false,\"error\":\"tic80ctl browser host not initialized\"}");

    try
    {
        var result = await Module.tic80ctlBrowserHost.invoke(UTF8ToString(request_json));
        if(typeof result !== "string")
            result = JSON.stringify(result || {});
        return cloneString(result);
    }
    catch(error)
    {
        var message = error && error.message ? error.message : String(error);
        return cloneString(JSON.stringify({ok:false, error:message}));
    }
});

static void sb_init(StringBuilder* sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_clear(StringBuilder* sb)
{
    if(sb->data)
        sb->data[0] = '\0';
    sb->len = 0;
}

static void sb_free(StringBuilder* sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static bool sb_reserve(StringBuilder* sb, size_t extra)
{
    size_t needed = sb->len + extra + 1;
    if(needed <= sb->cap) return true;

    size_t cap = sb->cap ? sb->cap : 256;
    while(cap < needed) cap *= 2;

    char* data = realloc(sb->data, cap);
    if(!data) return false;
    sb->data = data;
    sb->cap = cap;
    return true;
}

static bool sb_append_n(StringBuilder* sb, const char* text, size_t len)
{
    if(!sb_reserve(sb, len)) return false;
    memcpy(sb->data + sb->len, text, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return true;
}

static bool sb_append(StringBuilder* sb, const char* text)
{
    return sb_append_n(sb, text ? text : "", strlen(text ? text : ""));
}

static bool sb_appendf(StringBuilder* sb, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if(needed < 0)
    {
        va_end(args);
        return false;
    }
    if(!sb_reserve(sb, (size_t)needed))
    {
        va_end(args);
        return false;
    }
    vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, args);
    va_end(args);
    sb->len += (size_t)needed;
    return true;
}

static bool sb_append_json_string(StringBuilder* sb, const char* text)
{
    if(!sb_append(sb, "\"")) return false;
    for(const unsigned char* p = (const unsigned char*)(text ? text : ""); *p; p++)
    {
        switch(*p)
        {
        case '\\': if(!sb_append(sb, "\\\\")) return false; break;
        case '"': if(!sb_append(sb, "\\\"")) return false; break;
        case '\n': if(!sb_append(sb, "\\n")) return false; break;
        case '\r': if(!sb_append(sb, "\\r")) return false; break;
        case '\t': if(!sb_append(sb, "\\t")) return false; break;
        default:
            if(*p < 0x20)
            {
                if(!sb_appendf(sb, "\\u%04x", *p)) return false;
            }
            else if(!sb_append_n(sb, (const char*)p, 1)) return false;
            break;
        }
    }
    return sb_append(sb, "\"");
}

static void out_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if(needed < 0 || !sb_reserve(&StdoutBuffer, (size_t)needed))
    {
        va_end(args);
        return;
    }
    vsnprintf(StdoutBuffer.data + StdoutBuffer.len, StdoutBuffer.cap - StdoutBuffer.len, fmt, args);
    va_end(args);
    StdoutBuffer.len += (size_t)needed;
}

static void err_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if(needed < 0 || !sb_reserve(&StderrBuffer, (size_t)needed))
    {
        va_end(args);
        return;
    }
    vsnprintf(StderrBuffer.data + StderrBuffer.len, StderrBuffer.cap - StderrBuffer.len, fmt, args);
    va_end(args);
    StderrBuffer.len += (size_t)needed;
}

static bool parse_long_strict(const char* text, long* out)
{
    if(!text || !*text) return false;
    char* end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if(errno != 0 || !end || *end) return false;
    *out = value;
    return true;
}

static bool parse_int_strict(const char* text, int* out)
{
    long value = 0;
    if(!parse_long_strict(text, &value)) return false;
    if(value < INT_MIN || value > INT_MAX) return false;
    *out = (int)value;
    return true;
}

static int hex_value(char c)
{
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return 10 + c - 'a';
    if(c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static const char* skip_ws(const char* s)
{
    while(*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void failf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    va_start(args, fmt);
    if(needed > 0 && sb_reserve(&StderrBuffer, (size_t)needed + 1))
    {
        vsnprintf(StderrBuffer.data + StderrBuffer.len, StderrBuffer.cap - StderrBuffer.len, fmt, args);
        StderrBuffer.len += (size_t)needed;
        StderrBuffer.data[StderrBuffer.len++] = '\n';
        StderrBuffer.data[StderrBuffer.len] = '\0';
    }
    va_end(args);

    if(FailJumpActive)
        longjmp(FailJump, 1);
}

static const char* find_json_key(const char* json, const char* key)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = json;
    while((p = strstr(p, pattern)) != NULL)
    {
        const char* after = skip_ws(p + strlen(pattern));
        if(*after == ':')
            return skip_ws(after + 1);
        p += strlen(pattern);
    }
    return NULL;
}

static char* parse_json_string_value(const char* start, const char** end_out)
{
    if(!start || *start != '"') return NULL;
    start++;
    StringBuilder sb;
    sb_init(&sb);

    for(const char* p = start; *p; p++)
    {
        char c = *p;
        if(c == '"')
        {
            if(end_out) *end_out = p + 1;
            return sb.data ? sb.data : strdup("");
        }
        if(c == '\\')
        {
            p++;
            if(!*p)
            {
                sb_free(&sb);
                return NULL;
            }
            switch(*p)
            {
            case '\\':
            case '"':
            case '/': if(!sb_append_n(&sb, p, 1)) goto oom; break;
            case 'n': if(!sb_append_n(&sb, "\n", 1)) goto oom; break;
            case 'r': if(!sb_append_n(&sb, "\r", 1)) goto oom; break;
            case 't': if(!sb_append_n(&sb, "\t", 1)) goto oom; break;
            case 'b': if(!sb_append_n(&sb, "\b", 1)) goto oom; break;
            case 'f': if(!sb_append_n(&sb, "\f", 1)) goto oom; break;
            case 'u':
                if(!sb_append_n(&sb, "?", 1)) goto oom;
                for(int i = 0; i < 4 && p[1]; i++) p++;
                break;
            default:
                sb_free(&sb);
                return NULL;
            }
            continue;
        }
        if(!sb_append_n(&sb, &c, 1)) goto oom;
    }

    sb_free(&sb);
    return NULL;

oom:
    sb_free(&sb);
    return NULL;
}

static bool json_get_string(const char* json, const char* key, char* out, size_t out_size)
{
    const char* p = find_json_key(json, key);
    if(!p) return false;
    char* value = parse_json_string_value(p, NULL);
    if(!value) return false;
    snprintf(out, out_size, "%s", value);
    free(value);
    return true;
}

static bool json_get_bool(const char* json, const char* key, bool* out)
{
    const char* p = find_json_key(json, key);
    if(!p) return false;
    if(strncmp(p, "true", 4) == 0)
    {
        *out = true;
        return true;
    }
    if(strncmp(p, "false", 5) == 0)
    {
        *out = false;
        return true;
    }
    return false;
}

static bool json_get_long(const char* json, const char* key, long* out)
{
    const char* p = find_json_key(json, key);
    if(!p) return false;
    char* end = NULL;
    errno = 0;
    long value = strtol(p, &end, 10);
    if(errno != 0 || end == p) return false;
    *out = value;
    return true;
}

static bool json_get_raw_value(const char* json, const char* key, char* out, size_t out_size)
{
    const char* p = find_json_key(json, key);
    if(!p) return false;
    const char* start = p;
    const char* end = p;

    if(*p == '"')
    {
        char* value = parse_json_string_value(p, &end);
        if(!value) return false;
        bool ok = snprintf(out, out_size, "%s", value) < (int)out_size;
        free(value);
        return ok;
    }

    if(*p == '{' || *p == '[')
    {
        int depth = 0;
        bool in_string = false;
        bool escape = false;
        for(; *end; end++)
        {
            char c = *end;
            if(in_string)
            {
                if(escape) escape = false;
                else if(c == '\\') escape = true;
                else if(c == '"') in_string = false;
                continue;
            }

            if(c == '"')
            {
                in_string = true;
                continue;
            }

            if(c == '{' || c == '[') depth++;
            else if(c == '}' || c == ']')
            {
                depth--;
                if(depth == 0)
                {
                    end++;
                    break;
                }
            }
        }
    }
    else while(*end && *end != ',' && *end != '}') end++;

    if(end <= start) return false;
    size_t len = (size_t)(end - start);
    if(len >= out_size) return false;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static char* browser_host_invoke(const char* request_json)
{
    char* raw = browserHostInvokeRaw(request_json);
    if(!raw)
        return strdup("{\"ok\":false,\"error\":\"browser host invocation failed\"}");
    return raw;
}

static bool browser_request_json(const char* request_json, BrowserResponse* response)
{
    memset(response, 0, sizeof(*response));
    char* raw = browser_host_invoke(request_json);
    if(!raw) return false;

    json_get_bool(raw, "ok", &response->ok);
    json_get_bool(raw, "running", &response->running);
    json_get_bool(raw, "initialized", &response->initialized);
    json_get_bool(raw, "owned", &response->owned);
    json_get_bool(raw, "closed", &response->closed);
    json_get_bool(raw, "is_error", &response->is_error);
    json_get_bool(raw, "stopped", &response->stopped);
    json_get_string(raw, "target_kind", response->target_kind, sizeof(response->target_kind));
    json_get_string(raw, "origin", response->origin, sizeof(response->origin));
    json_get_string(raw, "error", response->error, sizeof(response->error));
    json_get_string(raw, "verb", response->verb, sizeof(response->verb));
    json_get_string(raw, "last_load_target", response->last_load_target, sizeof(response->last_load_target));
    json_get_string(raw, "stderr_tail", response->stderr_tail, sizeof(response->stderr_tail));
    json_get_string(raw, "mcp_raw", response->mcp_raw, sizeof(response->mcp_raw));
    free(raw);
    return true;
}

static BrowserResponse* alloc_browser_response(void)
{
    return calloc(1, sizeof(BrowserResponse));
}

static bool browser_read_text_file(const char* path, char** text_out, char* error, size_t error_size)
{
    StringBuilder request;
    sb_init(&request);
    sb_append(&request, "{\"op\":\"read_text_file\",\"path\":");
    sb_append_json_string(&request, path);
    sb_append(&request, "}");

    char* raw = browser_host_invoke(request.data);
    sb_free(&request);
    if(!raw) return false;

    bool ok = false;
    json_get_bool(raw, "ok", &ok);
    if(!ok)
    {
        json_get_string(raw, "error", error, error_size);
        free(raw);
        return false;
    }

    const char* text_value = find_json_key(raw, "text");
    if(!text_value)
    {
        free(raw);
        snprintf(error, error_size, "missing file text");
        return false;
    }

    *text_out = parse_json_string_value(text_value, NULL);
    free(raw);
    if(!*text_out)
    {
        snprintf(error, error_size, "failed to decode file text");
        return false;
    }
    return true;
}

static void print_usage(void)
{
    out_printf("%s", usage_text);
}

static int print_help(const char* topic)
{
    if(topic && *topic && strcmp(topic, "start") != 0 && strcmp(topic, "status") != 0 && strcmp(topic, "stop") != 0
        && strcmp(topic, "load") != 0 && strcmp(topic, "run") != 0 && strcmp(topic, "eval") != 0
        && strcmp(topic, "screenshot") != 0 && strcmp(topic, "playtest") != 0
        && strcmp(topic, "sfx") != 0 && strcmp(topic, "music") != 0
        && strcmp(topic, "sprite") != 0 && strcmp(topic, "map") != 0
        && strcmp(topic, "lint-cart") != 0 && strcmp(topic, "lint-playtest-script") != 0)
    {
        err_printf("tic80ctl: unknown help topic: %s\n\n", topic);
        print_usage();
        return 1;
    }

    out_printf("%s", help_text);
    return 0;
}

static bool line_has_call(const char* line, const char* name)
{
    size_t name_len = strlen(name);
    const char* ptr = line;
    while((ptr = strstr(ptr, name)) != NULL)
    {
        const char before = ptr > line ? ptr[-1] : '\0';
        if((ptr == line || !(isalnum((unsigned char)before) || before == '_')))
        {
            const char* after = skip_ws(ptr + name_len);
            if(*after == '(') return true;
        }
        ptr += name_len;
    }
    return false;
}

static bool line_has_cart_callback(const char* line, char* callback, size_t callback_size)
{
    static const char* callbacks[] = {"TIC", "BOOT", "SCN", "OVR", "BDR", "MENU"};
    const char* ptr = skip_ws(line);
    if(strncmp(ptr, "function", 8) != 0 || !(ptr[8] == ' ' || ptr[8] == '\t'))
        return false;
    ptr = skip_ws(ptr + 8);
    for(size_t i = 0; i < sizeof(callbacks) / sizeof(callbacks[0]); i++)
    {
        size_t len = strlen(callbacks[i]);
        if(strncmp(ptr, callbacks[i], len) == 0)
        {
            const char* after = skip_ws(ptr + len);
            if(*after == '(')
            {
                snprintf(callback, callback_size, "%s", callbacks[i]);
                return true;
            }
        }
    }
    return false;
}

static const char* script_cart_comment_for_path(const char* path)
{
    const char* dot = strrchr(path, '.');
    if(!dot) return NULL;
    for(size_t i = 0; i < sizeof(ScriptCartComments) / sizeof(ScriptCartComments[0]); i++)
        if(strcmp(dot, ScriptCartComments[i].ext) == 0)
            return ScriptCartComments[i].comment;
    return NULL;
}

static const ScriptCartSectionRule* script_cart_section_rule(const char* tag, int* bank_out)
{
    char base[32];
    size_t base_len = strcspn(tag, "0123456789");
    if(base_len == 0 || base_len >= sizeof(base)) return NULL;
    memcpy(base, tag, base_len);
    base[base_len] = '\0';

    const char* suffix = tag + base_len;
    int bank = 0;
    if(*suffix)
    {
        if(!parse_int_strict(suffix, &bank) || bank < 1 || bank > 7)
            return NULL;
    }

    for(size_t i = 0; i < sizeof(ScriptCartSections) / sizeof(ScriptCartSections[0]); i++)
        if(strcmp(base, ScriptCartSections[i].tag) == 0)
        {
            if(bank > ScriptCartSections[i].max_bank) return NULL;
            if(bank_out) *bank_out = bank;
            return &ScriptCartSections[i];
        }
    return NULL;
}

static void set_lint_error(LintResult* result, int line, const char* fmt, ...)
{
    result->ok = false;
    result->line = line;
    va_list args;
    va_start(args, fmt);
    vsnprintf(result->message, sizeof(result->message), fmt, args);
    va_end(args);
}

static bool parse_tag_line(const char* line, const char* comment, bool* is_end_tag, char* tag, size_t tag_size)
{
    size_t comment_len = strlen(comment);
    if(strncmp(line, comment, comment_len) != 0 || line[comment_len] != ' ')
        return false;

    const char* ptr = line + comment_len + 1;
    if(*ptr != '<') return false;
    ptr++;
    bool end_tag = false;
    if(*ptr == '/')
    {
        end_tag = true;
        ptr++;
    }

    const char* tag_start = ptr;
    while((*ptr >= 'A' && *ptr <= 'Z') || (*ptr >= '0' && *ptr <= '9'))
        ptr++;
    if(ptr == tag_start || *ptr != '>') return false;

    size_t tag_len = (size_t)(ptr - tag_start);
    if(tag_len >= tag_size) return false;
    memcpy(tag, tag_start, tag_len);
    tag[tag_len] = '\0';
    ptr = skip_ws(ptr + 1);
    if(*ptr != '\0') return false;

    if(is_end_tag) *is_end_tag = end_tag;
    return true;
}

static bool parse_section_row_line(const char* line, const char* comment, int* index_out, const char** payload_out, size_t* payload_len_out)
{
    size_t comment_len = strlen(comment);
    if(strncmp(line, comment, comment_len) != 0 || line[comment_len] != ' ')
        return false;
    const char* ptr = line + comment_len + 1;
    if(!isdigit((unsigned char)ptr[0]) || !isdigit((unsigned char)ptr[1]) || !isdigit((unsigned char)ptr[2]) || ptr[3] != ':')
        return false;
    int index = (ptr[0] - '0') * 100 + (ptr[1] - '0') * 10 + (ptr[2] - '0');
    ptr += 4;
    if(index_out) *index_out = index;
    if(payload_out) *payload_out = ptr;
    if(payload_len_out) *payload_len_out = strlen(ptr);
    return true;
}

static bool span_is_hex(const char* text, size_t len)
{
    for(size_t i = 0; i < len; i++)
        if(hex_value(text[i]) < 0)
            return false;
    return true;
}

static int line_number_for_offset(const char* text, const char* offset)
{
    int line = 1;
    for(const char* ptr = text; ptr < offset; ptr++)
        if(*ptr == '\n') line++;
    return line;
}

static size_t script_cart_code_length(const char* text, const char* comment, int* first_tag_line_out)
{
    char tag_start[32];
    snprintf(tag_start, sizeof(tag_start), "\n%s <", comment);
    const char* tag = strstr(text, tag_start);
    if(tag)
    {
        if(first_tag_line_out)
            *first_tag_line_out = line_number_for_offset(text, tag) + 1;
        return (size_t)(tag - text);
    }
    if(first_tag_line_out) *first_tag_line_out = 0;
    return strlen(text);
}

static bool seen_section_tag(char seen[][32], int count, const char* tag)
{
    for(int i = 0; i < count; i++)
        if(strcmp(seen[i], tag) == 0)
            return true;
    return false;
}

static void strip_cr_chars(char* text)
{
    if(!text) return;
    char* src = text;
    char* dst = text;
    while(*src)
    {
        if(*src != '\r') *dst++ = *src;
        src++;
    }
    *dst = '\0';
}

static bool lint_script_cart_text(const char* path, char* text, LintResult* result)
{
    memset(result, 0, sizeof(*result));
    result->ok = true;
    if(!text || !text[0])
    {
        set_lint_error(result, 0, "file is empty");
        return false;
    }

    const char* comment = script_cart_comment_for_path(path);
    if(!comment)
    {
        set_lint_error(result, 0, "unsupported script-cart extension");
        return false;
    }

    strip_cr_chars(text);

    int first_tag_line = 0;
    size_t code_length = script_cart_code_length(text, comment, &first_tag_line);
    if(code_length > ScriptCartCodeCapacity)
    {
        set_lint_error(result, first_tag_line > 0 ? first_tag_line : 1, "code section is too large");
        return false;
    }

    bool in_section = false;
    bool saw_row = false;
    bool seen_rows[256] = {0};
    char seen_sections[128][32] = {{0}};
    int seen_section_count = 0;
    char current_tag[32] = {0};
    const ScriptCartSectionRule* current_rule = NULL;

    int line_no = 1;
    for(char* line = text; line; line_no++)
    {
        char* next = strchr(line, '\n');
        if(next) *next = '\0';

        bool is_end_tag = false;
        char tag[32];

        if(parse_tag_line(line, comment, &is_end_tag, tag, sizeof(tag)))
        {
            if(is_end_tag)
            {
                if(!in_section)
                {
                    set_lint_error(result, line_no, "unexpected closing tag </%s>", tag);
                    return false;
                }
                if(strcmp(tag, current_tag) != 0)
                {
                    set_lint_error(result, line_no, "closing tag </%s> does not match <%s>", tag, current_tag);
                    return false;
                }
                if(!saw_row)
                {
                    set_lint_error(result, line_no, "section <%s> has no rows", current_tag);
                    return false;
                }
                in_section = false;
                saw_row = false;
                current_tag[0] = '\0';
                current_rule = NULL;
                memset(seen_rows, 0, sizeof(seen_rows));
            }
            else
            {
                int bank = 0;
                if(in_section)
                {
                    set_lint_error(result, line_no, "nested section <%s>", tag);
                    return false;
                }
                current_rule = script_cart_section_rule(tag, &bank);
                if(!current_rule)
                {
                    set_lint_error(result, line_no, "unknown section <%s>", tag);
                    return false;
                }
                if(seen_section_tag(seen_sections, seen_section_count, tag))
                {
                    set_lint_error(result, line_no, "duplicate section <%s>", tag);
                    return false;
                }
                snprintf(seen_sections[seen_section_count++], sizeof(seen_sections[0]), "%s", tag);
                snprintf(current_tag, sizeof(current_tag), "%s", tag);
                in_section = true;
                saw_row = false;
                memset(seen_rows, 0, sizeof(seen_rows));
                (void)bank;
            }
        }
        else if(in_section)
        {
            int row_index = -1;
            const char* payload = NULL;
            size_t payload_len = 0;
            if(!parse_section_row_line(line, comment, &row_index, &payload, &payload_len))
            {
                set_lint_error(result, line_no, "malformed row in <%s>", current_tag);
                return false;
            }
            if(row_index < 0 || row_index >= current_rule->row_count)
            {
                set_lint_error(result, line_no, "row out of range in <%s>", current_tag);
                return false;
            }
            if(seen_rows[row_index])
            {
                set_lint_error(result, line_no, "duplicate row %03d in <%s>", row_index, current_tag);
                return false;
            }
            if((int)payload_len != current_rule->payload_hex_chars)
            {
                set_lint_error(result, line_no, "wrong payload length in <%s>", current_tag);
                return false;
            }
            if(!span_is_hex(payload, payload_len))
            {
                set_lint_error(result, line_no, "non-hex payload in <%s>", current_tag);
                return false;
            }
            seen_rows[row_index] = true;
            saw_row = true;
        }

        if(!next) break;
        line = next + 1;
    }

    if(in_section)
    {
        set_lint_error(result, line_no - 1, "section <%s> missing closing tag", current_tag);
        return false;
    }

    return true;
}

static bool lint_playtest_script_text(const char* path, char* text, LintResult* result)
{
    memset(result, 0, sizeof(*result));
    result->ok = true;

    if(!path || !path[0])
    {
        set_lint_error(result, 0, "lint-playtest-script requires a file path");
        return false;
    }

    const char* dot = strrchr(path, '.');
    if(!dot || strcmp(dot, ".lua") != 0)
    {
        set_lint_error(result, 0, "playtest scripts must be .lua");
        return false;
    }

    if(!text || !text[0])
    {
        set_lint_error(result, 0, "file is empty");
        return false;
    }

    strip_cr_chars(text);

    bool explicit_marker = false;
    bool saw_playtest_comment = false;
    bool saw_cart_header = false;
    bool saw_cart_section = false;
    bool saw_cart_callback = false;
    bool saw_frameadvance = false;
    bool saw_set_input = false;
    bool saw_log = false;
    bool saw_end_episode = false;
    int nonempty_lines = 0;
    int cart_header_line = 0;
    int cart_section_line = 0;
    int cart_callback_line = 0;
    int set_input_line = 0;
    char cart_section_tag[32] = {0};
    char cart_callback_name[16] = {0};

    int line_no = 1;
    for(char* line = text; line; line_no++)
    {
        char* next = strchr(line, '\n');
        if(next) *next = '\0';

        const char* trimmed = skip_ws(line);
        if(*trimmed) nonempty_lines++;

        if(*trimmed)
        {
            if(nonempty_lines <= 8 && strncmp(trimmed, "--", 2) == 0 && strcasecmp(skip_ws(trimmed + 2), PlaytestScriptMarker) == 0)
                explicit_marker = true;

            if(!saw_cart_header && strncmp(trimmed, "--", 2) == 0 && strncasecmp(skip_ws(trimmed + 2), "script:", 7) == 0)
            {
                saw_cart_header = true;
                cart_header_line = line_no;
            }

            if(!saw_playtest_comment && strncmp(trimmed, "--", 2) == 0 && strncasecmp(skip_ws(trimmed + 2), "playtest script", 15) == 0)
                saw_playtest_comment = true;

            if(!saw_cart_section)
            {
                bool is_end_tag = false;
                if(parse_tag_line(trimmed, "--", &is_end_tag, cart_section_tag, sizeof(cart_section_tag)))
                {
                    saw_cart_section = true;
                    cart_section_line = line_no;
                }
            }

            if(!saw_cart_callback && line_has_cart_callback(trimmed, cart_callback_name, sizeof(cart_callback_name)))
            {
                saw_cart_callback = true;
                cart_callback_line = line_no;
            }

            if(strncmp(trimmed, "--", 2) != 0)
            {
                if(!saw_frameadvance && line_has_call(trimmed, "frameadvance"))
                    saw_frameadvance = true;
                if(!saw_set_input && line_has_call(trimmed, "set_input"))
                {
                    saw_set_input = true;
                    set_input_line = line_no;
                }
                if(!saw_log && line_has_call(trimmed, "log"))
                    saw_log = true;
                if(!saw_end_episode && line_has_call(trimmed, "end_episode"))
                    saw_end_episode = true;
            }
        }

        if(!next) break;
        line = next + 1;
    }

    if(saw_cart_header)
    {
        set_lint_error(result, cart_header_line, "looks like a TIC-80 script cart");
        return false;
    }
    if(saw_cart_section)
    {
        set_lint_error(result, cart_section_line, "looks like a TIC-80 script cart section <%s>", cart_section_tag);
        return false;
    }
    if(saw_cart_callback)
    {
        set_lint_error(result, cart_callback_line, "looks like a TIC-80 cart callback %s()", cart_callback_name);
        return false;
    }
    if(saw_set_input && !saw_frameadvance)
    {
        set_lint_error(result, set_input_line, "uses set_input() but never frameadvance()");
        return false;
    }
    if(!explicit_marker && !saw_frameadvance && !saw_set_input && !saw_log && !saw_end_episode && !saw_playtest_comment)
    {
        set_lint_error(result, 1, "does not look like a TIC-80 playtest script");
        return false;
    }

    snprintf(result->message, sizeof(result->message), "lint ok");
    return true;
}

static int print_lint_json(const char* path, const LintResult* result, const char* kind)
{
    StringBuilder sb;
    sb_init(&sb);
    sb_append(&sb, "{");
    sb_append(&sb, "\"ok\":");
    sb_append(&sb, result->ok ? "true" : "false");
    sb_append(&sb, ",\"file\":");
    sb_append_json_string(&sb, path ? path : "");
    sb_append(&sb, ",\"message\":");
    sb_append_json_string(&sb, result->message);
    if(result->line > 0) sb_appendf(&sb, ",\"line\":%d", result->line);
    sb_append(&sb, ",\"kind\":");
    sb_append_json_string(&sb, kind);
    sb_append(&sb, "}\n");
    out_printf("%s", sb.data ? sb.data : "{}\n");
    sb_free(&sb);
    return result->ok ? 0 : 1;
}

static int lint_cart_command(const char* path, bool json_output)
{
    if(!path || !path[0])
    {
        if(json_output)
        {
            LintResult result = {.ok = false, .line = 0};
            snprintf(result.message, sizeof(result.message), "tic80ctl: lint-cart requires a file path");
            return print_lint_json("", &result, "script_cart");
        }
        err_printf("tic80ctl: lint-cart requires a file path\n");
        return 1;
    }

    char error[512] = {0};
    char* text = NULL;
    if(!browser_read_text_file(path, &text, error, sizeof(error)))
    {
        LintResult result = {.ok = false, .line = 0};
        snprintf(result.message, sizeof(result.message), "%s", error[0] ? error : "failed to read file");
        if(json_output)
            return print_lint_json(path, &result, "script_cart");
        err_printf("lint failed: %s: %s\n", path, result.message);
        return 1;
    }

    LintResult result;
    bool ok = lint_script_cart_text(path, text, &result);
    free(text);

    if(ok) snprintf(result.message, sizeof(result.message), "lint ok");
    if(json_output) return print_lint_json(path, &result, "script_cart");
    if(ok)
    {
        out_printf("lint ok: %s\n", path);
        return 0;
    }
    err_printf("lint failed: %s%s%s\n", path, result.line > 0 ? ":" : "", result.line > 0 ? "" : "");
    err_printf("%s\n", result.message);
    return 1;
}

static int lint_playtest_script_command(const char* path, bool json_output)
{
    if(!path || !path[0])
    {
        if(json_output)
        {
            LintResult result = {.ok = false, .line = 0};
            snprintf(result.message, sizeof(result.message), "tic80ctl: lint-playtest-script requires a file path");
            return print_lint_json("", &result, "playtest_script");
        }
        err_printf("tic80ctl: lint-playtest-script requires a file path\n");
        return 1;
    }

    char error[512] = {0};
    char* text = NULL;
    if(!browser_read_text_file(path, &text, error, sizeof(error)))
    {
        LintResult result = {.ok = false, .line = 0};
        snprintf(result.message, sizeof(result.message), "%s", error[0] ? error : "failed to read file");
        if(json_output)
            return print_lint_json(path, &result, "playtest_script");
        err_printf("lint failed: %s: %s\n", path, result.message);
        return 1;
    }

    LintResult result;
    bool ok = lint_playtest_script_text(path, text, &result);
    free(text);

    if(json_output) return print_lint_json(path, &result, "playtest_script");
    if(ok)
    {
        out_printf("%s: %s\n", result.message[0] ? result.message : "lint ok", path);
        return 0;
    }
    err_printf("lint failed: %s: %s\n", path, result.message);
    return 1;
}

static bool extract_first_text(const char* raw, char* out, size_t out_size)
{
    const char* p = strstr(raw, "\"text\"");
    if(!p) return false;
    p = strchr(p, ':');
    if(!p) return false;
    p = skip_ws(p + 1);
    char* text = parse_json_string_value(p, NULL);
    if(!text) return false;
    snprintf(out, out_size, "%s", text);
    free(text);
    return true;
}

static bool extract_structured_content(const char* raw, char* out, size_t out_size)
{
    return json_get_raw_value(raw, "structuredContent", out, out_size);
}

static void trim_text_in_place(char* text)
{
    char* start = text;
    while(*start && isspace((unsigned char)*start)) start++;
    if(start != text) memmove(text, start, strlen(start) + 1);
    size_t len = strlen(text);
    while(len > 0 && isspace((unsigned char)text[len - 1])) text[--len] = '\0';
}

static void parse_command_text(const char* text, char* verb, size_t verb_size, const char** rest_out)
{
    const char* start = text ? skip_ws(text) : "";
    size_t len = 0;
    while(start[len] && !isspace((unsigned char)start[len])) len++;
    size_t copy = len < verb_size - 1 ? len : verb_size - 1;
    memcpy(verb, start, copy);
    verb[copy] = '\0';
    if(rest_out) *rest_out = skip_ws(start + len);
}

static bool text_line_value(const char* text, const char* key, char* out, size_t out_size)
{
    size_t key_len = strlen(key);
    const char* p = text;
    while(p && *p)
    {
        const char* line_end = strchr(p, '\n');
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if(line_len > key_len && strncmp(p, key, key_len) == 0 && p[key_len] == '=')
        {
            size_t value_len = line_len - key_len - 1;
            if(value_len >= out_size) value_len = out_size - 1;
            memcpy(out, p + key_len + 1, value_len);
            out[value_len] = '\0';
            return true;
        }
        p = line_end ? line_end + 1 : NULL;
    }
    if(out_size) out[0] = '\0';
    return false;
}

static void append_command_diagnostics_json(StringBuilder* sb, const char* request_text, const BrowserResponse* response)
{
    char verb[64] = {0};
    char request_target[PATH_MAX] = {0};
    const char* rest = "";
    parse_command_text(request_text ? request_text : "", verb, sizeof(verb), &rest);
    if(strcmp(verb, "load") == 0)
        snprintf(request_target, sizeof(request_target), "%s", rest);

    sb_append(sb, ",\"diagnostics\":{");
    sb_append(sb, "\"verb\":");
    sb_append_json_string(sb, verb[0] ? verb : "");
    sb_append(sb, ",\"request_target\":");
    sb_append_json_string(sb, request_target);
    sb_append(sb, ",\"last_load_target\":");
    sb_append_json_string(sb, response->last_load_target);
    sb_append(sb, ",\"stderr_tail\":");
    sb_append_json_string(sb, response->stderr_tail);
    sb_append(sb, "}");
}

static void append_playtest_diagnostics_json(StringBuilder* sb, const BrowserResponse* response)
{
    char text[BROWSER_TEXT_BUFFER];
    if(!response->is_error || !extract_first_text(response->mcp_raw, text, sizeof(text)))
        return;

    char status[64] = {0};
    char message[512] = {0};
    text_line_value(text, "status", status, sizeof(status));
    text_line_value(text, "message", message, sizeof(message));

    sb_append(sb, ",\"diagnostics\":{");
    sb_append(sb, "\"status\":");
    sb_append_json_string(sb, status);
    sb_append(sb, ",\"message\":");
    sb_append_json_string(sb, message);
    sb_append(sb, "}");
}

static bool print_structured_value(const char* raw)
{
    if(raw[0] == '"')
    {
        char* text = parse_json_string_value(raw, NULL);
        if(!text) return false;
        out_printf("%s", text);
        free(text);
        return true;
    }
    out_printf("%s", raw);
    return true;
}

static bool print_structured_lines(const char* structured)
{
    static const char* Keys[] = {
        "track", "frame", "pattern", "row", "bank", "vbank", "id", "sfx", "waveform",
        "target", "x", "y", "width", "height", "tempo", "speed", "start", "size",
        "left", "right", "patterns", "rows", "values", "colors", "tiles", "sprites",
    };

    bool printed = false;
    for(size_t i = 0; i < sizeof(Keys) / sizeof(Keys[0]); i++)
    {
        char value[BROWSER_TEXT_BUFFER];
        if(!json_get_raw_value(structured, Keys[i], value, sizeof(value)))
            continue;
        if(printed) out_printf("\n");
        out_printf("%s=", Keys[i]);
        if(!print_structured_value(value)) return false;
        printed = true;
    }
    if(printed) out_printf("\n");
    return printed;
}

static bool print_run_success_human(const char* request_text, const BrowserResponse* response)
{
    char verb[64] = {0};
    const char* rest = "";
    parse_command_text(request_text ? request_text : "", verb, sizeof(verb), &rest);
    (void)rest;
    if(response->is_error || strcmp(verb, "run") != 0)
        return false;

    char text[BROWSER_TEXT_BUFFER];
    if(extract_first_text(response->mcp_raw, text, sizeof(text)))
    {
        trim_text_in_place(text);
        if(text[0] != '\0' && strcmp(text, ">") != 0)
            return false;
    }
    out_printf("run started\n");
    return true;
}

static void print_command_diagnostics_human(const char* request_text, const BrowserResponse* response)
{
    char verb[64] = {0};
    parse_command_text(request_text ? request_text : "", verb, sizeof(verb), NULL);
    if(response->is_error && verb[0])
    {
        char text[BROWSER_TEXT_BUFFER];
        if(extract_first_text(response->mcp_raw, text, sizeof(text)))
        {
            trim_text_in_place(text);
            out_printf("%s failed: %s\n", verb, text);
        }
    }
}

static void print_playtest_diagnostics_human(const BrowserResponse* response)
{
    char text[BROWSER_TEXT_BUFFER];
    if(!response->is_error || !extract_first_text(response->mcp_raw, text, sizeof(text)))
        return;
    trim_text_in_place(text);
    if(text[0] != '\0')
        out_printf("playtest failed: %s\n", text);
}

static int print_mcp_response(const char* command_name, const char* request_text, const BrowserResponse* response, bool json_output)
{
    if(json_output)
    {
        StringBuilder sb;
        sb_init(&sb);
        sb_append(&sb, "{\"command\":");
        sb_append_json_string(&sb, command_name);
        sb_append(&sb, ",\"response\":");
        sb_append(&sb, response->mcp_raw);
        if(request_text && *request_text)
            append_command_diagnostics_json(&sb, request_text, response);
        else if(strcmp(command_name, "run_playtest_episode") == 0)
            append_playtest_diagnostics_json(&sb, response);
        sb_append(&sb, "}");
        out_printf("%s\n", sb.data ? sb.data : "{}");
        sb_free(&sb);
    }
    else
    {
        char text[BROWSER_TEXT_BUFFER];
        char structured[BROWSER_TEXT_BUFFER];
        if(request_text && *request_text)
        {
            if(print_run_success_human(request_text, response))
                ;
            else if(extract_first_text(response->mcp_raw, text, sizeof(text)))
                out_printf("%s\n", text);
            else
                out_printf("%s\n", response->mcp_raw);
        }
        else if(extract_structured_content(response->mcp_raw, structured, sizeof(structured)))
        {
            if(!print_structured_lines(structured))
                out_printf("%s\n", structured);
        }
        else if(extract_first_text(response->mcp_raw, text, sizeof(text)))
            out_printf("%s\n", text);
        else
            out_printf("%s\n", response->mcp_raw);

        if(request_text && *request_text)
            print_command_diagnostics_human(request_text, response);
        else if(strcmp(command_name, "run_playtest_episode") == 0)
            print_playtest_diagnostics_human(response);
    }

    return response->is_error ? 1 : 0;
}

static int print_transport_error(const char* message)
{
    err_printf("%s\n", message);
    return 1;
}

static bool consume_json_flag(int* argc, char*** argv, bool* json_output)
{
    if(*argc > 0 && strcmp((*argv)[0], "--json") == 0)
    {
        *json_output = true;
        (*argv)++;
        (*argc)--;
        return true;
    }
    return false;
}

static int start_session(bool json_output)
{
    BrowserResponse* response = alloc_browser_response();
    if(!response)
        return print_transport_error("tic80ctl: out of memory");
    if(!browser_request_json("{\"op\":\"start\"}", response) || !response->ok)
    {
        int rc = print_transport_error(response->error[0] ? response->error : "tic80ctl: failed to start browser session");
        free(response);
        return rc;
    }

    if(json_output)
        out_printf("{\"running\":true,\"initialized\":%s,\"owned\":%s,\"targetKind\":\"%s\",\"origin\":%s}\n",
                   response->initialized ? "true" : "false",
                   response->owned ? "true" : "false",
                   response->target_kind[0] ? response->target_kind : "unknown",
                   "\"*\"");
    else
        out_printf("started tic80ctl browser session (%s)\n", response->target_kind[0] ? response->target_kind : "target");
    free(response);
    return 0;
}

static int status_session(bool json_output)
{
    BrowserResponse* response = alloc_browser_response();
    if(!response)
        return print_transport_error("tic80ctl: out of memory");
    if(!browser_request_json("{\"op\":\"status\"}", response) || !response->ok)
    {
        int rc = print_transport_error(response->error[0] ? response->error : "tic80ctl: failed to query browser session");
        free(response);
        return rc;
    }

    if(json_output)
        out_printf("{\"running\":%s,\"initialized\":%s,\"owned\":%s,\"closed\":%s,\"targetKind\":",
                   response->running ? "true" : "false",
                   response->initialized ? "true" : "false",
                   response->owned ? "true" : "false",
                   response->closed ? "true" : "false");
    if(json_output)
    {
        StringBuilder sb;
        sb_init(&sb);
        sb_append_json_string(&sb, response->target_kind[0] ? response->target_kind : "");
        out_printf("%s,\"origin\":", sb.data ? sb.data : "\"\"");
        sb_clear(&sb);
        sb_append_json_string(&sb, response->origin);
        out_printf("%s}\n", sb.data ? sb.data : "\"\"");
        sb_free(&sb);
    }
    else if(response->running)
        out_printf("running target=%s initialized=%s owned=%s\n", response->target_kind[0] ? response->target_kind : "unknown", response->initialized ? "true" : "false", response->owned ? "true" : "false");
    else
        out_printf("tic80ctl: no active session\n");

    int rc = response->running ? 0 : 1;
    free(response);
    return rc;
}

static int stop_session(bool json_output)
{
    BrowserResponse* response = alloc_browser_response();
    if(!response)
        return print_transport_error("tic80ctl: out of memory");
    if(!browser_request_json("{\"op\":\"stop\"}", response) || !response->ok)
    {
        int rc = print_transport_error(response->error[0] ? response->error : "tic80ctl: failed to stop browser session");
        free(response);
        return rc;
    }

    if(json_output)
        out_printf("{\"stopped\":%s,\"closed\":%s}\n", response->stopped ? "true" : "false", response->closed ? "true" : "false");
    else
        out_printf("stopped tic80ctl browser session\n");
    free(response);
    return 0;
}

static int send_tool_request(const char* request_json, const char* command_name, const char* request_text, bool json_output)
{
    BrowserResponse* response = alloc_browser_response();
    if(!response)
        return print_transport_error("tic80ctl: out of memory");
    if(!browser_request_json(request_json, response))
    {
        free(response);
        return print_transport_error("tic80ctl: browser host request failed");
    }
    if(!response->ok)
    {
        int rc = print_transport_error(response->error[0] ? response->error : "tic80ctl: no active session; run `tic80ctl start`");
        free(response);
        return rc;
    }
    int rc = print_mcp_response(command_name, request_text, response, json_output);
    free(response);
    return rc;
}

static int run_command_request(const char* value, const char* command_name, bool json_output)
{
    StringBuilder request;
    sb_init(&request);
    sb_append(&request, "{\"op\":\"tool\",\"tool\":\"run_command\",\"arguments\":{\"command\":");
    sb_append_json_string(&request, value);
    sb_append(&request, "},\"request_text\":");
    sb_append_json_string(&request, value);
    sb_append(&request, "}");
    int rc = send_tool_request(request.data, command_name, value, json_output);
    sb_free(&request);
    return rc;
}

static int screenshot_request(const char* value, bool json_output)
{
    StringBuilder request;
    sb_init(&request);
    sb_append(&request, "{\"op\":\"tool\",\"tool\":\"capture_screenshot\",\"arguments\":");
    if(value)
    {
        sb_append(&request, "{\"path\":");
        sb_append_json_string(&request, value);
        sb_append(&request, "}");
    }
    else
        sb_append(&request, "{}");
    sb_append(&request, "}");
    int rc = send_tool_request(request.data, "capture_screenshot", NULL, json_output);
    sb_free(&request);
    return rc;
}

static int playtest_request(const char* script, bool has_timeout, long timeout_seconds, bool input_overlay, bool json_output)
{
    StringBuilder request;
    sb_init(&request);
    sb_append(&request, "{\"op\":\"tool\",\"tool\":\"run_playtest_episode\",\"arguments\":{\"script\":");
    sb_append_json_string(&request, script);
    if(has_timeout) sb_appendf(&request, ",\"timeout_seconds\":%ld", timeout_seconds);
    sb_appendf(&request, ",\"input_overlay\":%s}}", input_overlay ? "true" : "false");
    int rc = send_tool_request(request.data, "run_playtest_episode", NULL, json_output);
    sb_free(&request);
    return rc;
}

static int tool_request(const char* tool_name, const char* args_json, const char* command_name, bool json_output)
{
    StringBuilder request;
    sb_init(&request);
    sb_append(&request, "{\"op\":\"tool\",\"tool\":");
    sb_append_json_string(&request, tool_name);
    sb_append(&request, ",\"arguments\":");
    sb_append(&request, args_json ? args_json : "{}");
    sb_append(&request, "}");
    int rc = send_tool_request(request.data, command_name ? command_name : tool_name, NULL, json_output);
    sb_free(&request);
    return rc;
}

static bool append_json_int_array(StringBuilder* sb, const int* values, int count)
{
    if(!sb_append(sb, "[")) return false;
    for(int i = 0; i < count; i++)
    {
        if(i > 0 && !sb_append(sb, ",")) return false;
        if(!sb_appendf(sb, "%d", values[i])) return false;
    }
    return sb_append(sb, "]");
}

static bool parse_fixed_int_csv(const char* text, int expected, int min, int max, int* values)
{
    char* copy = strdup(text);
    if(!copy) return false;
    int count = 0;
    char* save = NULL;
    for(char* tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
    {
        int value = 0;
        if(count >= expected || !parse_int_strict(tok, &value) || value < min || value > max)
        {
            free(copy);
            return false;
        }
        values[count++] = value;
    }
    free(copy);
    return count == expected;
}

static bool parse_var_int_csv(const char* text, int min, int max, int* values, int max_count, int* out_count)
{
    char* copy = strdup(text);
    if(!copy) return false;
    int count = 0;
    char* save = NULL;
    for(char* tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
    {
        int value = 0;
        if(count >= max_count || !parse_int_strict(tok, &value) || value < min || value > max)
        {
            free(copy);
            return false;
        }
        values[count++] = value;
    }
    free(copy);
    if(count <= 0) return false;
    *out_count = count;
    return true;
}

static bool parse_bool_token(const char* text, bool* out)
{
    if(!text) return false;
    if(strcmp(text, "1") == 0 || strcasecmp(text, "true") == 0 || strcasecmp(text, "yes") == 0 || strcasecmp(text, "on") == 0)
    {
        *out = true;
        return true;
    }
    if(strcmp(text, "0") == 0 || strcasecmp(text, "false") == 0 || strcasecmp(text, "no") == 0 || strcasecmp(text, "off") == 0)
    {
        *out = false;
        return true;
    }
    return false;
}

static bool append_bool_json(StringBuilder* sb, const char* key, bool value)
{
    return sb_appendf(sb, ",\"%s\":%s", key, value ? "true" : "false");
}

static bool consume_long_option(int* argc, char*** argv, const char* name, long* out)
{
    if(*argc >= 2 && strcmp((*argv)[0], name) == 0)
    {
        if(!parse_long_strict((*argv)[1], out))
            failf("tic80ctl: invalid value for %s: %s", name, (*argv)[1]);
        *argc -= 2;
        *argv += 2;
        return true;
    }
    return false;
}

static bool consume_int_option(int* argc, char*** argv, const char* name, int* out)
{
    long value = 0;
    if(!consume_long_option(argc, argv, name, &value)) return false;
    if(value < INT_MIN || value > INT_MAX) failf("tic80ctl: invalid value for %s", name);
    *out = (int)value;
    return true;
}

static bool consume_bool_option(int* argc, char*** argv, const char* name, bool* out)
{
    if(*argc >= 1 && strcmp((*argv)[0], name) == 0)
    {
        if(*argc >= 2 && (*argv)[1][0] != '-')
        {
            if(!parse_bool_token((*argv)[1], out))
                failf("tic80ctl: invalid boolean for %s: %s", name, (*argv)[1]);
            *argc -= 2;
            *argv += 2;
        }
        else
        {
            *out = true;
            *argc -= 1;
            *argv += 1;
        }
        return true;
    }
    return false;
}

static bool consume_args_json_option(int* argc, char*** argv, const char** out)
{
    if(*argc >= 2 && strcmp((*argv)[0], "--args-json") == 0)
    {
        *out = (*argv)[1];
        *argc -= 2;
        *argv += 2;
        return true;
    }
    return false;
}

static bool consume_bank_option(int* argc, char*** argv, int* bank)
{
    return consume_int_option(argc, argv, "--bank", bank);
}

static bool append_optional_int(StringBuilder* sb, const char* key, int value)
{
    if(value < 0) return true;
    return sb_appendf(sb, ",\"%s\":%d", key, value);
}

static void init_object_args(StringBuilder* sb)
{
    sb_init(sb);
    sb_append(sb, "{");
}

static int build_and_send_tool(const char* tool_name, const char* command_name, bool json_output, StringBuilder* args)
{
    if(!sb_append(args, "}"))
    {
        sb_free(args);
        return 1;
    }
    int rc = tool_request(tool_name, args->data, command_name, json_output);
    sb_free(args);
    return rc;
}

static bool append_sprite_rows_json(StringBuilder* sb, const char* payload)
{
    char* copy = strdup(payload);
    if(!copy) return false;
    bool ok = sb_append(sb, "[");
    int count = 0;
    char* save = NULL;
    for(char* tok = strtok_r(copy, ",", &save); tok && ok; tok = strtok_r(NULL, ",", &save))
    {
        if(count >= 8 || strlen(tok) != 8) { ok = false; break; }
        for(int i = 0; i < 8; i++) if(hex_value(tok[i]) < 0) ok = false;
        if(!ok) break;
        if(count > 0) ok = sb_append(sb, ",");
        if(ok) ok = sb_append_json_string(sb, tok);
        count++;
    }
    if(ok) ok = (count == 8) && sb_append(sb, "]");
    free(copy);
    return ok;
}

static bool append_palette_json(StringBuilder* sb, const char* payload)
{
    char* copy = strdup(payload);
    if(!copy) return false;
    bool ok = sb_append(sb, "[");
    int count = 0;
    char* save = NULL;
    for(char* tok = strtok_r(copy, ",", &save); tok && ok; tok = strtok_r(NULL, ",", &save))
    {
        if(count >= 16 || strlen(tok) != 6) { ok = false; break; }
        for(int i = 0; i < 6; i++) if(hex_value(tok[i]) < 0) ok = false;
        if(!ok) break;
        if(count > 0) ok = sb_append(sb, ",");
        if(ok) ok = sb_append_json_string(sb, tok);
        count++;
    }
    if(ok) ok = (count == 16) && sb_append(sb, "]");
    free(copy);
    return ok;
}

static bool append_region_sprites_json(StringBuilder* sb, const char* payload, int expected_tiles)
{
    char* copy = strdup(payload);
    if(!copy) return false;
    bool ok = sb_append(sb, "[");
    int count = 0;
    char* save = NULL;
    for(char* tile = strtok_r(copy, ";", &save); tile && ok; tile = strtok_r(NULL, ";", &save))
    {
        if(count >= expected_tiles) { ok = false; break; }
        if(count > 0) ok = sb_append(sb, ",");
        if(ok) ok = sb_append(sb, "{\"rows\":");
        if(ok) ok = append_sprite_rows_json(sb, tile);
        if(ok) ok = sb_append(sb, "}");
        count++;
    }
    if(ok) ok = (count == expected_tiles) && sb_append(sb, "]");
    free(copy);
    return ok;
}

static bool append_frame_patterns_json(StringBuilder* sb, const char* payload)
{
    char* copy = strdup(payload);
    if(!copy) return false;
    bool ok = sb_append(sb, "[");
    int count = 0;
    char* save = NULL;
    for(char* tok = strtok_r(copy, ",", &save); tok && ok; tok = strtok_r(NULL, ",", &save))
    {
        int value = -1;
        if(count >= 4 || (strcmp(tok, "-") != 0 && (!parse_int_strict(tok, &value) || value < -1 || value > 63)))
        {
            ok = false;
            break;
        }
        if(count > 0) ok = sb_append(sb, ",");
        if(ok) ok = sb_appendf(sb, "%d", value);
        count++;
    }
    if(ok) ok = (count == 4) && sb_append(sb, "]");
    free(copy);
    return ok;
}

static bool append_music_row_json(StringBuilder* sb, int row, const char* note, const char* sfx, const char* command)
{
    if(row < 0 || row >= 64) return false;
    if(!sb_appendf(sb, "{\"row\":%d", row)) return false;

    if(strcmp(note, "-") != 0)
    {
        char note_buf[8];
        if(strcmp(note, "OFF") == 0)
        {
            if(!sb_append(sb, ",\"note\":\"OFF\",\"octave\":0")) return false;
        }
        else
        {
            size_t len = strlen(note);
            if(len < 3 || len > 4) return false;
            char octave_char = note[len - 1];
            if(octave_char < '0' || octave_char > '7') return false;
            memcpy(note_buf, note, len - 1);
            note_buf[len - 1] = '\0';
            if(!sb_append(sb, ",\"note\":") || !sb_append_json_string(sb, note_buf)) return false;
            if(!sb_appendf(sb, ",\"octave\":%d", octave_char - '0')) return false;
        }
    }

    if(strcmp(sfx, "-") != 0)
    {
        int sfx_id = 0;
        if(!parse_int_strict(sfx, &sfx_id) || sfx_id < 0 || sfx_id > 63) return false;
        if(!sb_appendf(sb, ",\"sfx\":%d", sfx_id)) return false;
    }

    if(strcmp(command, "-") != 0)
    {
        size_t len = strlen(command);
        if(len != 3) return false;
        int p1 = hex_value(command[1]);
        int p2 = hex_value(command[2]);
        if(!isalpha((unsigned char)command[0]) || p1 < 0 || p2 < 0) return false;
        char cmd[2] = {(char)toupper((unsigned char)command[0]), '\0'};
        if(!sb_append(sb, ",\"command\":") || !sb_append_json_string(sb, cmd)) return false;
        if(!sb_appendf(sb, ",\"param1\":%d,\"param2\":%d", p1, p2)) return false;
    }

    return sb_append(sb, "}");
}

static bool append_music_rows_json(StringBuilder* sb, const char* payload, bool single_row_mode, int fixed_row)
{
    char* copy = strdup(payload);
    if(!copy) return false;
    bool ok = true;
    if(single_row_mode)
    {
        char* save = NULL;
        char* note = strtok_r(copy, ":", &save);
        char* sfx = strtok_r(NULL, ":", &save);
        char* command = strtok_r(NULL, ":", &save);
        ok = note && sfx && command && !strtok_r(NULL, ":", &save);
        if(ok) ok = append_music_row_json(sb, fixed_row, note, sfx, command);
        free(copy);
        return ok;
    }

    ok = sb_append(sb, "{\"rows\":[");
    int count = 0;
    char* save_rows = NULL;
    for(char* row_spec = strtok_r(copy, ",", &save_rows); row_spec && ok; row_spec = strtok_r(NULL, ",", &save_rows))
    {
        char* save = NULL;
        char* row_text = strtok_r(row_spec, ":", &save);
        char* note = strtok_r(NULL, ":", &save);
        char* sfx = strtok_r(NULL, ":", &save);
        char* command = strtok_r(NULL, ":", &save);
        int row = 0;
        if(!row_text || !note || !sfx || !command || strtok_r(NULL, ":", &save) || !parse_int_strict(row_text, &row))
        {
            ok = false;
            break;
        }
        if(count > 0) ok = sb_append(sb, ",");
        if(ok) ok = append_music_row_json(sb, row, note, sfx, command);
        count++;
    }
    if(ok) ok = count > 0 && sb_append(sb, "]}");
    free(copy);
    return ok;
}

static bool append_object_body(StringBuilder* sb, const char* object_json)
{
    size_t len = strlen(object_json);
    if(len < 2 || object_json[0] != '{' || object_json[len - 1] != '}') return false;
    return sb_append_n(sb, object_json + 1, len - 2);
}

static int handle_sfx_command(int argc, char** argv, bool json_output)
{
    if(argc <= 0) failf("tic80ctl: sfx requires a subcommand");
    const char* mode = argv[0];
    argc--;
    argv++;

    if(strcmp(mode, "wavetable") == 0)
    {
        int bank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("sfx_set_wavetable", args_json, "sfx_wavetable", json_output);
        if(argc < 1 || argc > 2) failf("tic80ctl: sfx wavetable <sfx> [csv]");
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx)) failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        if(argc == 1) return build_and_send_tool("sfx_get_wavetable", "sfx_wavetable", json_output, &args);
        int values[32];
        if(!parse_fixed_int_csv(argv[1], 32, 0, 15, values)) failf("tic80ctl: invalid sfx wavetable payload");
        sb_append(&args, ",\"values\":");
        append_json_int_array(&args, values, 32);
        return build_and_send_tool("sfx_set_wavetable", "sfx_wavetable", json_output, &args);
    }

    if(strcmp(mode, "volume") == 0 || strcmp(mode, "wave") == 0 || strcmp(mode, "pitch") == 0)
    {
        int bank = -1;
        bool reverse = false;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_bool_option(&argc, &argv, "--reverse", &reverse);
        consume_args_json_option(&argc, &argv, &args_json);
        const char* get_tool = strcmp(mode, "volume") == 0 ? "sfx_get_volume" : strcmp(mode, "wave") == 0 ? "sfx_get_wave" : "sfx_get_pitch";
        const char* set_tool = strcmp(mode, "volume") == 0 ? "sfx_set_volume" : strcmp(mode, "wave") == 0 ? "sfx_set_wave" : "sfx_set_pitch";
        if(args_json) return tool_request(set_tool, args_json, mode, json_output);
        if(argc < 1 || argc > 2) failf("tic80ctl: sfx %s <sfx> [payload]", mode);
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx)) failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        if(argc == 1) return build_and_send_tool(get_tool, mode, json_output, &args);
        int parsed[30];
        int count = 0;
        if(!parse_var_int_csv(argv[1], 0, strcmp(mode, "pitch") == 0 ? 15 : 15, parsed, 30, &count))
            failf("tic80ctl: invalid sfx %s payload", mode);
        for(int i = count; i < 30; i++) parsed[i] = parsed[count - 1];
        sb_append(&args, ",\"values\":");
        append_json_int_array(&args, parsed, 30);
        append_bool_json(&args, "reverse", reverse);
        return build_and_send_tool(set_tool, mode, json_output, &args);
    }

    if(strcmp(mode, "arpeggio") == 0)
    {
        int bank = -1;
        bool reverse = false;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_bool_option(&argc, &argv, "--reverse", &reverse);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("sfx_set_arpeggio", args_json, "sfx_arpeggio", json_output);
        if(argc < 1 || argc > 2) failf("tic80ctl: sfx arpeggio <sfx> [csv]");
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx)) failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        if(argc == 1) return build_and_send_tool("sfx_get_arpeggio", "sfx_arpeggio", json_output, &args);
        int parsed[30];
        int count = 0;
        if(!parse_var_int_csv(argv[1], 0, 15, parsed, 30, &count)) failf("tic80ctl: invalid sfx arpeggio payload");
        for(int i = count; i < 30; i++) parsed[i] = parsed[count - 1];
        sb_append(&args, ",\"values\":");
        append_json_int_array(&args, parsed, 30);
        append_bool_json(&args, "reverse", reverse);
        return build_and_send_tool("sfx_set_arpeggio", "sfx_arpeggio", json_output, &args);
    }

    if(strcmp(mode, "panning") == 0)
    {
        int bank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("sfx_set_panning", args_json, "sfx_panning", json_output);
        if(argc < 1 || argc > 2) failf("tic80ctl: sfx panning <sfx> [left,right]");
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx)) failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        if(argc == 1) return build_and_send_tool("sfx_get_panning", "sfx_panning", json_output, &args);
        char* copy = strdup(argv[1]);
        char* save = NULL;
        char* left_text = strtok_r(copy, ",", &save);
        char* right_text = strtok_r(NULL, ",", &save);
        bool left = false, right = false;
        bool ok = left_text && right_text && !strtok_r(NULL, ",", &save) && parse_bool_token(left_text, &left) && parse_bool_token(right_text, &right);
        free(copy);
        if(!ok) failf("tic80ctl: invalid sfx panning payload");
        append_bool_json(&args, "left", left);
        append_bool_json(&args, "right", right);
        return build_and_send_tool("sfx_set_panning", "sfx_panning", json_output, &args);
    }

    if(strcmp(mode, "speed") == 0)
    {
        int bank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("sfx_set_speed", args_json, "sfx_speed", json_output);
        if(argc < 1 || argc > 2) failf("tic80ctl: sfx speed <sfx> [value]");
        int sfx = 0;
        int speed = 0;
        if(!parse_int_strict(argv[0], &sfx)) failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        if(argc == 1) return build_and_send_tool("sfx_get_speed", "sfx_speed", json_output, &args);
        if(!parse_int_strict(argv[1], &speed)) failf("tic80ctl: invalid sfx speed: %s", argv[1]);
        sb_appendf(&args, ",\"speed\":%d", speed);
        return build_and_send_tool("sfx_set_speed", "sfx_speed", json_output, &args);
    }

    if(strcmp(mode, "loop") == 0)
    {
        int bank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("sfx_set_loop_points", args_json, "sfx_loop", json_output);
        if(argc < 2 || argc > 3) failf("tic80ctl: sfx loop <sfx> <target> [start:size]");
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx)) failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        sb_append(&args, ",\"target\":");
        sb_append_json_string(&args, argv[1]);
        if(argc == 2) return build_and_send_tool("sfx_get_loop_points", "sfx_loop", json_output, &args);
        char* copy = strdup(argv[2]);
        char* colon = strchr(copy, ':');
        int start = 0, size = 0;
        bool ok = false;
        if(colon)
        {
            *colon = '\0';
            ok = parse_int_strict(copy, &start) && parse_int_strict(colon + 1, &size);
        }
        free(copy);
        if(!ok) failf("tic80ctl: invalid sfx loop payload");
        sb_appendf(&args, ",\"start\":%d,\"size\":%d", start, size);
        return build_and_send_tool("sfx_set_loop_points", "sfx_loop", json_output, &args);
    }

    failf("tic80ctl: unknown sfx subcommand: %s", mode);
    return 1;
}

static int handle_music_command(int argc, char** argv, bool json_output)
{
    if(argc <= 0) failf("tic80ctl: music requires a subcommand");
    const char* mode = argv[0];
    argc--;
    argv++;

    if(strcmp(mode, "track") == 0)
    {
        const char* args_json = NULL;
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("music_set_track", args_json, "music_track", json_output);
        if(argc < 1 || argc > 2) failf("tic80ctl: music track <track> [tempo,speed,rows]");
        int track = 0;
        if(!parse_int_strict(argv[0], &track)) failf("tic80ctl: invalid track id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"track\":%d", track);
        if(argc == 1) return build_and_send_tool("music_get_track", "music_track", json_output, &args);
        int values[3];
        if(!parse_fixed_int_csv(argv[1], 3, 0, 999, values)) failf("tic80ctl: invalid music track payload");
        sb_appendf(&args, ",\"tempo\":%d,\"speed\":%d,\"rows\":%d", values[0], values[1], values[2]);
        return build_and_send_tool("music_set_track", "music_track", json_output, &args);
    }

    if(strcmp(mode, "frame") == 0)
    {
        const char* args_json = NULL;
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("music_set_frame", args_json, "music_frame", json_output);
        if(argc < 2 || argc > 3) failf("tic80ctl: music frame <track> <frame> [p0,p1,p2,p3]");
        int track = 0, frame = 0;
        if(!parse_int_strict(argv[0], &track) || !parse_int_strict(argv[1], &frame)) failf("tic80ctl: invalid music frame selector");
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"track\":%d,\"frame\":%d", track, frame);
        if(argc == 2) return build_and_send_tool("music_get_frame", "music_frame", json_output, &args);
        sb_append(&args, ",\"patterns\":");
        if(!append_frame_patterns_json(&args, argv[2])) failf("tic80ctl: invalid music frame payload");
        return build_and_send_tool("music_set_frame", "music_frame", json_output, &args);
    }

    if(strcmp(mode, "row") == 0)
    {
        const char* args_json = NULL;
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("music_set_pattern_row", args_json, "music_row", json_output);
        if(argc < 2 || argc > 3) failf("tic80ctl: music row <pattern> <row> [note:sfx:cmd]");
        int pattern = 0, row = 0;
        if(!parse_int_strict(argv[0], &pattern) || !parse_int_strict(argv[1], &row)) failf("tic80ctl: invalid music row selector");
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"pattern\":%d,\"row\":%d", pattern, row);
        if(argc == 2) return build_and_send_tool("music_get_pattern_row", "music_row", json_output, &args);
        StringBuilder row_args;
        sb_init(&row_args);
        if(!append_music_rows_json(&row_args, argv[2], true, row) || !sb_append(&args, ",") || !append_object_body(&args, row_args.data))
            failf("tic80ctl: invalid music row payload");
        sb_free(&row_args);
        return build_and_send_tool("music_set_pattern_row", "music_row", json_output, &args);
    }

    if(strcmp(mode, "rows") == 0)
    {
        const char* args_json = NULL;
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("music_set_pattern_rows", args_json, "music_rows", json_output);
        if(argc != 2) failf("tic80ctl: music rows <pattern> <rows>");
        int pattern = 0;
        if(!parse_int_strict(argv[0], &pattern)) failf("tic80ctl: invalid music pattern id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"pattern\":%d", pattern);
        if(strchr(argv[1], ':'))
        {
            StringBuilder rows_args;
            sb_init(&rows_args);
            if(!append_music_rows_json(&rows_args, argv[1], false, 0) || !sb_append(&args, ",") || !append_object_body(&args, rows_args.data))
                failf("tic80ctl: invalid music rows payload");
            sb_free(&rows_args);
            return build_and_send_tool("music_set_pattern_rows", "music_rows", json_output, &args);
        }
        int rows[64];
        int row_count = 0;
        if(!parse_var_int_csv(argv[1], 0, 63, rows, 64, &row_count)) failf("tic80ctl: invalid music rows selector list");
        sb_append(&args, ",\"rows\":");
        append_json_int_array(&args, rows, row_count);
        return build_and_send_tool("music_get_pattern_rows", "music_rows", json_output, &args);
    }

    failf("tic80ctl: unknown music subcommand: %s", mode);
    return 1;
}

static int handle_sprite_command(int argc, char** argv, bool json_output)
{
    if(argc <= 0) failf("tic80ctl: sprite requires a subcommand");
    const char* mode = argv[0];
    argc--;
    argv++;

    if(strcmp(mode, "tile") == 0)
    {
        const char* args_json = NULL;
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("sprite_set_sprite", args_json, "sprite_tile", json_output);
        if(argc < 1 || argc > 2) failf("tic80ctl: sprite tile <id> [rows]");
        int id = 0;
        if(!parse_int_strict(argv[0], &id)) failf("tic80ctl: invalid sprite id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"id\":%d", id);
        if(argc == 1) return build_and_send_tool("sprite_get_sprite", "sprite_tile", json_output, &args);
        sb_append(&args, ",\"rows\":");
        if(!append_sprite_rows_json(&args, argv[1])) failf("tic80ctl: invalid sprite tile payload");
        return build_and_send_tool("sprite_set_sprite", "sprite_tile", json_output, &args);
    }

    if(strcmp(mode, "region") == 0)
    {
        int bank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("sprite_set_spritesheet_region", args_json, "sprite_region", json_output);
        if(argc < 4 || argc > 5) failf("tic80ctl: sprite region <x> <y> <width> <height> [payload]");
        int x = 0, y = 0, width = 0, height = 0;
        if(!parse_int_strict(argv[0], &x) || !parse_int_strict(argv[1], &y) || !parse_int_strict(argv[2], &width) || !parse_int_strict(argv[3], &height))
            failf("tic80ctl: invalid sprite region selector");
        StringBuilder args;
        init_object_args(&args);
        if(bank >= 0) sb_appendf(&args, "\"bank\":%d,", bank);
        sb_appendf(&args, "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d", x, y, width, height);
        if(argc == 4) return build_and_send_tool("sprite_get_spritesheet_region", "sprite_region", json_output, &args);
        sb_append(&args, ",\"sprites\":");
        if(!append_region_sprites_json(&args, argv[4], width * height)) failf("tic80ctl: invalid sprite region payload");
        return build_and_send_tool("sprite_set_spritesheet_region", "sprite_region", json_output, &args);
    }

    if(strcmp(mode, "palette") == 0)
    {
        int bank = -1;
        int vbank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_int_option(&argc, &argv, "--vbank", &vbank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json) return tool_request("sprite_set_palette", args_json, "sprite_palette", json_output);
        if(argc > 1) failf("tic80ctl: sprite palette [RRGGBB,...]");
        StringBuilder args;
        init_object_args(&args);
        bool first = true;
        if(bank >= 0) { sb_appendf(&args, "\"bank\":%d", bank); first = false; }
        if(vbank >= 0) { if(!first) sb_append(&args, ","); sb_appendf(&args, "\"vbank\":%d", vbank); first = false; }
        if(argc == 0) return build_and_send_tool("sprite_get_palette", "sprite_palette", json_output, &args);
        if(!first) sb_append(&args, ",");
        sb_append(&args, "\"colors\":");
        if(!append_palette_json(&args, argv[0])) failf("tic80ctl: invalid sprite palette payload");
        return build_and_send_tool("sprite_set_palette", "sprite_palette", json_output, &args);
    }

    failf("tic80ctl: unknown sprite subcommand: %s", mode);
    return 1;
}

static int handle_map_command(int argc, char** argv, bool json_output)
{
    if(argc <= 0) failf("tic80ctl: map requires a subcommand");
    const char* mode = argv[0];
    argc--;
    argv++;

    if(strcmp(mode, "rect") == 0 || strcmp(mode, "chunk") == 0)
    {
        int bank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
        {
            const char* tool = strcmp(mode, "rect") == 0 ? "map_set_rect" : "map_set_chunk";
            return tool_request(tool, args_json, tool, json_output);
        }
        if(argc < 4 || argc > 5) failf("tic80ctl: map %s <x> <y> <width> <height> [payload]", mode);
        int x = 0, y = 0, width = 0, height = 0;
        if(!parse_int_strict(argv[0], &x) || !parse_int_strict(argv[1], &y) || !parse_int_strict(argv[2], &width) || !parse_int_strict(argv[3], &height))
            failf("tic80ctl: invalid map selector");
        StringBuilder args;
        init_object_args(&args);
        if(bank >= 0) sb_appendf(&args, "\"bank\":%d,", bank);
        sb_appendf(&args, "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d", x, y, width, height);
        if(argc == 4) return build_and_send_tool(strcmp(mode, "rect") == 0 ? "map_get_rect" : "map_get_chunk", mode, json_output, &args);
        if(strcmp(mode, "rect") == 0)
        {
            int tile = 0;
            if(!parse_int_strict(argv[4], &tile)) failf("tic80ctl: invalid map rect tile value");
            sb_appendf(&args, ",\"tile\":%d", tile);
            return build_and_send_tool("map_set_rect", "map_rect", json_output, &args);
        }
        int tiles[4096];
        int count = 0;
        if(!parse_var_int_csv(argv[4], 0, 65535, tiles, 4096, &count) || count != width * height)
            failf("tic80ctl: map chunk payload must contain width*height tile ids");
        sb_append(&args, ",\"tiles\":");
        append_json_int_array(&args, tiles, count);
        return build_and_send_tool("map_set_chunk", "map_chunk", json_output, &args);
    }

    failf("tic80ctl: unknown map subcommand: %s", mode);
    return 1;
}

static int execute_cli(int argc, char** argv)
{
    bool json_output = false;

    while(argc > 0 && strcmp(argv[0], "--json") == 0)
    {
        json_output = true;
        argc--;
        argv++;
    }

    if(argc == 0)
    {
        print_usage();
        return 1;
    }

    if(strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-h") == 0)
        return print_help(argc > 1 ? argv[1] : NULL);

    const char* subcommand = argv[0];
    argc--;
    argv++;
    consume_json_flag(&argc, &argv, &json_output);

    if(strcmp(subcommand, "help") == 0)
        return print_help(argc > 0 ? argv[0] : NULL);

    if(strcmp(subcommand, "start") == 0)
        return start_session(json_output);
    if(strcmp(subcommand, "status") == 0)
        return status_session(json_output);
    if(strcmp(subcommand, "stop") == 0)
        return stop_session(json_output);

    if(strcmp(subcommand, "cmd") == 0)
    {
        if(argc == 0)
        {
            err_printf("tic80ctl: cmd requires a TIC-80 command string\n");
            return 1;
        }
        return run_command_request(argv[0], "run_command", json_output);
    }

    if(strcmp(subcommand, "lint-cart") == 0)
        return lint_cart_command(argc > 0 ? argv[0] : NULL, json_output);
    if(strcmp(subcommand, "lint-playtest-script") == 0)
        return lint_playtest_script_command(argc > 0 ? argv[0] : NULL, json_output);

    if(strcmp(subcommand, "load") == 0)
    {
        if(argc == 0)
        {
            err_printf("tic80ctl: load requires a cart path\n");
            return 1;
        }
        StringBuilder command;
        sb_init(&command);
        sb_append(&command, "load ");
        sb_append(&command, argv[0]);
        int rc = run_command_request(command.data, "run_command", json_output);
        sb_free(&command);
        return rc;
    }

    if(strcmp(subcommand, "run") == 0)
        return run_command_request("run", "run_command", json_output);

    if(strcmp(subcommand, "eval") == 0)
    {
        if(argc == 0)
        {
            err_printf("tic80ctl: eval requires an expression\n");
            return 1;
        }
        StringBuilder command;
        sb_init(&command);
        sb_append(&command, "eval ");
        sb_append(&command, argv[0]);
        int rc = run_command_request(command.data, "run_command", json_output);
        sb_free(&command);
        return rc;
    }

    if(strcmp(subcommand, "screenshot") == 0)
        return screenshot_request(argc > 0 ? argv[0] : NULL, json_output);

    if(strcmp(subcommand, "playtest") == 0)
    {
        const char* script_file = NULL;
        bool input_overlay = true;
        bool has_timeout = false;
        long timeout_seconds = 0;

        while(argc > 0)
        {
            if(strcmp(argv[0], "--script-file") == 0)
            {
                if(argc < 2) failf("tic80ctl: --script-file is required");
                script_file = argv[1];
                argc -= 2;
                argv += 2;
                continue;
            }
            if(strcmp(argv[0], "--timeout") == 0)
            {
                if(argc < 2) failf("tic80ctl: --timeout requires a value");
                timeout_seconds = strtol(argv[1], NULL, 10);
                has_timeout = true;
                argc -= 2;
                argv += 2;
                continue;
            }
            if(strcmp(argv[0], "--input-overlay") == 0)
            {
                input_overlay = true;
                argc--;
                argv++;
                continue;
            }
            if(strcmp(argv[0], "--no-input-overlay") == 0)
            {
                input_overlay = false;
                argc--;
                argv++;
                continue;
            }
            err_printf("tic80ctl: unknown playtest option: %s\n", argv[0]);
            return 1;
        }

        if(!script_file)
        {
            err_printf("tic80ctl: --script-file is required\n");
            return 1;
        }

        char error[512] = {0};
        char* script = NULL;
        if(!browser_read_text_file(script_file, &script, error, sizeof(error)))
        {
            err_printf("tic80ctl: script file not found: %s\n", script_file);
            return 1;
        }

        int rc = playtest_request(script, has_timeout, timeout_seconds, input_overlay, json_output);
        free(script);
        return rc;
    }

    if(strcmp(subcommand, "sfx") == 0)
        return handle_sfx_command(argc, argv, json_output);
    if(strcmp(subcommand, "music") == 0)
        return handle_music_command(argc, argv, json_output);
    if(strcmp(subcommand, "sprite") == 0)
        return handle_sprite_command(argc, argv, json_output);
    if(strcmp(subcommand, "map") == 0)
        return handle_map_command(argc, argv, json_output);

    print_usage();
    return 1;
}

static bool parse_argv_json(const char* input_json, int* argc_out, char*** argv_out)
{
    const char* value = find_json_key(input_json, "argv");
    if(!value || *value != '[') return false;
    value++;

    int count = 0;
    int cap = 8;
    char** argv = calloc((size_t)cap, sizeof(char*));
    if(!argv) return false;

    while(*value)
    {
        value = skip_ws(value);
        if(*value == ']')
        {
            *argc_out = count;
            *argv_out = argv;
            return true;
        }
        if(*value != '"') break;
        char* item = parse_json_string_value(value, &value);
        if(!item) break;
        if(count >= cap)
        {
            cap *= 2;
            char** grown = realloc(argv, (size_t)cap * sizeof(char*));
            if(!grown)
            {
                free(item);
                break;
            }
            argv = grown;
        }
        argv[count++] = item;
        value = skip_ws(value);
        if(*value == ',')
        {
            value++;
            continue;
        }
        if(*value == ']')
        {
            *argc_out = count;
            *argv_out = argv;
            return true;
        }
        break;
    }

    for(int i = 0; i < count; i++) free(argv[i]);
    free(argv);
    return false;
}

static void free_argv_json(int argc, char** argv)
{
    for(int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
}

EMSCRIPTEN_KEEPALIVE
const char* tic80ctl_browser_run_from_json(const char* input_json)
{
    if(!StdoutBuffer.cap) sb_init(&StdoutBuffer);
    if(!StderrBuffer.cap) sb_init(&StderrBuffer);
    if(!ResultBuffer.cap) sb_init(&ResultBuffer);
    sb_clear(&StdoutBuffer);
    sb_clear(&StderrBuffer);
    sb_clear(&ResultBuffer);

    int argc = 0;
    char** argv = NULL;
    int exit_code = 1;

    if(!parse_argv_json(input_json ? input_json : "", &argc, &argv))
    {
        err_printf("tic80ctl: invalid argv payload\n");
        goto done;
    }

    FailJumpActive = true;
    if(setjmp(FailJump) == 0)
        exit_code = execute_cli(argc, argv);
    else
        exit_code = 1;
    FailJumpActive = false;

done:
    if(argv) free_argv_json(argc, argv);

    sb_append(&ResultBuffer, "{\"stdout\":");
    sb_append_json_string(&ResultBuffer, StdoutBuffer.data ? StdoutBuffer.data : "");
    sb_append(&ResultBuffer, ",\"stderr\":");
    sb_append_json_string(&ResultBuffer, StderrBuffer.data ? StderrBuffer.data : "");
    sb_appendf(&ResultBuffer, ",\"exit_code\":%d}", exit_code);
    return ResultBuffer.data ? strdup(ResultBuffer.data) : strdup("{\"stdout\":\"\",\"stderr\":\"tic80ctl: internal error\",\"exit_code\":1}");
}
