#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>

#ifdef CB_EXPORTS // (the default macro in Visual Studio)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __declspec(dllimport)
#endif