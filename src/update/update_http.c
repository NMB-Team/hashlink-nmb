#include "update_http.h"
#include "update_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include "../hl.h"

int update_download(const char* url, const char* destination, uint64_t limit) {
	wchar_t *address = update_to_wide(url), *target = update_to_wide(destination);
	HINTERNET session = nullptr, connection = nullptr, request = nullptr;
	FILE* file = nullptr;
	int ok = 0;
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
	}
	ok = !ferror(file) && fclose(file) == 0;
	file = nullptr;
done:
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
#include <curl/curl.h>
#include "../hl.h"

typedef struct {
	FILE* file;
	uint64_t total, limit;
} download_output;

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

int update_download(const char* url, const char* destination, uint64_t limit) {
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
		CURLcode result = curl_easy_perform(curl);
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		ok = result == CURLE_OK && status == 200 && !ferror(file);
		curl_easy_cleanup(curl);
	}
	if (fclose(file) != 0)
		ok = 0;
	if (!ok)
		remove(destination);
	curl_global_cleanup();
	return ok;
}
#else
int update_download(const char* url, const char* destination, uint64_t limit) {
	(void)url;
	(void)destination;
	(void)limit;
	return 0;
}
#endif
