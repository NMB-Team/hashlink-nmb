#ifdef NDEBUG
#undef NDEBUG
#endif

#include "../../src/update/update_http.c"
#include <assert.h>

#ifdef _WIN32
#define dup _dup
#define dup2 _dup2
#define close _close
#define fileno _fileno
#endif

static void read_output(FILE* file, char* text, size_t capacity) {
	fflush(stdout);
	assert(fseek(file, 0, SEEK_SET) == 0);
	size_t length = fread(text, 1, capacity - 1, file);
	assert(!ferror(file));
	text[length] = 0;
	assert(fseek(file, 0, SEEK_END) == 0);
}

#ifdef __MINGW32__
int wmain(void) {
#else
int main(void) {
#endif
	FILE* output = tmpfile();
	assert(output != NULL);
	fflush(stdout);
	int saved = dup(fileno(stdout));
	assert(saved >= 0);
	assert(dup2(fileno(output), fileno(stdout)) >= 0);
	assert(!progress_is_tty());

	char text[2048];
	download_progress silent = { .enabled = progress_is_tty() };
	progress_show(&silent, 1024 * 1024, 2 * 1024 * 1024, 0);
	progress_show(&silent, 2 * 1024 * 1024, 2 * 1024 * 1024, 1);
	read_output(output, text, sizeof(text));
	assert(text[0] == 0);

	download_progress known = { .enabled = 1 };
	progress_show(&known, 2 * 1024 * 1024, 4 * 1024 * 1024, 0);
	read_output(output, text, sizeof(text));
	assert(strcmp(text, "\r[##########----------]  2.00 MB / 4.00 MB") == 0);
	long before = ftell(output);
	known.last_tick = progress_tick();
	progress_show(&known, 3 * 1024 * 1024, 4 * 1024 * 1024, 0);
	assert(ftell(output) == before);
	assert(known.downloaded == 3 * 1024 * 1024);
	progress_show(&known, 4 * 1024 * 1024, 4 * 1024 * 1024, 1);
	read_output(output, text, sizeof(text));
	assert(strstr(text, "\r[####################]  4.00 MB / 4.00 MB\n") != NULL);

	download_progress unknown = { .enabled = 1 };
	progress_show(&unknown, 1024 * 1024, 0, 0);
	unknown.last_tick = 0;
	progress_show(&unknown, 2 * 1024 * 1024, 0, 0);
	progress_show(&unknown, 2 * 1024 * 1024, 0, 1);
	read_output(output, text, sizeof(text));
	assert(strstr(text, "\r[###-----------------]  1.00 MB / unknown") != NULL);
	assert(strstr(text, "\r[-###----------------]  2.00 MB / unknown") != NULL);
	assert(text[strlen(text) - 1] == '\n');

	download_progress partial = { .enabled = 1 };
	progress_show(&partial, 1024 * 1024, 4 * 1024 * 1024, 0);
	progress_show(&partial, partial.downloaded, partial.expected, 1);
	read_output(output, text, sizeof(text));
	assert(strstr(text, "\r[#####---------------]  1.00 MB / 4.00 MB\n") != NULL);

	download_progress large = { .enabled = 1 };
	progress_show(&large, UINT64_MAX / 2, UINT64_MAX, 0);
	progress_show(&large, UINT64_MAX, 1, 1);
	read_output(output, text, sizeof(text));
	assert(strstr(text, "\r[##########----------]  8796093022208.00 MB") != NULL);
	assert(strstr(text, "\r[####################]  17592186044416.00 MB / 0.00 MB") != NULL);

	fflush(stdout);
	assert(dup2(saved, fileno(stdout)) >= 0);
	assert(close(saved) == 0);
	assert(fclose(output) == 0);
	puts("download progress tests passed");
	return 0;
}
