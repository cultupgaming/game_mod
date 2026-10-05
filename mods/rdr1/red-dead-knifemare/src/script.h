#pragma once

#include <types.h>
#include <windows.h>

extern HMODULE g_moduleHandle;

enum class KnifeExecutionResult
{
    Success,
    Aborted,
    Failed
};

KnifeExecutionResult RunFrontThrowingKnifeExecution(
    Actor target,
    bool automaticThrowFlow = false);
void ScriptMain();
