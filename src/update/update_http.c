#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "update_http.h"
#include "update_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <io.h>
#include <errno.h>
#elif (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#endif

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)
#include "../hl.h"

typedef struct {
	int enabled, shown, width;
	unsigned frame;
	uint64_t last_tick, downloaded, expected;
} download_progress;

static int progress_is_tty(void) {
#ifdef _WIN32
	DWORD mode;
	return GetConsoleMode((HANDLE)_get_osfhandle(_fileno(stdout)), &mode) != 0;
#else
	return isatty(fileno(stdout));
#endif
}

static uint64_t progress_tick(void) {
#ifdef _WIN32
	return GetTickCount64();
#else
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
}

static void progress_show(download_progress* progress, uint64_t downloaded, uint64_t expected, int finish) {
	progress->downloaded = downloaded;
	progress->expected = expected;
	if (!progress->enabled)
		return;
	uint64_t tick = progress_tick();
	if (!finish && progress->shown && tick - progress->last_tick < 100)
		return;
	char bar[21], line[128];
	memset(bar, '-', 20);
	bar[20] = 0;
	if (expected) {
		int filled = downloaded >= expected ? 20 : (int)((double)downloaded / (double)expected * 20);
		memset(bar, '#', filled);
		snprintf(line, sizeof(line), "[%s]  %.2f MB / %.2f MB", bar, (double)downloaded / (1024 * 1024), (double)expected / (1024 * 1024));
	} else {
		memset(bar + progress->frame++ % 18, '#', 3);
		snprintf(line, sizeof(line), "[%s]  %.2f MB / unknown", bar, (double)downloaded / (1024 * 1024));
	}
	int width = (int)strlen(line);
	if (width > progress->width)
		progress->width = width;
	printf("\r%-*s", progress->width, line);
	if (finish)
		putchar('\n');
	fflush(stdout);
	progress->shown = 1;
	progress->last_tick = tick;
}
#endif

#ifdef _WIN32
int update_download(const char* url, const char* destination, uint64_t limit, int show_progress) {
	wchar_t *address = update_to_wide(url), *target = update_to_wide(destination);
	HINTERNET session = nullptr, connection = nullptr, request = nullptr;
	FILE* file = nullptr;
	int ok = 0;
	download_progress progress = { 0 };
	if (!address || !target)
		goto done;
	URL_COMPONENTS parts = { 0 };
	parts.dwStructSize = sizeof(parts);
	parts.dwHostNameLength = (DWORD)-1;
	parts.dwUrlPathLength = (DWORD)-1;
	parts.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(address, 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
		goto done;
	wchar_t* host = malloc(((size_t)parts.dwHostNameLength + 1) * sizeof(wchar_t));
	wchar_t* path = malloc(((size_t)parts.dwUrlPathLength + parts.dwExtraInfoLength + 1) * sizeof(wchar_t));
	if (!host || !path) {
		free(host);
		free(path);
		goto done;
	}
	wmemcpy(host, parts.lpszHostName, parts.dwHostNameLength);
	host[parts.dwHostNameLength] = 0;
	wmemcpy(path, parts.lpszUrlPath, parts.dwUrlPathLength);
	wmemcpy(path + parts.dwUrlPathLength, parts.lpszExtraInfo, parts.dwExtraInfoLength);
	path[parts.dwUrlPathLength + parts.dwExtraInfoLength] = 0;
	session = WinHttpOpen(L"HashLink-NMB-updater/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session) {
		WinHttpSetTimeouts(session, 10000, 10000, 30000, 30000);
		connection = WinHttpConnect(session, host, parts.nPort, 0);
	}
	if (connection)
		request = WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	free(host);
	free(path);
	if (!request)
		goto done;
	DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
	if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)))
		goto done;
	if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr))
		goto done;
	DWORD status = 0, size = sizeof(status);
	if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200)
		goto done;
	file = _wfopen(target, L"wb");
	if (!file)
		goto done;
	wchar_t length[32], *end;
	size = sizeof(length);
	uint64_t expected = 0;
	if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &size, WINHTTP_NO_HEADER_INDEX)) {
		errno = 0;
		uint64_t value = wcstoull(length, &end, 10);
		if (!errno && end != length && !*end && length[0] != L'-')
			expected = value;
	}
	progress.enabled = show_progress && progress_is_tty();
	progress_show(&progress, 0, expected, 0);
	uint64_t total = 0;
	for (;;) {
		unsigned char buffer[65536];
		DWORD read = 0;
		if (!WinHttpReadData(request, buffer, sizeof(buffer), &read))
			goto done;
		if (!read)
			break;
		if (total + read > limit || fwrite(buffer, 1, read, file) != read)
			goto done;
		total += read;
		progress_show(&progress, total, expected, 0);
	}
	ok = !ferror(file) && fclose(file) == 0;
	file = nullptr;
done:
	if (progress.shown)
		progress_show(&progress, progress.downloaded, progress.expected, 1);
	if (file)
		fclose(file);
	if (!ok && target)
		_wremove(target);
	if (request)
		WinHttpCloseHandle(request);
	if (connection)
		WinHttpCloseHandle(connection);
	if (session)
		WinHttpCloseHandle(session);
	free(address);
	free(target);
	return ok;
}
#elif (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)

typedef struct {
	FILE* file;
	uint64_t total, limit;
} download_output;

static int transfer_progress(void* context, curl_off_t expected, curl_off_t downloaded, curl_off_t upload_total, curl_off_t uploaded) {
	(void)upload_total;
	(void)uploaded;
	progress_show(context, (uint64_t)downloaded, (uint64_t)expected, 0);
	return 0;
}

static size_t write_data(char* data, size_t size, size_t count, void* context) {
	download_output* output = context;
	if (size && count > SIZE_MAX / size)
		return 0;
	size_t length = size * count;
	if (length > output->limit - output->total)
		return 0;
	size_t written = fwrite(data, 1, length, output->file);
	output->total += written;
	return written;
}

int update_download(const char* url, const char* destination, uint64_t limit, int show_progress) {
	if (strncmp(url, "https://", 8) != 0)
		return 0;
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
		return 0;
	FILE* file = fopen(destination, "wb");
	if (!file) {
		curl_global_cleanup();
		return 0;
	}
	CURL* curl = curl_easy_init();
	int ok = 0;
	download_progress progress = { .enabled = show_progress && progress_is_tty() };
	uint64_t total = 0;
	if (curl) {
		download_output output = { file, 0, limit };
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "HashLink-NMB-updater/1");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
		if (progress.enabled) {
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress);
			curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		}
		CURLcode result = curl_easy_perform(curl);
		total = output.total;
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		ok = result == CURLE_OK && status == 200 && !ferror(file);
		curl_easy_cleanup(curl);
	}
	if (fclose(file) != 0)
		ok = 0;
	if (progress.shown)
		progress_show(&progress, total, progress.expected, 1);
	if (!ok)
		remove(destination);
	curl_global_cleanup();
	return ok;
}
#else
int update_download(const char* url, const char* destination, uint64_t limit, int show_progress) {
	(void)url;
	(void)destination;
	(void)limit;
	(void)show_progress;
	return 0;
}
#endif
