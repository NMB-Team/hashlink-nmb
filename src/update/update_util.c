#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "update_util.h"
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
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#if defined(__linux__) || defined(__APPLE__)
#include <zlib.h>
#endif
#endif

#include "../hl.h"

static int update_failure(const char* message) {
	fprintf(stderr, "Update failed: %s\n", message);
	return 1;
}

#ifdef _WIN32
wchar_t* update_to_wide(const char* value) {
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

char* update_to_utf8(const wchar_t* value) {
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
	wchar_t* name = update_to_wide(path);
	FILE* file = name ? _wfopen(name, mode) : nullptr;
	free(name);
	return file;
}

static int make_dir(const char* path) {
	wchar_t* name = update_to_wide(path);
	int result = name ? _wmkdir(name) : -1;
	free(name);
	return result == 0 || errno == EEXIST;
}

static int remove_file(const char* path) {
	wchar_t* name = update_to_wide(path);
	int result = name ? _wremove(name) : -1;
	free(name);
	return result == 0;
}

static int remove_dir(const char* path) {
	wchar_t* name = update_to_wide(path);
	int result = name ? _wrmdir(name) : -1;
	free(name);
	return result == 0;
}

static int move_file(const char* source, const char* destination) {
	wchar_t *from = update_to_wide(source), *to = update_to_wide(destination);
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
	char* value = update_to_utf8(buffer);
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
	#ifdef __APPLE__
	char buffer[UPDATE_PATH_MAX];
	uint32_t length = sizeof(buffer);
	if (_NSGetExecutablePath(buffer, &length) != 0)
		return 0;
	char* resolved = realpath(buffer, nullptr);
	if (!resolved)
		return 0;
	int ok = strlen(resolved) < capacity;
	if (ok)
		strcpy(path, resolved);
	free(resolved);
	return ok;
#else
	ssize_t length = readlink("/proc/self/exe", path, capacity - 1);
	if (length < 0 || (size_t)length >= capacity - 1)
		return 0;
	path[length] = 0;
	return 1;
#endif
}
#endif

int update_path_join(char* out, size_t capacity, const char* base, const char* part) {
	int count = snprintf(out, capacity, "%s%s%s", base, base[strlen(base) - 1] == '/' ? "" : "/", part);
	return count >= 0 && (size_t)count < capacity;
}

int update_file_info(const char* path, uint64_t* size, int* regular) {
#ifdef _WIN32
	wchar_t* name = update_to_wide(path);
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
	wchar_t* name = update_to_wide(path);
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
	*exists = update_file_info(path, &size, &regular);
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

int update_read_file(const char* path, char** data, size_t limit, size_t* length) {
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

int update_safe_target(const char* root, const char* target) {
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
			int found = update_file_info(path, &size, &regular);
			if (found && (saved ? regular : (!regular && !file_link(path))))
				return 0;
			if (found) {
#ifdef _WIN32
				wchar_t* name = update_to_wide(path);
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

int update_create_stage(char stage[UPDATE_PATH_MAX]) {
	stage[0] = 0;
#ifdef _WIN32
	wchar_t temp[UPDATE_PATH_MAX], name[UPDATE_PATH_MAX];
	DWORD length = GetTempPathW(UPDATE_PATH_MAX, temp);
	if (!length || length >= UPDATE_PATH_MAX || !GetTempFileNameW(temp, L"hlu", 0, name))
		return 0;
	if (!DeleteFileW(name) || !CreateDirectoryW(name, nullptr))
		return 0;
	char* converted = update_to_utf8(name);
	int ok = converted && strlen(converted) < UPDATE_PATH_MAX;
	if (ok)
		strcpy(stage, converted);
	else
		RemoveDirectoryW(name);
	free(converted);
	for (char* p = stage; ok && *p; p++)
		if (*p == '\\')
			*p = '/';
	return ok;
#else
	strcpy(stage, "/tmp/hl-update-XXXXXX");
	if (mkdtemp(stage))
		return 1;
	stage[0] = 0;
	return 0;
#endif
}

int update_write_stage_owner(const char* stage, const char* root) {
	char path[UPDATE_PATH_MAX];
	if (!update_path_join(path, sizeof(path), stage, ".hl-update-owner"))
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

int update_remove_tree(const char* path) {
#ifdef _WIN32
	wchar_t* wide = update_to_wide(path);
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
			char *name = update_to_utf8(data.cFileName), child[UPDATE_PATH_MAX];
			if (!name || !update_path_join(child, sizeof(child), path, name))
				ok = 0;
			else if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				ok = update_remove_tree(child) && ok;
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
		if (!update_path_join(child, sizeof(child), path, entry->d_name) || lstat(child, &info) != 0) {
			ok = 0;
			continue;
		}
		if (S_ISDIR(info.st_mode))
			ok = update_remove_tree(child) && ok;
		else
			ok = remove_file(child) && ok;
	}
	closedir(directory);
	return remove_dir(path) && ok;
#endif
}

static int archive_relative(const char* original, const update_manifest* manifest, int standalone, char* relative, size_t capacity) {
	if (!standalone)
		return update_archive_path(original, manifest->platform, relative, capacity);
	const char* path = strncmp(original, "./", 2) == 0 ? original + 2 : original;
	if (!update_valid_path(path) || strlen(path) >= capacity)
		return 0;
	strcpy(relative, path);
	return 1;
}

static int extract_zip(const char* archive, const char* stage, const update_manifest* manifest, int standalone) {
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
			if (!(standalone && strcmp(directory, ".") == 0) && !update_valid_path(standalone && strncmp(directory, "./", 2) == 0 ? directory + 2 : directory)) {
				ok = 0;
				break;
			}
			result = mz_zip_reader_goto_next_entry(reader);
			continue;
		}
		char relative[UPDATE_PATH_MAX];
		if (++count > manifest->count || !archive_relative(info->filename, manifest, standalone, relative, sizeof(relative))) {
			ok = 0;
			break;
		}
		const update_file* expected = update_find_file(manifest, relative);
		if (!expected || info->uncompressed_size < 0 || (uint64_t)info->uncompressed_size != expected->size) {
			ok = 0;
			break;
		}
		char destination[UPDATE_PATH_MAX];
		if (!update_path_join(destination, sizeof(destination), stage, expected->path) || !update_safe_target(stage, destination) || !ensure_parents(destination)) {
			ok = 0;
			break;
		}
		uint64_t existing_size;
		int regular;
		if (update_file_info(destination, &existing_size, &regular)) {
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
	return update_verify_stage(stage, manifest);
}

#if defined(__linux__) || defined(__APPLE__)
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

static int extract_tar_gz(const char* archive_path, const char* stage, const update_manifest* manifest, int standalone) {
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
			if (size || (!(standalone && strcmp(original, ".") == 0) && !update_valid_path(standalone && strncmp(original, "./", 2) == 0 ? original + 2 : original)))
				ok = 0;
			continue;
		}
		if (standalone && (type == '1' || type == '2')) {
			ok = 0;
			break;
		}
		if ((type != '0' && type != '1' && type != '2') || !archive_relative(original, manifest, standalone, relative, sizeof(relative))) {
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
		if (!update_path_join(destination, sizeof(destination), stage, relative) || !update_safe_target(stage, destination) || !ensure_parents(destination)) {
			ok = 0;
			break;
		}
		uint64_t existing_size;
		int regular;
		if (update_file_info(destination, &existing_size, &regular)) {
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
			if (!update_path_join(target, sizeof(target), stage, links[i].target) || !update_path_join(destination, sizeof(destination), stage, links[i].path) || !update_safe_target(stage, target) || !update_safe_target(stage, destination)) {
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
	return update_verify_stage(stage, manifest);
}
#endif

int update_extract_archive(const char* archive, const char* stage, const update_manifest* manifest, int standalone) {
	size_t length = strlen(archive);
	if (length >= 4 && strcmp(archive + length - 4, ".zip") == 0)
		return extract_zip(archive, stage, manifest, standalone);
#if defined(__linux__) || defined(__APPLE__)
	if (length >= 7 && strcmp(archive + length - 7, ".tar.gz") == 0)
		return extract_tar_gz(archive, stage, manifest, standalone);
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
	if (strlen(destination) >= sizeof(operation->destination) || !update_safe_target(root, destination) || !ensure_parents(destination))
		return 0;
	strcpy(operation->destination, destination);
	operation->replace = source != nullptr;
	operation->had_old = update_file_info(destination, &size, &regular);
	if (operation->had_old && !regular && !file_link(destination))
		return 0;
	if (!operation_path(operation->prepared, sizeof(operation->prepared), destination, "new", pid) || !operation_path(operation->backup, sizeof(operation->backup), destination, "backup", pid))
		return 0;
	if (update_file_info(operation->prepared, &size, &regular) || update_file_info(operation->backup, &size, &regular))
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

int update_apply_release(const char* root, int prefix, const char* stage, const update_manifest* latest, const update_manifest* previous, int force, const char* manifest_name, const char* executable) {
	update_operation* operations = calloc(latest->count + previous->count + 1, sizeof(update_operation));
	if (!operations)
		return update_failure("out of memory");
	size_t count = 0;
	char target[UPDATE_PATH_MAX], source[UPDATE_PATH_MAX], manifest_source[UPDATE_PATH_MAX];
#ifdef _WIN32
	unsigned pid = GetCurrentProcessId();
#else
	unsigned pid = (unsigned)getpid();
#endif
	for (size_t i = 0; i < latest->count; i++) {
		const update_file* file = &latest->files[i];
		if (executable && strcmp(file->path, executable) == 0)
			continue;
		int exists;
		if (!update_install_path(root, prefix, file->path, target, sizeof(target)) || !update_safe_target(root, target))
			goto failed;
		int matches = update_file_matches(target, file, &exists);
		if (!executable && exists) {
			uint64_t size;
			int regular;
			if (!update_file_info(target, &size, &regular) || !regular)
				goto failed;
		}
		if (!update_find_file(previous, file->path) && exists && (executable ? previous->count && !matches : 1)) {
			fprintf(stderr, "Update failed: %s exists but is not owned by this package. Move it before updating.\n", target);
			goto failed;
		}
		if (!force && matches)
			continue;
		if (!update_path_join(source, sizeof(source), stage, file->path) || !prepare_operation(&operations[count], root, target, source, file->executable, pid))
			goto failed;
		count++;
	}
	for (size_t i = 0; i < previous->count; i++) {
		const update_file* file = &previous->files[i];
		if (update_find_file(latest, file->path))
			continue;
		int exists;
		if (!update_install_path(root, prefix, file->path, target, sizeof(target)) || !update_safe_target(root, target))
			goto failed;
		if (!update_file_matches(target, file, &exists))
			continue;
		if (!prepare_operation(&operations[count], root, target, nullptr, 0, pid))
			goto failed;
		count++;
	}
	if (!update_path_join(manifest_source, sizeof(manifest_source), stage, manifest_name) || !update_path_join(target, sizeof(target), root, manifest_name) || !prepare_operation(&operations[count], root, target, manifest_source, 0, pid))
		goto failed;
	count++;
	if (executable) {
		if (!update_install_path(root, prefix, executable, target, sizeof(target)) || !update_path_join(source, sizeof(source), stage, executable))
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
	}
	printf("Installing...\n");
	int ok = apply_operations(operations, count);
	discard_prepared(operations, count);
	free(operations);
	return ok ? 0 : update_failure("installation failed; previous files were restored where possible");
failed:
	discard_prepared(operations, count + 1);
	free(operations);
	fprintf(stderr, "Update failed: could not prepare installation under %s. Check write permissions and file paths.\n", root);
	return 1;
}

int update_load_manifest(const char* path, update_manifest* manifest) {
	char* text;
	size_t length;
	if (!update_read_file(path, &text, 8 * 1024 * 1024, &length))
		return 0;
	int ok = update_parse_manifest(text, length, manifest);
	free(text);
	return ok;
}

int update_manifests_same(const update_manifest* a, const update_manifest* b) {
	if (strcmp(a->commit, b->commit) != 0 || strcmp(a->platform, b->platform) != 0 || a->count != b->count)
		return 0;
	for (size_t i = 0; i < a->count; i++) {
		const update_file* file = update_find_file(b, a->files[i].path);
		if (!file || file->size != a->files[i].size || file->executable != a->files[i].executable || strcmp(file->hash, a->files[i].hash) != 0)
			return 0;
	}
	return 1;
}

int update_save_manifest(const char* root, const char* source, const char* destination) {
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

int update_verify_stage(const char* stage, const update_manifest* manifest) {
	for (size_t i = 0; i < manifest->count; i++) {
		char path[UPDATE_PATH_MAX];
		int exists;
		if (!update_path_join(path, sizeof(path), stage, manifest->files[i].path) || !update_file_matches(path, &manifest->files[i], &exists))
			return 0;
	}
	return 1;
}


int update_extract_package(const char* archive, const char* stage, const update_manifest* manifest) {
	return update_extract_archive(archive, stage, manifest, 0);
}

int update_package_owned(const char* root, int prefix, const update_manifest* manifest, const update_manifest* other) {
	for (size_t i = 0; i < manifest->count; i++) {
		char target[UPDATE_PATH_MAX];
		const char* path = manifest->files[i].path;
		const char* portable = !prefix && strncmp(path, "bin/", 4) == 0 ? path + 4 : path;
#ifdef _WIN32
		if (_strnicmp(portable, ".hl-", 4) == 0 || _strnicmp(portable, ".limen-", 7) == 0)
#else
		if (strncmp(portable, ".hl-", 4) == 0 || strncmp(portable, ".limen-", 7) == 0)
#endif
			return 0;
		if (!update_install_path(root, prefix, path, target, sizeof(target)) || !update_safe_target(root, target))
			return 0;
		for (size_t j = 0; j < other->count; j++) {
			char occupied[UPDATE_PATH_MAX];
			if (!update_install_path(root, prefix, other->files[j].path, occupied, sizeof(occupied)))
				return 0;
#ifdef _WIN32
			if (_stricmp(target, occupied) == 0 ||
			    (_strnicmp(target, occupied, strlen(occupied)) == 0 && target[strlen(occupied)] == '/') ||
			    (_strnicmp(occupied, target, strlen(target)) == 0 && occupied[strlen(target)] == '/'))
#else
			if (strcmp(target, occupied) == 0 ||
			    (strncmp(target, occupied, strlen(occupied)) == 0 && target[strlen(occupied)] == '/') ||
			    (strncmp(occupied, target, strlen(target)) == 0 && occupied[strlen(target)] == '/'))
#endif
				return 0;
		}
	}
	return 1;
}

int update_current_root(char root[UPDATE_PATH_MAX], int* prefix) {
	char executable[UPDATE_PATH_MAX];
	if (!update_executable_path(executable, sizeof(executable)) || !update_resolve_root(executable, root, UPDATE_PATH_MAX, prefix))
		return 0;
#ifndef _WIN32
	if (*prefix) {
		char candidate[UPDATE_PATH_MAX];
		uint64_t size;
		int regular;
#ifdef __APPLE__
		const char* library = "libhl.dylib";
#else
		const char* library = "libhl.so";
#endif
		snprintf(candidate, sizeof(candidate), "%s/bin/%s", root, library);
		if (update_file_info(candidate, &size, &regular)) {
			if (strlen(root) + 4 >= UPDATE_PATH_MAX)
				return 0;
			strcat(root, "/bin");
			*prefix = 0;
		} else {
			snprintf(candidate, sizeof(candidate), "%s/lib/%s", root, library);
			if (!update_file_info(candidate, &size, &regular)) {
				snprintf(candidate, sizeof(candidate), "%s/lib64/%s", root, library);
				if (update_file_info(candidate, &size, &regular))
					*prefix = 2;
			}
		}
	}
#endif
	return 1;
}

int update_download_verified(const char* base, const char* name, const char* target, uint64_t limit, const char* expected, int show_progress) {
	char url[512], actual[65];
	uint64_t size;
	int count = snprintf(url, sizeof(url), "%s%s", base, name);
	return count > 0 && (size_t)count < sizeof(url) && update_download(url, target, limit, show_progress) &&
	       hash_file(target, actual, &size) && strcmp(actual, expected) == 0;
}

int update_fetch_manifest(const char* api, const char* base, const char* name, const char* package, const char* stage, const char* manifest_name, update_manifest* manifest, char package_hash[65]) {
	char metadata[UPDATE_PATH_MAX], target[UPDATE_PATH_MAX], manifest_hash[65];
	char* data = nullptr;
	size_t length;
	if (!update_path_join(metadata, sizeof(metadata), stage, "release.json") || !update_download(api, metadata, 8 * 1024 * 1024, 0) ||
	    !update_read_file(metadata, &data, 8 * 1024 * 1024, &length))
		return 0;
	int ok = update_release_digests(data, length, name, package, manifest_hash, package_hash);
	free(data);
	return ok && update_path_join(target, sizeof(target), stage, manifest_name) &&
	       update_download_verified(base, name, target, 8 * 1024 * 1024, manifest_hash, 0) && update_load_manifest(target, manifest);
}

int update_remove_package(const char* root, int prefix, const update_manifest* manifest, const char* manifest_name) {
	update_operation* operations = calloc(manifest->count + 1, sizeof(update_operation));
	if (!operations)
		return 1;
#ifdef _WIN32
	unsigned pid = GetCurrentProcessId();
#else
	unsigned pid = (unsigned)getpid();
#endif
	size_t count = 0, modified = 0;
	char target[UPDATE_PATH_MAX];
	for (size_t i = 0; i < manifest->count; i++) {
		int exists;
		if (!update_install_path(root, prefix, manifest->files[i].path, target, sizeof(target)) || !update_safe_target(root, target))
			goto failed;
		if (!update_file_matches(target, &manifest->files[i], &exists)) {
			if (exists) {
				printf("KEEP modified file: %s\n", manifest->files[i].path);
				modified++;
			}
			continue;
		}
		if (!prepare_operation(&operations[count], root, target, nullptr, 0, pid))
			goto failed;
		count++;
	}
	if (!modified) {
		if (!update_path_join(target, sizeof(target), root, manifest_name) || !prepare_operation(&operations[count], root, target, nullptr, 0, pid))
			goto failed;
		count++;
	}
	int ok = apply_operations(operations, count);
	free(operations);
	if (!ok)
		return update_failure("removal failed; previous files were restored where possible");
	puts(modified ? "Modified files preserved; Limen manifest retained." : "Limen removed.");
	return modified ? 2 : 0;
failed:
	free(operations);
	return update_failure("unsafe target or removal preparation failed");
}
