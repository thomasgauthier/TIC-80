// MIT License

#include "mcp_music.h"

#define JSMN_PARENT_LINKS
#include "mcp_tool_helpers.h"
#include "studio.h"

#define MCP_TRACKER_ROWS (MUSIC_PATTERN_ROWS / 4)

static const char* MusicNotes[] = SFX_NOTES;
static const char MusicCommands[] = {
#define MCP_CMD_CHAR(_, c, ...) #c[0],
    MUSIC_CMD_LIST(MCP_CMD_CHAR)
#undef MCP_CMD_CHAR
};

static tic_track* getTrackById(Studio* studio, int track)
{
    tic_music* music = getBankMusicData(studio, 0);
    if(studio == NULL || track < 0 || track >= MUSIC_TRACKS)
        return NULL;
    if(music == NULL)
        return NULL;

    return &music->tracks.data[track];
}

static tic_track_pattern* getPatternById(Studio* studio, int pattern)
{
    tic_music* music = getBankMusicData(studio, 0);
    if(studio == NULL || pattern < 0 || pattern >= MUSIC_PATTERNS)
        return NULL;
    if(music == NULL)
        return NULL;

    return &music->patterns.data[pattern];
}

static void syncMusic(Studio* studio)
{
    if(studio != NULL)
        studioSyncMusicBank(studio, 0);
}

static int commandFromChar(char c)
{
    for(int i = 0; i < tic_music_cmd_count; i++)
        if(MusicCommands[i] == c)
            return i;
    return -1;
}

static const char* noteToString(const tic_track_row* row)
{
    if(row->note == NoteStop)
        return "OFF";
    if(row->note >= NoteStart && row->note < NoteStart + NOTES)
        return MusicNotes[row->note - NoteStart];
    return "";
}

static bool parseNoteString(const char* note, tic_track_row* row)
{
    if(note == NULL || note[0] == '\0')
    {
        row->note = NoteNone;
        row->octave = 0;
        return true;
    }

    if(strcmp(note, "OFF") == 0)
    {
        row->note = NoteStop;
        row->octave = 0;
        return true;
    }

    for(int i = 0; i < NOTES; i++)
        if(strcmp(note, MusicNotes[i]) == 0)
        {
            row->note = (u8)(NoteStart + i);
            return true;
        }

    return false;
}

static bool parseRowObject(const char* json, const jsmntok_t* tokens, int count, int objectTok, tic_track_row* row, int* rowIndex)
{
    int value = 0;
    const int rowTok = mcpJsonFindObjectValue(json, tokens, count, objectTok, "row");
    if(rowTok < 0 || !mcpJsonCopyInt(json, &tokens[rowTok], rowIndex) || *rowIndex < 0 || *rowIndex >= MUSIC_PATTERN_ROWS)
        return false;

    memset(row, 0, sizeof(*row));

    const int noteTok = mcpJsonFindObjectValue(json, tokens, count, objectTok, "note");
    if(noteTok >= 0)
    {
        char note[8];
        if(!mcpJsonCopyString(json, &tokens[noteTok], note, sizeof note) || !parseNoteString(note, row))
            return false;
    }

    const int octaveTok = mcpJsonFindObjectValue(json, tokens, count, objectTok, "octave");
    if(octaveTok >= 0)
    {
        if(!mcpJsonCopyInt(json, &tokens[octaveTok], &value) || value < 0 || value > 7)
            return false;
        row->octave = (u8)value;
    }

    const int sfxTok = mcpJsonFindObjectValue(json, tokens, count, objectTok, "sfx");
    if(sfxTok >= 0)
    {
        if(!mcpJsonCopyInt(json, &tokens[sfxTok], &value) || value < 0 || value >= SFX_COUNT)
            return false;
        tic_tool_set_track_row_sfx(row, value);
    }

    const int commandTok = mcpJsonFindObjectValue(json, tokens, count, objectTok, "command");
    if(commandTok >= 0)
    {
        char command[4];
        if(!mcpJsonCopyString(json, &tokens[commandTok], command, sizeof command))
            return false;

        if(command[0] == '\0')
            row->command = tic_music_cmd_empty;
        else
        {
            const int cmd = commandFromChar(command[0]);
            if(cmd <= tic_music_cmd_empty)
                return false;
            row->command = (u8)cmd;
        }
    }

    const int p1Tok = mcpJsonFindObjectValue(json, tokens, count, objectTok, "param1");
    if(p1Tok >= 0)
    {
        if(!mcpJsonCopyInt(json, &tokens[p1Tok], &value) || value < 0 || value > 0xf)
            return false;
        row->param1 = (u8)value;
    }

    const int p2Tok = mcpJsonFindObjectValue(json, tokens, count, objectTok, "param2");
    if(p2Tok >= 0)
    {
        if(!mcpJsonCopyInt(json, &tokens[p2Tok], &value) || value < 0 || value > 0xf)
            return false;
        row->param2 = (u8)value;
    }

    return true;
}

static bool appendTrackRowJson(McpStringBuilder* sb, int rowIndex, const tic_track_row* row)
{
    const int sfx = tic_tool_get_track_row_sfx(row);
    const char noteChar[2] = {row->command > tic_music_cmd_empty ? MusicCommands[row->command] : '\0', '\0'};

    return mcpSbAppendf(sb, "{\"row\":%d,\"note\":", rowIndex)
        && mcpSbAppendJsonString(sb, noteToString(row))
        && mcpSbAppendf(sb, ",\"octave\":%u,\"sfx\":%d,\"command\":", row->octave, sfx)
        && mcpSbAppendJsonString(sb, noteChar[0] ? noteChar : "")
        && mcpSbAppendf(sb, ",\"param1\":%u,\"param2\":%u}", row->param1, row->param2);
}

static char* handleGetTrack(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int trackId = -1;
    McpStringBuilder data;
    mcpSbInit(&data);

    if(!mcpJsonGetRequiredInt(json, tokens, count, root, "track", &trackId))
    {
        *invalidParams = true;
        return NULL;
    }

    const tic_track* track = getTrackById(studio, trackId);
    if(track == NULL)
        return mcpResultText(true, "invalid track id");

    if(!mcpSbAppendf(&data, "{\"track\":%d,\"tempo\":%d,\"speed\":%d,\"rows\":%d}",
                     trackId, track->tempo + DEFAULT_TEMPO, track->speed + DEFAULT_SPEED, MUSIC_PATTERN_ROWS - track->rows))
    {
        mcpSbFree(&data);
        *invalidParams = true;
        return NULL;
    }

    return mcpResultStructured("fetched track", data.data);
}

static char* handleSetTrack(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int trackId = -1;
    int tempo = 0;
    int speed = 0;
    int rows = 0;

    if(!mcpJsonGetRequiredInt(json, tokens, count, root, "track", &trackId)
        || !mcpJsonGetRequiredInt(json, tokens, count, root, "tempo", &tempo)
        || !mcpJsonGetRequiredInt(json, tokens, count, root, "speed", &speed)
        || !mcpJsonGetRequiredInt(json, tokens, count, root, "rows", &rows))
    {
        *invalidParams = true;
        return NULL;
    }

    tic_track* track = getTrackById(studio, trackId);
    if(track == NULL || tempo < 40 || tempo > 250 || speed < 1 || speed > 31 || rows < MCP_TRACKER_ROWS || rows > MUSIC_PATTERN_ROWS)
        return mcpResultText(true, "invalid track settings");

    track->tempo = (s8)(tempo - DEFAULT_TEMPO);
    track->speed = (s8)(speed - DEFAULT_SPEED);
    track->rows = (u8)(MUSIC_PATTERN_ROWS - rows);
    syncMusic(studio);
    return mcpResultText(false, "updated track");
}

static char* handleGetFrame(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int trackId = -1;
    int frame = -1;
    McpStringBuilder data;
    mcpSbInit(&data);

    if(!mcpJsonGetRequiredInt(json, tokens, count, root, "track", &trackId)
        || !mcpJsonGetRequiredInt(json, tokens, count, root, "frame", &frame))
    {
        *invalidParams = true;
        return NULL;
    }

    tic_track* track = getTrackById(studio, trackId);
    if(track == NULL || frame < 0 || frame >= MUSIC_FRAMES)
        return mcpResultText(true, "invalid frame id");

    if(!mcpSbAppendf(&data,
                     "{\"track\":%d,\"frame\":%d,\"patterns\":[%d,%d,%d,%d]}",
                     trackId,
                     frame,
                     tic_tool_get_pattern_id(track, frame, 0),
                     tic_tool_get_pattern_id(track, frame, 1),
                     tic_tool_get_pattern_id(track, frame, 2),
                     tic_tool_get_pattern_id(track, frame, 3)))
    {
        mcpSbFree(&data);
        *invalidParams = true;
        return NULL;
    }

    return mcpResultStructured("fetched frame", data.data);
}

static char* handleSetFrame(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int trackId = -1;
    int frame = -1;
    if(!mcpJsonGetRequiredInt(json, tokens, count, root, "track", &trackId)
        || !mcpJsonGetRequiredInt(json, tokens, count, root, "frame", &frame))
    {
        *invalidParams = true;
        return NULL;
    }

    const int patternsTok = mcpJsonFindObjectValue(json, tokens, count, root, "patterns");
    if(patternsTok < 0 || tokens[patternsTok].type != JSMN_ARRAY || tokens[patternsTok].size != TIC_SOUND_CHANNELS)
    {
        *invalidParams = true;
        return NULL;
    }

    tic_track* track = getTrackById(studio, trackId);
    if(track == NULL || frame < 0 || frame >= MUSIC_FRAMES)
        return mcpResultText(true, "invalid frame id");

    int channel = 0;
    for(int i = patternsTok + 1; i < count && channel < TIC_SOUND_CHANNELS; i++)
    {
        if(tokens[i].parent != patternsTok)
            continue;

        int pattern = 0;
        if(!mcpJsonCopyInt(json, &tokens[i], &pattern) || pattern < 0 || pattern >= MUSIC_PATTERNS)
        {
            *invalidParams = true;
            return NULL;
        }
        tic_tool_set_pattern_id(track, frame, channel++, pattern);
    }

    syncMusic(studio);
    return mcpResultText(false, "updated frame");
}

static char* handleGetPatternRow(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int patternId = -1;
    int rowIndex = -1;
    McpStringBuilder data;
    mcpSbInit(&data);

    if(!mcpJsonGetRequiredInt(json, tokens, count, root, "pattern", &patternId)
        || !mcpJsonGetRequiredInt(json, tokens, count, root, "row", &rowIndex))
    {
        *invalidParams = true;
        return NULL;
    }

    tic_track_pattern* pattern = getPatternById(studio, patternId);
    if(pattern == NULL || rowIndex < 0 || rowIndex >= MUSIC_PATTERN_ROWS)
        return mcpResultText(true, "invalid pattern row");

    if(!mcpSbAppendf(&data, "{\"pattern\":%d,\"row\":", patternId)
        || !appendTrackRowJson(&data, rowIndex, &pattern->rows[rowIndex])
        || !mcpSbAppend(&data, "}"))
    {
        mcpSbFree(&data);
        *invalidParams = true;
        return NULL;
    }

    return mcpResultStructured("fetched pattern row", data.data);
}

static char* handleSetPatternRow(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int patternId = -1;
    tic_track_row row;
    int rowIndex = -1;

    if(!mcpJsonGetRequiredInt(json, tokens, count, root, "pattern", &patternId))
    {
        *invalidParams = true;
        return NULL;
    }

    tic_track_pattern* pattern = getPatternById(studio, patternId);
    if(pattern == NULL)
        return mcpResultText(true, "invalid pattern id");

    if(!parseRowObject(json, tokens, count, root, &row, &rowIndex))
    {
        *invalidParams = true;
        return NULL;
    }

    pattern->rows[rowIndex] = row;
    syncMusic(studio);
    return mcpResultText(false, "updated pattern row");
}

static char* handleGetPatternRows(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int patternId = -1;
    McpStringBuilder rows;
    McpStringBuilder data;
    mcpSbInit(&rows);
    mcpSbInit(&data);

    if(!mcpJsonGetRequiredInt(json, tokens, count, root, "pattern", &patternId))
    {
        *invalidParams = true;
        return NULL;
    }

    tic_track_pattern* pattern = getPatternById(studio, patternId);
    if(pattern == NULL)
        return mcpResultText(true, "invalid pattern id");

    const int rowsTok = mcpJsonFindObjectValue(json, tokens, count, root, "rows");
    if(rowsTok < 0 || tokens[rowsTok].type != JSMN_ARRAY || tokens[rowsTok].size <= 0)
    {
        *invalidParams = true;
        return NULL;
    }

    if(!mcpSbAppend(&rows, "["))
        goto fail;

    int out = 0;
    for(int i = rowsTok + 1; i < count; i++)
    {
        if(tokens[i].parent != rowsTok)
            continue;

        int rowIndex = 0;
        if(!mcpJsonCopyInt(json, &tokens[i], &rowIndex) || rowIndex < 0 || rowIndex >= MUSIC_PATTERN_ROWS)
        {
            *invalidParams = true;
            goto fail;
        }

        if(out++ > 0 && !mcpSbAppend(&rows, ","))
            goto fail;
        if(!appendTrackRowJson(&rows, rowIndex, &pattern->rows[rowIndex]))
            goto fail;
    }

    if(!mcpSbAppend(&rows, "]")
        || !mcpSbAppendf(&data, "{\"pattern\":%d,\"rows\":%s}", patternId, rows.data))
        goto fail;

    mcpSbFree(&rows);
    return mcpResultStructured("fetched pattern rows", data.data);

fail:
    *invalidParams = true;
    mcpSbFree(&rows);
    mcpSbFree(&data);
    return NULL;
}

static char* handleSetPatternRows(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    int patternId = -1;
    if(!mcpJsonGetRequiredInt(json, tokens, count, root, "pattern", &patternId))
    {
        *invalidParams = true;
        return NULL;
    }

    tic_track_pattern* pattern = getPatternById(studio, patternId);
    if(pattern == NULL)
        return mcpResultText(true, "invalid pattern id");

    const int rowsTok = mcpJsonFindObjectValue(json, tokens, count, root, "rows");
    if(rowsTok < 0 || tokens[rowsTok].type != JSMN_ARRAY || tokens[rowsTok].size <= 0)
    {
        *invalidParams = true;
        return NULL;
    }

    tic_track_row pending[MUSIC_PATTERN_ROWS];
    bool hasRow[MUSIC_PATTERN_ROWS] = {0};

    for(int i = rowsTok + 1; i < count; i++)
    {
        if(tokens[i].parent != rowsTok)
            continue;
        if(tokens[i].type != JSMN_OBJECT)
        {
            *invalidParams = true;
            return NULL;
        }

        tic_track_row row;
        int rowIndex = -1;
        if(!parseRowObject(json, tokens, count, i, &row, &rowIndex))
        {
            *invalidParams = true;
            return NULL;
        }

        pending[rowIndex] = row;
        hasRow[rowIndex] = true;
    }

    for(int i = 0; i < MUSIC_PATTERN_ROWS; i++)
        if(hasRow[i])
            pattern->rows[i] = pending[i];

    syncMusic(studio);
    return mcpResultText(false, "updated pattern rows");
}

char* studio_music_tool_mcp(Studio* studio, const char* tool, const char* argumentsJson, bool* invalidParams)
{
    jsmntok_t* tokens = NULL;
    int count = 0;

    if(invalidParams)
        *invalidParams = false;

    if(studio == NULL || tool == NULL || argumentsJson == NULL)
        return mcpResultText(true, "music MCP tool unavailable");

    if(!mcpJsonParse(argumentsJson, &tokens, &count) || count <= 0 || tokens[0].type != JSMN_OBJECT)
    {
        if(invalidParams)
            *invalidParams = true;
        free(tokens);
        return NULL;
    }

    char* result = NULL;
    if(strcmp(tool, "music_get_track") == 0)
        result = handleGetTrack(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "music_set_track") == 0)
        result = handleSetTrack(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "music_get_frame") == 0)
        result = handleGetFrame(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "music_set_frame") == 0)
        result = handleSetFrame(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "music_get_pattern_row") == 0)
        result = handleGetPatternRow(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "music_set_pattern_row") == 0)
        result = handleSetPatternRow(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "music_get_pattern_rows") == 0)
        result = handleGetPatternRows(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "music_set_pattern_rows") == 0)
        result = handleSetPatternRows(studio, argumentsJson, tokens, count, 0, invalidParams);
    else
        result = mcpResultText(true, "unknown music MCP tool");

    free(tokens);
    return result;
}
