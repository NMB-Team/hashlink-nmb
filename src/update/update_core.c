#include "update_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "../hl.h"

static int same_path(const char* a, const char* b) {
#ifdef _WIN32
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return *a == *b;
#else
	return strcmp(a, b) == 0;
#endif
}

int update_valid_path(const char* path) {
	const char* segment = path;
	if (!*path || strlen(path) > 512 || *path == '/')
		return 0;
	for (const char* p = path;; p++) {
		unsigned char ch = (unsigned char)*p;
		if (ch == '/' || ch == 0) {
			size_t length = (size_t)(p - segment);
			if (length == 0 || (length == 1 && segment[0] == '.') || (length == 2 && segment[0] == '.' && segment[1] == '.'))
				return 0;
#ifdef _WIN32
			if (segment[length - 1] == '.' || segment[length - 1] == ' ')
				return 0;
			char device[5] = { 0 };
			for (size_t i = 0; i < length && i < 4 && segment[i] != '.'; i++)
				device[i] = (char)toupper((unsigned char)segment[i]);
			if (strcmp(device, "CON") == 0 || strcmp(device, "PRN") == 0 || strcmp(device, "AUX") == 0 || strcmp(device, "NUL") == 0 ||
			    (strlen(device) == 4 && (strncmp(device, "COM", 3) == 0 || strncmp(device, "LPT", 3) == 0) && device[3] >= '1' && device[3] <= '9'))
				return 0;
#endif
			if (ch == 0)
				return 1;
			segment = p + 1;
		} else if (ch < 32 || ch > 126 || ch == '\\' || ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*')
			return 0;
	}
}

int update_archive_path(const char* original, const char* platform, char* relative, size_t capacity) {
	if (!update_valid_path(original))
		return 0;
	const char* slash = strchr(original, '/');
	if (!slash || !slash[1])
		return 0;
	const char* path = slash + 1;
	if (strncmp(path, "lib/", 4) == 0)
		path += 4;
	else if (strncmp(path, "lib64/", 6) == 0)
		path += 6;
	else if (strncmp(path, "bin/", 4) == 0 && (strcmp(platform, "win64") == 0 || strcmp(path, "bin/hl") == 0))
		path += 4;
	if (!update_valid_path(path) || strncmp(path, ".hl-", 4) == 0 || strlen(path) >= capacity)
		return 0;
	strcpy(relative, path);
	return 1;
}

void update_free_manifest(update_manifest* manifest) {
	for (size_t i = 0; i < manifest->count; i++)
		free(manifest->files[i].path);
	free(manifest->files);
	memset(manifest, 0, sizeof(*manifest));
}

const update_file* update_find_file(const update_manifest* manifest, const char* path) {
	for (size_t i = 0; i < manifest->count; i++)
		if (same_path(manifest->files[i].path, path))
			return &manifest->files[i];
	return nullptr;
}

typedef struct {
	const char* current;
	const char* end;
} json_reader;

static void json_space(json_reader* reader) {
	while (reader->current < reader->end && (*reader->current == ' ' || *reader->current == '\t' || *reader->current == '\r' || *reader->current == '\n'))
		reader->current++;
}

static int json_take(json_reader* reader, char character) {
	json_space(reader);
	if (reader->current == reader->end || *reader->current != character)
		return 0;
	reader->current++;
	return 1;
}

static int json_string(json_reader* reader, const char** value, size_t* length, int* escaped) {
	if (!json_take(reader, '"'))
		return 0;
	const char* start = reader->current;
	*escaped = 0;
	while (reader->current < reader->end) {
		unsigned char ch = (unsigned char)*reader->current++;
		if (ch == '"') {
			*value = start;
			*length = (size_t)(reader->current - start - 1);
			return 1;
		}
		if (ch < 32)
			return 0;
		if (ch != '\\')
			continue;
		*escaped = 1;
		if (reader->current == reader->end)
			return 0;
		ch = (unsigned char)*reader->current++;
		if (ch == 'u') {
			for (int i = 0; i < 4; i++) {
				if (reader->current == reader->end || !isxdigit((unsigned char)*reader->current++))
					return 0;
			}
		} else if (ch != '"' && ch != '\\' && ch != '/' && ch != 'b' && ch != 'f' && ch != 'n' && ch != 'r' && ch != 't')
			return 0;
	}
	return 0;
}

static int json_equals(const char* value, size_t length, int escaped, const char* expected) {
	return value && !escaped && length == strlen(expected) && memcmp(value, expected, length) == 0;
}

static int json_skip(json_reader* reader, int depth) {
	if (depth > 32)
		return 0;
	json_space(reader);
	if (reader->current == reader->end)
		return 0;
	if (*reader->current == '"') {
		const char* value;
		size_t length;
		int escaped;
		return json_string(reader, &value, &length, &escaped);
	}
	if (json_take(reader, '{')) {
		if (json_take(reader, '}'))
			return 1;
		do {
			const char* key;
			size_t length;
			int escaped;
			if (!json_string(reader, &key, &length, &escaped) || !json_take(reader, ':') || !json_skip(reader, depth + 1))
				return 0;
			if (json_take(reader, '}'))
				return 1;
		} while (json_take(reader, ','));
		return 0;
	}
	if (json_take(reader, '[')) {
		if (json_take(reader, ']'))
			return 1;
		do {
			if (!json_skip(reader, depth + 1))
				return 0;
			if (json_take(reader, ']'))
				return 1;
		} while (json_take(reader, ','));
		return 0;
	}
	const char* start = reader->current;
	while (reader->current < reader->end && *reader->current != ',' && *reader->current != '}' && *reader->current != ']' && *reader->current != ' ' && *reader->current != '\t' && *reader->current != '\r' && *reader->current != '\n')
		reader->current++;
	size_t length = (size_t)(reader->current - start);
	if ((length == 4 && memcmp(start, "true", 4) == 0) || (length == 5 && memcmp(start, "false", 5) == 0) || (length == 4 && memcmp(start, "null", 4) == 0))
		return 1;
	const char* cursor = start;
	const char* end = reader->current;
	if (cursor < end && *cursor == '-')
		cursor++;
	if (cursor == end)
		return 0;
	if (*cursor == '0')
		cursor++;
	else if (*cursor >= '1' && *cursor <= '9')
		while (cursor < end && *cursor >= '0' && *cursor <= '9')
			cursor++;
	else
		return 0;
	if (cursor < end && *cursor == '.') {
		cursor++;
		if (cursor == end || *cursor < '0' || *cursor > '9')
			return 0;
		while (cursor < end && *cursor >= '0' && *cursor <= '9')
			cursor++;
	}
	if (cursor < end && (*cursor == 'e' || *cursor == 'E')) {
		cursor++;
		if (cursor < end && (*cursor == '+' || *cursor == '-'))
			cursor++;
		if (cursor == end || *cursor < '0' || *cursor > '9')
			return 0;
		while (cursor < end && *cursor >= '0' && *cursor <= '9')
			cursor++;
	}
	return cursor == end;
}

static int json_asset(json_reader* reader, const char* manifest_name, const char* package_name, char manifest_hash[65], char package_hash[65], int found[2]) {
	if (!json_take(reader, '{'))
		return 0;
	const char *name = nullptr, *digest = nullptr;
	size_t name_length = 0, digest_length = 0;
	int name_escaped = 0, digest_escaped = 0, name_seen = 0, digest_seen = 0;
	if (!json_take(reader, '}')) {
		do {
			const char* key;
			size_t length;
			int escaped;
			if (!json_string(reader, &key, &length, &escaped) || !json_take(reader, ':'))
				return 0;
			if (json_equals(key, length, escaped, "name")) {
				if (name_seen++ || !json_string(reader, &name, &name_length, &name_escaped))
					return 0;
			} else if (json_equals(key, length, escaped, "digest")) {
				if (digest_seen++)
					return 0;
				json_space(reader);
				if (reader->current < reader->end && *reader->current == '"') {
					if (!json_string(reader, &digest, &digest_length, &digest_escaped))
						return 0;
				} else if (!json_skip(reader, 1))
					return 0;
			} else if (!json_skip(reader, 1))
				return 0;
			if (json_take(reader, '}'))
				break;
			if (!json_take(reader, ','))
				return 0;
		} while (1);
	}
	int index = json_equals(name, name_length, name_escaped, manifest_name) ? 0 : json_equals(name, name_length, name_escaped, package_name) ? 1 : -1;
	if (index < 0)
		return 1;
	if (found[index]++ || digest_escaped || !digest || digest_length != 71 || memcmp(digest, "sha256:", 7) != 0)
		return 0;
	for (int i = 7; i < 71; i++)
		if (!((digest[i] >= '0' && digest[i] <= '9') || (digest[i] >= 'a' && digest[i] <= 'f')))
			return 0;
	char* output = index == 0 ? manifest_hash : package_hash;
	memcpy(output, digest + 7, 64);
	output[64] = 0;
	return 1;
}

int update_release_digests(const char* data, size_t length, const char* manifest_name, const char* package_name, char manifest_hash[65], char package_hash[65]) {
	json_reader reader = { data, data + length };
	int assets_seen = 0, found[2] = { 0 };
	if (!json_take(&reader, '{'))
		return 0;
	if (!json_take(&reader, '}')) {
		do {
			const char* key;
			size_t key_length;
			int escaped;
			if (!json_string(&reader, &key, &key_length, &escaped) || !json_take(&reader, ':'))
				return 0;
			if (json_equals(key, key_length, escaped, "assets")) {
				if (assets_seen++ || !json_take(&reader, '['))
					return 0;
				if (!json_take(&reader, ']')) {
					do {
						if (!json_asset(&reader, manifest_name, package_name, manifest_hash, package_hash, found))
							return 0;
						if (json_take(&reader, ']'))
							break;
						if (!json_take(&reader, ','))
							return 0;
					} while (1);
				}
			} else if (!json_skip(&reader, 0))
				return 0;
			if (json_take(&reader, '}'))
				break;
			if (!json_take(&reader, ','))
				return 0;
		} while (1);
	}
	json_space(&reader);
	return reader.current == reader.end && assets_seen == 1 && found[0] == 1 && found[1] == 1;
}

static int parse_line(char* line, update_file* file) {
	char *fields[4], *end;
	fields[0] = line;
	for (int i = 1; i < 4; i++) {
		char* tab = strchr(fields[i - 1], '\t');
		if (tab == nullptr)
			return 0;
		*tab = 0;
		fields[i] = tab + 1;
	}
	if (strchr(fields[3], '\t') || strlen(fields[0]) != 64 || !update_valid_path(fields[3]) || strncmp(fields[3], ".hl-", 4) == 0)
		return 0;
	for (int i = 0; i < 64; i++)
		if (!isxdigit((unsigned char)fields[0][i]) || isupper((unsigned char)fields[0][i]))
			return 0;
	if (!*fields[1] || (fields[1][0] == '0' && fields[1][1]))
		return 0;
	for (const char* p = fields[1]; *p; p++)
		if (!isdigit((unsigned char)*p))
			return 0;
	errno = 0;
	file->size = strtoull(fields[1], &end, 10);
	if (*end || errno == ERANGE)
		return 0;
	if (strcmp(fields[2], "-") == 0)
		file->executable = 0;
	else if (strcmp(fields[2], "executable") == 0)
		file->executable = 1;
	else
		return 0;
	memcpy(file->hash, fields[0], 65);
	file->path = malloc(strlen(fields[3]) + 1);
	if (file->path)
		strcpy(file->path, fields[3]);
	return file->path != nullptr;
}

int update_parse_manifest(const char* data, size_t length, update_manifest* manifest) {
	char *copy, *cursor, *line;
	int header = 0;
	uint64_t total = 0;
	memset(manifest, 0, sizeof(*manifest));
	if (length == 0 || length > 8 * 1024 * 1024 || memchr(data, 0, length) || data[length - 1] != '\n')
		return 0;
	copy = malloc(length + 1);
	if (copy == nullptr)
		return 0;
	memcpy(copy, data, length);
	copy[length] = 0;
	cursor = copy;
	while (*cursor) {
		line = cursor;
		cursor = strchr(cursor, '\n');
		if (cursor == nullptr)
			break;
		*cursor++ = 0;
		if (strchr(line, '\r'))
			goto fail;
		if (header == 0) {
			if (strcmp(line, "format\t1"))
				goto fail;
			header = 1;
		} else if (header == 1) {
			if (strncmp(line, "platform\t", 9) || strlen(line + 9) >= sizeof(manifest->platform) || !*(line + 9))
				goto fail;
			strcpy(manifest->platform, line + 9);
			header = 2;
		} else if (header == 2) {
			if (strncmp(line, "commit\t", 7) || strlen(line + 7) != 40)
				goto fail;
			for (const char* p = line + 7; *p; p++)
				if (!isxdigit((unsigned char)*p) || isupper((unsigned char)*p))
					goto fail;
			strcpy(manifest->commit, line + 7);
			header = 3;
		} else if (header == 3) {
			if (*line)
				goto fail;
			header = 4;
		} else {
			if (manifest->count >= 8192)
				goto fail;
			update_file file = { 0 };
			if (!parse_line(line, &file))
				goto fail;
			if (file.size > 4ULL * 1024 * 1024 * 1024 - total) {
				free(file.path);
				goto fail;
			}
			total += file.size;
			if (update_find_file(manifest, file.path) != nullptr) {
				free(file.path);
				goto fail;
			}
			if ((strncmp(file.path, "bin/", 4) == 0 && update_find_file(manifest, file.path + 4))) {
				free(file.path);
				goto fail;
			}
			for (size_t i = 0; i < manifest->count; i++)
				if (strncmp(manifest->files[i].path, "bin/", 4) == 0 && same_path(manifest->files[i].path + 4, file.path)) {
					free(file.path);
					goto fail;
				}
			update_file* files = realloc(manifest->files, (manifest->count + 1) * sizeof(update_file));
			if (files == nullptr) {
				free(file.path);
				goto fail;
			}
			manifest->files = files;
			manifest->files[manifest->count++] = file;
		}
	}
	free(copy);
	if (header == 4 && manifest->count)
		return 1;
	update_free_manifest(manifest);
	return 0;
fail:
	free(copy);
	update_free_manifest(manifest);
	return 0;
}

update_status update_classify(const update_file* latest, const update_file* previous, int exists, int matches) {
	if (latest) {
		if (!exists)
			return previous ? UPDATE_MISSING : UPDATE_NEW;
		if (matches)
			return UPDATE_OK;
		return previous ? UPDATE_MODIFIED : UPDATE_CONFLICT;
	}
	return exists && matches ? UPDATE_REMOVE : UPDATE_KEEP;
}

int update_select_platform(char name[24], const char** executable) {
#if defined(_WIN32) && defined(_M_X64)
	strcpy(name, "win64");
	*executable = "hl.exe";
	return 1;
#elif defined(_WIN32) && defined(__x86_64__)
	strcpy(name, "win64");
	*executable = "hl.exe";
	return 1;
#elif defined(__linux__) && !defined(__ANDROID__) && defined(__x86_64__)
	strcpy(name, "linux-amd64");
	*executable = "hl";
	return 1;
#elif defined(__linux__) && !defined(__ANDROID__) && defined(__aarch64__)
	strcpy(name, "linux-arm64");
	*executable = "hl";
	return 1;
#else
	(void)name;
	(void)executable;
	return 0;
#endif
}

int update_resolve_root(const char* executable_path, char* root, size_t capacity, int* prefix) {
	const char* slash = strrchr(executable_path, '/');
	if (slash == nullptr)
		return 0;
	size_t length = (size_t)(slash - executable_path);
	*prefix = length >= 4 && memcmp(executable_path + length - 4, "/bin", 4) == 0;
#ifdef _WIN32
	*prefix = 0;
#endif
	if (*prefix)
		length -= 4;
	if (length == 0)
		length = 1;
#ifdef _WIN32
	if (length == 2 && executable_path[1] == ':')
		length = 3;
#endif
	if (length >= capacity)
		return 0;
	memcpy(root, executable_path, length);
	root[length] = 0;
	return 1;
}

int update_install_path(const char* root, int prefix, const char* relative, char* target, size_t capacity) {
	const char* directory = "";
	if (!update_valid_path(relative))
		return 0;
	if (!prefix && strncmp(relative, "bin/", 4) == 0)
		relative += 4;
	if (prefix) {
		if (strcmp(relative, "hl") == 0)
			directory = "bin/";
		else if (strncmp(relative, "bin/", 4) != 0 && strncmp(relative, "include/", 8) != 0 && strncmp(relative, "share/", 6) != 0)
			directory = prefix == 2 ? "lib64/" : "lib/";
	}
	int count = snprintf(target, capacity, "%s%s%s%s", root, root[strlen(root) - 1] == '/' ? "" : "/", directory, relative);
	return count >= 0 && (size_t)count < capacity;
}
