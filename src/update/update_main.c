#include "update.h"

#include <stdio.h>
#include <string.h>

#ifndef HL_COMMIT_SHA
#define HL_COMMIT_SHA "unknown"
#endif
#ifndef HL_VERSION_TEXT
#define HL_VERSION_TEXT "unknown"
#endif

int main(int argc, char** argv) {
	if (argc > 1 && strcmp(argv[1], "update") == 0)
		return hl_update_command(argc, (const char* const*)argv, HL_COMMIT_SHA);
	if (argc > 1 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
		printf("%s+%s\n", HL_VERSION_TEXT, HL_COMMIT_SHA);
		return 0;
	}
	if (argc > 1 && (strcmp(argv[1], "--commit") == 0 || strcmp(argv[1], "-c") == 0)) {
		printf("Commit: %s\n", HL_COMMIT_SHA);
		return 0;
	}
	if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
		puts("Usage: hl update [--force | --check]\nHashLink VM/JIT is unavailable on ARM64.");
		return 0;
	}
	fputs("HashLink VM/JIT is unavailable on ARM64. Use hl update to maintain this installation.\n", stderr);
	return 1;
}
