// MIT License

#pragma once

#include <stdbool.h>

typedef struct Studio Studio;

char* studio_visual_tool_mcp(Studio* studio, const char* tool, const char* argumentsJson, bool* invalidParams);
