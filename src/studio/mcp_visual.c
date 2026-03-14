// MIT License

#include "mcp_visual.h"

#define JSMN_PARENT_LINKS
#include "mcp_tool_helpers.h"
#include "studio.h"

static bool getOptionalInt(const char* json, const jsmntok_t* tokens, int count, int objectIndex, const char* key, int* value)
{
    const int tok = mcpJsonFindObjectValue(json, tokens, count, objectIndex, key);
    if(tok < 0)
        return false;

    return mcpJsonCopyInt(json, &tokens[tok], value);
}

static bool getRequiredInt(const char* json, const jsmntok_t* tokens, int count, int objectIndex, const char* key, int* value)
{
    const int tok = mcpJsonFindObjectValue(json, tokens, count, objectIndex, key);
    return tok >= 0 && mcpJsonCopyInt(json, &tokens[tok], value);
}

static bool getRequiredBool(const char* json, const jsmntok_t* tokens, int count, int objectIndex, const char* key, bool* value)
{
    const int tok = mcpJsonFindObjectValue(json, tokens, count, objectIndex, key);
    return tok >= 0 && mcpJsonCopyBool(json, &tokens[tok], value);
}

static tic_tile* getTileByGlobalId(Studio* studio, int id)
{
    if(studio == NULL || id < 0 || id >= TIC_SPRITES)
        return NULL;

    const int bank = id / TIC_BANK_SPRITES;
    const int local = id % TIC_BANK_SPRITES;
    tic_tiles* tiles = getBankTilesData(studio, bank);
    return tiles ? &tiles->data[local] : NULL;
}

static tic_tile* getTileByRegion(Studio* studio, int bank, int x, int y)
{
    if(studio == NULL || bank < 0 || bank >= TIC_BANKS)
        return NULL;

    if(x < 0 || x >= TIC_SPRITESHEET_COLS || y < 0 || y >= TIC_SPRITESHEET_COLS)
        return NULL;

    tic_tiles* tiles = getBankTilesData(studio, bank);
    return tiles ? &tiles->data[x + y * TIC_SPRITESHEET_COLS] : NULL;
}

static void syncVisualBank(Studio* studio, int bank, bool syncTiles, bool syncMap, bool syncPalette)
{
    if(studio == NULL || bank < 0 || bank >= TIC_BANKS)
        return;

    if(syncTiles)
        studioSyncTilesBank(studio, bank);

    if(syncMap)
        studioSyncMapBank(studio, bank);

    if(syncPalette)
        studioSyncPaletteBank(studio, bank, false);
}

static bool parseTileRows(const char* json, const jsmntok_t* tokens, int count, int objectIndex, tic_tile* tile)
{
    const int rowsTok = mcpJsonFindObjectValue(json, tokens, count, objectIndex, "rows");
    if(rowsTok < 0 || tokens[rowsTok].type != JSMN_ARRAY || tokens[rowsTok].size != TIC_SPRITESIZE)
        return false;

    memset(tile, 0, sizeof(*tile));

    int rowCount = 0;
    for(int i = rowsTok + 1; i < count && rowCount < TIC_SPRITESIZE; i++)
    {
        if(tokens[i].parent != rowsTok)
            continue;

        char row[16];
        if(!mcpJsonCopyString(json, &tokens[i], row, sizeof row) || strlen(row) != TIC_SPRITESIZE)
            return false;

        for(int x = 0; x < TIC_SPRITESIZE; x++)
        {
            char c = row[x];
            int value = 0;
            if(c >= '0' && c <= '9') value = c - '0';
            else if(c >= 'a' && c <= 'f') value = 10 + c - 'a';
            else if(c >= 'A' && c <= 'F') value = 10 + c - 'A';
            else return false;

            tic_tool_poke4(tile->data, x + rowCount * TIC_SPRITESIZE, (u8)value);
        }

        rowCount++;
    }

    return rowCount == TIC_SPRITESIZE;
}

static char* buildTileStructuredResult(int bank, int id, const tic_tile* tile, const char* summary)
{
    McpStringBuilder rows;
    McpStringBuilder data;
    mcpSbInit(&rows);
    mcpSbInit(&data);

    if(!mcpSbAppend(&rows, "["))
        goto fail;

    for(int y = 0; y < TIC_SPRITESIZE; y++)
    {
        char row[TIC_SPRITESIZE + 1];
        for(int x = 0; x < TIC_SPRITESIZE; x++)
        {
            const int value = tic_tool_peek4(tile->data, x + y * TIC_SPRITESIZE);
            row[x] = "0123456789abcdef"[value & 0xf];
        }
        row[TIC_SPRITESIZE] = '\0';

        if(y > 0 && !mcpSbAppend(&rows, ","))
            goto fail;
        if(!mcpSbAppendJsonString(&rows, row))
            goto fail;
    }

    if(!mcpSbAppend(&rows, "]"))
        goto fail;

    if(!mcpSbAppendf(&data, "{\"bank\":%d,\"id\":%d,\"rows\":%s}", bank, id, rows.data))
        goto fail;

    char* result = mcpResultStructured(summary, data.data);
    mcpSbFree(&rows);
    mcpSbFree(&data);
    return result;

fail:
    mcpSbFree(&rows);
    mcpSbFree(&data);
    return NULL;
}

static char* buildSpritesheetRegionStructuredResult(Studio* studio, int bank, int x, int y, int width, int height, const char* summary)
{
    tic_tiles* tiles = getBankTilesData(studio, bank);
    McpStringBuilder sprites;
    McpStringBuilder data;
    mcpSbInit(&sprites);
    mcpSbInit(&data);

    if(tiles == NULL)
        return mcpResultText(true, "invalid spritesheet region");

    if(!mcpSbAppend(&sprites, "["))
        goto fail;

    for(int index = 0; index < width * height; index++)
    {
        int tileX = x + (index % width);
        int tileY = y + (index / width);
        tic_tile* tile = &tiles->data[tileX + tileY * TIC_SPRITESHEET_COLS];
        McpStringBuilder rows;
        mcpSbInit(&rows);

        if(index > 0 && !mcpSbAppend(&sprites, ","))
        {
            mcpSbFree(&rows);
            goto fail;
        }

        if(!mcpSbAppend(&rows, "["))
        {
            mcpSbFree(&rows);
            goto fail;
        }

        for(int row = 0; row < TIC_SPRITESIZE; row++)
        {
            char line[TIC_SPRITESIZE + 1];
            for(int col = 0; col < TIC_SPRITESIZE; col++)
            {
                int value = tic_tool_peek4(tile->data, col + row * TIC_SPRITESIZE);
                line[col] = "0123456789abcdef"[value & 0xf];
            }
            line[TIC_SPRITESIZE] = '\0';

            if(row > 0 && !mcpSbAppend(&rows, ","))
            {
                mcpSbFree(&rows);
                goto fail;
            }
            if(!mcpSbAppendJsonString(&rows, line))
            {
                mcpSbFree(&rows);
                goto fail;
            }
        }

        if(!mcpSbAppend(&rows, "]")
            || !mcpSbAppendf(&sprites, "{\"index\":%d,\"rows\":%s}", bank * TIC_BANK_SPRITES + tileX + tileY * TIC_SPRITESHEET_COLS, rows.data))
        {
            mcpSbFree(&rows);
            goto fail;
        }

        mcpSbFree(&rows);
    }

    if(!mcpSbAppend(&sprites, "]")
        || !mcpSbAppendf(&data, "{\"bank\":%d,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"sprites\":%s}", bank, x, y, width, height, sprites.data))
        goto fail;

    mcpSbFree(&sprites);
    return mcpResultStructured(summary, data.data);

fail:
    mcpSbFree(&sprites);
    mcpSbFree(&data);
    return NULL;
}

static char* handleSetSprite(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int id = -1;
    if(!getRequiredInt(json, tokens, count, root, "id", &id))
    {
        *invalidParams = true;
        return NULL;
    }

    tic_tile* tile = getTileByGlobalId(studio, id);
    if(tile == NULL)
        return mcpResultText(true, "invalid sprite id");

    if(!parseTileRows(json, tokens, count, root, tile))
    {
        *invalidParams = true;
        return NULL;
    }

    syncVisualBank(studio, id / TIC_BANK_SPRITES, true, false, false);
    return mcpResultText(false, "updated sprite");
}

static char* handleGetSprite(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int id = -1;
    if(!getRequiredInt(json, tokens, count, root, "id", &id))
    {
        *invalidParams = true;
        return NULL;
    }

    tic_tile* tile = getTileByGlobalId(studio, id);
    if(tile == NULL)
        return mcpResultText(true, "invalid sprite id");

    return buildTileStructuredResult(id / TIC_BANK_SPRITES, id, tile, "fetched sprite");
}

static char* handleSetSpritesheetRegion(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int bank = 0;
    int x = -1;
    int y = -1;
    int width = 0;
    int height = 0;
    if(!getOptionalInt(json, tokens, count, root, "bank", &bank))
        bank = 0;
    if(!getRequiredInt(json, tokens, count, root, "x", &x)
        || !getRequiredInt(json, tokens, count, root, "y", &y)
        || !getRequiredInt(json, tokens, count, root, "width", &width)
        || !getRequiredInt(json, tokens, count, root, "height", &height))
    {
        *invalidParams = true;
        return NULL;
    }

    if(bank < 0 || bank >= TIC_BANKS || x < 0 || y < 0 || width <= 0 || height <= 0
        || x + width > TIC_SPRITESHEET_COLS || y + height > TIC_SPRITESHEET_COLS)
        return mcpResultText(true, "invalid spritesheet region");

    const int spritesTok = mcpJsonFindObjectValue(json, tokens, count, root, "sprites");
    if(spritesTok < 0 || tokens[spritesTok].type != JSMN_ARRAY || tokens[spritesTok].size != width * height)
    {
        *invalidParams = true;
        return NULL;
    }

    for(int index = 0; index < width * height; index++)
    {
        int spriteTok = -1;
        int tileX = x + (index % width);
        int tileY = y + (index / width);
        tic_tile* tile = getTileByRegion(studio, bank, tileX, tileY);

        if(tile == NULL)
            return mcpResultText(true, "invalid spritesheet region");

        for(int i = spritesTok + 1, seen = 0; i < count; i++)
        {
            if(tokens[i].parent != spritesTok)
                continue;
            if(seen++ == index)
            {
                spriteTok = i;
                break;
            }
        }

        if(spriteTok < 0 || tokens[spriteTok].type != JSMN_OBJECT || !parseTileRows(json, tokens, count, spriteTok, tile))
        {
            *invalidParams = true;
            return NULL;
        }
    }

    syncVisualBank(studio, bank, true, false, false);
    return mcpResultText(false, "updated spritesheet region");
}

static char* handleGetSpritesheetRegion(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int bank = 0;
    int x = -1;
    int y = -1;
    int width = 0;
    int height = 0;
    if(!getOptionalInt(json, tokens, count, root, "bank", &bank))
        bank = 0;
    if(!getRequiredInt(json, tokens, count, root, "x", &x)
        || !getRequiredInt(json, tokens, count, root, "y", &y)
        || !getRequiredInt(json, tokens, count, root, "width", &width)
        || !getRequiredInt(json, tokens, count, root, "height", &height))
    {
        *invalidParams = true;
        return NULL;
    }

    if(bank < 0 || bank >= TIC_BANKS || x < 0 || y < 0 || width <= 0 || height <= 0
        || x + width > TIC_SPRITESHEET_COLS || y + height > TIC_SPRITESHEET_COLS)
        return mcpResultText(true, "invalid spritesheet region");

    return buildSpritesheetRegionStructuredResult(studio, bank, x, y, width, height, "fetched spritesheet region");
}

static char* handleSetPalette(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int bank = 0;
    int vbank = 0;
    if(!getOptionalInt(json, tokens, count, root, "bank", &bank))
        bank = 0;
    if(!getOptionalInt(json, tokens, count, root, "vbank", &vbank))
        vbank = 0;

    if(bank < 0 || bank >= TIC_BANKS || vbank < 0 || vbank >= TIC_PALETTES)
        return mcpResultText(true, "invalid palette bank");

    const int colorsTok = mcpJsonFindObjectValue(json, tokens, count, root, "colors");
    if(colorsTok < 0 || tokens[colorsTok].type != JSMN_ARRAY || tokens[colorsTok].size != TIC_PALETTE_SIZE)
    {
        *invalidParams = true;
        return NULL;
    }

    tic_palette* pal = getBankPaletteData(studio, bank, vbank != 0);
    if(pal == NULL)
        return mcpResultText(true, "invalid palette bank");
    int idx = 0;
    for(int i = colorsTok + 1; i < count && idx < TIC_PALETTE_SIZE; i++)
    {
        if(tokens[i].parent != colorsTok)
            continue;

        char color[8];
        if(!mcpJsonCopyString(json, &tokens[i], color, sizeof color) || strlen(color) != 6)
        {
            *invalidParams = true;
            return NULL;
        }

        unsigned int rgb = 0;
        if(sscanf(color, "%06x", &rgb) != 1)
        {
            *invalidParams = true;
            return NULL;
        }

        pal->colors[idx].r = (rgb >> 16) & 0xff;
        pal->colors[idx].g = (rgb >> 8) & 0xff;
        pal->colors[idx].b = rgb & 0xff;
        idx++;
    }

    if(vbank == 0) syncVisualBank(studio, bank, false, false, true);
    else studioSyncPaletteBank(studio, bank, true);
    return mcpResultText(false, "updated palette");
}

static char* handleGetPalette(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int bank = 0;
    int vbank = 0;
    McpStringBuilder colors;
    McpStringBuilder data;
    mcpSbInit(&colors);
    mcpSbInit(&data);

    if(!getOptionalInt(json, tokens, count, root, "bank", &bank))
        bank = 0;
    if(!getOptionalInt(json, tokens, count, root, "vbank", &vbank))
        vbank = 0;

    if(bank < 0 || bank >= TIC_BANKS || vbank < 0 || vbank >= TIC_PALETTES)
        return mcpResultText(true, "invalid palette bank");

    tic_palette* pal = getBankPaletteData(studio, bank, vbank != 0);
    if(pal == NULL)
        return mcpResultText(true, "invalid palette bank");
    if(!mcpSbAppend(&colors, "["))
        goto fail;

    for(int i = 0; i < TIC_PALETTE_SIZE; i++)
    {
        char color[7];
        snprintf(color, sizeof color, "%02x%02x%02x", pal->colors[i].r, pal->colors[i].g, pal->colors[i].b);
        if(i > 0 && !mcpSbAppend(&colors, ","))
            goto fail;
        if(!mcpSbAppendJsonString(&colors, color))
            goto fail;
    }
    if(!mcpSbAppend(&colors, "]"))
        goto fail;

    if(!mcpSbAppendf(&data, "{\"bank\":%d,\"vbank\":%d,\"colors\":%s}", bank, vbank, colors.data))
        goto fail;

    char* result = mcpResultStructured("fetched palette", data.data);
    mcpSbFree(&colors);
    mcpSbFree(&data);
    return result;

fail:
    *invalidParams = true;
    mcpSbFree(&colors);
    mcpSbFree(&data);
    return NULL;
}

static char* handleSetMapRect(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int bank = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int tile = 0;

    if(!getOptionalInt(json, tokens, count, root, "bank", &bank))
        bank = 0;
    if(!getRequiredInt(json, tokens, count, root, "x", &x)
        || !getRequiredInt(json, tokens, count, root, "y", &y)
        || !getRequiredInt(json, tokens, count, root, "width", &width)
        || !getRequiredInt(json, tokens, count, root, "height", &height)
        || !getRequiredInt(json, tokens, count, root, "tile", &tile))
    {
        *invalidParams = true;
        return NULL;
    }

    if(bank < 0 || bank >= TIC_BANKS || tile < 0 || tile >= TIC_SPRITES
        || width <= 0 || height <= 0
        || x < 0 || y < 0 || x + width > TIC_MAP_WIDTH || y + height > TIC_MAP_HEIGHT)
        return mcpResultText(true, "invalid map rectangle");

    tic_map* map = getBankMapData(studio, bank);
    if(map == NULL)
        return mcpResultText(true, "invalid map rectangle");
    for(int yy = 0; yy < height; yy++)
        for(int xx = 0; xx < width; xx++)
            map->data[(x + xx) + (y + yy) * TIC_MAP_WIDTH] = (u8)tile;

    syncVisualBank(studio, bank, false, true, false);
    return mcpResultText(false, "updated map rectangle");
}

static char* buildMapChunkResult(int bank, int x, int y, int width, int height, const tic_map* map, const char* summary)
{
    McpStringBuilder tiles;
    McpStringBuilder data;
    mcpSbInit(&tiles);
    mcpSbInit(&data);

    if(!mcpSbAppend(&tiles, "["))
        goto fail;

    for(int yy = 0; yy < height; yy++)
    {
        for(int xx = 0; xx < width; xx++)
        {
            if(yy > 0 || xx > 0)
                if(!mcpSbAppend(&tiles, ","))
                    goto fail;
            if(!mcpSbAppendf(&tiles, "%u", map->data[(x + xx) + (y + yy) * TIC_MAP_WIDTH]))
                goto fail;
        }
    }

    if(!mcpSbAppend(&tiles, "]"))
        goto fail;

    if(!mcpSbAppendf(&data, "{\"bank\":%d,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"tiles\":%s}",
                     bank, x, y, width, height, tiles.data))
        goto fail;

    char* result = mcpResultStructured(summary, data.data);
    mcpSbFree(&tiles);
    mcpSbFree(&data);
    return result;

fail:
    mcpSbFree(&tiles);
    mcpSbFree(&data);
    return NULL;
}

static char* handleSetMapChunk(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int bank = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    if(!getOptionalInt(json, tokens, count, root, "bank", &bank))
        bank = 0;
    if(!getRequiredInt(json, tokens, count, root, "x", &x)
        || !getRequiredInt(json, tokens, count, root, "y", &y)
        || !getRequiredInt(json, tokens, count, root, "width", &width)
        || !getRequiredInt(json, tokens, count, root, "height", &height))
    {
        *invalidParams = true;
        return NULL;
    }

    const int tilesTok = mcpJsonFindObjectValue(json, tokens, count, root, "tiles");
    if(tilesTok < 0 || tokens[tilesTok].type != JSMN_ARRAY || tokens[tilesTok].size != width * height)
    {
        *invalidParams = true;
        return NULL;
    }

    if(bank < 0 || bank >= TIC_BANKS
        || width <= 0 || height <= 0
        || x < 0 || y < 0 || x + width > TIC_MAP_WIDTH || y + height > TIC_MAP_HEIGHT)
        return mcpResultText(true, "invalid map chunk");

    tic_map* map = getBankMapData(studio, bank);
    if(map == NULL)
        return mcpResultText(true, "invalid map chunk");
    int idx = 0;
    for(int i = tilesTok + 1; i < count && idx < width * height; i++)
    {
        if(tokens[i].parent != tilesTok)
            continue;

        int tile = 0;
        if(!mcpJsonCopyInt(json, &tokens[i], &tile) || tile < 0 || tile >= TIC_SPRITES)
        {
            *invalidParams = true;
            return NULL;
        }

        const int xx = idx % width;
        const int yy = idx / width;
        map->data[(x + xx) + (y + yy) * TIC_MAP_WIDTH] = (u8)tile;
        idx++;
    }

    syncVisualBank(studio, bank, false, true, false);
    return mcpResultText(false, "updated map chunk");
}

static char* handleGetMapRegion(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams, const char* summary)
{
    int bank = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    if(!getOptionalInt(json, tokens, count, root, "bank", &bank))
        bank = 0;
    if(!getRequiredInt(json, tokens, count, root, "x", &x)
        || !getRequiredInt(json, tokens, count, root, "y", &y)
        || !getRequiredInt(json, tokens, count, root, "width", &width)
        || !getRequiredInt(json, tokens, count, root, "height", &height))
    {
        *invalidParams = true;
        return NULL;
    }

    if(bank < 0 || bank >= TIC_BANKS
        || width <= 0 || height <= 0
        || x < 0 || y < 0 || x + width > TIC_MAP_WIDTH || y + height > TIC_MAP_HEIGHT)
        return mcpResultText(true, "invalid map region");

    tic_map* map = getBankMapData(studio, bank);
    if(map == NULL)
        return mcpResultText(true, "invalid map region");

    return buildMapChunkResult(bank, x, y, width, height, map, summary);
}

char* studio_visual_tool_mcp(Studio* studio, const char* tool, const char* argumentsJson, bool* invalidParams)
{
    jsmntok_t* tokens = NULL;
    int count = 0;

    if(invalidParams)
        *invalidParams = false;

    if(studio == NULL || tool == NULL || argumentsJson == NULL)
        return mcpResultText(true, "visual MCP tool unavailable");

    if(!mcpJsonParse(argumentsJson, &tokens, &count) || count <= 0 || tokens[0].type != JSMN_OBJECT)
    {
        if(invalidParams)
            *invalidParams = true;
        free(tokens);
        return NULL;
    }

    char* result = NULL;

    if(strcmp(tool, "sprite_set_sprite") == 0)
        result = handleSetSprite(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sprite_get_sprite") == 0)
        result = handleGetSprite(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sprite_set_spritesheet_region") == 0)
        result = handleSetSpritesheetRegion(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sprite_get_spritesheet_region") == 0)
        result = handleGetSpritesheetRegion(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sprite_set_palette") == 0)
        result = handleSetPalette(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sprite_get_palette") == 0)
        result = handleGetPalette(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "map_set_rect") == 0)
        result = handleSetMapRect(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "map_get_rect") == 0)
        result = handleGetMapRegion(studio, argumentsJson, tokens, count, 0, invalidParams, "fetched map rectangle");
    else if(strcmp(tool, "map_set_chunk") == 0)
        result = handleSetMapChunk(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "map_get_chunk") == 0)
        result = handleGetMapRegion(studio, argumentsJson, tokens, count, 0, invalidParams, "fetched map chunk");
    else
        result = mcpResultText(true, "unknown visual MCP tool");

    free(tokens);
    return result;
}
