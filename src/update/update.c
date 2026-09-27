#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "update.h"
#include "update_internal.h"
#include "update_http.h"
#include "update_sha256.h"
#include "../../include/minizip-ng/mz.h"
#include "../../include/minizip-ng/mz_strm.h"
#include "../../include/minizip-ng/mz_zip.h"
#include "../../include/minizip-ng/mz_zip_rw.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <zlib.h>
#endif
#endif

#include "../hl.h"

#define UPDATE_PATH_MAX 4096
#define UPDATE_BASE "https://github.com/NMB-Team/hashlink-nmb/releases/download/latest/"
#define UPDATE_RELEASE_API "https://api.github.com/repos/NMB-Team/hashlink-nmb/releases/tags/latest"

static int fail(const char* message) {
	fprintf(stderr, "Update failed: %s\n", message);
	return 1;
}

#ifdef _WIN32
static wchar_t* to_wide(const char* value) {
	int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
	if (!count)
		return nullptr;
	wchar_t* result = malloc((size_t)count * sizeof(wchar_t));
	if (result && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result, count)) {
		free(result);
		return nullptr;
	}
	return result;
}

static char* to_utf8(const wchar_t* value) {
	int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
	if (!count)
		return nullptr;
	char* result = malloc(count);
	if (result && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result, count, nullptr, nullptr)) {
		free(result);
		return nullptr;
	}
	return result;
}

static FILE* open_file(const char* path, const wchar_t* mode) {
	wchar_t* name = to_wide(path);
	FILE* file = name ? _wfopen(name, mode) : nullptr;
	free(name);
	return file;
}

static int make_dir(const char* path) {
	wchar_t* name = to_wide(path);
	int result = name ? _wmkdir(name) : -1;
	free(name);
	return result == 0 || errno == EEXIST;
}

static int remove_file(const char* path) {
	wchar_t* name = to_wide(path);
	int result = name ? _wremove(name) : -1;
	free(name);
	return result == 0;
}

static int remove_dir(const char* path) {
	wchar_t* name = to_wide(path);
	int result = name ? _wrmdir(name) : -1;
	free(name);
	return result == 0;
}

static int move_file(const char* source, const char* destination) {
	wchar_t *from = to_wide(source), *to = to_wide(destination);
	int ok = from && to && MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	free(from);
	free(to);
	return ok;
}

int update_executable_path(char* path, size_t capacity) {
	wchar_t buffer[UPDATE_PATH_MAX];
	DWORD count = GetModuleFileNameW(nullptr, buffer, UPDATE_PATH_MAX);
	if (!count || count == UPDATE_PATH_MAX)
		return 0;
	char* value = to_utf8(buffer);
	if (!value)
		return 0;
	int ok = strlen(value) < capacity;
	if (ok)
		strcpy(path, value);
	free(value);
	for (char* p = path; ok && *p; p++)
		if (*p == '\\')
			*p = '/';
	return ok;
}
#else
static FILE* open_file(const char* path, const char* mode) {
	return fopen(path, mode);
}
static int make_dir(const char* path) {
	return mkdir(path, 0755) == 0 || errno == EEXIST;
}
static int remove_file(const char* path) {
	return unlink(path) == 0;
}
static int remove_dir(const char* path) {
	return rmdir(path) == 0;
}
static int move_file(const char* source, const char* destination) {
	return rename(source, destination) == 0;
}
int update_executable_path(char* path, size_t capacity) {
	ssize_t length = readlink("/proc/self/exe", path, capacity - 1);
	if (length < 0 || (size_t)length >= capacity - 1)
		return 0;
	path[length] = 0;
	return 1;
}
#endif

static int path_join(char* out, size_t capacity, const char* base, const char* part) {
	int count = snprintf(out, capacity, "%s%s%s", base, base[strlen(base) - 1] == '/' ? "" : "/", part);
	return count >= 0 && (size_t)count < capacity;
}

static int file_info(const char* path, uint64_t* size, int* regular) {
#ifdef _WIN32
	wchar_t* name = to_wide(path);
	WIN32_FILE_ATTRIBUTE_DATA data;
	int found = name && GetFileAttributesExW(name, GetFileExInfoStandard, &data);
	free(name);
	if (!found)
		return 0;
	*regular = !(data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
	*size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
#else
	struct stat data;
	if (lstat(path, &data) != 0)
		return 0;
	*regular = S_ISREG(data.st_mode);
	*size = (uint64_t)data.st_size;
#endif
	return 1;
}

static int file_link(const char* path) {
#ifdef _WIN32
	wchar_t* name = to_wide(path);
	DWORD attributes = name ? GetFileAttributesW(name) : INVALID_FILE_ATTRIBUTES;
	free(name);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat data;
	return lstat(path, &data) == 0 && S_ISLNK(data.st_mode);
#endif
}

static int hash_file(const char* path, char hex[65], uint64_t* size) {
	unsigned char digest[32];
	if (!update_sha256_file(path, digest, size))
		return 0;
	update_sha256_hex(digest, hex);
	return 1;
}

int update_file_matches(const char* path, const update_file* expected, int* exists) {
	uint64_t size;
	int regular;
	*exists = file_info(path, &size, &regular);
	if (!*exists || !regular || size != expected->size)
		return 0;
	char hex[65];
	if (!hash_file(path, hex, &size) || size != expected->size || strcmp(hex, expected->hash) != 0)
		return 0;
#ifndef _WIN32
	if (expected->executable) {
		struct stat mode;
		if (stat(path, &mode) != 0 || !(mode.st_mode & 0111))
			return 0;
	}
#endif
	return 1;
}

static int read_file(const char* path, char** data, size_t limit, size_t* length) {
	FILE* file = open_file(path,
#ifdef _WIN32
	                       L"rb"
#else
	                       "rb"
#endif
	);
	if (!file)
		return 0;
	char* buffer = malloc(limit + 1);
	if (!buffer) {
		fclose(file);
		return 0;
	}
	*length = fread(buffer, 1, limit + 1, file);
	int ok = !ferror(file) && *length <= limit && feof(file);
	fclose(file);
	if (!ok) {
		free(buffer);
		return 0;
	}
	buffer[*length] = 0;
	*data = buffer;
	return 1;
}

static int copy_file(const char* source, const char* destination, int executable) {
	FILE *from = open_file(source,
#ifdef _WIN32
	                       L"rb"
#else
	                       "rb"
#endif
	                       ),
	     *to = nullptr;
	if (!from)
		return 0;
	to = open_file(destination,
#ifdef _WIN32
	               L"wb"
#else
	               "wb"
#endif
	);
	if (!to) {
		fclose(from);
		return 0;
	}
	unsigned char buffer[65536];
	size_t count;
	int ok = 1;
	while ((count = fread(buffer, 1, sizeof(buffer), from)) != 0)
		if (fwrite(buffer, 1, count, to) != count) {
			ok = 0;
			break;
		}
	if (ferror(from) || fflush(to) != 0)
		ok = 0;
#ifndef _WIN32
	if (ok && fchmod(fileno(to), executable ? 0755 : 0644) != 0)
		ok = 0;
	if (ok && fsync(fileno(to)) != 0)
		ok = 0;
#else
	(void)executable;
#endif
	if (fclose(from) != 0)
		ok = 0;
	if (fclose(to) != 0)
		ok = 0;
	if (!ok)
		remove_file(destination);
	return ok;
}

#ifndef _WIN32
static int sync_parent(const char* path) {
	char parent[UPDATE_PATH_MAX];
	if (strlen(path) >= sizeof(parent))
		return 0;
	strcpy(parent, path);
	char* slash = strrchr(parent, '/');
	if (!slash)
		return 0;
	if (slash == parent)
		slash[1] = 0;
	else
		*slash = 0;
	int fd = open(parent, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return 0;
	int ok = fsync(fd) == 0;
	close(fd);
	return ok;
}
#endif

static int ensure_parents(const char* path) {
	char parent[UPDATE_PATH_MAX];
	size_t length = strlen(path);
	if (length >= sizeof(parent))
		return 0;
	strcpy(parent, path);
	for (size_t i = 1; i < length; i++)
		if (parent[i] == '/') {
			if (i == 2 && parent[1] == ':')
				continue;
			parent[i] = 0;
			if (!make_dir(parent))
				return 0;
			parent[i] = '/';
		}
	return 1;
}

static int safe_target(const char* root, const char* target) {
	char path[UPDATE_PATH_MAX];
	size_t root_length = strlen(root);
	int trailing = root[root_length - 1] == '/';
	if (strncmp(target, root, root_length) != 0 || (!trailing && target[root_length] != '/') || !target[root_length + !trailing] || strlen(target) >= sizeof(path))
		return 0;
	strcpy(path, target);
	for (char* p = path + root_length + !trailing;; p++)
		if (*p == '/' || *p == 0) {
			char saved = *p;
			*p = 0;
			uint64_t size;
			int regular;
			int found = file_info(path, &size, &regular);
			if (found && (saved ? regular : (!regular && !file_link(path))))
				return 0;
			if (found) {
#ifdef _WIN32
				wchar_t* name = to_wide(path);
				DWORD attributes = name ? GetFileAttributesW(name) : INVALID_FILE_ATTRIBUTES;
				free(name);
				if (attributes == INVALID_FILE_ATTRIBUTES || (saved && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)))
					return 0;
#else
				struct stat info;
				if (lstat(path, &info) != 0 || (saved && S_ISLNK(info.st_mode)))
					return 0;
#endif
			}
			*p = saved;
			if (!saved)
				break;
		}
	return 1;
}

static int create_stage(char stage[UPDATE_PATH_MAX]) {
#ifdef _WIN32
	wchar_t temp[UPDATE_PATH_MAX], name[UPDATE_PATH_MAX];
	if (!GetTempPathW(UPDATE_PATH_MAX, temp) || !GetTempFileNameW(temp, L"hlu", 0, name))
		return 0;
	DeleteFileW(name);
	if (!CreateDirectoryW(name, nullptr))
		return 0;
	char* converted = to_utf8(name);
	if (!converted)
		return 0;
	int ok = strlen(converted) < UPDATE_PATH_MAX;
	if (ok)
		strcpy(stage, converted);
	free(converted);
	for (char* p = stage; ok && *p; p++)
		if (*p == '\\')
			*p = '/';
	return ok;
#else
	strcpy(stage, "/tmp/hl-update-XXXXXX");
	return mkdtemp(stage) != nullptr;
#endif
}

static int write_stage_owner(const char* stage, const char* root) {
	char path[UPDATE_PATH_MAX];
	if (!path_join(path, sizeof(path), stage, ".hl-update-owner"))
		return 0;
	FILE* file = open_file(path,
#ifdef _WIN32
	                       L"wb"
#else
	                       "wb"
#endif
	);
	if (!file)
		return 0;
	int ok = fwrite(root, 1, strlen(root), file) == strlen(root);
	if (fclose(file) != 0)
		ok = 0;
	return ok;
}

static int remove_tree(const char* path) {
#ifdef _WIN32
	wchar_t* wide = to_wide(path);
	if (!wide)
		return 0;
	size_t length = wcslen(wide);
	wchar_t* pattern = malloc((length + 3) * sizeof(wchar_t));
	if (!pattern) {
		free(wide);
		return 0;
	}
	wcscpy(pattern, wide);
	wcscat(pattern, L"\\*");
	WIN32_FIND_DATAW data;
	HANDLE search = FindFirstFileW(pattern, &data);
	free(pattern);
	int ok = 1;
	if (search != INVALID_HANDLE_VALUE) {
		do {
			if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
				continue;
			char *name = to_utf8(data.cFileName), child[UPDATE_PATH_MAX];
			if (!name || !path_join(child, sizeof(child), path, name))
				ok = 0;
			else if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				ok = remove_tree(child) && ok;
			else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				ok = remove_dir(child) && ok;
			else
				ok = remove_file(child) && ok;
			free(name);
		} while (FindNextFileW(search, &data));
		FindClose(search);
	}
	free(wide);
	return remove_dir(path) && ok;
#else
	DIR* directory = opendir(path);
	if (!directory)
		return 0;
	struct dirent* entry;
	int ok = 1;
	while ((entry = readdir(directory)) != nullptr) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		char child[UPDATE_PATH_MAX];
		struct stat info;
		if (!path_join(child, sizeof(child), path, entry->d_name) || lstat(child, &info) != 0) {
			ok = 0;
			continue;
		}
		if (S_ISDIR(info.st_mode))
			ok = remove_tree(child) && ok;
		else
			ok = remove_file(child) && ok;
	}
	closedir(directory);
	return remove_dir(path) && ok;
#endif
}

static int extract_zip(const char* archive, const char* stage, const update_manifest* manifest) {
	void* reader = mz_zip_reader_create();
	if (!reader)
		return 0;
	int ok = mz_zip_reader_open_file(reader, archive) == MZ_OK;
	void* zip = nullptr;
	if (ok)
		ok = mz_zip_reader_get_zip_handle(reader, &zip) == MZ_OK;
	int32_t result = ok ? mz_zip_reader_goto_first_entry(reader) : MZ_INTERNAL_ERROR;
	size_t count = 0;
	while (result == MZ_OK && ok) {
		mz_zip_file* info = nullptr;
		if (mz_zip_reader_entry_get_info(reader, &info) != MZ_OK || !info || !info->filename || strlen(info->filename) != info->filename_size || mz_zip_entry_is_symlink(zip) == MZ_OK || (info->flag & MZ_ZIP_FLAG_ENCRYPTED) ||
		    (info->compression_method != MZ_COMPRESS_METHOD_STORE && info->compression_method != MZ_COMPRESS_METHOD_DEFLATE)) {
			ok = 0;
			break;
		}
		if (mz_zip_reader_entry_is_dir(reader) == MZ_OK) {
			size_t length = strlen(info->filename);
			char directory[UPDATE_PATH_MAX];
			if (!length || length >= sizeof(directory)) {
				ok = 0;
				break;
			}
			memcpy(directory, info->filename, length + 1);
			if (directory[length - 1] == '/')
				directory[length - 1] = 0;
			if (!update_valid_path(directory)) {
				ok = 0;
				break;
			}
			result = mz_zip_reader_goto_next_entry(reader);
			continue;
		}
		char relative[UPDATE_PATH_MAX];
		if (++count > manifest->count || !update_archive_path(info->filename, manifest->platform, relative, sizeof(relative))) {
			ok = 0;
			break;
		}
		const update_file* expected = update_find_file(manifest, relative);
		if (!expected || info->uncompressed_size < 0 || (uint64_t)info->uncompressed_size != expected->size) {
			ok = 0;
			break;
		}
		char destination[UPDATE_PATH_MAX];
		if (!path_join(destination, sizeof(destination), stage, expected->path) || !safe_target(stage, destination) || !ensure_parents(destination)) {
			ok = 0;
			break;
		}
		FILE* target = open_file(destination,
#ifdef _WIN32
		                         L"wb"
#else
		                         "wb"
#endif
		);
		if (!target) {
			ok = 0;
			break;
		}
		ok = mz_zip_reader_entry_open(reader) == MZ_OK;
		uint64_t written = 0;
		while (ok) {
			unsigned char buffer[65536];
			int32_t received = mz_zip_reader_entry_read(reader, buffer, sizeof(buffer));
			if (received < 0 || (uint64_t)received > expected->size - written) {
				ok = 0;
				break;
			}
			if (!received)
				break;
			if (fwrite(buffer, 1, (size_t)received, target) != (size_t)received) {
				ok = 0;
				break;
			}
			written += (uint64_t)received;
		}
		if (mz_zip_reader_entry_close(reader) != MZ_OK || written != expected->size)
			ok = 0;
		if (fclose(target) != 0)
			ok = 0;
		if (ok)
			result = mz_zip_reader_goto_next_entry(reader);
	}
	if (result != MZ_END_OF_LIST || count != manifest->count)
		ok = 0;
	if (mz_zip_reader_close(reader) != MZ_OK)
		ok = 0;
	mz_zip_reader_delete(&reader);
	if (!ok)
		return 0;
	for (size_t i = 0; i < manifest->count; i++) {
		char path[UPDATE_PATH_MAX];
		int exists;
		if (!path_join(path, sizeof(path), stage, manifest->files[i].path) || !update_file_matches(path, &manifest->files[i], &exists))
			return 0;
	}
	return 1;
}

#ifdef __linux__
typedef struct {
	char path[513];
	char target[513];
	int copied;
} tar_link;

static int gzip_read(gzFile archive, void* output, size_t length) {
	unsigned char* bytes = output;
	while (length) {
		unsigned chunk = length > 65536 ? 65536 : (unsigned)length;
		int received = gzread(archive, bytes, chunk);
		if (received <= 0)
			return 0;
		bytes += received;
		length -= (size_t)received;
	}
	return 1;
}

static int tar_text(const unsigned char* field, size_t field_size, char* output, size_t capacity) {
	size_t length = 0;
	while (length < field_size && field[length])
		length++;
	if (!length || length >= capacity)
		return 0;
	memcpy(output, field, length);
	output[length] = 0;
	return 1;
}

static int tar_number(const unsigned char* field, size_t length, uint64_t* value) {
	size_t i = 0;
	while (i < length && (field[i] == ' ' || field[i] == 0))
		i++;
	uint64_t number = 0;
	int digits = 0;
	while (i < length && field[i] >= '0' && field[i] <= '7') {
		if (number > (UINT64_MAX - (field[i] - '0')) / 8)
			return 0;
		number = number * 8 + field[i++] - '0';
		digits = 1;
	}
	while (i < length && (field[i] == ' ' || field[i] == 0))
		i++;
	if (i != length || !digits)
		return 0;
	*value = number;
	return 1;
}

static int tar_header(const unsigned char header[512], char path[513], char link[513], uint64_t* size, unsigned char* type) {
	uint64_t checksum;
	if (memcmp(header + 257, "ustar", 5) != 0 || !tar_number(header + 148, 8, &checksum) || !tar_number(header + 124, 12, size))
		return 0;
	uint64_t actual = 0;
	for (size_t i = 0; i < 512; i++)
		actual += i >= 148 && i < 156 ? 32 : header[i];
	if (actual != checksum)
		return 0;
	char name[101];
	if (!tar_text(header, 100, name, sizeof(name)))
		return 0;
	if (header[345]) {
		char prefix[156];
		if (!tar_text(header + 345, 155, prefix, sizeof(prefix)))
			return 0;
		int count = snprintf(path, 513, "%s/%s", prefix, name);
		if (count < 0 || count >= 513)
			return 0;
	} else
		strcpy(path, name);
	*type = header[156] ? header[156] : '0';
	link[0] = 0;
	if (*type == '1' || *type == '2')
		return tar_text(header + 157, 100, link, 513);
	return 1;
}

static int extract_tar_gz(const char* archive_path, const char* stage, const update_manifest* manifest) {
	gzFile archive = gzopen(archive_path, "rb");
	if (!archive)
		return 0;
	unsigned char* seen = calloc(manifest->count, 1);
	tar_link* links = calloc(manifest->count, sizeof(tar_link));
	int ok = seen != nullptr && links != nullptr;
	size_t count = 0, link_count = 0;
	int zero_blocks = 0;
	while (ok && zero_blocks < 2) {
		unsigned char header[512];
		if (!gzip_read(archive, header, sizeof(header))) {
			ok = 0;
			break;
		}
		int zero = 1;
		for (size_t i = 0; i < sizeof(header); i++)
			if (header[i]) {
				zero = 0;
				break;
			}
		if (zero) {
			zero_blocks++;
			continue;
		}
		if (zero_blocks) {
			ok = 0;
			break;
		}
		char original[513], link[513], relative[513];
		uint64_t size;
		unsigned char type;
		if (!tar_header(header, original, link, &size, &type)) {
			ok = 0;
			break;
		}
		if (type == '5') {
			size_t length = strlen(original);
			if (length && original[length - 1] == '/')
				original[length - 1] = 0;
			if (size || !update_valid_path(original))
				ok = 0;
			continue;
		}
		if ((type != '0' && type != '1' && type != '2') || !update_archive_path(original, manifest->platform, relative, sizeof(relative))) {
			ok = 0;
			break;
		}
		const update_file* expected = update_find_file(manifest, relative);
		if (!expected || count >= manifest->count || seen[expected - manifest->files]++) {
			ok = 0;
			break;
		}
		count++;
		if (type == '1' || type == '2') {
			if (size || link_count >= manifest->count) {
				ok = 0;
				break;
			}
			char target[513];
			if (type == '1' && strchr(link, '/')) {
				if (!update_archive_path(link, manifest->platform, target, sizeof(target))) {
					ok = 0;
					break;
				}
			} else {
				if (strchr(link, '/') || !update_valid_path(link)) {
					ok = 0;
					break;
				}
				const char* slash = strrchr(relative, '/');
				int length = slash ? snprintf(target, sizeof(target), "%.*s/%s", (int)(slash - relative), relative, link) : snprintf(target, sizeof(target), "%s", link);
				if (length < 0 || (size_t)length >= sizeof(target)) {
					ok = 0;
					break;
				}
			}
			const update_file* source = update_find_file(manifest, target);
			if (!source || source == expected || source->size != expected->size || strcmp(source->hash, expected->hash) != 0) {
				ok = 0;
				break;
			}
			strcpy(links[link_count].path, relative);
			strcpy(links[link_count++].target, target);
			continue;
		}
		if (size != expected->size) {
			ok = 0;
			break;
		}
		char destination[UPDATE_PATH_MAX];
		if (!path_join(destination, sizeof(destination), stage, relative) || !safe_target(stage, destination) || !ensure_parents(destination)) {
			ok = 0;
			break;
		}
		FILE* file = fopen(destination, "wb");
		if (!file) {
			ok = 0;
			break;
		}
		uint64_t remaining = size;
		while (remaining && ok) {
			unsigned char buffer[65536];
			size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
			ok = gzip_read(archive, buffer, chunk) && fwrite(buffer, 1, chunk, file) == chunk;
			remaining -= chunk;
		}
		if (fclose(file) != 0)
			ok = 0;
		if (ok && expected->executable && chmod(destination, 0755) != 0)
			ok = 0;
		if (!ok)
			break;
		unsigned padding = (unsigned)((512 - size % 512) % 512);
		if (padding) {
			unsigned char buffer[512];
			ok = gzip_read(archive, buffer, padding);
		}
	}
	if (ok && count != manifest->count)
		ok = 0;
	if (ok) {
		unsigned char buffer[65536];
		int received;
		while ((received = gzread(archive, buffer, sizeof(buffer))) > 0)
			for (int i = 0; i < received; i++)
				if (buffer[i])
					ok = 0;
		if (received < 0)
			ok = 0;
	}
	if (gzclose(archive) != Z_OK)
		ok = 0;
	for (size_t remaining = link_count; ok && remaining;) {
		size_t copied = 0;
		for (size_t i = 0; i < link_count; i++) {
			if (links[i].copied)
				continue;
			const update_file* source = update_find_file(manifest, links[i].target);
			char target[UPDATE_PATH_MAX], destination[UPDATE_PATH_MAX];
			int exists;
			if (!path_join(target, sizeof(target), stage, links[i].target) || !path_join(destination, sizeof(destination), stage, links[i].path) || !safe_target(stage, target) || !safe_target(stage, destination)) {
				ok = 0;
				break;
			}
			if (!update_file_matches(target, source, &exists))
				continue;
			const update_file* expected = update_find_file(manifest, links[i].path);
			if (!ensure_parents(destination) || !copy_file(target, destination, expected->executable)) {
				ok = 0;
				break;
			}
			links[i].copied = 1;
			copied++;
			remaining--;
		}
		if (!copied)
			ok = 0;
	}
	free(links);
	free(seen);
	if (!ok)
		return 0;
	for (size_t i = 0; i < manifest->count; i++) {
		char path[UPDATE_PATH_MAX];
		int exists;
		if (!path_join(path, sizeof(path), stage, manifest->files[i].path) || !update_file_matches(path, &manifest->files[i], &exists))
			return 0;
	}
	return 1;
}
#endif

int update_extract_package(const char* archive, const char* stage, const update_manifest* manifest) {
	size_t length = strlen(archive);
	if (length >= 4 && strcmp(archive + length - 4, ".zip") == 0)
		return extract_zip(archive, stage, manifest);
#ifdef __linux__
	if (length >= 7 && strcmp(archive + length - 7, ".tar.gz") == 0)
		return extract_tar_gz(archive, stage, manifest);
#endif
	return 0;
}

typedef struct {
	char destination[UPDATE_PATH_MAX];
	char prepared[UPDATE_PATH_MAX];
	char backup[UPDATE_PATH_MAX];
	int replace;
	int had_old;
	int moved_old;
	int installed;
} update_operation;

static int operation_path(char* out, size_t capacity, const char* destination, const char* suffix, unsigned pid) {
	int count = snprintf(out, capacity, "%s.hl-%s-%u", destination, suffix, pid);
	return count >= 0 && (size_t)count < capacity;
}

static int prepare_operation(update_operation* operation, const char* root, const char* destination, const char* source, int executable, unsigned pid) {
	uint64_t size;
	int regular;
	memset(operation, 0, sizeof(*operation));
	if (strlen(destination) >= sizeof(operation->destination) || !safe_target(root, destination) || !ensure_parents(destination))
		return 0;
	strcpy(operation->destination, destination);
	operation->replace = source != nullptr;
	operation->had_old = file_info(destination, &size, &regular);
	if (operation->had_old && !regular && !file_link(destination))
		return 0;
	if (!operation_path(operation->prepared, sizeof(operation->prepared), destination, "new", pid) || !operation_path(operation->backup, sizeof(operation->backup), destination, "backup", pid))
		return 0;
	if (file_info(operation->prepared, &size, &regular) || file_info(operation->backup, &size, &regular))
		return 0;
	if (source && !copy_file(source, operation->prepared, executable))
		return 0;
#ifndef _WIN32
	if (source && !sync_parent(operation->prepared)) {
		remove_file(operation->prepared);
		return 0;
	}
#endif
	return 1;
}

static void discard_prepared(update_operation* operations, size_t count) {
	for (size_t i = 0; i < count; i++)
		if (operations[i].replace)
			remove_file(operations[i].prepared);
}

static int apply_operations(update_operation* operations, size_t count) {
	for (size_t i = 0; i < count; i++) {
		update_operation* op = &operations[i];
		if (op->had_old) {
			if (!move_file(op->destination, op->backup))
				goto rollback;
			op->moved_old = 1;
		}
		if (op->replace) {
			if (!move_file(op->prepared, op->destination))
				goto rollback;
			op->installed = 1;
		}
		continue;
	rollback:
		for (size_t n = i + 1; n > 0; n--) {
			update_operation* previous = &operations[n - 1];
			if (previous->installed)
				remove_file(previous->destination);
			if (previous->moved_old && !move_file(previous->backup, previous->destination))
				fprintf(stderr, "Rollback failed for %s; backup remains at %s\n", previous->destination, previous->backup);
		}
		return 0;
	}
	for (size_t i = 0; i < count; i++)
		if (operations[i].moved_old)
			remove_file(operations[i].backup);
	return 1;
}

int update_install_release(const char* root, int prefix, const char* stage, const update_manifest* latest, const update_manifest* previous, int force) {
	update_operation* operations = calloc(latest->count + previous->count + 1, sizeof(update_operation));
	if (!operations)
		return fail("out of memory");
	size_t count = 0;
	char target[UPDATE_PATH_MAX], source[UPDATE_PATH_MAX], manifest_source[UPDATE_PATH_MAX];
	const char* executable;
	char platform[24];
	update_select_platform(platform, &executable);
#ifdef _WIN32
	unsigned pid = GetCurrentProcessId();
#else
	unsigned pid = (unsigned)getpid();
#endif
	for (size_t i = 0; i < latest->count; i++) {
		const update_file* file = &latest->files[i];
		if (strcmp(file->path, executable) == 0)
			continue;
		int exists;
		if (!update_install_path(root, prefix, file->path, target, sizeof(target)) || !safe_target(root, target))
			goto failed;
		int matches = update_file_matches(target, file, &exists);
		if (previous->count && !update_find_file(previous, file->path) && exists && !matches) {
			fprintf(stderr, "Update failed: %s exists but is not managed by HashLink. Move it before updating.\n", target);
			goto failed;
		}
		if (!force && matches)
			continue;
		if (!path_join(source, sizeof(source), stage, file->path) || !prepare_operation(&operations[count], root, target, source, file->executable, pid))
			goto failed;
		count++;
	}
	for (size_t i = 0; i < previous->count; i++) {
		const update_file* file = &previous->files[i];
		if (update_find_file(latest, file->path))
			continue;
		int exists;
		if (!update_install_path(root, prefix, file->path, target, sizeof(target)) || !safe_target(root, target))
			goto failed;
		if (!update_file_matches(target, file, &exists))
			continue;
		if (!prepare_operation(&operations[count], root, target, nullptr, 0, pid))
			goto failed;
		count++;
	}
	if (!path_join(manifest_source, sizeof(manifest_source), stage, ".hl-manifest") || !path_join(target, sizeof(target), root, ".hl-manifest") || !prepare_operation(&operations[count], root, target, manifest_source, 0, pid))
		goto failed;
	count++;
	if (!update_install_path(root, prefix, executable, target, sizeof(target)) || !path_join(source, sizeof(source), stage, executable))
		goto failed;
	const update_file* new_executable = update_find_file(latest, executable);
	if (!new_executable)
		goto failed;
	int exists;
	if (force || !update_file_matches(target, new_executable, &exists)) {
		if (!prepare_operation(&operations[count], root, target, source, 1, pid))
			goto failed;
		count++;
	}
	printf("Installing...\n");
	int ok = apply_operations(operations, count);
	discard_prepared(operations, count);
	free(operations);
	return ok ? 0 : fail("installation failed; previous files were restored where possible");
failed:
	discard_prepared(operations, count + 1);
	free(operations);
	fprintf(stderr, "Update failed: could not prepare installation under %s. Check write permissions and file paths.\n", root);
	return 1;
}

static int download_asset(const char* name, const char* destination, uint64_t limit) {
	char url[512];
	int count = snprintf(url, sizeof(url), "%s%s", UPDATE_BASE, name);
	return count > 0 && (size_t)count < sizeof(url) && update_download(url, destination, limit);
}

static int load_manifest(const char* path, update_manifest* manifest) {
	char* text;
	size_t length;
	if (!read_file(path, &text, 8 * 1024 * 1024, &length))
		return 0;
	int ok = update_parse_manifest(text, length, manifest);
	free(text);
	return ok;
}

static int manifests_same(const update_manifest* a, const update_manifest* b) {
	if (strcmp(a->commit, b->commit) != 0 || strcmp(a->platform, b->platform) != 0 || a->count != b->count)
		return 0;
	for (size_t i = 0; i < a->count; i++) {
		const update_file* file = update_find_file(b, a->files[i].path);
		if (!file || file->size != a->files[i].size || file->executable != a->files[i].executable || strcmp(file->hash, a->files[i].hash) != 0)
			return 0;
	}
	return 1;
}

static int save_manifest(const char* root, const char* source, const char* destination) {
	update_operation operation;
#ifdef _WIN32
	unsigned pid = GetCurrentProcessId();
#else
	unsigned pid = (unsigned)getpid();
#endif
	if (!prepare_operation(&operation, root, destination, source, 0, pid))
		return 0;
	int ok = apply_operations(&operation, 1);
	discard_prepared(&operation, 1);
	return ok;
}

static int verify_stage(const char* stage, const update_manifest* manifest) {
	for (size_t i = 0; i < manifest->count; i++) {
		char path[UPDATE_PATH_MAX];
		int exists;
		if (!path_join(path, sizeof(path), stage, manifest->files[i].path) || !update_file_matches(path, &manifest->files[i], &exists))
			return 0;
	}
	return 1;
}

#ifdef _WIN32
static int spawn_helper(const char* executable, const char* command, unsigned pid, const char* root, const char* stage) {
	wchar_t *program = to_wide(executable), *target = to_wide(root), *temporary = to_wide(stage);
	if (!program || !target || !temporary) {
		free(program);
		free(target);
		free(temporary);
		return 0;
	}
	HANDLE inherited = nullptr;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &inherited, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) {
		free(program);
		free(target);
		free(temporary);
		return 0;
	}
	size_t length = wcslen(program) + wcslen(target) + wcslen(temporary) + strlen(command) + 96;
	wchar_t* line = malloc(length * sizeof(wchar_t));
	if (!line) {
		CloseHandle(inherited);
		free(program);
		free(target);
		free(temporary);
		return 0;
	}
	wchar_t* wide_command = to_wide(command);
	if (!wide_command) {
		CloseHandle(inherited);
		free(program);
		free(target);
		free(temporary);
		free(line);
		return 0;
	}
	swprintf(line, length, L"\"%ls\" %ls %u %llu \"%ls\" \"%ls\"", program, wide_command, pid, (unsigned long long)(uintptr_t)inherited, target, temporary);
	STARTUPINFOW startup = { 0 };
	PROCESS_INFORMATION process = { 0 };
	SECURITY_ATTRIBUTES security = { 0 };

	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;

	HANDLE null_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);

	HANDLE null_out = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);

	if (null_in == INVALID_HANDLE_VALUE || null_out == INVALID_HANDLE_VALUE) {
		if (null_in != INVALID_HANDLE_VALUE)
			CloseHandle(null_in);
		if (null_out != INVALID_HANDLE_VALUE)
			CloseHandle(null_out);

		CloseHandle(inherited);
		free(wide_command);
		free(line);
		free(program);
		free(target);
		free(temporary);
		return 0;
	}

	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = null_in;
	startup.hStdOutput = null_out;
	startup.hStdError = null_out;

	int ok = CreateProcessW(program, line, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);

	CloseHandle(null_in);
	CloseHandle(null_out);

	if (ok) {
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
	}

	CloseHandle(inherited);
	free(wide_command);
	free(line);
	free(program);
	free(target);
	free(temporary);
	return ok;
}

static int wait_for_process(unsigned pid, HANDLE process) {
	if (!process)
		return 0;
	if (GetProcessId(process) != pid) {
		CloseHandle(process);
		return 0;
	}
	DWORD result = WaitForSingleObject(process, 60000);
	CloseHandle(process);
	return result == WAIT_OBJECT_0;
}

static int normalized_windows_path(const char* path) {
	if (strlen(path) < 3 || !((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) || path[1] != ':' || path[2] != '/')
		return 0;
	for (const char* part = path + 3; *part;) {
		const char* end = strchr(part, '/');
		size_t length = end ? (size_t)(end - part) : strlen(part);
		if (!length || (length == 1 && part[0] == '.') || (length == 2 && part[0] == '.' && part[1] == '.'))
			return 0;
		if (!end)
			break;
		part = end + 1;
	}
	return strchr(path, '\\') == nullptr;
}

static int valid_helper_paths(const char* root, const char* stage, int cleanup) {
	char executable[UPDATE_PATH_MAX], expected[UPDATE_PATH_MAX];
	if (!normalized_windows_path(root) || !normalized_windows_path(stage))
		return 0;
	if (!update_executable_path(executable, sizeof(executable)) || !path_join(expected, sizeof(expected), cleanup ? root : stage, "hl.exe"))
		return 0;
	if (_stricmp(executable, expected) != 0)
		return 0;
	if (_stricmp(root, stage) == 0)
		return 0;
	wchar_t temp[UPDATE_PATH_MAX];
	DWORD length = GetTempPathW(UPDATE_PATH_MAX, temp);
	char* temporary = length && length < UPDATE_PATH_MAX ? to_utf8(temp) : nullptr;
	if (!temporary)
		return 0;
	for (char* p = temporary; *p; p++)
		if (*p == '\\')
			*p = '/';
	int in_temp = _strnicmp(stage, temporary, strlen(temporary)) == 0 && strncmp(stage + strlen(temporary), "hlu", 3) == 0;
	free(temporary);
	if (!in_temp)
		return 0;
	char owner_path[UPDATE_PATH_MAX], *owner = nullptr;
	size_t owner_length;
	if (!path_join(owner_path, sizeof(owner_path), stage, ".hl-update-owner") || !read_file(owner_path, &owner, UPDATE_PATH_MAX, &owner_length))
		return 0;
	int owned = owner_length == strlen(root) && _stricmp(owner, root) == 0;
	free(owner);
	if (!owned)
		return 0;
	return 1;
}

#ifdef _WIN32
static int verify_installed_hl(const char* root) {
	char executable[UPDATE_PATH_MAX];

	if (!path_join(executable, sizeof(executable), root, "hl.exe"))
		return 0;

	wchar_t* program = to_wide(executable);
	if (!program)
		return 0;

	size_t length = wcslen(program) + 32;
	wchar_t* command_line = malloc(length * sizeof(wchar_t));
	if (!command_line) {
		free(program);
		return 0;
	}

	swprintf(command_line, length, L"\"%ls\" --version", program);

	STARTUPINFOW startup = { 0 };
	PROCESS_INFORMATION process = { 0 };

	startup.cb = sizeof(startup);

	int ok = CreateProcessW(program, command_line, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
	free(command_line);
	free(program);
	if (!ok)
		return 0;

	CloseHandle(process.hThread);

	DWORD wait = WaitForSingleObject(process.hProcess, 10000);
	if (wait != WAIT_OBJECT_0) {
		TerminateProcess(process.hProcess, 1);
		CloseHandle(process.hProcess);
		return 0;
	}

	DWORD exit_code = 1;
	ok = GetExitCodeProcess(process.hProcess, &exit_code);
	CloseHandle(process.hProcess);
	return ok && exit_code == 0;
}
#endif

static int internal_command(int argc, const wchar_t* const* argv) {
	if (argc != 6)
		return fail("invalid internal update arguments");
	wchar_t* end;
	unsigned long value = wcstoul(argv[2], &end, 10);
	if (!value || *end || value > UINT32_MAX)
		return fail("invalid internal update process ID");
	unsigned long long handle_value = _wcstoui64(argv[3], &end, 10);
	if (!handle_value || *end)
		return fail("invalid internal update process handle");
	HANDLE parent = (HANDLE)(uintptr_t)handle_value;
	char *root = to_utf8(argv[4]), *stage = to_utf8(argv[5]);
	int cleanup = wcscmp(argv[1], L"--internal-update-cleanup") == 0;
	if (!root || !stage || !valid_helper_paths(root, stage, cleanup)) {
		free(root);
		free(stage);
		return fail("invalid internal update paths");
	}
	if (!wait_for_process((unsigned)value, parent)) {
		free(root);
		free(stage);
		return fail("timed out waiting for the previous updater process");
	}
	if (cleanup) {
		int cleanup_ok = remove_tree(stage);
		if (!cleanup_ok) {
			free(root);
			free(stage);
			return fail("update succeeded, but staging cleanup failed");
		}
		int verify_ok = verify_installed_hl(root);
		free(root);
		free(stage);
		if (!verify_ok)
			return fail("HashLink was installed, but the verification run failed");
		return 0;
	}
	char manifest_path[UPDATE_PATH_MAX], platform[24];
	const char* executable;
	update_manifest latest = { 0 }, previous = { 0 };
	int result = 1;
	if (!path_join(manifest_path, sizeof(manifest_path), stage, ".hl-manifest") || !load_manifest(manifest_path, &latest) || !update_select_platform(platform, &executable) || strcmp(latest.platform, platform) || !verify_stage(stage, &latest))
		goto done;
	const update_file* staged_executable = update_find_file(&latest, executable);
	if (!staged_executable || !staged_executable->executable)
		goto done;
	if (!path_join(manifest_path, sizeof(manifest_path), root, ".hl-manifest"))
		goto done;
	uint64_t size;
	int regular;
	if (file_info(manifest_path, &size, &regular) && (!regular || !load_manifest(manifest_path, &previous)))
		goto done;
	if (previous.count && (strcmp(previous.platform, platform) != 0 || !update_find_file(&previous, executable)))
		goto done;
	int prefix = 0;
	result = update_install_release(root, prefix, stage, &latest, &previous, 1);
	if (result == 0) {
		char installed[UPDATE_PATH_MAX];
		if (!path_join(installed, sizeof(installed), root, "hl.exe") || !spawn_helper(installed, "--internal-update-cleanup", GetCurrentProcessId(), root, stage))
			fprintf(stderr, "Update succeeded, but automatic staging cleanup could not start: %s\n", stage);
	}
done:
	if (result != 0)
		fail("staging validation or installation failed");
	update_free_manifest(&latest);
	update_free_manifest(&previous);
	free(root);
	free(stage);
	return result;
}
#endif

int hl_update_command(int argc,
#ifdef _WIN32
                      const wchar_t* const* argv,
#else
                      const char* const* argv,
#endif
                      const char* current_commit) {
#ifdef _WIN32
	if (argc > 1 && (wcscmp(argv[1], L"--internal-update-apply") == 0 || wcscmp(argv[1], L"--internal-update-cleanup") == 0))
		return internal_command(argc, argv);
	if (argc < 2 || wcscmp(argv[1], L"update") != 0)
		return 0;
	int force = argc == 3 && wcscmp(argv[2], L"--force") == 0;
	int check = argc == 3 && wcscmp(argv[2], L"--check") == 0;
#else
	if (argc < 2 || strcmp(argv[1], "update") != 0)
		return 0;
	int force = argc == 3 && strcmp(argv[2], "--force") == 0;
	int check = argc == 3 && strcmp(argv[2], "--check") == 0;
#endif
	if (argc > 3 || (argc == 3 && !force && !check))
		return fail("usage: hl update [--force | --check]");
	char platform[24], executable_path[UPDATE_PATH_MAX], root[UPDATE_PATH_MAX], stage[UPDATE_PATH_MAX];
	const char* executable;
	int prefix;
	if (!update_select_platform(platform, &executable))
		return fail("self-update is not supported on this platform");
	if (!update_executable_path(executable_path, sizeof(executable_path)) || !update_resolve_root(executable_path, root, sizeof(root), &prefix))
		return fail("could not resolve the running hl executable");
#ifndef _WIN32
	if (prefix) {
		char candidate[UPDATE_PATH_MAX];
		uint64_t size;
		int regular;
		int count = snprintf(candidate, sizeof(candidate), "%s/bin/libhl.so", root);
		if (count > 0 && (size_t)count < sizeof(candidate) && strlen(root) + 4 < sizeof(root) && file_info(candidate, &size, &regular)) {
			strcat(root, "/bin");
			prefix = 0;
		} else {
			count = snprintf(candidate, sizeof(candidate), "%s/lib/libhl.so", root);
			int in_lib = count > 0 && (size_t)count < sizeof(candidate) && file_info(candidate, &size, &regular);
			if (!in_lib) {
				count = snprintf(candidate, sizeof(candidate), "%s/lib64/libhl.so", root);
				if (count > 0 && (size_t)count < sizeof(candidate) && file_info(candidate, &size, &regular))
					prefix = 2;
			}
		}
	}
#endif
	if (!create_stage(stage))
		return fail("could not create a staging directory");
	if (!write_stage_owner(stage, root)) {
		remove_tree(stage);
		return fail("could not prepare staging directory");
	}
	int result = 1;
	update_manifest latest = { 0 }, previous = { 0 };
	char name[128], package_name[128], manifest_path[UPDATE_PATH_MAX], local_path[UPDATE_PATH_MAX], release_path[UPDATE_PATH_MAX];
	char manifest_hash[65], package_hash[65];
	snprintf(name, sizeof(name), "hashlink-latest-%s.manifest", platform);
	snprintf(package_name, sizeof(package_name), "hashlink-latest-%s.%s", platform, strcmp(platform, "win64") == 0 ? "zip" : "tar.gz");
	if (!path_join(release_path, sizeof(release_path), stage, "release.json") || !update_download(UPDATE_RELEASE_API, release_path, 8 * 1024 * 1024)) {
		fail("could not retrieve GitHub release metadata");
		goto done;
	}
	char* release_data = nullptr;
	size_t release_length;
	if (!read_file(release_path, &release_data, 8 * 1024 * 1024, &release_length)) {
		fail("could not read GitHub release metadata");
		goto done;
	}
	int metadata_ok = update_release_digests(release_data, release_length, name, package_name, manifest_hash, package_hash);
	free(release_data);
	if (!metadata_ok) {
		fail("GitHub release is missing valid updater asset digests");
		goto done;
	}
	uint64_t downloaded_size;
	char downloaded_hash[65];
	if (!path_join(manifest_path, sizeof(manifest_path), stage, ".hl-manifest") || !download_asset(name, manifest_path, 8 * 1024 * 1024) || !hash_file(manifest_path, downloaded_hash, &downloaded_size) || strcmp(downloaded_hash, manifest_hash) != 0 ||
	    !load_manifest(manifest_path, &latest)) {
		fail("release manifest download, SHA-256, or format verification failed");
		goto done;
	}
	const update_file* latest_executable = update_find_file(&latest, executable);
	if (strcmp(latest.platform, platform) != 0 || !latest_executable || !latest_executable->executable) {
		fail("release manifest does not match this platform");
		goto done;
	}
	if (!path_join(local_path, sizeof(local_path), root, ".hl-manifest"))
		goto done;
	uint64_t size;
	int regular;
	if (file_info(local_path, &size, &regular) && (!regular || !load_manifest(local_path, &previous))) {
		fail("installed manifest is invalid; refusing to remove old files");
		goto done;
	}
	if (previous.count && strcmp(previous.platform, platform) != 0) {
		fail("installed manifest has the wrong platform");
		goto done;
	}
	if (previous.count && !update_find_file(&previous, executable)) {
		fail("installed manifest has no hl executable");
		goto done;
	}
	printf("HashLink NMB updater\n\nCurrent: %.7s\nLatest:  %.7s\n\nChecking installation...\n", current_commit, latest.commit);
	size_t changes = 0;
	size_t conflicts = 0;
	for (size_t i = 0; i < latest.count; i++) {
		const update_file* file = &latest.files[i];
		char path[UPDATE_PATH_MAX];
		int exists;
		if (!update_install_path(root, prefix, file->path, path, sizeof(path)) || !safe_target(root, path)) {
			fail("unsafe installation path");
			goto done;
		}
		int matches = update_file_matches(path, file, &exists);
		update_status status = update_classify(file, update_find_file(&previous, file->path), exists, matches);
		if (previous.count == 0 && status == UPDATE_CONFLICT)
			status = UPDATE_MODIFIED;
		if (force && status == UPDATE_OK)
			status = UPDATE_MODIFIED;
		if (status != UPDATE_OK)
			changes++;
		if (status == UPDATE_CONFLICT)
			conflicts++;
		printf("%-9s %s\n", status == UPDATE_OK ? "OK" : status == UPDATE_MISSING ? "MISSING" : status == UPDATE_NEW ? "NEW" : status == UPDATE_CONFLICT ? "CONFLICT" : "MODIFIED", file->path);
	}
	for (size_t i = 0; i < previous.count; i++) {
		const update_file* file = &previous.files[i];
		if (update_find_file(&latest, file->path))
			continue;
		char path[UPDATE_PATH_MAX];
		int exists;
		if (!update_install_path(root, prefix, file->path, path, sizeof(path)) || !safe_target(root, path)) {
			fail("unsafe installed manifest path");
			goto done;
		}
		int matches = update_file_matches(path, file, &exists);
		update_status status = update_classify(nullptr, file, exists, matches);
		if (status == UPDATE_REMOVE)
			changes++;
		printf("%-9s %s\n", status == UPDATE_REMOVE ? "REMOVE" : "KEEP", file->path);
	}
	if (changes == 0) {
		if (!check && !manifests_same(&latest, &previous) && !save_manifest(root, manifest_path, local_path)) {
			fail("files are valid, but the installed manifest could not be saved; check write permissions");
			goto done;
		}
		puts("All managed files are up to date.");
		result = 0;
		goto done;
	}
	printf("\n%zu change%s required.\n", changes, changes == 1 ? "" : "s");
	if (check) {
		result = 2;
		goto done;
	}
	if (conflicts) {
		fail("unmanaged files conflict with the new release; move them before updating");
		goto done;
	}
	char package_path[UPDATE_PATH_MAX];
	if (!path_join(package_path, sizeof(package_path), stage, package_name))
		goto done;
	printf("Downloading %s...\n", package_name);
	if (!download_asset(package_name, package_path, 2ULL * 1024 * 1024 * 1024)) {
		fail("package download failed");
		goto done;
	}
	char actual_hash[65];
	if (!hash_file(package_path, actual_hash, &size) || strcmp(actual_hash, package_hash) != 0) {
		fail("package SHA-256 mismatch");
		goto done;
	}
	puts("Verifying package... OK");
	if (!update_extract_package(package_path, stage, &latest)) {
		fail("archive extraction or extracted file verification failed");
		goto done;
	}
	puts("Extracting... OK\nVerifying files... OK");
#ifdef _WIN32
	char staged_executable[UPDATE_PATH_MAX];

	puts("Installing...");

	if (!path_join(staged_executable, sizeof(staged_executable), stage, "hl.exe") || !spawn_helper(staged_executable, "--internal-update-apply", GetCurrentProcessId(), root, stage)) {
		fail("could not start the verified update helper");
		goto done;
	}

	puts("Update prepared; installation will finish in the background.");
	result = 0;
	stage[0] = 0;
#else
	result = update_install_release(root, prefix, stage, &latest, &previous, force);
	if (result == 0)
		puts("HashLink updated successfully.");
#endif
done:
	update_free_manifest(&latest);
	update_free_manifest(&previous);
	if (stage[0])
		remove_tree(stage);
	return result;
}
