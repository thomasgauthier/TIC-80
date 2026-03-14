// MIT License

#pragma once

#include <stdbool.h>

typedef struct Studio Studio;

char* mcp_editor_tools_json(void);
char* mcp_editor_call_tool(Studio* studio, const char* toolName, const char* argsJson, bool* invalidParams, bool* handled);
