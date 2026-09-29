#ifndef HL_LIMEN_UPDATE_H
#define HL_LIMEN_UPDATE_H

#ifdef _WIN32
#include <wchar.h>
int hl_limen_command(int argc, const wchar_t* const* argv);
#else
int hl_limen_command(int argc, const char* const* argv);
#endif

int limen_select_platform(char platform[24]);

#endif
