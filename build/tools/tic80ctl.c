#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define POLL_INTERVAL_MS 100

typedef struct
{
    char* data;
    size_t len;
    size_t cap;
} StringBuilder;

typedef struct
{
    char state_dir[PATH_MAX];
    char pid_path[PATH_MAX];
    char port_path[PATH_MAX];
    char token_path[PATH_MAX];
    char cwd_path[PATH_MAX];
    char mode_path[PATH_MAX];
    char bin_path[PATH_MAX];
    char stdout_log_path[PATH_MAX];
    char stderr_log_path[PATH_MAX];
    char started_at_path[PATH_MAX];
    char ready_path[PATH_MAX];
    char startup_error_path[PATH_MAX];
} StatePaths;

typedef struct
{
    StatePaths paths;
    char cwd[PATH_MAX];
    char tic80_bin[PATH_MAX];
    char launch_command[4096];
    char token[65];
    char last_command[64];
    char last_load_target[PATH_MAX];
    char last_stderr_tail[1024];
    int listen_fd;
    pid_t child_pid;
    int child_stdin_fd;
    FILE* child_stdout;
    FILE* stdout_log;
    FILE* stderr_log;
    int next_id;
    bool child_running;
    int child_status;
} Server;

typedef struct
{
    bool ok;
    bool running;
    long pid;
    char verb[64];
    char mode[64];
    char cwd[PATH_MAX];
    char bin[PATH_MAX];
    char stdout_path[PATH_MAX];
    char stderr_path[PATH_MAX];
    char error[1024];
    char last_load_target[PATH_MAX];
    char stderr_tail[1024];
    char mcp_raw[262144];
    bool has_mcp_raw;
    bool is_error;
    bool stopped;
} ServerResponse;

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
    "Operate a live TIC-80 session from the shell.\n"
    "\n"
    "usage:\n"
    "  tic80ctl [--json] <command> [args...]\n"
    "  tic80ctl --help\n"
    "  tic80ctl help [command]\n"
    "\n"
    "quick start:\n"
    "  tic80ctl start\n"
    "  tic80ctl load game.lua\n"
    "  tic80ctl run\n"
    "  tic80ctl eval \"trace(type(TIC))\"\n"
    "\n"
    "session commands:\n"
    "  start             start a TIC-80 session rooted at the current directory\n"
    "  status            report whether the session is alive\n"
    "  stop              stop the active session\n"
    "\n"
    "runtime commands:\n"
    "  cmd \"...\"        send a raw TIC-80 console command\n"
    "  lint-cart <file>  validate TIC-80 script-cart structure before load\n"
    "  lint-playtest-script <file>\n"
    "                    validate a Lua playtest episode script offline\n"
    "  load <cart>       load a cart or script cart into the active session\n"
    "  run               start the loaded cart\n"
    "  eval \"<expr>\"    run a short Lua expression in the active runtime\n"
    "  screenshot [path] capture one frame; path must be relative to the TIC filesystem root\n"
    "\n"
    "playtest:\n"
    "  playtest --script-file <file> [--timeout <seconds>] [--input-overlay|--no-input-overlay]\n"
    "    runs one bounded scripted episode\n"
    "    re-runs the currently loaded cart before the episode\n"
    "    writes artifacts under ./playtest/episode_N/\n"
    "\n"
    "editor commands:\n"
    "  sfx ...           inspect or edit SFX envelopes, wavetable, panning, speed, and loop points\n"
    "  music ...         inspect or edit track, frame, and pattern-row data\n"
    "  sprite ...        inspect or edit sprite tiles, regions, and palette data\n"
    "  map ...           inspect or edit map rectangles and chunks\n"
    "\n"
    "important notes:\n"
    "  - start from the repo or project root you want TIC-80 to see\n"
    "  - `eval` needs a live runtime; usually call `run` first\n"
    "  - `playtest` is the preferred multi-frame verification tool\n"
    "  - script carts like .lua include code at the top and tagged resources at the bottom\n"
    "  - `resume reload` is useful when the cart is written to preserve state across reloads\n"
    "\n"
    "help topics:\n"
    "  tic80ctl help start\n"
    "  tic80ctl help lint-cart\n"
    "  tic80ctl help lint-playtest-script\n"
    "  tic80ctl help load\n"
    "  tic80ctl help run\n"
    "  tic80ctl help eval\n"
    "  tic80ctl help screenshot\n"
    "  tic80ctl help playtest\n"
    "  tic80ctl help sfx\n"
    "  tic80ctl help music\n"
    "  tic80ctl help sprite\n"
    "  tic80ctl help map\n";

static const char* help_start_text =
    "tic80ctl start\n"
    "\n"
    "Start TIC-80 from the current working directory.\n"
    "\n"
    "Use this from the repo or project root you want TIC-80 to treat as its filesystem root.\n"
    "Load carts after start.\n";

static const char* help_lint_cart_text =
    "tic80ctl lint-cart <file>\n"
    "\n"
    "Validate a TIC-80 script cart offline before you try to load it.\n"
    "\n"
    "This checks tagged resource sections such as PALETTE, MAP, SCREEN, SFX, and others\n"
    "for malformed tags, row syntax, duplicate rows, non-hex payloads, and wrong payload lengths.\n"
    "\n"
    "Examples:\n"
    "  tic80ctl lint-cart game.lua\n"
    "  tic80ctl --json lint-cart game.lua\n"
    "\n"
    "Use this when a script cart load fails without a specific TIC-80 error.\n";

static const char* help_lint_playtest_script_text =
    "tic80ctl lint-playtest-script <file>\n"
    "\n"
    "Validate a Lua playtest episode script offline before you run `tic80ctl playtest`.\n"
    "\n"
    "This checks that the file looks like a playtest script instead of a TIC-80 cart, and catches\n"
    "common route-authoring mistakes such as calling `set_input()` without ever advancing a frame.\n"
    "\n"
    "For explicit kind detection, add this near the top of the file:\n"
    "  -- tic80ctl: playtest-script\n"
    "\n"
    "Examples:\n"
    "  tic80ctl lint-playtest-script episode.lua\n"
    "  tic80ctl --json lint-playtest-script episode.lua\n";

static const char* help_load_text =
    "tic80ctl load <cart>\n"
    "\n"
    "Load a cart or script cart into the active session.\n"
    "\n"
    "Examples:\n"
    "  tic80ctl load game.lua\n"
    "  tic80ctl load game.tic\n"
    "\n"
    "For script carts, keep code near the top of the file and leave tagged resource blocks alone\n"
    "unless you intentionally edit them.\n"
    "\n"
    "If a script cart load does not confirm cleanly, run `tic80ctl lint-cart <file>` first.\n";

static const char* help_run_text =
    "tic80ctl run\n"
    "\n"
    "Start the currently loaded cart.\n"
    "\n"
    "If this fails immediately, suspect a startup/runtime error in the cart's first frame.\n";

static const char* help_eval_text =
    "tic80ctl eval \"<expr>\"\n"
    "\n"
    "Run a short Lua expression in the active cart runtime.\n"
    "\n"
    "Examples:\n"
    "  tic80ctl eval \"trace(type(TIC))\"\n"
    "  tic80ctl eval \"trace(player.x)\"\n"
    "  tic80ctl eval \"debug_flag = true\"\n"
    "\n"
    "Call `run` first. `eval` needs a live runtime.\n";

static const char* help_screenshot_text =
    "tic80ctl screenshot [path]\n"
    "\n"
    "Capture one frame from the active session.\n"
    "\n"
    "Examples:\n"
    "  tic80ctl screenshot\n"
    "  tic80ctl screenshot shots/frame.png\n"
    "\n"
    "Paths are relative to the TIC filesystem root, not absolute host paths.\n";

static const char* help_playtest_text =
    "tic80ctl playtest --script-file <file> [--timeout <seconds>] [--input-overlay|--no-input-overlay]\n"
    "\n"
    "Run one deterministic scripted episode and collect artifacts.\n"
    "\n"
    "Important behavior:\n"
    "  - each playtest fully restarts the currently loaded cart before the episode\n"
    "  - live runtime state from prior `run`/`eval` commands is discarded\n"
    "  - artifacts are written under ./playtest/episode_N/\n"
    "  - inspect script.lua, log.txt, console.txt, and screenshots/\n"
    "\n"
    "Targeted section starts belong in cart code, not in `tic80ctl` flags:\n"
    "  - if you need \"start at level 2\" or \"start at core_entry\", implement that\n"
    "    inside the cart's own boot/reset flow\n"
    "  - keep that path explicitly debug-only, for example by checking `DEBUG_MODE`\n"
    "  - do not rely on `tic80ctl eval` before `playtest`; the restart will wipe it\n"
    "\n"
    "Typical flow:\n"
    "  tic80ctl load game.lua\n"
    "  # optional explicit marker inside episode.lua:\n"
    "  # -- tic80ctl: playtest-script\n"
    "  tic80ctl playtest --script-file route.lua --timeout 20\n";

static const char* help_sfx_text =
    "tic80ctl sfx ...\n"
    "\n"
    "Inspect or edit SFX wavetable, volume/wave/pitch envelopes, arpeggio, panning, speed, and loop points.\n"
    "\n"
    "Examples:\n"
    "  tic80ctl sfx wavetable 0\n"
    "  tic80ctl sfx volume 0 0:15,8:8,29:0\n"
    "  tic80ctl sfx arpeggio 0 0,4,7,12\n";

static const char* help_music_text =
    "tic80ctl music ...\n"
    "\n"
    "Inspect or edit track settings, frame pattern assignments, and pattern-row note data.\n"
    "\n"
    "Examples:\n"
    "  tic80ctl music track 0\n"
    "  tic80ctl music frame 0 0\n"
    "  tic80ctl music row 1 5 C-4:2:F1a\n";

static const char* help_sprite_text =
    "tic80ctl sprite ...\n"
    "\n"
    "Inspect or edit sprite tiles, regions, and palette data.\n"
    "\n"
    "Examples:\n"
    "  tic80ctl sprite tile 3\n"
    "  tic80ctl sprite palette\n";

static const char* help_map_text =
    "tic80ctl map ...\n"
    "\n"
    "Inspect or edit map rectangles and map chunks.\n"
    "\n"
    "Examples:\n"
    "  tic80ctl map rect 5 7 3 2\n"
    "  tic80ctl map chunk 10 12 3 2 1,2,3,4,5,6\n";

static void sb_init(StringBuilder* sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
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
    return sb_append_n(sb, text, strlen(text));
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

    for(const unsigned char* p = (const unsigned char*)text; *p; p++)
    {
        switch(*p)
        {
        case '\\':
            if(!sb_append(sb, "\\\\")) return false;
            break;
        case '"':
            if(!sb_append(sb, "\\\"")) return false;
            break;
        case '\n':
            if(!sb_append(sb, "\\n")) return false;
            break;
        case '\r':
            if(!sb_append(sb, "\\r")) return false;
            break;
        case '\t':
            if(!sb_append(sb, "\\t")) return false;
            break;
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

static void failf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

static void path_join(char* out, size_t out_size, const char* a, const char* b)
{
    if(snprintf(out, out_size, "%s/%s", a, b) >= (int)out_size)
        failf("tic80ctl: path too long");
}

static void init_state_paths(StatePaths* paths, const char* state_dir)
{
    memset(paths, 0, sizeof(*paths));
    if(snprintf(paths->state_dir, sizeof(paths->state_dir), "%s", state_dir) >= (int)sizeof(paths->state_dir))
        failf("tic80ctl: state directory path too long");

    path_join(paths->pid_path, sizeof(paths->pid_path), state_dir, "pid");
    path_join(paths->port_path, sizeof(paths->port_path), state_dir, "port");
    path_join(paths->token_path, sizeof(paths->token_path), state_dir, "token");
    path_join(paths->cwd_path, sizeof(paths->cwd_path), state_dir, "cwd");
    path_join(paths->mode_path, sizeof(paths->mode_path), state_dir, "mode");
    path_join(paths->bin_path, sizeof(paths->bin_path), state_dir, "bin");
    path_join(paths->stdout_log_path, sizeof(paths->stdout_log_path), state_dir, "stdout.log");
    path_join(paths->stderr_log_path, sizeof(paths->stderr_log_path), state_dir, "stderr.log");
    path_join(paths->started_at_path, sizeof(paths->started_at_path), state_dir, "started_at");
    path_join(paths->ready_path, sizeof(paths->ready_path), state_dir, "ready");
    path_join(paths->startup_error_path, sizeof(paths->startup_error_path), state_dir, "startup_error");
}

static bool mkdir_p(const char* path)
{
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if(len >= sizeof(tmp)) return false;
    memcpy(tmp, path, len + 1);

    for(char* p = tmp + 1; *p; p++)
    {
        if(*p != '/') continue;
        *p = '\0';
        if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return false;
        *p = '/';
    }

    if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return false;
    return true;
}

static bool write_text_file(const char* path, const char* text)
{
    FILE* f = fopen(path, "w");
    if(!f) return false;
    bool ok = fputs(text, f) >= 0;
    ok = ok && fflush(f) == 0;
    ok = ok && fclose(f) == 0;
    return ok;
}

static bool write_long_file(const char* path, long value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%ld\n", value);
    return write_text_file(path, buf);
}

static char* read_text_file_trimmed(const char* path)
{
    FILE* f = fopen(path, "r");
    if(!f) return NULL;
    StringBuilder sb;
    sb_init(&sb);
    char buffer[4096];
    while(fgets(buffer, sizeof(buffer), f))
    {
        if(!sb_append(&sb, buffer))
        {
            fclose(f);
            sb_free(&sb);
            return NULL;
        }
    }
    fclose(f);

    while(sb.len > 0 && (sb.data[sb.len - 1] == '\n' || sb.data[sb.len - 1] == '\r'))
        sb.data[--sb.len] = '\0';

    return sb.data ? sb.data : strdup("");
}

static char* read_text_file(const char* path)
{
    FILE* f = fopen(path, "r");
    if(!f) return NULL;

    StringBuilder sb;
    sb_init(&sb);

    char buffer[4096];
    while(fgets(buffer, sizeof(buffer), f))
    {
        if(!sb_append(&sb, buffer))
        {
            fclose(f);
            sb_free(&sb);
            return NULL;
        }
    }

    if(ferror(f))
    {
        fclose(f);
        sb_free(&sb);
        return NULL;
    }

    fclose(f);
    return sb.data ? sb.data : strdup("");
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
            if(bank > ScriptCartSections[i].max_bank)
                return NULL;
            if(bank_out) *bank_out = bank;
            return &ScriptCartSections[i];
        }

    return NULL;
}

static const char* skip_spaces_inline(const char* ptr)
{
    while(*ptr == ' ' || *ptr == '\t') ptr++;
    return ptr;
}

static void trim_span_end(const char** start, size_t* len)
{
    while(*len > 0)
    {
        char c = (*start)[*len - 1];
        if(c != ' ' && c != '\t' && c != '\r')
            break;
        (*len)--;
    }
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

static bool line_has_call(const char* line, const char* name)
{
    size_t name_len = strlen(name);
    const char* ptr = line;

    while((ptr = strstr(ptr, name)) != NULL)
    {
        const char before = ptr > line ? ptr[-1] : '\0';
        if((ptr == line || !(isalnum((unsigned char)before) || before == '_')))
        {
            const char* after = skip_spaces_inline(ptr + name_len);
            if(*after == '(')
                return true;
        }

        ptr += name_len;
    }

    return false;
}

static bool line_has_cart_callback(const char* line, char* callback, size_t callback_size)
{
    static const char* callbacks[] = {"TIC", "BOOT", "SCN", "OVR", "BDR", "MENU"};

    const char* ptr = skip_spaces_inline(line);
    if(strncmp(ptr, "function", 8) != 0 || !(ptr[8] == ' ' || ptr[8] == '\t'))
        return false;

    ptr = skip_spaces_inline(ptr + 8);

    for(size_t i = 0; i < sizeof(callbacks) / sizeof(callbacks[0]); i++)
    {
        size_t len = strlen(callbacks[i]);
        if(strncmp(ptr, callbacks[i], len) == 0)
        {
            const char* after = skip_spaces_inline(ptr + len);
            if(*after == '(')
            {
                if(callback && callback_size > 0)
                    snprintf(callback, callback_size, "%s", callbacks[i]);
                return true;
            }
        }
    }

    return false;
}

static bool parse_tag_line(const char* line, const char* comment, bool* is_end_tag, char* tag, size_t tag_size)
{
    size_t comment_len = strlen(comment);
    if(strncmp(line, comment, comment_len) != 0 || line[comment_len] != ' ')
        return false;

    const char* ptr = line + comment_len + 1;
    if(*ptr != '<')
        return false;

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

    if(ptr == tag_start || *ptr != '>')
        return false;

    size_t tag_len = (size_t)(ptr - tag_start);
    if(tag_len >= tag_size)
        return false;

    memcpy(tag, tag_start, tag_len);
    tag[tag_len] = '\0';
    ptr++;
    ptr = skip_spaces_inline(ptr);

    if(*ptr != '\0')
        return false;

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

    const char* payload = ptr;
    size_t payload_len = strlen(payload);
    trim_span_end(&payload, &payload_len);

    if(index_out) *index_out = index;
    if(payload_out) *payload_out = payload;
    if(payload_len_out) *payload_len_out = payload_len;
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
        if(*ptr == '\n')
            line++;

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

    if(first_tag_line_out)
        *first_tag_line_out = 0;
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
        if(*src != '\r')
            *dst++ = *src;
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
        set_lint_error(result, 0, "unsupported script-cart extension; supported: .lua .moon .wasmp .js .nut .wren .py .rb .janet .scm .fnl");
        return false;
    }

    strip_cr_chars(text);

    int first_tag_line = 0;
    size_t code_length = script_cart_code_length(text, comment, &first_tag_line);
    if(code_length > ScriptCartCodeCapacity)
    {
        if(first_tag_line > 0)
            set_lint_error(result, first_tag_line, "code before first tagged section is %zu bytes; TIC-80 loader truncates at %zu", code_length, ScriptCartCodeCapacity);
        else
            set_lint_error(result, 1, "code section is %zu bytes; TIC-80 loader truncates at %zu", code_length, ScriptCartCodeCapacity);
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
        if(next)
            *next = '\0';

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
                    set_lint_error(result, line_no, "closing tag </%s> does not match open section <%s>", tag, current_tag);
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
                if(in_section)
                {
                    set_lint_error(result, line_no, "nested section <%s> inside <%s>", tag, current_tag);
                    return false;
                }

                int bank = 0;
                current_rule = script_cart_section_rule(tag, &bank);
                if(!current_rule)
                {
                    set_lint_error(result, line_no, "unknown or unsupported TIC-80 section tag <%s>", tag);
                    return false;
                }

                if(seen_section_tag(seen_sections, seen_section_count, tag))
                {
                    set_lint_error(result, line_no, "duplicate section block <%s>; TIC-80 text loader only reads the first block", tag);
                    return false;
                }
                else if(seen_section_count < (int)(sizeof(seen_sections) / sizeof(seen_sections[0])))
                    snprintf(seen_sections[seen_section_count++], sizeof(seen_sections[0]), "%s", tag);

                (void)bank;
                snprintf(current_tag, sizeof(current_tag), "%s", tag);
                in_section = true;
                saw_row = false;
                memset(seen_rows, 0, sizeof(seen_rows));
            }
        }
        else if(in_section)
        {
            int row_index = -1;
            const char* payload = NULL;
            size_t payload_len = 0;

            if(!parse_section_row_line(line, comment, &row_index, &payload, &payload_len))
            {
                set_lint_error(result, line_no, "malformed row in section <%s>; expected `%s 000:<hex>`", current_tag, comment);
                return false;
            }

            if(row_index < 0 || row_index >= current_rule->row_count)
            {
                set_lint_error(result, line_no, "section <%s> row %03d is out of range; expected 000-%03d", current_tag, row_index, current_rule->row_count - 1);
                return false;
            }

            if(seen_rows[row_index])
            {
                set_lint_error(result, line_no, "section <%s> row %03d is duplicated", current_tag, row_index);
                return false;
            }

            if((int)payload_len != current_rule->payload_hex_chars)
            {
                set_lint_error(result, line_no, "section <%s> row %03d has %zu hex chars; expected %d", current_tag, row_index, payload_len, current_rule->payload_hex_chars);
                return false;
            }

            if(!span_is_hex(payload, payload_len))
            {
                set_lint_error(result, line_no, "section <%s> row %03d contains non-hex characters", current_tag, row_index);
                return false;
            }

            seen_rows[row_index] = true;
            saw_row = true;
        }

        if(!next)
            break;

        line = next + 1;
    }

    if(in_section)
    {
        set_lint_error(result, line_no - 1, "section <%s> is missing closing tag </%s>", current_tag, current_tag);
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
        set_lint_error(result, 0, "playtest scripts must be Lua files with a .lua extension");
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
    int frameadvance_line = 0;
    int set_input_line = 0;
    char cart_section_tag[32] = {0};
    char cart_callback_name[16] = {0};

    int line_no = 1;
    for(char* line = text; line; line_no++)
    {
        char* next = strchr(line, '\n');
        if(next)
            *next = '\0';

        const char* trimmed = skip_spaces_inline(line);
        if(*trimmed)
            nonempty_lines++;

        if(*trimmed)
        {
            if(nonempty_lines <= 8
                && strncmp(trimmed, "--", 2) == 0
                && strcasecmp(skip_spaces_inline(trimmed + 2), PlaytestScriptMarker) == 0)
                explicit_marker = true;

            if(!saw_cart_header
                && strncmp(trimmed, "--", 2) == 0
                && strncasecmp(skip_spaces_inline(trimmed + 2), "script:", 7) == 0)
            {
                saw_cart_header = true;
                cart_header_line = line_no;
            }

            if(!saw_playtest_comment
                && strncmp(trimmed, "--", 2) == 0
                && strncasecmp(skip_spaces_inline(trimmed + 2), "playtest script", 15) == 0)
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
                {
                    saw_frameadvance = true;
                    frameadvance_line = line_no;
                }

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

        if(!next)
            break;

        line = next + 1;
    }

    if(saw_cart_header)
    {
        set_lint_error(result, cart_header_line, "looks like a TIC-80 script cart, not a playtest script; found `-- script:` header");
        return false;
    }

    if(saw_cart_section)
    {
        set_lint_error(result, cart_section_line, "looks like a TIC-80 script cart, not a playtest script; found tagged section <%s>", cart_section_tag);
        return false;
    }

    if(saw_cart_callback)
    {
        set_lint_error(result, cart_callback_line, "looks like a TIC-80 cart source file, not a playtest script; found callback function %s()", cart_callback_name);
        return false;
    }

    if(saw_set_input && !saw_frameadvance)
    {
        set_lint_error(result, set_input_line, "uses set_input() but never calls frameadvance(); prepared input is only consumed when the episode advances a frame");
        return false;
    }

    if(!explicit_marker && !saw_frameadvance && !saw_set_input && !saw_log && !saw_end_episode && !saw_playtest_comment)
    {
        set_lint_error(result, 1, "does not look like a TIC-80 playtest script; add `-- tic80ctl: playtest-script` near the top or use playtest APIs like frameadvance(), set_input(), log(), or end_episode()");
        return false;
    }

    if(explicit_marker)
        snprintf(result->message, sizeof(result->message), "lint ok");
    else
        snprintf(result->message, sizeof(result->message), "lint ok (heuristic playtest script; add `-- %s` near the top for explicit kind)", PlaytestScriptMarker);

    (void)frameadvance_line;
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
    sb_append_json_string(&sb, path);
    sb_append(&sb, ",\"message\":");
    sb_append_json_string(&sb, result->message[0] ? result->message : (result->ok ? "lint ok" : "lint failed"));
    if(result->line > 0)
        sb_appendf(&sb, ",\"line\":%d", result->line);
    sb_append(&sb, ",\"kind\":");
    sb_append_json_string(&sb, kind);
    sb_append(&sb, "}\n");
    fputs(sb.data ? sb.data : "{}", stdout);
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

        fprintf(stderr, "tic80ctl: lint-cart requires a file path\n");
        return 1;
    }

    char* text = read_text_file(path);
    if(!text)
    {
        LintResult result = {.ok = false, .line = 0};
        snprintf(result.message, sizeof(result.message), "failed to read file: %s", strerror(errno));
        if(json_output)
            return print_lint_json(path, &result, "script_cart");

        fprintf(stderr, "lint failed: %s: %s\n", path, result.message);
        return 1;
    }

    LintResult result;
    bool ok = lint_script_cart_text(path, text, &result);
    free(text);

    if(ok)
        snprintf(result.message, sizeof(result.message), "lint ok");

    if(json_output)
        return print_lint_json(path, &result, "script_cart");

    if(ok)
    {
        printf("lint ok: %s\n", path);
        return 0;
    }

    if(result.line > 0)
        fprintf(stderr, "lint failed: %s:%d: %s\n", path, result.line, result.message);
    else
        fprintf(stderr, "lint failed: %s: %s\n", path, result.message);

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

        fprintf(stderr, "tic80ctl: lint-playtest-script requires a file path\n");
        return 1;
    }

    char* text = read_text_file(path);
    if(!text)
    {
        LintResult result = {.ok = false, .line = 0};
        snprintf(result.message, sizeof(result.message), "failed to read file: %s", strerror(errno));
        if(json_output)
            return print_lint_json(path, &result, "playtest_script");

        fprintf(stderr, "lint failed: %s: %s\n", path, result.message);
        return 1;
    }

    LintResult result;
    bool ok = lint_playtest_script_text(path, text, &result);
    free(text);

    if(json_output)
        return print_lint_json(path, &result, "playtest_script");

    if(ok)
    {
        printf("%s: %s\n", result.message[0] ? result.message : "lint ok", path);
        return 0;
    }

    if(result.line > 0)
        fprintf(stderr, "lint failed: %s:%d: %s\n", path, result.line, result.message);
    else
        fprintf(stderr, "lint failed: %s: %s\n", path, result.message);

    return 1;
}

static bool file_exists(const char* path)
{
    return access(path, F_OK) == 0;
}

static bool file_is_executable(const char* path)
{
    return access(path, X_OK) == 0;
}

static void unlink_session_files(const StatePaths* paths)
{
    unlink(paths->pid_path);
    unlink(paths->port_path);
    unlink(paths->token_path);
    unlink(paths->ready_path);
    unlink(paths->startup_error_path);
}

static bool get_cwd(char* out, size_t out_size)
{
    return getcwd(out, out_size) != NULL;
}

static void default_state_dir(char* out, size_t out_size)
{
    const char* env = getenv("TIC80CTL_STATE_DIR");
    if(env && *env)
    {
        if(snprintf(out, out_size, "%s", env) >= (int)out_size)
            failf("tic80ctl: state directory path too long");
        return;
    }

    char cwd[PATH_MAX];
    if(!get_cwd(cwd, sizeof(cwd)))
        failf("tic80ctl: failed to get current working directory: %s", strerror(errno));

    if(snprintf(out, out_size, "%s/.local/tic80ctl", cwd) >= (int)out_size)
        failf("tic80ctl: state directory path too long");
}

static void default_tic80_bin(char* out, size_t out_size)
{
    const char* env = getenv("TIC80CTL_BIN");
    if(env && *env)
    {
        snprintf(out, out_size, "%s", env);
        return;
    }

    if(file_is_executable("./build/bin/tic80"))
    {
        snprintf(out, out_size, "%s", "./build/bin/tic80");
        return;
    }

    snprintf(out, out_size, "%s", "tic80");
}

static void default_tic80_launch_command(char* out, size_t out_size)
{
    const char* env = getenv("TIC80CTL_LAUNCH_COMMAND");
    if(env && *env)
    {
        if(snprintf(out, out_size, "%s", env) >= (int)out_size)
            failf("tic80ctl: launch command is too long");
        return;
    }

    out[0] = '\0';
}

static void self_path(char* out, size_t out_size, const char* argv0)
{
    ssize_t len = readlink("/proc/self/exe", out, out_size - 1);
    if(len >= 0)
    {
        out[len] = '\0';
        return;
    }

    snprintf(out, out_size, "%s", argv0);
}

static void sleep_ms(long ms)
{
    if(ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while(nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

static void write_startup_error_path(const char* state_dir, const char* message)
{
    if(state_dir == NULL || *state_dir == '\0' || message == NULL)
        return;

    StatePaths paths;
    init_state_paths(&paths, state_dir);
    mkdir_p(paths.state_dir);
    write_text_file(paths.startup_error_path, message);
}

static char* last_nonempty_line(const char* path)
{
    FILE* f = fopen(path, "r");
    if(!f) return NULL;
    char* line = NULL;
    size_t cap = 0;
    char* last = NULL;
    while(getline(&line, &cap, f) >= 0)
    {
        size_t len = strlen(line);
        while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if(len == 0) continue;
        free(last);
        last = strdup(line);
    }
    free(line);
    fclose(f);
    return last;
}

static void copy_last_nonempty_line(const char* path, char* out, size_t out_size)
{
    char* last = last_nonempty_line(path);
    if(last)
    {
        snprintf(out, out_size, "%s", last);
        free(last);
    }
    else if(out_size > 0)
    {
        out[0] = '\0';
    }
}

static const char* skip_ws(const char* s);

static void parse_command_text(const char* text, char* verb, size_t verb_size, const char** rest_out)
{
    const char* start = text ? skip_ws(text) : "";
    size_t len = 0;
    while(start[len] && !isspace((unsigned char)start[len]))
        len++;

    if(verb_size > 0)
    {
        size_t copy = len < verb_size - 1 ? len : verb_size - 1;
        memcpy(verb, start, copy);
        verb[copy] = '\0';
    }

    if(rest_out)
        *rest_out = skip_ws(start + len);
}

static bool random_hex_token(char* out, size_t out_size)
{
    if(out_size < 65) return false;

    unsigned char bytes[32];
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t got = -1;
    if(fd >= 0)
    {
        got = read(fd, bytes, sizeof(bytes));
        close(fd);
    }

    if(got != (ssize_t)sizeof(bytes))
    {
        srand((unsigned int)(time(NULL) ^ getpid()));
        for(size_t i = 0; i < sizeof(bytes); i++) bytes[i] = (unsigned char)(rand() & 0xff);
    }

    static const char hex[] = "0123456789abcdef";
    for(size_t i = 0; i < sizeof(bytes); i++)
    {
        out[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    out[64] = '\0';
    return true;
}

static const char* skip_ws(const char* s)
{
    while(*s && isspace((unsigned char)*s)) s++;
    return s;
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
    if(*start != '"') return NULL;
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
            case '/':
                if(!sb_append_n(&sb, p, 1)) goto oom;
                break;
            case 'n':
                if(!sb_append_n(&sb, "\n", 1)) goto oom;
                break;
            case 'r':
                if(!sb_append_n(&sb, "\r", 1)) goto oom;
                break;
            case 't':
                if(!sb_append_n(&sb, "\t", 1)) goto oom;
                break;
            case 'b':
                if(!sb_append_n(&sb, "\b", 1)) goto oom;
                break;
            case 'f':
                if(!sb_append_n(&sb, "\f", 1)) goto oom;
                break;
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
    else
    {
        while(*end && *end != ',' && *end != '}') end++;
    }

    if(end <= start) return false;
    size_t len = (size_t)(end - start);
    if(len >= out_size) return false;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool socket_read_line(FILE* io, char** line_out)
{
    char* line = NULL;
    size_t cap = 0;
    ssize_t len = getline(&line, &cap, io);
    if(len < 0)
    {
        free(line);
        return false;
    }

    while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';

    *line_out = line;
    return true;
}

static bool child_is_running(Server* server)
{
    if(!server->child_running) return false;
    int status = 0;
    pid_t result = waitpid(server->child_pid, &status, WNOHANG);
    if(result == 0) return true;
    if(result == server->child_pid)
    {
        server->child_running = false;
        server->child_status = status;
        unlink(server->paths.ready_path);
        return false;
    }
    return false;
}

static void write_startup_error(Server* server, const char* message)
{
    write_text_file(server->paths.startup_error_path, message);
}

static void server_cleanup(Server* server)
{
    if(server->child_stdout)
    {
        fclose(server->child_stdout);
        server->child_stdout = NULL;
    }
    if(server->stdout_log)
    {
        fclose(server->stdout_log);
        server->stdout_log = NULL;
    }
    if(server->stderr_log)
    {
        fclose(server->stderr_log);
        server->stderr_log = NULL;
    }
    if(server->child_stdin_fd >= 0)
    {
        close(server->child_stdin_fd);
        server->child_stdin_fd = -1;
    }
    if(server->listen_fd >= 0)
    {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
}

static char* startup_error_message(Server* server)
{
    char* last = last_nonempty_line(server->paths.stderr_log_path);
    if(last && *last)
    {
        StringBuilder sb;
        sb_init(&sb);
        sb_appendf(&sb, "tic80ctl: session failed during startup: %s", last);
        free(last);
        return sb.data;
    }

    free(last);
    return strdup("tic80ctl: session failed during startup");
}

static bool send_jsonrpc(Server* server, int id, const char* method, const char* params_json)
{
    if(dprintf(server->child_stdin_fd,
               "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":%s}\n",
               id,
               method,
               params_json) < 0)
        return false;
    return true;
}

static bool send_notification(Server* server, const char* method)
{
    if(dprintf(server->child_stdin_fd, "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":{}}\n", method) < 0)
        return false;
    return true;
}

static bool line_has_matching_id(const char* line, int id)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"id\":%d", id);
    return strstr(line, pattern) != NULL;
}

static bool wait_for_mcp_response(Server* server, int id, char* out, size_t out_size)
{
    int fd = fileno(server->child_stdout);

    while(true)
    {
        if(!child_is_running(server))
            return false;

        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = POLL_INTERVAL_MS * 1000;

        int ready = select(fd + 1, &set, NULL, NULL, &tv);
        if(ready < 0)
        {
            if(errno == EINTR) continue;
            return false;
        }

        if(ready == 0)
            continue;

        char* line = NULL;
        size_t cap = 0;
        ssize_t len = getline(&line, &cap, server->child_stdout);
        if(len < 0)
        {
            free(line);
            return false;
        }

        if(server->stdout_log)
        {
            fputs(line, server->stdout_log);
            fflush(server->stdout_log);
        }

        while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if(line_has_matching_id(line, id))
        {
            snprintf(out, out_size, "%s", line);
            free(line);
            return true;
        }

        free(line);
    }

    return false;
}

static bool launch_tic80(Server* server)
{
    int stdin_pipe[2];
    int stdout_pipe[2];
    if(pipe(stdin_pipe) != 0) return false;
    if(pipe(stdout_pipe) != 0)
    {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return false;
    }

    int err_fd = open(server->paths.stderr_log_path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if(err_fd < 0)
    {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if(pid < 0)
    {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(err_fd);
        return false;
    }

    if(pid == 0)
    {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(err_fd, STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(err_fd);

        if(server->launch_command[0])
        {
            execl("/bin/sh", "sh", "-lc", server->launch_command, (char*)NULL);
        }
        else
        {
            execlp("xvfb-run",
                   "xvfb-run",
                   "--auto-servernum",
                   server->tic80_bin,
                   "--skip",
                   "--soft",
                   "--fs",
                   server->cwd,
                   "--mcp",
                   (char*)NULL);
        }
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(err_fd);

    server->child_pid = pid;
    server->child_stdin_fd = stdin_pipe[1];
    server->child_stdout = fdopen(stdout_pipe[0], "r");
    if(!server->child_stdout)
    {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        return false;
    }

    server->child_running = true;
    server->child_status = 0;
    return true;
}

static bool mcp_initialize(Server* server)
{
    char response[262144];
    int init_id = server->next_id++;
    if(!send_jsonrpc(server,
                     init_id,
                     "initialize",
                     "{\"protocolVersion\":\"2025-03-26\",\"capabilities\":{},\"clientInfo\":{\"name\":\"tic80ctl\",\"version\":\"0\"}}"))
        return false;
    if(!wait_for_mcp_response(server, init_id, response, sizeof(response)))
        return false;

    if(strstr(response, "\"result\"") == NULL || strstr(response, "\"serverInfo\"") == NULL)
        return false;

    if(!send_notification(server, "notifications/initialized"))
        return false;

    int tools_id = server->next_id++;
    if(!send_jsonrpc(server, tools_id, "tools/list", "{}"))
        return false;
    if(!wait_for_mcp_response(server, tools_id, response, sizeof(response)))
        return false;

    if(strstr(response, "\"run_command\"") == NULL || strstr(response, "\"capture_screenshot\"") == NULL)
        return false;

    return child_is_running(server);
}

static bool create_listen_socket(Server* server, int* port_out)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) return false;

    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        close(fd);
        return false;
    }

    if(listen(fd, 16) != 0)
    {
        close(fd);
        return false;
    }

    socklen_t len = sizeof(addr);
    if(getsockname(fd, (struct sockaddr*)&addr, &len) != 0)
    {
        close(fd);
        return false;
    }

    server->listen_fd = fd;
    *port_out = ntohs(addr.sin_port);
    return true;
}

static bool setup_server(Server* server, const char* state_dir, const char* cwd, const char* tic80_bin, const char* launch_command)
{
    memset(server, 0, sizeof(*server));
    server->listen_fd = -1;
    server->child_stdin_fd = -1;
    server->next_id = 1;

    init_state_paths(&server->paths, state_dir);
    snprintf(server->cwd, sizeof(server->cwd), "%s", cwd);
    snprintf(server->tic80_bin, sizeof(server->tic80_bin), "%s", tic80_bin);
    snprintf(server->launch_command, sizeof(server->launch_command), "%s", launch_command ? launch_command : "");

    if(!mkdir_p(server->paths.state_dir))
        return false;

    unlink(server->paths.ready_path);
    unlink(server->paths.startup_error_path);

    server->stdout_log = fopen(server->paths.stdout_log_path, "w");
    if(!server->stdout_log) return false;
    server->stderr_log = fopen(server->paths.stderr_log_path, "w");
    if(!server->stderr_log) return false;

    int port = 0;
    if(!create_listen_socket(server, &port))
        return false;

    if(!random_hex_token(server->token, sizeof(server->token)))
        return false;

    time_t now = time(NULL);
    char started_buf[64];
    snprintf(started_buf, sizeof(started_buf), "%ld\n", (long)now);

    if(!write_long_file(server->paths.pid_path, (long)getpid()) ||
       !write_long_file(server->paths.port_path, port) ||
       !write_text_file(server->paths.token_path, server->token) ||
       !write_text_file(server->paths.cwd_path, server->cwd) ||
       !write_text_file(server->paths.mode_path, "xvfb\n") ||
       !write_text_file(server->paths.bin_path, server->launch_command[0] ? server->launch_command : server->tic80_bin) ||
       !write_text_file(server->paths.started_at_path, started_buf))
        return false;

    return true;
}

static bool mcp_result_is_error(const char* raw)
{
    return strstr(raw, "\"isError\":true") != NULL;
}

static bool server_send_line(FILE* io, const char* line)
{
    return fprintf(io, "%s\n", line) >= 0 && fflush(io) == 0;
}

static void respond_error(FILE* io, const char* message)
{
    StringBuilder sb;
    sb_init(&sb);
    sb_append(&sb, "{\"ok\":false,\"error\":");
    sb_append_json_string(&sb, message);
    sb_append(&sb, "}");
    server_send_line(io, sb.data ? sb.data : "{\"ok\":false,\"error\":\"out of memory\"}");
    sb_free(&sb);
}

static void respond_status(FILE* io, Server* server)
{
    bool running = child_is_running(server);
    StringBuilder sb;
    sb_init(&sb);
    sb_appendf(&sb, "{\"ok\":true,\"running\":%s,\"pid\":%ld,\"mode\":\"xvfb\",\"cwd\":",
               running ? "true" : "false",
               (long)getpid());
    sb_append_json_string(&sb, server->cwd);
    sb_append(&sb, ",\"bin\":");
    sb_append_json_string(&sb, server->tic80_bin);
    sb_append(&sb, ",\"stdout\":");
    sb_append_json_string(&sb, server->paths.stdout_log_path);
    sb_append(&sb, ",\"stderr\":");
    sb_append_json_string(&sb, server->paths.stderr_log_path);
    if(!running)
    {
        char* last = last_nonempty_line(server->paths.stderr_log_path);
        sb_append(&sb, ",\"error\":");
        sb_append_json_string(&sb, (last && *last) ? last : "tic80ctl child exited");
        free(last);
    }
    sb_append(&sb, "}");
    server_send_line(io, sb.data);
    sb_free(&sb);
}

static bool run_mcp_tool(Server* server, const char* name, const char* arguments_json, char* out, size_t out_size)
{
    if(!child_is_running(server))
        return false;

    int id = server->next_id++;
    StringBuilder params;
    sb_init(&params);
    sb_append(&params, "{\"name\":");
    sb_append_json_string(&params, name);
    sb_append(&params, ",\"arguments\":");
    sb_append(&params, arguments_json);
    sb_append(&params, "}");

    bool ok = send_jsonrpc(server, id, "tools/call", params.data ? params.data : "{}") &&
              wait_for_mcp_response(server, id, out, out_size);
    sb_free(&params);
    return ok;
}

static bool handle_tool_request(Server* server, FILE* io, const char* tool_name, const char* args_json)
{
    if(!child_is_running(server))
    {
        respond_error(io, "tic80ctl: no active session; run `tic80ctl start`");
        return true;
    }

    char request_verb[64] = {0};
    char request_text[262144] = {0};
    const char* request_rest = "";
    if(strcmp(tool_name, "run_command") == 0 && args_json && json_get_string(args_json, "command", request_text, sizeof(request_text)))
    {
        parse_command_text(request_text, request_verb, sizeof(request_verb), &request_rest);
        snprintf(server->last_command, sizeof(server->last_command), "%s", request_verb);
        if(strcmp(request_verb, "load") == 0)
            snprintf(server->last_load_target, sizeof(server->last_load_target), "%s", request_rest);
    }
    else
    {
        snprintf(server->last_command, sizeof(server->last_command), "%s", tool_name);
    }

    char raw[262144];
    if(!run_mcp_tool(server, tool_name, args_json, raw, sizeof(raw)))
    {
        copy_last_nonempty_line(server->paths.stderr_log_path, server->last_stderr_tail, sizeof(server->last_stderr_tail));
        respond_error(io, child_is_running(server)
                              ? "tic80ctl: timed out waiting for MCP response"
                              : "tic80ctl: session terminated while waiting for response");
        return true;
    }

    copy_last_nonempty_line(server->paths.stderr_log_path, server->last_stderr_tail, sizeof(server->last_stderr_tail));

    StringBuilder sb;
    sb_init(&sb);
    sb_appendf(&sb, "{\"ok\":true,\"is_error\":%s,\"mcp_raw\":", mcp_result_is_error(raw) ? "true" : "false");
    sb_append_json_string(&sb, raw);
    sb_append(&sb, ",\"verb\":");
    sb_append_json_string(&sb, request_verb[0] ? request_verb : tool_name);
    sb_append(&sb, ",\"last_load_target\":");
    sb_append_json_string(&sb, server->last_load_target);
    sb_append(&sb, ",\"stderr_tail\":");
    sb_append_json_string(&sb, server->last_stderr_tail);
    sb_append(&sb, "}");
    server_send_line(io, sb.data);
    sb_free(&sb);
    return true;
}

static bool process_request(Server* server, FILE* io, const char* json)
{
    char token[128];
    char command[64];
    if(!json_get_string(json, "token", token, sizeof(token)) || strcmp(token, server->token) != 0)
    {
        respond_error(io, "tic80ctl: invalid session token");
        return true;
    }
    if(!json_get_string(json, "command", command, sizeof(command)))
    {
        respond_error(io, "tic80ctl: malformed request");
        return true;
    }

    if(strcmp(command, "status") == 0)
    {
        respond_status(io, server);
        return true;
    }

    if(strcmp(command, "stop") == 0)
    {
        bool running = child_is_running(server);
        if(running) kill(server->child_pid, SIGTERM);
        if(running) waitpid(server->child_pid, &server->child_status, 0);
        server->child_running = false;

        StringBuilder sb;
        sb_init(&sb);
        sb_appendf(&sb, "{\"ok\":true,\"stopped\":true,\"running\":false,\"pid\":%ld}", (long)getpid());
        server_send_line(io, sb.data);
        sb_free(&sb);
        return false;
    }

    if(strcmp(command, "run_command") == 0)
    {
        char value[262144];
        if(!json_get_string(json, "value", value, sizeof(value)))
        {
            respond_error(io, "tic80ctl: malformed request");
            return true;
        }
        StringBuilder args;
        sb_init(&args);
        sb_append(&args, "{\"command\":");
        sb_append_json_string(&args, value);
        sb_append(&args, "}");
        bool keep_running = handle_tool_request(server, io, "run_command", args.data);
        sb_free(&args);
        return keep_running;
    }

    if(strcmp(command, "screenshot") == 0)
    {
        char value[PATH_MAX];
        bool has_path = json_get_string(json, "value", value, sizeof(value));
        if(has_path)
        {
            StringBuilder args;
            sb_init(&args);
            sb_append(&args, "{\"path\":");
            sb_append_json_string(&args, value);
            sb_append(&args, "}");
            bool keep_running = handle_tool_request(server, io, "capture_screenshot", args.data);
            sb_free(&args);
            return keep_running;
        }
        return handle_tool_request(server, io, "capture_screenshot", "{}");
    }

    if(strcmp(command, "playtest") == 0)
    {
        char script[262144];
        if(!json_get_string(json, "script", script, sizeof(script)))
        {
            respond_error(io, "tic80ctl: malformed request");
            return true;
        }

        bool input_overlay = true;
        json_get_bool(json, "input_overlay", &input_overlay);
        long timeout_seconds = 0;
        bool has_timeout = json_get_long(json, "timeout_seconds", &timeout_seconds);

        StringBuilder args;
        sb_init(&args);
        sb_append(&args, "{\"script\":");
        sb_append_json_string(&args, script);
        if(has_timeout) sb_appendf(&args, ",\"timeout_seconds\":%ld", timeout_seconds);
        sb_appendf(&args, ",\"input_overlay\":%s}", input_overlay ? "true" : "false");

        bool keep_running = handle_tool_request(server, io, "run_playtest_episode", args.data);
        sb_free(&args);
        return keep_running;
    }

    if(strcmp(command, "tool") == 0)
    {
        char tool[128];
        char args_json[262144];
        if(!json_get_string(json, "tool", tool, sizeof(tool))
            || !json_get_raw_value(json, "args_json", args_json, sizeof(args_json)))
        {
            respond_error(io, "tic80ctl: malformed request");
            return true;
        }

        return handle_tool_request(server, io, tool, args_json);
    }

    respond_error(io, "tic80ctl: malformed request");
    return true;
}

static int server_main(const char* state_dir, const char* cwd, const char* tic80_bin, const char* launch_command)
{
    Server server;
    if(!setup_server(&server, state_dir, cwd, tic80_bin, launch_command))
    {
        write_startup_error_path(state_dir, "tic80ctl: failed to initialize supervisor");
        return 1;
    }

    if(chdir(cwd) != 0)
    {
        write_startup_error(&server, "tic80ctl: failed to enter session working directory");
        server_cleanup(&server);
        unlink(server.paths.pid_path);
        unlink(server.paths.port_path);
        unlink(server.paths.token_path);
        unlink(server.paths.ready_path);
        return 1;
    }

    if(!launch_tic80(&server) || !mcp_initialize(&server))
    {
        char* message = startup_error_message(&server);
        write_startup_error(&server, message);
        free(message);
        if(server.child_running)
        {
            kill(server.child_pid, SIGTERM);
            waitpid(server.child_pid, &server.child_status, 0);
            server.child_running = false;
        }
        server_cleanup(&server);
        unlink(server.paths.pid_path);
        unlink(server.paths.port_path);
        unlink(server.paths.token_path);
        unlink(server.paths.ready_path);
        return 1;
    }

    write_text_file(server.paths.ready_path, "ready\n");
    unlink(server.paths.startup_error_path);

    while(true)
    {
        int client_fd = accept(server.listen_fd, NULL, NULL);
        if(client_fd < 0)
        {
            if(errno == EINTR) continue;
            break;
        }

        FILE* io = fdopen(client_fd, "r+");
        if(!io)
        {
            close(client_fd);
            continue;
        }

        char* line = NULL;
        bool keep_running = true;
        if(socket_read_line(io, &line))
        {
            keep_running = process_request(&server, io, line);
            free(line);
        }

        fclose(io);
        if(!keep_running) break;
    }

    if(server.child_running)
    {
        kill(server.child_pid, SIGTERM);
        waitpid(server.child_pid, &server.child_status, 0);
        server.child_running = false;
    }

    server_cleanup(&server);
    unlink_session_files(&server.paths);
    return 0;
}

static int spawn_server_process(const char* self, const char* state_dir, const char* cwd, const char* tic80_bin, const char* launch_command)
{
    pid_t child = fork();
    if(child < 0) return -1;

    if(child == 0)
    {
        if(setsid() < 0) _exit(1);

        pid_t grandchild = fork();
        if(grandchild < 0) _exit(1);
        if(grandchild > 0) _exit(0);

        int null_fd = open("/dev/null", O_RDWR);
        if(null_fd >= 0)
        {
            dup2(null_fd, STDIN_FILENO);
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if(null_fd > STDERR_FILENO) close(null_fd);
        }

        if(launch_command && *launch_command)
            execl(self, self, "--server", "--state-dir", state_dir, "--cwd", cwd, "--tic80-bin", tic80_bin, "--launch-command", launch_command, (char*)NULL);
        else
            execl(self, self, "--server", "--state-dir", state_dir, "--cwd", cwd, "--tic80-bin", tic80_bin, (char*)NULL);

        write_startup_error_path(state_dir, "tic80ctl: failed to launch supervisor");
        _exit(127);
    }

    int status = 0;
    if(waitpid(child, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool connect_to_server(const StatePaths* paths, FILE** io_out)
{
    char* port_text = read_text_file_trimmed(paths->port_path);
    char* token = read_text_file_trimmed(paths->token_path);
    if(!port_text || !token)
    {
        free(port_text);
        free(token);
        return false;
    }

    long port = strtol(port_text, NULL, 10);
    free(port_text);
    if(port <= 0 || port > 65535)
    {
        free(token);
        return false;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        free(token);
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        close(fd);
        free(token);
        return false;
    }

    FILE* io = fdopen(fd, "r+");
    if(!io)
    {
        close(fd);
        free(token);
        return false;
    }

    setvbuf(io, NULL, _IOLBF, 0);
    *io_out = io;
    free(token);
    return true;
}

static bool read_server_token(const StatePaths* paths, char* out, size_t out_size)
{
    char* token = read_text_file_trimmed(paths->token_path);
    if(!token) return false;
    snprintf(out, out_size, "%s", token);
    free(token);
    return true;
}

static bool request_server_json(const StatePaths* paths, const char* request, ServerResponse* response)
{
    memset(response, 0, sizeof(*response));

    FILE* io = NULL;
    if(!connect_to_server(paths, &io))
        return false;

    if(fprintf(io, "%s\n", request) < 0 || fflush(io) != 0)
    {
        fclose(io);
        return false;
    }

    char* line = NULL;
    bool ok = socket_read_line(io, &line);
    fclose(io);
    if(!ok) return false;

    json_get_bool(line, "ok", &response->ok);
    json_get_bool(line, "running", &response->running);
    json_get_long(line, "pid", &response->pid);
    json_get_string(line, "mode", response->mode, sizeof(response->mode));
    json_get_string(line, "cwd", response->cwd, sizeof(response->cwd));
    json_get_string(line, "bin", response->bin, sizeof(response->bin));
    json_get_string(line, "stdout", response->stdout_path, sizeof(response->stdout_path));
    json_get_string(line, "stderr", response->stderr_path, sizeof(response->stderr_path));
    json_get_string(line, "error", response->error, sizeof(response->error));
    json_get_string(line, "verb", response->verb, sizeof(response->verb));
    json_get_string(line, "last_load_target", response->last_load_target, sizeof(response->last_load_target));
    json_get_string(line, "stderr_tail", response->stderr_tail, sizeof(response->stderr_tail));
    response->has_mcp_raw = json_get_string(line, "mcp_raw", response->mcp_raw, sizeof(response->mcp_raw));
    json_get_bool(line, "is_error", &response->is_error);
    json_get_bool(line, "stopped", &response->stopped);
    free(line);
    return true;
}

static bool build_request(StringBuilder* sb, const StatePaths* paths, const char* command)
{
    char token[128];
    if(!read_server_token(paths, token, sizeof(token))) return false;
    sb_append(sb, "{\"token\":");
    sb_append_json_string(sb, token);
    sb_append(sb, ",\"command\":");
    sb_append_json_string(sb, command);
    return true;
}

static bool query_status(const StatePaths* paths, ServerResponse* response)
{
    StringBuilder request;
    sb_init(&request);
    bool ok = build_request(&request, paths, "status") && sb_append(&request, "}");
    if(!ok)
    {
        sb_free(&request);
        return false;
    }

    ok = request_server_json(paths, request.data, response);
    sb_free(&request);
    return ok;
}

static bool send_stop_request(const StatePaths* paths, ServerResponse* response)
{
    StringBuilder request;
    sb_init(&request);
    bool ok = build_request(&request, paths, "stop") && sb_append(&request, "}");
    if(!ok)
    {
        sb_free(&request);
        return false;
    }

    ok = request_server_json(paths, request.data, response);
    sb_free(&request);
    return ok;
}

static void cleanup_stale_session(const StatePaths* paths)
{
    unlink_session_files(paths);
}

static void print_usage(FILE* out)
{
    fputs(usage_text, out);
}

static const char* help_topic_text(const char* topic)
{
    if(!topic || !topic[0]) return help_text;
    if(strcmp(topic, "start") == 0) return help_start_text;
    if(strcmp(topic, "lint-cart") == 0) return help_lint_cart_text;
    if(strcmp(topic, "lint-playtest-script") == 0) return help_lint_playtest_script_text;
    if(strcmp(topic, "load") == 0) return help_load_text;
    if(strcmp(topic, "run") == 0) return help_run_text;
    if(strcmp(topic, "eval") == 0) return help_eval_text;
    if(strcmp(topic, "screenshot") == 0) return help_screenshot_text;
    if(strcmp(topic, "playtest") == 0) return help_playtest_text;
    if(strcmp(topic, "sfx") == 0) return help_sfx_text;
    if(strcmp(topic, "music") == 0) return help_music_text;
    if(strcmp(topic, "sprite") == 0) return help_sprite_text;
    if(strcmp(topic, "map") == 0) return help_map_text;
    return NULL;
}

static int print_help(const char* topic)
{
    const char* text = help_topic_text(topic);
    if(!text)
    {
        fprintf(stderr, "tic80ctl: unknown help topic: %s\n\n", topic);
        print_usage(stderr);
        return 1;
    }

    fputs(text, stdout);
    return 0;
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

static void trim_text_in_place(char* text)
{
    char* start = text;
    while(*start && isspace((unsigned char)*start))
        start++;

    if(start != text)
        memmove(text, start, strlen(start) + 1);

    size_t len = strlen(text);
    while(len > 0 && isspace((unsigned char)text[len - 1]))
        text[--len] = '\0';
}

static bool extract_structured_content(const char* raw, char* out, size_t out_size)
{
    return json_get_raw_value(raw, "structuredContent", out, out_size);
}

static bool extract_first_text_into_buffer(const char* raw, char* out, size_t out_size)
{
    if(!extract_first_text(raw, out, out_size))
        return false;
    return true;
}

static bool print_structured_value(FILE* out, const char* raw)
{
    if(raw[0] == '"')
    {
        char* text = parse_json_string_value(raw, NULL);
        if(!text) return false;
        fputs(text, out);
        free(text);
        return true;
    }

    fputs(raw, out);
    return true;
}

static void append_command_diagnostics_json(StringBuilder* sb, const char* request_text, const ServerResponse* response)
{
    char verb[64] = {0};
    char request_target[PATH_MAX] = {0};
    const char* rest = "";
    parse_command_text(request_text ? request_text : "", verb, sizeof(verb), &rest);
    if(strcmp(verb, "load") == 0)
        snprintf(request_target, sizeof(request_target), "%s", rest);

    const char* failure_kind = "";
    const char* hint = "";
    bool load_prompt_missing = false;

    if(strcmp(verb, "run") == 0 && strstr(response->mcp_raw, "the code is empty"))
    {
        failure_kind = "empty_code";
        hint = "previous load did not produce a valid cart";
    }
    else if(strcmp(verb, "eval") == 0 && strstr(response->mcp_raw, "runtime not initialized"))
    {
        failure_kind = "runtime_not_initialized";
        hint = "the run command did not start a VM";
    }
    else if(strcmp(verb, "load") == 0)
    {
        char text[262144];
        if(!extract_first_text_into_buffer(response->mcp_raw, text, sizeof(text)) || text[0] == '\0')
        {
            failure_kind = "load_missing_confirmation";
            hint = "TIC-80 returned no explicit confirmation for the load";
            load_prompt_missing = true;
        }
        else if(response->is_error)
        {
            failure_kind = "project_loading_error";
            hint = "see stderr_tail for the underlying loader message";
        }
    }

    if(!failure_kind[0] && response->is_error && (strcmp(verb, "run") == 0 || strcmp(verb, "eval") == 0))
    {
        failure_kind = "command_error";
        hint = "see stderr_tail for the underlying TIC-80 error";
    }

    sb_append(sb, ",\"diagnostics\":{");
    sb_append(sb, "\"verb\":");
    sb_append_json_string(sb, verb[0] ? verb : "");
    sb_append(sb, ",\"request_target\":");
    sb_append_json_string(sb, request_target);
    sb_append(sb, ",\"last_load_target\":");
    sb_append_json_string(sb, response->last_load_target);
    sb_append(sb, ",\"stderr_tail\":");
    sb_append_json_string(sb, response->stderr_tail);
    if(failure_kind[0])
    {
        sb_append(sb, ",\"failure_kind\":");
        sb_append_json_string(sb, failure_kind);
        sb_append(sb, ",\"hint\":");
        sb_append_json_string(sb, hint);
    }
    else if(load_prompt_missing)
    {
        sb_append(sb, ",\"hint\":");
        sb_append_json_string(sb, "subsequent run may still fail if TIC-80 rejected the cart");
    }
    sb_append(sb, "}");
}

static bool print_structured_lines(const char* structured)
{
    static const char* Keys[] = {
        "track", "frame", "pattern", "row",
        "bank", "vbank", "id", "sfx", "waveform", "target",
        "x", "y", "width", "height",
        "tempo", "speed", "start", "size",
        "left", "right",
        "patterns", "rows", "values", "colors", "tiles", "sprites",
    };

    bool printed = false;
    for(size_t i = 0; i < sizeof(Keys) / sizeof(Keys[0]); i++)
    {
        char value[262144];
        if(!json_get_raw_value(structured, Keys[i], value, sizeof(value)))
            continue;

        if(printed)
            fputc('\n', stdout);
        printf("%s=", Keys[i]);
        if(!print_structured_value(stdout, value))
            return false;
        printed = true;
    }

    if(printed)
        fputc('\n', stdout);

    return printed;
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
            if(value_len >= out_size)
                value_len = out_size - 1;
            memcpy(out, p + key_len + 1, value_len);
            out[value_len] = '\0';
            return true;
        }

        p = line_end ? line_end + 1 : NULL;
    }

    if(out_size)
        out[0] = '\0';
    return false;
}

static const char* playtest_failure_phase(const char* text)
{
    if(strstr(text, "failed to reset cart into run mode"))
        return "cart restart";
    if(strstr(text, "failed to initialize cart runtime before enabling DEBUG_MODE"))
        return "cart runtime startup";
    if(strstr(text, "runtime not initialized"))
        return "cart runtime startup";
    if(strstr(text, "frameadvance() requires TIC-80 to be in run mode"))
        return "frame advance";
    if(strstr(text, "failed to initialize playtest runtime"))
        return "playtest harness startup";
    return "playtest";
}

static const char* playtest_failure_hint(const char* text)
{
    if(strstr(text, "runtime not initialized") || strstr(text, "failed to initialize cart runtime"))
        return "the cart did not reach a usable TIC() runtime; inspect the cart-side Lua error in console_path or rerun `tic80ctl run`";
    if(strstr(text, "failed to reset cart into run mode"))
        return "`tic80ctl run` failed while playtest tried to restart the loaded cart";
    if(strstr(text, "frameadvance() requires TIC-80 to be in run mode"))
        return "the episode advanced a frame after the cart left run mode or crashed";
    return "inspect log_path and console_path for the route segment and cart-side traces";
}

static bool extract_playtest_text(const ServerResponse* response, char* text, size_t text_size)
{
    if(!extract_first_text(response->mcp_raw, text, text_size))
        return false;
    trim_text_in_place(text);
    return text[0] != '\0';
}

static char* first_nonempty_line_local(const char* path)
{
    FILE* f = fopen(path, "r");
    if(!f)
        return NULL;

    char buffer[2048];
    while(fgets(buffer, sizeof(buffer), f))
    {
        char* start = buffer;
        while(*start && isspace((unsigned char)*start))
            start++;

        char* end = start + strlen(start);
        while(end > start && isspace((unsigned char)end[-1]))
            *--end = '\0';

        if(*start)
        {
            char* result = strdup(start);
            fclose(f);
            return result;
        }
    }

    fclose(f);
    return NULL;
}

static void append_playtest_diagnostics_json(StringBuilder* sb, const ServerResponse* response)
{
    char text[262144];
    if(!response->is_error || !extract_playtest_text(response, text, sizeof(text)))
        return;

    char status[64] = {0};
    char message[512] = {0};
    char console_path[PATH_MAX] = {0};
    text_line_value(text, "status", status, sizeof(status));
    text_line_value(text, "message", message, sizeof(message));
    text_line_value(text, "console_path", console_path, sizeof(console_path));
    char* console_first = console_path[0] ? first_nonempty_line_local(console_path) : NULL;
    char* console_tail = console_path[0] ? last_nonempty_line(console_path) : NULL;

    sb_append(sb, ",\"diagnostics\":{");
    sb_append(sb, "\"phase\":");
    sb_append_json_string(sb, playtest_failure_phase(text));
    sb_append(sb, ",\"status\":");
    sb_append_json_string(sb, status);
    sb_append(sb, ",\"message\":");
    sb_append_json_string(sb, message);
    sb_append(sb, ",\"console_path\":");
    sb_append_json_string(sb, console_path);
    sb_append(sb, ",\"console_first\":");
    sb_append_json_string(sb, console_first ? console_first : "");
    sb_append(sb, ",\"console_tail\":");
    sb_append_json_string(sb, console_tail ? console_tail : "");
    sb_append(sb, ",\"hint\":");
    sb_append_json_string(sb, playtest_failure_hint(text));
    sb_append(sb, "}");
    free(console_first);
    free(console_tail);
}

static void print_playtest_diagnostics_human(const ServerResponse* response)
{
    char text[262144];
    if(!response->is_error || !extract_playtest_text(response, text, sizeof(text)))
        return;

    char console_path[PATH_MAX] = {0};
    text_line_value(text, "console_path", console_path, sizeof(console_path));
    char* console_first = console_path[0] ? first_nonempty_line_local(console_path) : NULL;
    char* console_tail = console_path[0] ? last_nonempty_line(console_path) : NULL;

    printf("playtest failed during %s\n", playtest_failure_phase(text));
    if(console_first && *console_first)
        printf("console first: %s\n", console_first);
    if(console_tail && *console_tail)
        printf("console tail: %s\n", console_tail);
    printf("hint: %s\n", playtest_failure_hint(text));
    if(console_path[0])
        printf("console_path: %s\n", console_path);
    free(console_first);
    free(console_tail);
}

static void print_command_diagnostics_human(const char* request_text, const ServerResponse* response)
{
    char verb[64] = {0};
    char request_target[PATH_MAX] = {0};
    const char* rest = "";
    parse_command_text(request_text ? request_text : "", verb, sizeof(verb), &rest);
    if(strcmp(verb, "load") == 0)
        snprintf(request_target, sizeof(request_target), "%s", rest);

    if(strcmp(verb, "run") == 0 && strstr(response->mcp_raw, "the code is empty"))
    {
        printf("run failed: TIC-80 reports no loaded code\n");
        if(response->last_load_target[0])
            printf("last load target: %s\n", response->last_load_target);
        else if(request_target[0])
            printf("last load target: %s\n", request_target);
        if(response->stderr_tail[0])
            printf("stderr tail: %s\n", response->stderr_tail);
    }
    else if(strcmp(verb, "eval") == 0 && strstr(response->mcp_raw, "runtime not initialized"))
    {
        printf("eval failed: runtime not initialized\n");
        if(response->last_load_target[0])
            printf("last load target: %s\n", response->last_load_target);
        if(response->stderr_tail[0])
            printf("stderr tail: %s\n", response->stderr_tail);
    }
    else if(strcmp(verb, "load") == 0)
    {
        char text[262144];
        if(!extract_first_text_into_buffer(response->mcp_raw, text, sizeof(text)) || text[0] == '\0')
        {
            printf("load produced no explicit confirmation for %s\n", request_target[0] ? request_target : "<unknown>");
            if(response->stderr_tail[0])
                printf("stderr tail: %s\n", response->stderr_tail);
        }
        else if(response->is_error)
        {
            trim_text_in_place(text);
            printf("load failed: %s\n", text);
            if(response->last_load_target[0])
                printf("last load target: %s\n", response->last_load_target);
            else if(request_target[0])
                printf("last load target: %s\n", request_target);
            if(response->stderr_tail[0])
                printf("stderr tail: %s\n", response->stderr_tail);
        }
    }
    else if(response->is_error && (strcmp(verb, "run") == 0 || strcmp(verb, "eval") == 0))
    {
        char text[262144];
        if(extract_first_text_into_buffer(response->mcp_raw, text, sizeof(text)) && text[0] != '\0')
        {
            trim_text_in_place(text);
            printf("%s failed: %s\n", verb, text);
        }
        else
            printf("%s failed: tic80ctl reported an error\n", verb[0] ? verb : "command");
        if(response->last_load_target[0])
            printf("last load target: %s\n", response->last_load_target);
        if(response->stderr_tail[0])
            printf("stderr tail: %s\n", response->stderr_tail);
    }
}

static bool print_run_success_human(const char* request_text, const ServerResponse* response)
{
    char verb[64] = {0};
    const char* rest = "";
    parse_command_text(request_text ? request_text : "", verb, sizeof(verb), &rest);

    if(response->is_error || strcmp(verb, "run") != 0)
        return false;

    char text[262144];
    if(extract_first_text_into_buffer(response->mcp_raw, text, sizeof(text)))
    {
        trim_text_in_place(text);
        if(text[0] != '\0' && strcmp(text, ">") != 0)
            return false;
    }

    printf("run started\n");
    return true;
}

static int print_mcp_response(const char* command_name, const char* request_text, const ServerResponse* response, bool json_output)
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
        printf("%s\n", sb.data ? sb.data : "{\"command\":\"run_command\",\"response\":{}}");
        sb_free(&sb);
    }
    else
    {
        char text[262144];
        char structured[262144];
        if(request_text && *request_text)
        {
            if(print_run_success_human(request_text, response))
                ;
            else if(extract_first_text(response->mcp_raw, text, sizeof(text)))
                printf("%s\n", text);
            else
                printf("%s\n", response->mcp_raw);
        }
        else if(extract_structured_content(response->mcp_raw, structured, sizeof(structured)))
        {
            if(!print_structured_lines(structured))
                printf("%s\n", structured);
        }
        else if(extract_first_text(response->mcp_raw, text, sizeof(text)))
            printf("%s\n", text);
        else
            printf("%s\n", response->mcp_raw);

        if(request_text && *request_text)
            print_command_diagnostics_human(request_text, response);
        else if(strcmp(command_name, "run_playtest_episode") == 0)
            print_playtest_diagnostics_human(response);
    }

    return response->is_error ? 1 : 0;
}

static int print_transport_error(const char* message)
{
    fprintf(stderr, "%s\n", message);
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

static int start_session(const char* self_path_value, const StatePaths* paths, bool json_output)
{
    ServerResponse status;
    if(query_status(paths, &status) && status.ok && status.running)
    {
        if(json_output)
        {
            printf("{\"running\":true,\"pid\":%ld,\"cwd\":", status.pid);
            StringBuilder sb;
            sb_init(&sb);
            sb_append_json_string(&sb, status.cwd);
            printf("%s,\"mode\":\"%s\",\"bin\":", sb.data, status.mode[0] ? status.mode : "xvfb");
            sb_free(&sb);
            sb_init(&sb);
            sb_append_json_string(&sb, status.bin);
            printf("%s}\n", sb.data);
            sb_free(&sb);
        }
        else
        {
            printf("tic80ctl: session already running (pid=%ld, mode=%s)\n", status.pid, status.mode[0] ? status.mode : "xvfb");
        }
        return 0;
    }

    cleanup_stale_session(paths);
    if(!mkdir_p(paths->state_dir))
        return print_transport_error("tic80ctl: failed to create state directory");

    char cwd[PATH_MAX];
    char tic80_bin[PATH_MAX];
    char launch_command[4096];
    if(!get_cwd(cwd, sizeof(cwd)))
        return print_transport_error("tic80ctl: failed to get current working directory");
    default_tic80_bin(tic80_bin, sizeof(tic80_bin));
    default_tic80_launch_command(launch_command, sizeof(launch_command));

    if(spawn_server_process(self_path_value, paths->state_dir, cwd, tic80_bin, launch_command) != 0)
        return print_transport_error("tic80ctl: failed to launch supervisor");

    while(true)
    {
        if(file_exists(paths->startup_error_path))
        {
            char* message = read_text_file_trimmed(paths->startup_error_path);
            cleanup_stale_session(paths);
            if(message && *message)
            {
                fprintf(stderr, "%s\n", message);
                free(message);
                return 1;
            }
            free(message);
            return print_transport_error("tic80ctl: session failed during startup");
        }

        if(file_exists(paths->ready_path) && query_status(paths, &status) && status.ok && status.running)
        {
            if(json_output)
            {
                StringBuilder cwd_json;
                StringBuilder bin_json;
                sb_init(&cwd_json);
                sb_init(&bin_json);
                sb_append_json_string(&cwd_json, status.cwd);
                sb_append_json_string(&bin_json, status.bin);
                printf("{\"running\":true,\"pid\":%ld,\"cwd\":%s,\"mode\":\"%s\",\"bin\":%s}\n",
                       status.pid,
                       cwd_json.data,
                       status.mode[0] ? status.mode : "xvfb",
                       bin_json.data);
                sb_free(&cwd_json);
                sb_free(&bin_json);
            }
            else
            {
                printf("started tic80ctl session (pid=%ld, mode=%s)\n", status.pid, status.mode[0] ? status.mode : "xvfb");
            }
            return 0;
        }

        sleep_ms(POLL_INTERVAL_MS);
    }
}

static int status_session(const StatePaths* paths, bool json_output)
{
    ServerResponse status;
    if(query_status(paths, &status) && status.ok && status.running)
    {
        if(json_output)
        {
            StringBuilder cwd_json;
            StringBuilder bin_json;
            StringBuilder out_json;
            StringBuilder err_json;
            sb_init(&cwd_json);
            sb_init(&bin_json);
            sb_init(&out_json);
            sb_init(&err_json);
            sb_append_json_string(&cwd_json, status.cwd);
            sb_append_json_string(&bin_json, status.bin);
            sb_append_json_string(&out_json, status.stdout_path);
            sb_append_json_string(&err_json, status.stderr_path);
            printf("{\"running\":true,\"pid\":%ld,\"cwd\":%s,\"mode\":\"%s\",\"bin\":%s,\"stdout\":%s,\"stderr\":%s}\n",
                   status.pid,
                   cwd_json.data,
                   status.mode[0] ? status.mode : "xvfb",
                   bin_json.data,
                   out_json.data,
                   err_json.data);
            sb_free(&cwd_json);
            sb_free(&bin_json);
            sb_free(&out_json);
            sb_free(&err_json);
        }
        else
        {
            printf("running pid=%ld mode=%s cwd=%s\n", status.pid, status.mode[0] ? status.mode : "xvfb", status.cwd);
            printf("stdout=%s\nstderr=%s\n", status.stdout_path, status.stderr_path);
        }
        return 0;
    }

    if(json_output)
        printf("{\"running\":false}\n");
    else
        printf("tic80ctl: no active session\n");

    cleanup_stale_session(paths);
    return 1;
}

static int stop_session(const StatePaths* paths, bool json_output)
{
    ServerResponse response;
    if(!send_stop_request(paths, &response) || !response.ok)
    {
        if(json_output)
            printf("{\"stopped\":false,\"running\":false}\n");
        else
            printf("tic80ctl: no active session\n");
        cleanup_stale_session(paths);
        return 1;
    }

    cleanup_stale_session(paths);

    if(json_output)
        printf("{\"stopped\":true,\"pid\":%ld}\n", response.pid);
    else
        printf("stopped tic80ctl session (pid=%ld)\n", response.pid);

    return 0;
}

static int send_tool_request(const StatePaths* paths, const char* request_json, const char* command_name, const char* request_text, bool json_output)
{
    ServerResponse response;
    if(!request_server_json(paths, request_json, &response))
        return print_transport_error("tic80ctl: no active session; run `tic80ctl start`");

    if(!response.ok)
        return print_transport_error(response.error[0] ? response.error : "tic80ctl: no active session; run `tic80ctl start`");

    return print_mcp_response(command_name, request_text, &response, json_output);
}

static int run_command_request(const StatePaths* paths, const char* value, const char* command_name, bool json_output)
{
    StringBuilder request;
    sb_init(&request);
    if(!build_request(&request, paths, "run_command"))
    {
        sb_free(&request);
        return print_transport_error("tic80ctl: no active session; run `tic80ctl start`");
    }
    sb_append(&request, ",\"value\":");
    sb_append_json_string(&request, value);
    sb_append(&request, "}");
    int rc = send_tool_request(paths, request.data, command_name, value, json_output);
    sb_free(&request);
    return rc;
}

static int screenshot_request(const StatePaths* paths, const char* value, bool json_output)
{
    StringBuilder request;
    sb_init(&request);
    if(!build_request(&request, paths, "screenshot"))
    {
        sb_free(&request);
        return print_transport_error("tic80ctl: no active session; run `tic80ctl start`");
    }
    if(value)
    {
        sb_append(&request, ",\"value\":");
        sb_append_json_string(&request, value);
    }
    sb_append(&request, "}");
    int rc = send_tool_request(paths, request.data, "capture_screenshot", NULL, json_output);
    sb_free(&request);
    return rc;
}

static int playtest_request(const StatePaths* paths, const char* script, bool has_timeout, long timeout_seconds, bool input_overlay, bool json_output)
{
    StringBuilder request;
    sb_init(&request);
    if(!build_request(&request, paths, "playtest"))
    {
        sb_free(&request);
        return print_transport_error("tic80ctl: no active session; run `tic80ctl start`");
    }

    sb_append(&request, ",\"script\":");
    sb_append_json_string(&request, script);
    if(has_timeout) sb_appendf(&request, ",\"timeout_seconds\":%ld", timeout_seconds);
    sb_appendf(&request, ",\"input_overlay\":%s}", input_overlay ? "true" : "false");
    int rc = send_tool_request(paths, request.data, "run_playtest_episode", NULL, json_output);
    sb_free(&request);
    return rc;
}

static int tool_request(const StatePaths* paths, const char* tool_name, const char* args_json, const char* command_name, bool json_output)
{
    StringBuilder request;
    sb_init(&request);
    if(!build_request(&request, paths, "tool"))
    {
        sb_free(&request);
        return print_transport_error("tic80ctl: no active session; run `tic80ctl start`");
    }

    sb_append(&request, ",\"tool\":");
    sb_append_json_string(&request, tool_name);
    sb_append(&request, ",\"args_json\":");
    sb_append(&request, args_json ? args_json : "{}");
    sb_append(&request, "}");
    int rc = send_tool_request(paths, request.data, command_name ? command_name : tool_name, NULL, json_output);
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

static bool parse_hex_fixed_values(const char* text, int expected, int* values)
{
    if((int)strlen(text) != expected) return false;
    for(int i = 0; i < expected; i++)
    {
        int value = hex_value(text[i]);
        if(value < 0) return false;
        values[i] = value;
    }
    return true;
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

typedef struct
{
    int tick;
    int value;
} Keyframe;

static bool parse_keyframes_expand(const char* text, int steps, int min, int max, int* values)
{
    char* copy = strdup(text);
    if(!copy) return false;
    Keyframe frames[64];
    int count = 0;
    char* save = NULL;
    for(char* tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
    {
        char* colon = strchr(tok, ':');
        if(!colon || count >= (int)(sizeof(frames) / sizeof(frames[0])))
        {
            free(copy);
            return false;
        }
        *colon = '\0';
        int tick = 0;
        int value = 0;
        if(!parse_int_strict(tok, &tick) || !parse_int_strict(colon + 1, &value)
            || tick < 0 || tick >= steps || value < min || value > max)
        {
            free(copy);
            return false;
        }
        if(count > 0 && tick <= frames[count - 1].tick)
        {
            free(copy);
            return false;
        }
        frames[count].tick = tick;
        frames[count].value = value;
        count++;
    }
    free(copy);
    if(count <= 0) return false;

    for(int i = 0; i < frames[0].tick; i++)
        values[i] = frames[0].value;

    for(int i = 0; i + 1 < count; i++)
    {
        int start_tick = frames[i].tick;
        int end_tick = frames[i + 1].tick;
        int start_value = frames[i].value;
        int delta = frames[i + 1].value - start_value;
        int span = end_tick - start_tick;
        for(int tick = start_tick; tick <= end_tick; tick++)
        {
            int num = delta * (tick - start_tick);
            int value = start_value + (num >= 0 ? (num + span / 2) / span : (num - span / 2) / span);
            values[tick] = value;
        }
    }

    for(int i = frames[count - 1].tick; i < steps; i++)
        values[i] = frames[count - 1].value;

    return true;
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
        if(count >= 8 || strlen(tok) != 8)
        {
            ok = false;
            break;
        }
        for(int i = 0; i < 8; i++)
            if(hex_value(tok[i]) < 0)
            {
                ok = false;
                break;
            }
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
        if(count >= 16 || strlen(tok) != 6)
        {
            ok = false;
            break;
        }
        for(int i = 0; i < 6; i++)
            if(hex_value(tok[i]) < 0)
            {
                ok = false;
                break;
            }
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
        if(count >= expected_tiles)
        {
            ok = false;
            break;
        }
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
        if(count >= 4)
        {
            ok = false;
            break;
        }
        int value = -1;
        if(strcmp(tok, "-") != 0 && (!parse_int_strict(tok, &value) || value < -1 || value > 63))
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
        char* note = NULL;
        char* sfx = NULL;
        char* command = NULL;
        char* save = NULL;
        note = strtok_r(copy, ":", &save);
        sfx = strtok_r(NULL, ":", &save);
        command = strtok_r(NULL, ":", &save);
        if(!note || !sfx || !command || strtok_r(NULL, ":", &save))
            ok = false;
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
    if(len < 2 || object_json[0] != '{' || object_json[len - 1] != '}')
        return false;
    return sb_append_n(sb, object_json + 1, len - 2);
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
    if(!consume_long_option(argc, argv, name, &value))
        return false;
    if(value < INT_MIN || value > INT_MAX)
        failf("tic80ctl: invalid value for %s", name);
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

static int build_and_send_tool(const StatePaths* paths, const char* tool_name, const char* command_name, bool json_output, StringBuilder* args)
{
    bool ok = sb_append(args, "}");
    if(!ok)
    {
        sb_free(args);
        return 1;
    }

    int rc = tool_request(paths, tool_name, args->data, command_name, json_output);
    sb_free(args);
    return rc;
}

static void init_object_args(StringBuilder* sb)
{
    sb_init(sb);
    sb_append(sb, "{");
}

static int handle_sfx_command(const StatePaths* paths, int argc, char** argv, bool json_output)
{
    if(argc <= 0)
        failf("tic80ctl: sfx requires a subcommand");

    const char* mode = argv[0];
    argc--;
    argv++;

    if(strcmp(mode, "wavetable") == 0)
    {
        int bank = -1;
        int waveform = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        if(!consume_int_option(&argc, &argv, "--waveform", &waveform))
            consume_int_option(&argc, &argv, "--wave", &waveform);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "sfx_set_wavetable", args_json, "sfx_wavetable", json_output);
        if(argc < 1 || argc > 2)
            failf("tic80ctl: sfx wavetable <sfx> [hex32]");
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx))
            failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        append_optional_int(&args, "waveform", waveform);
        if(argc == 1)
            return build_and_send_tool(paths, "sfx_get_wavetable", "sfx_wavetable", json_output, &args);
        int values[32];
        if(!parse_hex_fixed_values(argv[1], 32, values))
            failf("tic80ctl: sfx wavetable payload must be 32 hex digits");
        sb_append(&args, ",\"values\":");
        append_json_int_array(&args, values, 32);
        return build_and_send_tool(paths, "sfx_set_wavetable", "sfx_wavetable", json_output, &args);
    }

    if(strcmp(mode, "volume") == 0 || strcmp(mode, "wave") == 0 || strcmp(mode, "pitch") == 0)
    {
        int bank = -1;
        bool pitch16x = false;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        if(strcmp(mode, "pitch") == 0)
            consume_bool_option(&argc, &argv, "--pitch16x", &pitch16x);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
        {
            const char* tool = strcmp(mode, "volume") == 0 ? "sfx_set_volume_envelope"
                : strcmp(mode, "wave") == 0 ? "sfx_set_wave_envelope"
                : "sfx_set_pitch_envelope";
            return tool_request(paths, tool, args_json, tool, json_output);
        }
        if(argc < 1 || argc > 2)
            failf("tic80ctl: sfx %s <sfx> [payload]", mode);
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx))
            failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        if(argc == 1)
        {
            const char* tool = strcmp(mode, "volume") == 0 ? "sfx_get_volume_envelope"
                : strcmp(mode, "wave") == 0 ? "sfx_get_wave_envelope"
                : "sfx_get_pitch_envelope";
            return build_and_send_tool(paths, tool, tool, json_output, &args);
        }
        int values[30];
        bool parsed = strcmp(mode, "volume") == 0 ? parse_keyframes_expand(argv[1], 30, 0, 15, values)
            : strcmp(mode, "wave") == 0 ? parse_keyframes_expand(argv[1], 30, 0, 15, values)
            : parse_keyframes_expand(argv[1], 30, -8, 7, values);
        if(!parsed)
            failf("tic80ctl: invalid sfx %s payload", mode);
        sb_append(&args, ",\"values\":");
        append_json_int_array(&args, values, 30);
        if(strcmp(mode, "pitch") == 0)
            append_bool_json(&args, "pitch16x", pitch16x);
        return build_and_send_tool(paths,
                                   strcmp(mode, "volume") == 0 ? "sfx_set_volume_envelope"
                                   : strcmp(mode, "wave") == 0 ? "sfx_set_wave_envelope"
                                   : "sfx_set_pitch_envelope",
                                   mode,
                                   json_output,
                                   &args);
    }

    if(strcmp(mode, "arpeggio") == 0)
    {
        int bank = -1;
        bool reverse = false;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_bool_option(&argc, &argv, "--reverse", &reverse);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "sfx_set_arpeggio", args_json, "sfx_arpeggio", json_output);
        if(argc < 1 || argc > 2)
            failf("tic80ctl: sfx arpeggio <sfx> [csv]");
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx))
            failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        if(argc == 1)
            return build_and_send_tool(paths, "sfx_get_arpeggio", "sfx_arpeggio", json_output, &args);
        int parsed[30];
        int count = 0;
        if(!parse_var_int_csv(argv[1], 0, 15, parsed, 30, &count))
            failf("tic80ctl: invalid sfx arpeggio payload");
        for(int i = count; i < 30; i++)
            parsed[i] = parsed[count - 1];
        sb_append(&args, ",\"values\":");
        append_json_int_array(&args, parsed, 30);
        append_bool_json(&args, "reverse", reverse);
        return build_and_send_tool(paths, "sfx_set_arpeggio", "sfx_arpeggio", json_output, &args);
    }

    if(strcmp(mode, "panning") == 0)
    {
        int bank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "sfx_set_panning", args_json, "sfx_panning", json_output);
        if(argc < 1 || argc > 2)
            failf("tic80ctl: sfx panning <sfx> [left,right]");
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx))
            failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        if(argc == 1)
            return build_and_send_tool(paths, "sfx_get_panning", "sfx_panning", json_output, &args);
        char* copy = strdup(argv[1]);
        if(!copy) return 1;
        char* save = NULL;
        char* left_text = strtok_r(copy, ",", &save);
        char* right_text = strtok_r(NULL, ",", &save);
        bool left = false;
        bool right = false;
        bool ok = left_text && right_text && !strtok_r(NULL, ",", &save)
            && parse_bool_token(left_text, &left)
            && parse_bool_token(right_text, &right);
        free(copy);
        if(!ok)
            failf("tic80ctl: invalid sfx panning payload");
        append_bool_json(&args, "left", left);
        append_bool_json(&args, "right", right);
        return build_and_send_tool(paths, "sfx_set_panning", "sfx_panning", json_output, &args);
    }

    if(strcmp(mode, "speed") == 0)
    {
        int bank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "sfx_set_speed", args_json, "sfx_speed", json_output);
        if(argc < 1 || argc > 2)
            failf("tic80ctl: sfx speed <sfx> [value]");
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx))
            failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        if(argc == 1)
            return build_and_send_tool(paths, "sfx_get_speed", "sfx_speed", json_output, &args);
        int speed = 0;
        if(!parse_int_strict(argv[1], &speed))
            failf("tic80ctl: invalid sfx speed: %s", argv[1]);
        sb_appendf(&args, ",\"speed\":%d", speed);
        return build_and_send_tool(paths, "sfx_set_speed", "sfx_speed", json_output, &args);
    }

    if(strcmp(mode, "loop") == 0)
    {
        int bank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "sfx_set_loop_points", args_json, "sfx_loop", json_output);
        if(argc < 2 || argc > 3)
            failf("tic80ctl: sfx loop <sfx> <target> [start:size]");
        int sfx = 0;
        if(!parse_int_strict(argv[0], &sfx))
            failf("tic80ctl: invalid sfx id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"sfx\":%d", sfx);
        append_optional_int(&args, "bank", bank);
        sb_append(&args, ",\"target\":");
        sb_append_json_string(&args, argv[1]);
        if(argc == 2)
            return build_and_send_tool(paths, "sfx_get_loop_points", "sfx_loop", json_output, &args);
        char* copy = strdup(argv[2]);
        if(!copy) return 1;
        char* colon = strchr(copy, ':');
        int start = 0;
        int size = 0;
        bool ok = false;
        if(colon)
        {
            *colon = '\0';
            ok = parse_int_strict(copy, &start) && parse_int_strict(colon + 1, &size);
        }
        free(copy);
        if(!ok)
            failf("tic80ctl: invalid sfx loop payload, expected start:size");
        sb_appendf(&args, ",\"start\":%d,\"size\":%d", start, size);
        return build_and_send_tool(paths, "sfx_set_loop_points", "sfx_loop", json_output, &args);
    }

    failf("tic80ctl: unknown sfx subcommand: %s", mode);
    return 1;
}

static int handle_music_command(const StatePaths* paths, int argc, char** argv, bool json_output)
{
    if(argc <= 0)
        failf("tic80ctl: music requires a subcommand");

    const char* mode = argv[0];
    argc--;
    argv++;

    if(strcmp(mode, "track") == 0)
    {
        const char* args_json = NULL;
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "music_set_track", args_json, "music_track", json_output);
        if(argc < 1 || argc > 2)
            failf("tic80ctl: music track <track> [tempo,speed,rows]");
        int track = 0;
        if(!parse_int_strict(argv[0], &track))
            failf("tic80ctl: invalid track id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"track\":%d", track);
        if(argc == 1)
            return build_and_send_tool(paths, "music_get_track", "music_track", json_output, &args);
        int values[3];
        if(!parse_fixed_int_csv(argv[1], 3, 0, 999, values))
            failf("tic80ctl: invalid music track payload");
        sb_appendf(&args, ",\"tempo\":%d,\"speed\":%d,\"rows\":%d", values[0], values[1], values[2]);
        return build_and_send_tool(paths, "music_set_track", "music_track", json_output, &args);
    }

    if(strcmp(mode, "frame") == 0)
    {
        const char* args_json = NULL;
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "music_set_frame", args_json, "music_frame", json_output);
        if(argc < 2 || argc > 3)
            failf("tic80ctl: music frame <track> <frame> [p0,p1,p2,p3]");
        int track = 0;
        int frame = 0;
        if(!parse_int_strict(argv[0], &track) || !parse_int_strict(argv[1], &frame))
            failf("tic80ctl: invalid music frame selector");
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"track\":%d,\"frame\":%d", track, frame);
        if(argc == 2)
            return build_and_send_tool(paths, "music_get_frame", "music_frame", json_output, &args);
        sb_append(&args, ",\"patterns\":");
        if(!append_frame_patterns_json(&args, argv[2]))
        {
            sb_free(&args);
            failf("tic80ctl: invalid music frame payload");
        }
        return build_and_send_tool(paths, "music_set_frame", "music_frame", json_output, &args);
    }

    if(strcmp(mode, "row") == 0)
    {
        const char* args_json = NULL;
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "music_set_pattern_row", args_json, "music_row", json_output);
        if(argc < 2 || argc > 3)
            failf("tic80ctl: music row <pattern> <row> [note:sfx:cmd]");
        int pattern = 0;
        int row = 0;
        if(!parse_int_strict(argv[0], &pattern) || !parse_int_strict(argv[1], &row))
            failf("tic80ctl: invalid music row selector");
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"pattern\":%d,\"row\":%d", pattern, row);
        if(argc == 2)
            return build_and_send_tool(paths, "music_get_pattern_row", "music_row", json_output, &args);
        StringBuilder row_args;
        sb_init(&row_args);
        if(!append_music_rows_json(&row_args, argv[2], true, row)
            || !sb_append(&args, ",")
            || !append_object_body(&args, row_args.data))
        {
            sb_free(&row_args);
            sb_free(&args);
            failf("tic80ctl: invalid music row payload");
        }
        sb_free(&row_args);
        return build_and_send_tool(paths, "music_set_pattern_row", "music_row", json_output, &args);
    }

    if(strcmp(mode, "rows") == 0)
    {
        const char* args_json = NULL;
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "music_set_pattern_rows", args_json, "music_rows", json_output);
        if(argc < 2 || argc > 2)
            failf("tic80ctl: music rows <pattern> <row,row,...|row:note:sfx:cmd,...>");
        int pattern = 0;
        if(!parse_int_strict(argv[0], &pattern))
            failf("tic80ctl: invalid music pattern id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"pattern\":%d", pattern);
        if(strchr(argv[1], ':'))
        {
            StringBuilder rows_args;
            sb_init(&rows_args);
            if(!append_music_rows_json(&rows_args, argv[1], false, 0)
                || !sb_append(&args, ",")
                || !append_object_body(&args, rows_args.data))
            {
                sb_free(&rows_args);
                sb_free(&args);
                failf("tic80ctl: invalid music rows payload");
            }
            sb_free(&rows_args);
            return build_and_send_tool(paths, "music_set_pattern_rows", "music_rows", json_output, &args);
        }
        int rows[64];
        int row_count = 0;
        if(!parse_var_int_csv(argv[1], 0, 63, rows, 64, &row_count))
        {
            sb_free(&args);
            failf("tic80ctl: invalid music rows selector list");
        }
        sb_append(&args, ",\"rows\":");
        append_json_int_array(&args, rows, row_count);
        return build_and_send_tool(paths, "music_get_pattern_rows", "music_rows", json_output, &args);
    }

    failf("tic80ctl: unknown music subcommand: %s", mode);
    return 1;
}

static int handle_sprite_command(const StatePaths* paths, int argc, char** argv, bool json_output)
{
    if(argc <= 0)
        failf("tic80ctl: sprite requires a subcommand");

    const char* mode = argv[0];
    argc--;
    argv++;

    if(strcmp(mode, "tile") == 0)
    {
        const char* args_json = NULL;
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "sprite_set_sprite", args_json, "sprite_tile", json_output);
        if(argc < 1 || argc > 2)
            failf("tic80ctl: sprite tile <id> [row0,...,row7]");
        int id = 0;
        if(!parse_int_strict(argv[0], &id))
            failf("tic80ctl: invalid sprite id: %s", argv[0]);
        StringBuilder args;
        init_object_args(&args);
        sb_appendf(&args, "\"id\":%d", id);
        if(argc == 1)
            return build_and_send_tool(paths, "sprite_get_sprite", "sprite_tile", json_output, &args);
        sb_append(&args, ",\"rows\":");
        if(!append_sprite_rows_json(&args, argv[1]))
        {
            sb_free(&args);
            failf("tic80ctl: invalid sprite tile payload");
        }
        return build_and_send_tool(paths, "sprite_set_sprite", "sprite_tile", json_output, &args);
    }

    if(strcmp(mode, "region") == 0)
    {
        int bank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "sprite_set_spritesheet_region", args_json, "sprite_region", json_output);
        if(argc < 4 || argc > 5)
            failf("tic80ctl: sprite region <x> <y> <width> <height> [tile;tile;...]");
        int x = 0, y = 0, width = 0, height = 0;
        if(!parse_int_strict(argv[0], &x) || !parse_int_strict(argv[1], &y) || !parse_int_strict(argv[2], &width) || !parse_int_strict(argv[3], &height))
            failf("tic80ctl: invalid sprite region selector");
        StringBuilder args;
        init_object_args(&args);
        if(bank >= 0) sb_appendf(&args, "\"bank\":%d,", bank);
        sb_appendf(&args, "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d", x, y, width, height);
        if(argc == 4)
            return build_and_send_tool(paths, "sprite_get_spritesheet_region", "sprite_region", json_output, &args);
        sb_append(&args, ",\"sprites\":");
        if(!append_region_sprites_json(&args, argv[4], width * height))
        {
            sb_free(&args);
            failf("tic80ctl: invalid sprite region payload");
        }
        return build_and_send_tool(paths, "sprite_set_spritesheet_region", "sprite_region", json_output, &args);
    }

    if(strcmp(mode, "palette") == 0)
    {
        int bank = -1;
        int vbank = -1;
        const char* args_json = NULL;
        consume_bank_option(&argc, &argv, &bank);
        consume_int_option(&argc, &argv, "--vbank", &vbank);
        consume_args_json_option(&argc, &argv, &args_json);
        if(args_json)
            return tool_request(paths, "sprite_set_palette", args_json, "sprite_palette", json_output);
        if(argc > 1)
            failf("tic80ctl: sprite palette [RRGGBB,...]");
        StringBuilder args;
        init_object_args(&args);
        bool first = true;
        if(bank >= 0)
        {
            sb_appendf(&args, "\"bank\":%d", bank);
            first = false;
        }
        if(vbank >= 0)
        {
            if(!first) sb_append(&args, ",");
            sb_appendf(&args, "\"vbank\":%d", vbank);
            first = false;
        }
        if(argc == 0)
            return build_and_send_tool(paths, "sprite_get_palette", "sprite_palette", json_output, &args);
        if(!first) sb_append(&args, ",");
        sb_append(&args, "\"colors\":");
        if(!append_palette_json(&args, argv[0]))
        {
            sb_free(&args);
            failf("tic80ctl: invalid sprite palette payload");
        }
        return build_and_send_tool(paths, "sprite_set_palette", "sprite_palette", json_output, &args);
    }

    failf("tic80ctl: unknown sprite subcommand: %s", mode);
    return 1;
}

static int handle_map_command(const StatePaths* paths, int argc, char** argv, bool json_output)
{
    if(argc <= 0)
        failf("tic80ctl: map requires a subcommand");

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
            return tool_request(paths, tool, args_json, tool, json_output);
        }
        if(argc < 4 || argc > 5)
            failf("tic80ctl: map %s <x> <y> <width> <height> [payload]", mode);
        int x = 0, y = 0, width = 0, height = 0;
        if(!parse_int_strict(argv[0], &x) || !parse_int_strict(argv[1], &y) || !parse_int_strict(argv[2], &width) || !parse_int_strict(argv[3], &height))
            failf("tic80ctl: invalid map selector");
        StringBuilder args;
        init_object_args(&args);
        if(bank >= 0) sb_appendf(&args, "\"bank\":%d,", bank);
        sb_appendf(&args, "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d", x, y, width, height);
        if(argc == 4)
            return build_and_send_tool(paths, strcmp(mode, "rect") == 0 ? "map_get_rect" : "map_get_chunk", mode, json_output, &args);
        if(strcmp(mode, "rect") == 0)
        {
            int tile = 0;
            if(!parse_int_strict(argv[4], &tile))
            {
                sb_free(&args);
                failf("tic80ctl: invalid map rect tile value");
            }
            sb_appendf(&args, ",\"tile\":%d", tile);
            return build_and_send_tool(paths, "map_set_rect", "map_rect", json_output, &args);
        }
        int tiles[4096];
        int count = 0;
        if(!parse_var_int_csv(argv[4], 0, 65535, tiles, 4096, &count) || count != width * height)
        {
            sb_free(&args);
            failf("tic80ctl: map chunk payload must contain width*height tile ids");
        }
        sb_append(&args, ",\"tiles\":");
        append_json_int_array(&args, tiles, count);
        return build_and_send_tool(paths, "map_set_chunk", "map_chunk", json_output, &args);
    }

    failf("tic80ctl: unknown map subcommand: %s", mode);
    return 1;
}

int main(int argc, char** argv)
{
    const char* argv0 = argv[0];

    if(argc >= 2 && strcmp(argv[1], "--server") == 0)
    {
        const char* state_dir = NULL;
        const char* cwd = NULL;
        const char* tic80_bin = NULL;
        const char* launch_command = NULL;
        for(int i = 2; i + 1 < argc; i += 2)
        {
            if(strcmp(argv[i], "--state-dir") == 0) state_dir = argv[i + 1];
            else if(strcmp(argv[i], "--cwd") == 0) cwd = argv[i + 1];
            else if(strcmp(argv[i], "--tic80-bin") == 0) tic80_bin = argv[i + 1];
            else if(strcmp(argv[i], "--launch-command") == 0) launch_command = argv[i + 1];
        }
        if(!state_dir || !cwd || !tic80_bin) return 1;
        return server_main(state_dir, cwd, tic80_bin, launch_command ? launch_command : "");
    }

    bool json_output = false;
    argc--;
    argv++;

    while(argc > 0 && strcmp(argv[0], "--json") == 0)
    {
        json_output = true;
        argc--;
        argv++;
    }

    if(argc == 0)
    {
        print_usage(stderr);
        return 1;
    }

    if(strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-h") == 0)
        return print_help(argc > 1 ? argv[1] : NULL);

    char state_dir[PATH_MAX];
    default_state_dir(state_dir, sizeof(state_dir));
    StatePaths paths;
    init_state_paths(&paths, state_dir);

    char self[PATH_MAX];
    self_path(self, sizeof(self), argv0);

    const char* subcommand = argv[0];
    argc--;
    argv++;
    consume_json_flag(&argc, &argv, &json_output);

    if(strcmp(subcommand, "help") == 0)
        return print_help(argc > 0 ? argv[0] : NULL);

    if(strcmp(subcommand, "start") == 0)
    {
        if(argc != 0)
        {
            fprintf(stderr, "tic80ctl: start does not accept a cart argument; use `tic80ctl load <cart>` after start\n");
            return 1;
        }
        return start_session(self, &paths, json_output);
    }

    if(strcmp(subcommand, "status") == 0)
        return status_session(&paths, json_output);

    if(strcmp(subcommand, "stop") == 0)
        return stop_session(&paths, json_output);

    if(strcmp(subcommand, "cmd") == 0)
    {
        if(argc == 0)
        {
            fprintf(stderr, "tic80ctl: cmd requires a TIC-80 command string\n");
            return 1;
        }
        return run_command_request(&paths, argv[0], "run_command", json_output);
    }

    if(strcmp(subcommand, "lint-cart") == 0)
        return lint_cart_command(argc > 0 ? argv[0] : NULL, json_output);

    if(strcmp(subcommand, "lint-playtest-script") == 0)
        return lint_playtest_script_command(argc > 0 ? argv[0] : NULL, json_output);

    if(strcmp(subcommand, "load") == 0)
    {
        if(argc == 0)
        {
            fprintf(stderr, "tic80ctl: load requires a cart path\n");
            return 1;
        }

        StringBuilder command;
        sb_init(&command);
        sb_append(&command, "load ");
        sb_append(&command, argv[0]);
        int rc = run_command_request(&paths, command.data, "run_command", json_output);
        sb_free(&command);
        return rc;
    }

    if(strcmp(subcommand, "run") == 0)
        return run_command_request(&paths, "run", "run_command", json_output);

    if(strcmp(subcommand, "eval") == 0)
    {
        if(argc == 0)
        {
            fprintf(stderr, "tic80ctl: eval requires an expression\n");
            return 1;
        }

        StringBuilder command;
        sb_init(&command);
        sb_append(&command, "eval ");
        sb_append(&command, argv[0]);
        int rc = run_command_request(&paths, command.data, "run_command", json_output);
        sb_free(&command);
        return rc;
    }

    if(strcmp(subcommand, "screenshot") == 0)
        return screenshot_request(&paths, argc > 0 ? argv[0] : NULL, json_output);

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
            fprintf(stderr, "tic80ctl: unknown playtest option: %s\n", argv[0]);
            return 1;
        }

        if(!script_file)
        {
            fprintf(stderr, "tic80ctl: --script-file is required\n");
            return 1;
        }

        FILE* f = fopen(script_file, "r");
        if(!f)
        {
            fprintf(stderr, "tic80ctl: script file not found: %s\n", script_file);
            return 1;
        }

        StringBuilder script;
        sb_init(&script);
        char buffer[4096];
        while(fgets(buffer, sizeof(buffer), f))
        {
            if(!sb_append(&script, buffer))
            {
                fclose(f);
                sb_free(&script);
                return 1;
            }
        }
        fclose(f);

        int rc = playtest_request(&paths, script.data ? script.data : "", has_timeout, timeout_seconds, input_overlay, json_output);
        sb_free(&script);
        return rc;
    }

    if(strcmp(subcommand, "sfx") == 0)
        return handle_sfx_command(&paths, argc, argv, json_output);

    if(strcmp(subcommand, "music") == 0)
        return handle_music_command(&paths, argc, argv, json_output);

    if(strcmp(subcommand, "sprite") == 0)
        return handle_sprite_command(&paths, argc, argv, json_output);

    if(strcmp(subcommand, "map") == 0)
        return handle_map_command(&paths, argc, argv, json_output);

    print_usage(stderr);
    return 1;
}
