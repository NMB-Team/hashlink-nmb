#include "update.h"
#include "limen_update.h"
#include "../hl.h"

#include <stdio.h>
#include <string.h>

#ifndef HL_COMMIT_SHA
#define HL_COMMIT_SHA "unknown"
#endif

int main(int argc, char** argv) {
	if (argc > 1 && strcmp(argv[1], "limen") == 0)
		return hl_limen_command(argc, (const char* const*)argv);
	if (argc > 1 && strcmp(argv[1], "update") == 0)
		return hl_update_command(argc, (const char* const*)argv, HL_COMMIT_SHA);
	if (argc > 1 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
#ifdef HL_VERSION_TEXT
		printf("%s+%s\n", HL_VERSION_TEXT, HL_COMMIT_SHA);
#else
		printf("%d.%d.%d+%s\n", HL_VERSION >> 16, (HL_VERSION >> 8) & 0xff, HL_VERSION & 0xff, HL_COMMIT_SHA);
#endif
		return 0;
	}
	if (argc > 1 && (strcmp(argv[1], "--commit") == 0 || strcmp(argv[1], "-c") == 0)) {
		printf("Commit: %s\n", HL_COMMIT_SHA);
		return 0;
	}
	if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
		puts("Usage: hl update [--force | --check]\n       hl limen install | update | reinstall | status | remove\nHashLink VM/JIT is unavailable on ARM64.");
		return 0;
	}
	fputs("HashLink VM/JIT is unavailable on ARM64. Use hl update to maintain this installation.\n", stderr);
	return 1;
}
