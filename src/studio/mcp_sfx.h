// MIT License

#pragma once

#include <stdbool.h>

typedef struct Studio Studio;

char* studio_sfx_tools_json(void);
char* studio_sfx_tool_mcp(Studio* studio, const char* tool, const char* argumentsJson, bool* invalidParams);
