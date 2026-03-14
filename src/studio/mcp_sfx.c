// MIT License

#include "mcp_sfx.h"

#define JSMN_PARENT_LINKS
#include "mcp_tool_helpers.h"
#include "studio.h"

typedef struct
{
    s32 bank;
    s32 sfx;
    tic_sfx* data;
    tic_sample* sample;
} SfxToolTarget;

static const char* SfxToolsJson =
    "["
    "{\"name\":\"sfx_set_wavetable\",\"description\":\"Set one 32-step SFX waveform.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"waveform\":{\"type\":\"integer\"},\"wave\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"},\"values\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"},\"minItems\":32,\"maxItems\":32}},\"required\":[\"sfx\",\"values\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_get_wavetable\",\"description\":\"Read one 32-step SFX waveform.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"waveform\":{\"type\":\"integer\"},\"wave\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"}},\"required\":[\"sfx\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_set_volume_envelope\",\"description\":\"Set the 30-step SFX volume envelope for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"},\"values\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"},\"minItems\":30,\"maxItems\":30}},\"required\":[\"sfx\",\"values\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_get_volume_envelope\",\"description\":\"Read the 30-step SFX volume envelope for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"}},\"required\":[\"sfx\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_set_wave_envelope\",\"description\":\"Set the 30-step SFX wave envelope for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"},\"values\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"},\"minItems\":30,\"maxItems\":30}},\"required\":[\"sfx\",\"values\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_get_wave_envelope\",\"description\":\"Read the 30-step SFX wave envelope for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"}},\"required\":[\"sfx\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_set_arpeggio\",\"description\":\"Set the 30-step SFX chord envelope for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"},\"values\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"},\"minItems\":30,\"maxItems\":30},\"reverse\":{\"type\":\"boolean\"}},\"required\":[\"sfx\",\"values\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_get_arpeggio\",\"description\":\"Read the 30-step SFX chord envelope for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"}},\"required\":[\"sfx\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_set_pitch_envelope\",\"description\":\"Set the 30-step SFX pitch envelope for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"},\"values\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"},\"minItems\":30,\"maxItems\":30},\"pitch16x\":{\"type\":\"boolean\"}},\"required\":[\"sfx\",\"values\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_get_pitch_envelope\",\"description\":\"Read the 30-step SFX pitch envelope for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"}},\"required\":[\"sfx\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_set_panning\",\"description\":\"Set SFX stereo channel enable flags for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"},\"left\":{\"type\":\"boolean\"},\"right\":{\"type\":\"boolean\"}},\"required\":[\"sfx\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_get_panning\",\"description\":\"Read SFX stereo channel enable flags for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"}},\"required\":[\"sfx\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_set_speed\",\"description\":\"Set SFX speed for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"},\"speed\":{\"type\":\"integer\"},\"value\":{\"type\":\"integer\"}},\"required\":[\"sfx\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_get_speed\",\"description\":\"Read SFX speed for one SFX slot.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"}},\"required\":[\"sfx\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_set_loop_points\",\"description\":\"Set loop points for one SFX envelope target.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"},\"target\":{\"type\":\"string\"},\"start\":{\"type\":\"integer\"},\"size\":{\"type\":\"integer\"}},\"required\":[\"sfx\",\"target\",\"start\",\"size\"],\"additionalProperties\":false}},"
    "{\"name\":\"sfx_get_loop_points\",\"description\":\"Read loop points for one SFX envelope target.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sfx\":{\"type\":\"integer\"},\"bank\":{\"type\":\"integer\"},\"target\":{\"type\":\"string\"}},\"required\":[\"sfx\",\"target\"],\"additionalProperties\":false}}"
    "]";

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

static bool copyOptionalInt(const char* json, const jsmntok_t* tokens, int count, int root, const char* key, int* value)
{
    const int tok = mcpJsonFindObjectValue(json, tokens, count, root, key);
    return tok >= 0 && mcpJsonCopyInt(json, &tokens[tok], value);
}

static bool copyOptionalBool(const char* json, const jsmntok_t* tokens, int count, int root, const char* key, bool* value)
{
    const int tok = mcpJsonFindObjectValue(json, tokens, count, root, key);
    return tok >= 0 && mcpJsonCopyBool(json, &tokens[tok], value);
}

static bool copyRequiredString(const char* json, const jsmntok_t* tokens, int count, int root, const char* key, char* value, size_t size)
{
    const int tok = mcpJsonFindObjectValue(json, tokens, count, root, key);
    return tok >= 0 && mcpJsonCopyString(json, &tokens[tok], value, size);
}

static bool parseFixedIntArray(const char* json, const jsmntok_t* tokens, int count, int objectIndex, const char* key, int expected, int min, int max, int* values)
{
    const int arrayTok = mcpJsonFindObjectValue(json, tokens, count, objectIndex, key);
    if(arrayTok < 0 || tokens[arrayTok].type != JSMN_ARRAY || tokens[arrayTok].size != expected)
        return false;

    int copied = 0;
    for(int i = arrayTok + 1; i < count && copied < expected; i++)
    {
        if(tokens[i].parent != arrayTok)
            continue;

        if(!mcpJsonCopyInt(json, &tokens[i], &values[copied]) || values[copied] < min || values[copied] > max)
            return false;

        copied++;
    }

    return copied == expected;
}

static int getLoopTargetIndex(const char* target)
{
    if(strcmp(target, "wave") == 0) return 0;
    if(strcmp(target, "volume") == 0) return 1;
    if(strcmp(target, "chord") == 0) return 2;
    if(strcmp(target, "pitch") == 0) return 3;
    return -1;
}

static tic_sfx* resolveSfxBank(Studio* studio, int bank)
{
    if(bank >= 0)
        return getBankSfxData(studio, bank);
    return studio_sfx(studio);
}

static void syncSfxBank(Studio* studio, int bank)
{
    if(bank >= 0)
        studioSyncSfxBank(studio, bank);
    else
        studio_sync_sfx(studio);
}

static bool getTarget(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, SfxToolTarget* target)
{
    int bank = -1;
    int sfxId = -1;

    if(copyOptionalInt(json, tokens, count, root, "bank", &bank) && (bank < 0 || bank >= TIC_BANKS))
        return false;

    if(!mcpJsonGetRequiredInt(json, tokens, count, root, "sfx", &sfxId))
    {
        if(!mcpJsonGetRequiredInt(json, tokens, count, root, "index", &sfxId))
            return false;
    }

    if(sfxId < 0 || sfxId >= SFX_COUNT)
        return false;

    target->bank = bank;
    target->sfx = sfxId;
    target->data = resolveSfxBank(studio, bank);
    target->sample = target->data ? &target->data->samples.data[sfxId] : NULL;
    return target->sample != NULL;
}

static bool resolveWaveformId(const char* json, const jsmntok_t* tokens, int count, int root, const SfxToolTarget* target, int* waveform)
{
    int wave = 0;
    if(copyOptionalInt(json, tokens, count, root, "waveform", &wave) || copyOptionalInt(json, tokens, count, root, "wave", &wave))
    {
        if(wave < 0 || wave >= WAVES_COUNT)
            return false;
        *waveform = wave;
        return true;
    }

    *waveform = target->sample->data[0].wave;
    return *waveform >= 0 && *waveform < WAVES_COUNT;
}

static char* buildIntArrayResult(const char* summary, const char* prefix, const int* values, int count, const char* suffix)
{
    McpStringBuilder data;
    mcpSbInit(&data);

    if(!mcpSbAppend(&data, prefix))
        goto fail;

    for(int i = 0; i < count; i++)
    {
        if(i > 0 && !mcpSbAppend(&data, ","))
            goto fail;
        if(!mcpSbAppendf(&data, "%d", values[i]))
            goto fail;
    }

    if(!mcpSbAppend(&data, suffix))
        goto fail;

    return mcpResultStructured(summary, data.data);

fail:
    mcpSbFree(&data);
    return NULL;
}

static char* handleSetWavetable(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    int waveform = 0;
    int values[WAVE_VALUES];

    if(!getTarget(studio, json, tokens, count, root, &target)
        || !resolveWaveformId(json, tokens, count, root, &target, &waveform))
        return mcpResultText(true, "invalid sfx target");

    if(!parseFixedIntArray(json, tokens, count, root, "values", WAVE_VALUES, 0, WAVE_MAX_VALUE, values))
    {
        *invalidParams = true;
        return NULL;
    }

    memset(&target.data->waveforms.items[waveform], 0, sizeof(tic_waveform));
    for(int i = 0; i < WAVE_VALUES; i++)
        tic_tool_poke4(target.data->waveforms.items[waveform].data, i, (u8)values[i]);

    syncSfxBank(studio, target.bank);
    return mcpResultText(false, "updated sfx wavetable");
}

static char* handleGetWavetable(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    int waveform = 0;
    int values[WAVE_VALUES];
    char prefix[96];

    if(!getTarget(studio, json, tokens, count, root, &target)
        || !resolveWaveformId(json, tokens, count, root, &target, &waveform))
        return mcpResultText(true, "invalid sfx target");

    for(int i = 0; i < WAVE_VALUES; i++)
        values[i] = tic_tool_peek4(target.data->waveforms.items[waveform].data, i);

    snprintf(prefix, sizeof(prefix), "{\"sfx\":%d,\"waveform\":%d,\"values\":[", target.sfx, waveform);
    return buildIntArrayResult("fetched sfx wavetable", prefix, values, WAVE_VALUES, "]}");
}

static void readEnvelope(const tic_sample* sample, int field, int* values)
{
    for(int i = 0; i < SFX_TICKS; i++)
    {
        switch(field)
        {
        case 0: values[i] = MAX_VOLUME - sample->data[i].volume; break;
        case 1: values[i] = sample->data[i].wave; break;
        case 2: values[i] = sample->data[i].chord; break;
        case 3: values[i] = sample->data[i].pitch; break;
        default: values[i] = 0; break;
        }
    }
}

static void writeEnvelope(tic_sample* sample, int field, const int* values)
{
    for(int i = 0; i < SFX_TICKS; i++)
    {
        switch(field)
        {
        case 0: sample->data[i].volume = (u8)(MAX_VOLUME - values[i]); break;
        case 1: sample->data[i].wave = (u8)values[i]; break;
        case 2: sample->data[i].chord = (s8)values[i]; break;
        case 3: sample->data[i].pitch = (s8)values[i]; break;
        }
    }
}

static char* handleSetEnvelope(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams, const char* summary, int field, int min, int max)
{
    SfxToolTarget target;
    int values[SFX_TICKS];

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    if(!parseFixedIntArray(json, tokens, count, root, "values", SFX_TICKS, min, max, values))
    {
        *invalidParams = true;
        return NULL;
    }

    writeEnvelope(target.sample, field, values);
    syncSfxBank(studio, target.bank);
    return mcpResultText(false, summary);
}

static char* handleGetEnvelope(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams, const char* summary, int field)
{
    SfxToolTarget target;
    int values[SFX_TICKS];
    char prefix[80];

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    readEnvelope(target.sample, field, values);
    snprintf(prefix, sizeof(prefix), "{\"sfx\":%d,\"values\":[", target.sfx);
    return buildIntArrayResult(summary, prefix, values, SFX_TICKS, "]}");
}

static char* handleSetArpeggio(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    int values[SFX_TICKS];
    bool reverse = false;

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    reverse = target.sample->reverse != 0;
    if(!parseFixedIntArray(json, tokens, count, root, "values", SFX_TICKS, 0, 15, values))
    {
        *invalidParams = true;
        return NULL;
    }

    if(copyOptionalBool(json, tokens, count, root, "reverse", &reverse) == false
        && mcpJsonFindObjectValue(json, tokens, count, root, "reverse") >= 0)
    {
        *invalidParams = true;
        return NULL;
    }

    writeEnvelope(target.sample, 2, values);
    target.sample->reverse = reverse ? 1 : 0;
    syncSfxBank(studio, target.bank);
    return mcpResultText(false, "updated sfx arpeggio");
}

static char* handleGetArpeggio(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    int values[SFX_TICKS];
    char prefix[112];

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    readEnvelope(target.sample, 2, values);
    snprintf(prefix, sizeof(prefix), "{\"sfx\":%d,\"reverse\":%s,\"values\":[", target.sfx, target.sample->reverse ? "true" : "false");
    return buildIntArrayResult("fetched sfx arpeggio", prefix, values, SFX_TICKS, "]}");
}

static char* handleSetPitch(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    int values[SFX_TICKS];
    bool pitch16x = false;

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    pitch16x = target.sample->pitch16x != 0;
    if(!parseFixedIntArray(json, tokens, count, root, "values", SFX_TICKS, -8, 7, values))
    {
        *invalidParams = true;
        return NULL;
    }

    if(copyOptionalBool(json, tokens, count, root, "pitch16x", &pitch16x) == false
        && mcpJsonFindObjectValue(json, tokens, count, root, "pitch16x") >= 0)
    {
        *invalidParams = true;
        return NULL;
    }

    writeEnvelope(target.sample, 3, values);
    target.sample->pitch16x = pitch16x ? 1 : 0;
    syncSfxBank(studio, target.bank);
    return mcpResultText(false, "updated sfx pitch envelope");
}

static char* handleGetPitch(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    int values[SFX_TICKS];
    char prefix[112];

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    readEnvelope(target.sample, 3, values);
    snprintf(prefix, sizeof(prefix), "{\"sfx\":%d,\"pitch16x\":%s,\"values\":[", target.sfx, target.sample->pitch16x ? "true" : "false");
    return buildIntArrayResult("fetched sfx pitch envelope", prefix, values, SFX_TICKS, "]}");
}

static char* handleSetPanning(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    bool left = true;
    bool right = true;
    const int leftTok = mcpJsonFindObjectValue(json, tokens, count, root, "left");
    const int rightTok = mcpJsonFindObjectValue(json, tokens, count, root, "right");

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    left = !target.sample->stereo_left;
    right = !target.sample->stereo_right;

    if((leftTok >= 0 && !copyOptionalBool(json, tokens, count, root, "left", &left))
        || (rightTok >= 0 && !copyOptionalBool(json, tokens, count, root, "right", &right)))
    {
        *invalidParams = true;
        return NULL;
    }

    target.sample->stereo_left = left ? 0 : 1;
    target.sample->stereo_right = right ? 0 : 1;
    syncSfxBank(studio, target.bank);
    return mcpResultText(false, "updated sfx panning");
}

static char* handleGetPanning(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    McpStringBuilder data;
    mcpSbInit(&data);

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    if(!mcpSbAppendf(&data, "{\"sfx\":%d,\"left\":%s,\"right\":%s}",
                     target.sfx,
                     target.sample->stereo_left ? "false" : "true",
                     target.sample->stereo_right ? "false" : "true"))
    {
        mcpSbFree(&data);
        *invalidParams = true;
        return NULL;
    }

    return mcpResultStructured("fetched sfx panning", data.data);
}

static char* handleSetSpeed(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    int speed = 0;

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    if(!copyOptionalInt(json, tokens, count, root, "speed", &speed)
        && !copyOptionalInt(json, tokens, count, root, "value", &speed))
    {
        *invalidParams = true;
        return NULL;
    }

    if(speed < -4 || speed > 3)
        return mcpResultText(true, "invalid sfx speed");

    target.sample->speed = (s8)speed;
    syncSfxBank(studio, target.bank);
    return mcpResultText(false, "updated sfx speed");
}

static char* handleGetSpeed(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    McpStringBuilder data;
    mcpSbInit(&data);

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    if(!mcpSbAppendf(&data, "{\"sfx\":%d,\"speed\":%d}", target.sfx, target.sample->speed))
    {
        mcpSbFree(&data);
        *invalidParams = true;
        return NULL;
    }

    return mcpResultStructured("fetched sfx speed", data.data);
}

static char* handleSetLoopPoints(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    char loopTarget[16];
    int start = 0;
    int size = 0;
    int index = -1;

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    if(!copyRequiredString(json, tokens, count, root, "target", loopTarget, sizeof(loopTarget))
        || !mcpJsonGetRequiredInt(json, tokens, count, root, "start", &start)
        || !mcpJsonGetRequiredInt(json, tokens, count, root, "size", &size))
    {
        *invalidParams = true;
        return NULL;
    }

    index = getLoopTargetIndex(loopTarget);
    if(index < 0 || start < 0 || start > 15 || size < 0 || size > 15)
        return mcpResultText(true, "invalid loop points");

    target.sample->loops[index].start = (u8)start;
    target.sample->loops[index].size = (u8)size;
    syncSfxBank(studio, target.bank);
    return mcpResultText(false, "updated sfx loop points");
}

static char* handleGetLoopPoints(Studio* studio, const char* json, const jsmntok_t* tokens, int count, int root, bool* invalidParams)
{
    SfxToolTarget target;
    char loopTarget[16];
    int index = -1;
    McpStringBuilder data;
    mcpSbInit(&data);

    if(!getTarget(studio, json, tokens, count, root, &target))
        return mcpResultText(true, "invalid sfx target");

    if(!copyRequiredString(json, tokens, count, root, "target", loopTarget, sizeof(loopTarget)))
    {
        *invalidParams = true;
        return NULL;
    }

    index = getLoopTargetIndex(loopTarget);
    if(index < 0)
        return mcpResultText(true, "invalid loop target");

    if(!mcpSbAppendf(&data,
                     "{\"sfx\":%d,\"target\":",
                     target.sfx)
        || !mcpSbAppendJsonString(&data, loopTarget)
        || !mcpSbAppendf(&data,
                         ",\"start\":%u,\"size\":%u}",
                         target.sample->loops[index].start,
                         target.sample->loops[index].size))
    {
        mcpSbFree(&data);
        *invalidParams = true;
        return NULL;
    }

    return mcpResultStructured("fetched sfx loop points", data.data);
}

char* studio_sfx_tools_json(void)
{
    return dupString(SfxToolsJson);
}

char* studio_sfx_tool_mcp(Studio* studio, const char* tool, const char* argumentsJson, bool* invalidParams)
{
    jsmntok_t* tokens = NULL;
    int count = 0;

    if(invalidParams)
        *invalidParams = false;

    if(studio == NULL || tool == NULL || argumentsJson == NULL)
        return mcpResultText(true, "sfx MCP tool unavailable");

    if(!mcpJsonParse(argumentsJson, &tokens, &count) || count <= 0 || tokens[0].type != JSMN_OBJECT)
    {
        if(invalidParams)
            *invalidParams = true;
        free(tokens);
        return NULL;
    }

    char* result = NULL;

    if(strcmp(tool, "sfx_set_wavetable") == 0) result = handleSetWavetable(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_get_wavetable") == 0) result = handleGetWavetable(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_set_volume_envelope") == 0) result = handleSetEnvelope(studio, argumentsJson, tokens, count, 0, invalidParams, "updated sfx volume envelope", 0, 0, 15);
    else if(strcmp(tool, "sfx_get_volume_envelope") == 0) result = handleGetEnvelope(studio, argumentsJson, tokens, count, 0, invalidParams, "fetched sfx volume envelope", 0);
    else if(strcmp(tool, "sfx_set_wave_envelope") == 0) result = handleSetEnvelope(studio, argumentsJson, tokens, count, 0, invalidParams, "updated sfx wave envelope", 1, 0, WAVES_COUNT - 1);
    else if(strcmp(tool, "sfx_get_wave_envelope") == 0) result = handleGetEnvelope(studio, argumentsJson, tokens, count, 0, invalidParams, "fetched sfx wave envelope", 1);
    else if(strcmp(tool, "sfx_set_arpeggio") == 0) result = handleSetArpeggio(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_get_arpeggio") == 0) result = handleGetArpeggio(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_set_pitch_envelope") == 0) result = handleSetPitch(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_get_pitch_envelope") == 0) result = handleGetPitch(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_set_panning") == 0) result = handleSetPanning(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_get_panning") == 0) result = handleGetPanning(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_set_speed") == 0) result = handleSetSpeed(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_get_speed") == 0) result = handleGetSpeed(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_set_loop_points") == 0) result = handleSetLoopPoints(studio, argumentsJson, tokens, count, 0, invalidParams);
    else if(strcmp(tool, "sfx_get_loop_points") == 0) result = handleGetLoopPoints(studio, argumentsJson, tokens, count, 0, invalidParams);
    else result = mcpResultText(true, "unknown sfx MCP tool");

    free(tokens);
    return result;
}
