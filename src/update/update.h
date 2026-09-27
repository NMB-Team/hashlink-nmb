#ifndef HL_UPDATE_H
#define HL_UPDATE_H

#ifdef _WIN32
#include <wchar.h>
int hl_update_command(int argc, const wchar_t* const* argv, const char* current_commit);
#else
int hl_update_command(int argc, const char* const* argv, const char* current_commit);
#endif

#endif
