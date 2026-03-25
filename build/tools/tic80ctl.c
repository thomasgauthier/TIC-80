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

#define STARTUP_TIMEOUT_MS 15000
#define MCP_TIMEOUT_MS 10000

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

static const char* usage_text =
    "usage: tic80ctl [--json] <command> [args...]\n"
    "\n"
    "commands:\n"
    "  start\n"
    "  status\n"
    "  stop\n"
    "  cmd \"<tic80 command>\"\n"
    "  load <cart>\n"
    "  run\n"
    "  eval \"<expr>\"\n"
    "  screenshot [path]\n"
    "  playtest --script-file <file> [--timeout <seconds>] [--input-overlay|--no-input-overlay]\n";

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
    unlink_session_files(&server->paths);
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

static bool wait_for_mcp_response(Server* server, int id, int timeout_ms, char* out, size_t out_size)
{
    int fd = fileno(server->child_stdout);
    int waited = 0;

    while(waited < timeout_ms)
    {
        if(!child_is_running(server))
            return false;

        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int ready = select(fd + 1, &set, NULL, NULL, &tv);
        if(ready < 0)
        {
            if(errno == EINTR) continue;
            return false;
        }

        if(ready == 0)
        {
            waited += 100;
            continue;
        }

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
    if(!wait_for_mcp_response(server, init_id, MCP_TIMEOUT_MS, response, sizeof(response)))
        return false;

    if(strstr(response, "\"result\"") == NULL || strstr(response, "\"serverInfo\"") == NULL)
        return false;

    if(!send_notification(server, "notifications/initialized"))
        return false;

    int tools_id = server->next_id++;
    if(!send_jsonrpc(server, tools_id, "tools/list", "{}"))
        return false;
    if(!wait_for_mcp_response(server, tools_id, MCP_TIMEOUT_MS, response, sizeof(response)))
        return false;

    if(strstr(response, "\"run_command\"") == NULL || strstr(response, "\"capture_screenshot\"") == NULL)
        return false;

    sleep_ms(200);
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
              wait_for_mcp_response(server, id, MCP_TIMEOUT_MS, out, out_size);
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

    respond_error(io, "tic80ctl: malformed request");
    return true;
}

static int server_main(const char* state_dir, const char* cwd, const char* tic80_bin, const char* launch_command)
{
    Server server;
    if(!setup_server(&server, state_dir, cwd, tic80_bin, launch_command))
        return 1;

    if(chdir(cwd) != 0)
    {
        write_startup_error(&server, "tic80ctl: failed to enter session working directory");
        server_cleanup(&server);
        unlink_session_files(&server.paths);
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
        unlink_session_files(&server.paths);
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

static void print_usage(void)
{
    fputs(usage_text, stderr);
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

    int waited = 0;
    while(waited < STARTUP_TIMEOUT_MS)
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

        sleep_ms(100);
        waited += 100;
    }

    cleanup_stale_session(paths);
    return print_transport_error("tic80ctl: session failed during startup");
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
        print_usage();
        return 1;
    }

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

    print_usage();
    return 1;
}
