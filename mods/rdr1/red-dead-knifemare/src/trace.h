#pragma once

#include <windows.h>

void InitTrace(HMODULE moduleHandle);
void WriteTrace(const char* text);
void Trace(const char* step);
void Trace(const char* step, long long value);
void TraceFloat(const char* step, float value);
