// MIT License

#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#ifndef JSMN_STATIC
#define JSMN_STATIC
#endif

#include "jsmn.h"

typedef struct
{
    char* data;
    size_t len;
    size_t cap;
} McpStringBuilder;

static inline void mcpSbInit(McpStringBuilder* sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static inline void mcpSbFree(McpStringBuilder* sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static inline bool mcpSbReserve(McpStringBuilder* sb, size_t extra)
{
    const size_t need = sb->len + extra + 1;
    if(need <= sb->cap)
        return true;

    size_t cap = sb->cap ? sb->cap : 256;
    while(cap < need)
        cap *= 2;

    char* data = realloc(sb->data, cap);
    if(data == NULL)
        return false;

    sb->data = data;
    sb->cap = cap;
    return true;
}

static inline bool mcpSbAppendN(McpStringBuilder* sb, const char* text, size_t len)
{
    if(!mcpSbReserve(sb, len))
        return false;

    memcpy(sb->data + sb->len, text, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return true;
}

static inline bool mcpSbAppend(McpStringBuilder* sb, const char* text)
{
    return mcpSbAppendN(sb, text, strlen(text));
}

static inline bool mcpSbAppendf(McpStringBuilder* sb, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);

    if(needed < 0 || !mcpSbReserve(sb, (size_t)needed))
    {
        va_end(args);
        return false;
    }

    vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, args);
    va_end(args);
    sb->len += (size_t)needed;
    return true;
}

static inline bool mcpSbAppendJsonString(McpStringBuilder* sb, const char* text)
{
    if(text == NULL)
        text = "";

    if(!mcpSbAppend(sb, "\""))
        return false;

    for(const unsigned char* p = (const unsigned char*)text; *p; p++)
    {
        switch(*p)
        {
        case '\\':
            if(!mcpSbAppend(sb, "\\\\")) return false;
            break;
        case '"':
            if(!mcpSbAppend(sb, "\\\"")) return false;
            break;
        case '\n':
            if(!mcpSbAppend(sb, "\\n")) return false;
            break;
        case '\r':
            if(!mcpSbAppend(sb, "\\r")) return false;
            break;
        case '\t':
            if(!mcpSbAppend(sb, "\\t")) return false;
            break;
        default:
            if(*p < 0x20)
            {
                if(!mcpSbAppendf(sb, "\\u%04x", *p)) return false;
            }
            else if(!mcpSbAppendN(sb, (const char*)p, 1)) return false;
            break;
        }
    }

    return mcpSbAppend(sb, "\"");
}

static inline const char* mcpSkipWs(const char* ptr)
{
    while(*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')
        ptr++;
    return ptr;
}

static inline bool mcpJsonParse(const char* json, jsmntok_t** tokens, int* count)
{
    jsmn_parser parser;
    jsmn_init(&parser);

    int capacity = 256;
    *tokens = malloc(sizeof(**tokens) * (size_t)capacity);
    if(*tokens == NULL)
        return false;

    const int size = (int)strlen(json);
    while((*count = jsmn_parse(&parser, json, size, *tokens, capacity)) == JSMN_ERROR_NOMEM)
    {
        capacity *= 2;
        jsmntok_t* next = realloc(*tokens, sizeof(**tokens) * (size_t)capacity);
        if(next == NULL)
        {
            free(*tokens);
            *tokens = NULL;
            return false;
        }
        *tokens = next;
    }

    return *count >= 0;
}

static inline int mcpJsonFindObjectValue(const char* json, const jsmntok_t* tokens, int count, int objectIndex, const char* key)
{
    if(objectIndex < 0 || objectIndex >= count || tokens[objectIndex].type != JSMN_OBJECT)
        return -1;

    for(int i = objectIndex + 1; i + 1 < count; i++)
        if(tokens[i].parent == objectIndex
            && tokens[i].type == JSMN_STRING
            && (tokens[i].end - tokens[i].start) == (int)strlen(key)
            && strncmp(json + tokens[i].start, key, (size_t)(tokens[i].end - tokens[i].start)) == 0)
            return i + 1;

    return -1;
}

static inline bool mcpJsonCopyRaw(const char* json, const jsmntok_t* tok, char* dst, size_t size)
{
    const int len = tok->end - tok->start;
    if(len < 0 || (size_t)len >= size)
        return false;

    memcpy(dst, json + tok->start, (size_t)len);
    dst[len] = '\0';
    return true;
}

static inline bool mcpJsonTokenEq(const char* json, const jsmntok_t* tok, const char* text)
{
    const size_t len = strlen(text);
    return tok->type == JSMN_STRING
        && (size_t)(tok->end - tok->start) == len
        && strncmp(json + tok->start, text, len) == 0;
}

static inline bool mcpJsonCopyString(const char* json, const jsmntok_t* tok, char* dst, size_t size)
{
    if(tok->type != JSMN_STRING)
        return false;

    size_t out = 0;
    for(int i = tok->start; i < tok->end; i++)
    {
        char c = json[i];
        if(c == '\\')
        {
            i++;
            if(i >= tok->end)
                return false;
            switch(json[i])
            {
            case '\\': c = '\\'; break;
            case '"': c = '"'; break;
            case '/': c = '/'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            default: return false;
            }
        }

        if(out + 1 >= size)
            return false;
        dst[out++] = c;
    }

    dst[out] = '\0';
    return true;
}

static inline bool mcpJsonCopyInt(const char* json, const jsmntok_t* tok, int* value)
{
    char buf[64];
    char* end = NULL;

    if(tok->type != JSMN_PRIMITIVE || !mcpJsonCopyRaw(json, tok, buf, sizeof buf))
        return false;

    long parsed = strtol(buf, &end, 10);
    if(end == NULL || *end != '\0')
        return false;

    *value = (int)parsed;
    return true;
}

static inline bool mcpJsonCopyBool(const char* json, const jsmntok_t* tok, bool* value)
{
    char buf[16];
    if(tok->type != JSMN_PRIMITIVE || !mcpJsonCopyRaw(json, tok, buf, sizeof buf))
        return false;

    if(strcmp(buf, "true") == 0)
    {
        *value = true;
        return true;
    }
    if(strcmp(buf, "false") == 0)
    {
        *value = false;
        return true;
    }
    return false;
}

static inline bool mcpJsonGetRequiredInt(const char* json, const jsmntok_t* tokens, int count, int objectIndex, const char* key, int* value)
{
    const int tok = mcpJsonFindObjectValue(json, tokens, count, objectIndex, key);
    return tok >= 0 && mcpJsonCopyInt(json, &tokens[tok], value);
}

static inline bool mcpJsonGetOptionalInt(const char* json, const jsmntok_t* tokens, int count, int objectIndex, const char* key, int* value)
{
    const int tok = mcpJsonFindObjectValue(json, tokens, count, objectIndex, key);
    return tok >= 0 && mcpJsonCopyInt(json, &tokens[tok], value);
}

static inline bool mcpJsonGetRequiredBool(const char* json, const jsmntok_t* tokens, int count, int objectIndex, const char* key, bool* value)
{
    const int tok = mcpJsonFindObjectValue(json, tokens, count, objectIndex, key);
    return tok >= 0 && mcpJsonCopyBool(json, &tokens[tok], value);
}

static inline char* mcpResultText(bool isError, const char* text)
{
    McpStringBuilder sb;
    mcpSbInit(&sb);
    if(mcpSbAppend(&sb, "{\"content\":[{\"type\":\"text\",\"text\":")
        && mcpSbAppendJsonString(&sb, text ? text : "")
        && mcpSbAppendf(&sb, "}],\"isError\":%s}", isError ? "true" : "false"))
        return sb.data;

    mcpSbFree(&sb);
    return NULL;
}

static inline char* mcpResultStructured(const char* summary, const char* structuredJson)
{
    McpStringBuilder sb;
    mcpSbInit(&sb);
    if(mcpSbAppend(&sb, "{\"content\":[{\"type\":\"text\",\"text\":")
        && mcpSbAppendJsonString(&sb, summary ? summary : "")
        && mcpSbAppend(&sb, "}],\"structuredContent\":")
        && mcpSbAppend(&sb, structuredJson ? structuredJson : "{}")
        && mcpSbAppend(&sb, ",\"isError\":false}"))
        return sb.data;

    mcpSbFree(&sb);
    return NULL;
}
