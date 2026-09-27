#ifndef HL_UPDATE_INTERNAL_H
#define HL_UPDATE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	char hash[65];
	uint64_t size;
	int executable;
	char* path;
} update_file;

typedef struct {
	char commit[41];
	char platform[24];
	update_file* files;
	size_t count;
} update_manifest;

typedef enum { UPDATE_OK, UPDATE_MISSING, UPDATE_MODIFIED, UPDATE_NEW, UPDATE_CONFLICT, UPDATE_REMOVE, UPDATE_KEEP } update_status;

int update_valid_path(const char* path);
int update_archive_path(const char* original, const char* platform, char* relative, size_t capacity);
int update_parse_manifest(const char* data, size_t length, update_manifest* manifest);
int update_release_digests(const char* data, size_t length, const char* manifest_name, const char* package_name, char manifest_hash[65], char package_hash[65]);
void update_free_manifest(update_manifest* manifest);
const update_file* update_find_file(const update_manifest* manifest, const char* path);
update_status update_classify(const update_file* latest, const update_file* previous, int exists, int matches);
int update_select_platform(char name[24], const char** executable);
int update_resolve_root(const char* executable_path, char* root, size_t capacity, int* prefix);
int update_install_path(const char* root, int prefix, const char* relative, char* target, size_t capacity);
int update_executable_path(char* path, size_t capacity);
int update_file_matches(const char* path, const update_file* expected, int* exists);
int update_extract_package(const char* archive, const char* stage, const update_manifest* manifest);
int update_install_release(const char* root, int prefix, const char* stage, const update_manifest* latest, const update_manifest* previous, int force);

#endif
