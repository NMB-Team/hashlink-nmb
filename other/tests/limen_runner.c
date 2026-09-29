#include "limen_update.h"
#include "update.h"
#include "update_util.h"
#include "update_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int update_download(const char* url, const char* destination, uint64_t limit, int show_progress) {
	const char* fixture = getenv("HL_TEST_RELEASE");
	const char* name = strrchr(url, '/');
	char path[UPDATE_PATH_MAX];
	if (!fixture || !name || !update_path_join(path, sizeof(path), fixture, strstr(url, "api.github.com") ? "release.json" : name + 1))
		return 0;
	if (show_progress != (!strstr(url, "api.github.com") && !strstr(name, ".manifest"))) {
		fprintf(stderr, "Incorrect progress setting for %s\n", name);
		return 0;
	}
	FILE* source = fopen(path, "rb");
#ifdef _WIN32
	wchar_t* wide = update_to_wide(destination);
	FILE* target = wide ? _wfopen(wide, L"wb") : NULL;
	free(wide);
#else
	FILE* target = fopen(destination, "wb");
#endif
	int ok = source && target;
	uint64_t total = 0;
	while (ok) {
		unsigned char buffer[65536];
		size_t length = fread(buffer, 1, sizeof(buffer), source);
		if (!length)
			break;
		total += length;
		ok = total <= limit && fwrite(buffer, 1, length, target) == length;
	}
	if (source) {
		ok = ok && !ferror(source);
		fclose(source);
	}
	if (target && fclose(target) != 0)
		ok = 0;
	return ok;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
	if (argc == 2 && wcscmp(argv[1], L"--platform") == 0) {
#else
int main(int argc, char** argv) {
	if (argc == 2 && strcmp(argv[1], "--platform") == 0) {
#endif
		char platform[24];
		if (!limen_select_platform(platform))
			return 1;
		puts(platform);
		return 0;
	}
#ifdef _WIN32
	if (argc > 1 && wcscmp(argv[1], L"update") == 0)
		return hl_update_command(argc, (const wchar_t* const*)argv, "0123456789abcdef0123456789abcdef01234567");
	return hl_limen_command(argc, (const wchar_t* const*)argv);
#else
	if (argc > 1 && strcmp(argv[1], "update") == 0)
		return hl_update_command(argc, (const char* const*)argv, "0123456789abcdef0123456789abcdef01234567");
	return hl_limen_command(argc, (const char* const*)argv);
#endif
}
