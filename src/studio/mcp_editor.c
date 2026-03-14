// MIT License

#include "mcp_editor.h"

#include "mcp_music.h"
#include "mcp_sfx.h"
#include "mcp_visual.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* dupString(const char* text)
{
    if(text == NULL)
        text = "";

    size_t len = strlen(text) + 1;
    char* copy = malloc(len);
    if(copy != NULL)
        memcpy(copy, text, len);

    return copy;
}

char* mcp_editor_tools_json(void)
{
    char* sfx = studio_sfx_tools_json();
    const char* music =
        "["
        "{\"name\":\"music_set_track\",\"description\":\"Set music track settings.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"track\":{\"type\":\"integer\"},\"tempo\":{\"type\":\"integer\"},\"speed\":{\"type\":\"integer\"},\"rows\":{\"type\":\"integer\"}},\"required\":[\"track\",\"tempo\",\"speed\",\"rows\"],\"additionalProperties\":false}},"
        "{\"name\":\"music_get_track\",\"description\":\"Read music track settings.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"track\":{\"type\":\"integer\"}},\"required\":[\"track\"],\"additionalProperties\":false}},"
        "{\"name\":\"music_set_frame\",\"description\":\"Set frame pattern assignments for a track.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"track\":{\"type\":\"integer\"},\"frame\":{\"type\":\"integer\"},\"patterns\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"},\"minItems\":4,\"maxItems\":4}},\"required\":[\"track\",\"frame\",\"patterns\"],\"additionalProperties\":false}},"
        "{\"name\":\"music_get_frame\",\"description\":\"Read frame pattern assignments for a track.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"track\":{\"type\":\"integer\"},\"frame\":{\"type\":\"integer\"}},\"required\":[\"track\",\"frame\"],\"additionalProperties\":false}},"
        "{\"name\":\"music_set_pattern_row\",\"description\":\"Set one sparse pattern row.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"integer\"},\"row\":{\"type\":\"integer\"},\"note\":{\"type\":\"string\"},\"octave\":{\"type\":\"integer\"},\"sfx\":{\"type\":\"integer\"},\"command\":{\"type\":\"string\"},\"param1\":{\"type\":\"integer\"},\"param2\":{\"type\":\"integer\"}},\"required\":[\"pattern\",\"row\"],\"additionalProperties\":false}},"
        "{\"name\":\"music_get_pattern_row\",\"description\":\"Read one pattern row.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"integer\"},\"row\":{\"type\":\"integer\"}},\"required\":[\"pattern\",\"row\"],\"additionalProperties\":false}},"
        "{\"name\":\"music_set_pattern_rows\",\"description\":\"Set multiple sparse pattern rows.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"integer\"},\"rows\":{\"type\":\"array\"}},\"required\":[\"pattern\",\"rows\"],\"additionalProperties\":false}},"
        "{\"name\":\"music_get_pattern_rows\",\"description\":\"Read selected pattern rows.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"integer\"},\"rows\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"}}},\"required\":[\"pattern\",\"rows\"],\"additionalProperties\":false}}"
        "]";
    const char* visual =
        "["
        "{\"name\":\"sprite_set_sprite\",\"description\":\"Set one sprite by flat sprite id.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"rows\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"minItems\":8,\"maxItems\":8}},\"required\":[\"id\",\"rows\"],\"additionalProperties\":false}},"
        "{\"name\":\"sprite_get_sprite\",\"description\":\"Read one sprite by flat sprite id.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"}},\"required\":[\"id\"],\"additionalProperties\":false}},"
        "{\"name\":\"sprite_set_spritesheet_region\",\"description\":\"Set a spritesheet region by bank and coordinates.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"bank\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"},\"sprites\":{\"type\":\"array\"}},\"required\":[\"x\",\"y\",\"width\",\"height\",\"sprites\"],\"additionalProperties\":false}},"
        "{\"name\":\"sprite_get_spritesheet_region\",\"description\":\"Read a spritesheet region by bank and coordinates.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"bank\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"}},\"required\":[\"x\",\"y\",\"width\",\"height\"],\"additionalProperties\":false}},"
        "{\"name\":\"sprite_set_palette\",\"description\":\"Set a sprite palette bank.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"bank\":{\"type\":\"integer\"},\"vbank\":{\"type\":\"integer\"},\"colors\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"minItems\":16,\"maxItems\":16}},\"required\":[\"colors\"],\"additionalProperties\":false}},"
        "{\"name\":\"sprite_get_palette\",\"description\":\"Read a sprite palette bank.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"bank\":{\"type\":\"integer\"},\"vbank\":{\"type\":\"integer\"}},\"additionalProperties\":false}},"
        "{\"name\":\"map_set_rect\",\"description\":\"Fill a map rectangle with one tile id.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"bank\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"},\"tile\":{\"type\":\"integer\"}},\"required\":[\"x\",\"y\",\"width\",\"height\",\"tile\"],\"additionalProperties\":false}},"
        "{\"name\":\"map_get_rect\",\"description\":\"Read a map rectangle.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"bank\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"}},\"required\":[\"x\",\"y\",\"width\",\"height\"],\"additionalProperties\":false}},"
        "{\"name\":\"map_set_chunk\",\"description\":\"Write a localized map chunk.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"bank\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"},\"tiles\":{\"type\":\"array\"}},\"required\":[\"x\",\"y\",\"width\",\"height\",\"tiles\"],\"additionalProperties\":false}},"
        "{\"name\":\"map_get_chunk\",\"description\":\"Read a localized map chunk.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"bank\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"}},\"required\":[\"x\",\"y\",\"width\",\"height\"],\"additionalProperties\":false}}"
        "]";

    if(sfx == NULL)
        return NULL;

    size_t len = strlen(sfx) + strlen(music) + strlen(visual) + 4;
    char* merged = calloc(len, 1);
    if(merged != NULL)
        snprintf(merged, len, "[%.*s,%.*s,%.*s]",
                 (int)strlen(sfx) - 2, sfx + 1,
                 (int)strlen(music) - 2, music + 1,
                 (int)strlen(visual) - 2, visual + 1);

    free(sfx);
    return merged;
}

char* mcp_editor_call_tool(Studio* studio, const char* toolName, const char* argsJson, bool* invalidParams, bool* handled)
{
    char* result = NULL;

    if(handled) *handled = false;
    if(invalidParams) *invalidParams = false;

    if(studio == NULL || toolName == NULL)
        return NULL;

    if(strncmp(toolName, "sfx_", 4) == 0)
    {
        if(handled) *handled = true;
        return studio_sfx_tool_mcp(studio, toolName, argsJson ? argsJson : "{}", invalidParams);
    }

    if(strncmp(toolName, "music_", 6) == 0)
    {
        if(handled) *handled = true;
        return studio_music_tool_mcp(studio, toolName, argsJson ? argsJson : "{}", invalidParams);
    }

    if(strncmp(toolName, "sprite_", 7) == 0 || strncmp(toolName, "map_", 4) == 0)
    {
        if(handled) *handled = true;
        return studio_visual_tool_mcp(studio, toolName, argsJson ? argsJson : "{}", invalidParams);
    }

    result = dupString(NULL);
    return result;
}
