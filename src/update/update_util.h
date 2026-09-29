#ifndef HL_UPDATE_UTIL_H
#define HL_UPDATE_UTIL_H

#include "update_internal.h"
#define UPDATE_PATH_MAX 4096

#ifdef _WIN32
#include <wchar.h>
wchar_t* update_to_wide(const char* value);
char* update_to_utf8(const wchar_t* value);
#endif
int update_path_join(char* out, size_t capacity, const char* base, const char* part);
int update_file_info(const char* path, uint64_t* size, int* regular);
int update_read_file(const char* path, char** data, size_t limit, size_t* length);
int update_safe_target(const char* root, const char* target);
int update_create_stage(char stage[UPDATE_PATH_MAX]);
int update_write_stage_owner(const char* stage, const char* root);
int update_remove_tree(const char* path);
int update_load_manifest(const char* path, update_manifest* manifest);
int update_manifests_same(const update_manifest* a, const update_manifest* b);
int update_save_manifest(const char* root, const char* source, const char* destination);
int update_verify_stage(const char* stage, const update_manifest* manifest);
int update_extract_archive(const char* archive, const char* stage, const update_manifest* manifest, int standalone);
int update_remove_package(const char* root, int prefix, const update_manifest* manifest, const char* manifest_name);
int update_apply_release(const char* root, int prefix, const char* stage, const update_manifest* latest, const update_manifest* previous, int force, const char* manifest_name, const char* executable);
int update_package_owned(const char* root, int prefix, const update_manifest* manifest, const update_manifest* other);
int update_current_root(char root[UPDATE_PATH_MAX], int* prefix);
int update_download_verified(const char* base, const char* name, const char* target, uint64_t limit, const char* expected, int show_progress);
int update_fetch_manifest(const char* api, const char* base, const char* name, const char* package, const char* stage, const char* manifest_name, update_manifest* manifest, char package_hash[65]);

#endif
