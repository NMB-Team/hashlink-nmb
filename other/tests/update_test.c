#include "update_internal.h"
#include "update_sha256.h"
#include "../../include/minizip-ng/mz.h"
#include "../../include/minizip-ng/mz_strm.h"
#include "../../include/minizip-ng/mz_zip.h"
#include "../../include/minizip-ng/mz_zip_rw.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#ifdef __linux__
#include <zlib.h>
#endif
#endif

static void manifest_tests(void) {
	char text[1024];
	const char* hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	snprintf(text, sizeof(text), "format\t1\nplatform\twin64\ncommit\t0123456789abcdef0123456789abcdef01234567\n\n%s\t3\texecutable\thl.exe\n%s\t4\t-\tlibhl.dll\n", hash, hash);
	update_manifest manifest;
	assert(update_parse_manifest(text, strlen(text), &manifest));
	assert(manifest.count == 2);
	assert(update_find_file(&manifest, "hl.exe") != NULL);
	assert(update_find_file(&manifest, "custom_plugin.hdll") == NULL);
	update_free_manifest(&manifest);

	char duplicate[1024];
	snprintf(duplicate, sizeof(duplicate), "format\t1\nplatform\twin64\ncommit\t0123456789abcdef0123456789abcdef01234567\n\n%s\t3\t-\thl.exe\n%s\t3\t-\thl.exe\n", hash, hash);
	assert(!update_parse_manifest(duplicate, strlen(duplicate), &manifest));
	char traversal[1024];
	snprintf(traversal, sizeof(traversal), "format\t1\nplatform\twin64\ncommit\t0123456789abcdef0123456789abcdef01234567\n\n%s\t3\t-\t../bad.xx\n", hash);
	assert(!update_parse_manifest(traversal, strlen(traversal), &manifest));
	snprintf(traversal, sizeof(traversal), "format\t1\nplatform\twin64\ncommit\t0123456789abcdef0123456789abcdef01234567\n\n%s\tinvalid\t-\thl.exe\n", hash);
	assert(!update_parse_manifest(traversal, strlen(traversal), &manifest));
	snprintf(traversal, sizeof(traversal), "format\t1\nplatform\twin64\ncommit\t0123456789abcdef0123456789abcdef01234567\n\n%s\t3\tunknown\thl.exe\n", hash);
	assert(!update_parse_manifest(traversal, strlen(traversal), &manifest));
	snprintf(traversal, sizeof(traversal), "format\t1\nplatform\twin64\ncommit\t0123456789abcdef0123456789abcdef01234567\n\n%s\t3\t-\t/absolute\n", hash);
	assert(!update_parse_manifest(traversal, strlen(traversal), &manifest));
	assert(!update_valid_path("/absolute"));
	assert(!update_valid_path("a/../../b"));
	assert(!update_valid_path("a\\b"));
	assert(update_valid_path("include/hl.h"));
	assert(!update_parse_manifest("format\t2\n", 9, &manifest));
}

static void release_digest_tests(void) {
	const char* manifest_name = "hashlink-latest-win64.manifest";
	const char* package_name = "hashlink-latest-win64.zip";
	const char* manifest_digest = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	const char* package_digest = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	char json[1024], manifest_hash[65], package_hash[65];
	snprintf(json, sizeof(json),
	         "{\"body\":\"text with \\\"assets\\\"\",\"assets\":[{\"name\":\"other.zip\",\"digest\":null},"
	         "{\"digest\":\"sha256:%s\",\"name\":\"%s\"},{\"name\":\"%s\",\"digest\":\"sha256:%s\",\"size\":12}]} ",
	         package_digest, package_name, manifest_name, manifest_digest);
	assert(update_release_digests(json, strlen(json), manifest_name, package_name, manifest_hash, package_hash));
	assert(strcmp(manifest_hash, manifest_digest) == 0);
	assert(strcmp(package_hash, package_digest) == 0);
	assert(!update_release_digests(json, strlen(json) - 3, manifest_name, package_name, manifest_hash, package_hash));
	snprintf(json, sizeof(json), "{\"assets\":[{\"name\":\"%s\",\"digest\":\"sha256:%s\"}]}", package_name, package_digest);
	assert(!update_release_digests(json, strlen(json), manifest_name, package_name, manifest_hash, package_hash));
	snprintf(json, sizeof(json),
	         "{\"assets\":[{\"name\":\"%s\",\"digest\":\"sha256:%s\"},"
	         "{\"name\":\"%s\",\"digest\":\"sha256:%s\"},{\"name\":\"%s\",\"digest\":\"sha256:%s\"}]}",
	         manifest_name, manifest_digest, manifest_name, manifest_digest, package_name, package_digest);
	assert(!update_release_digests(json, strlen(json), manifest_name, package_name, manifest_hash, package_hash));
	snprintf(json, sizeof(json),
	         "{\"assets\":[{\"name\":\"%s\",\"digest\":\"md5:%s\"},"
	         "{\"name\":\"%s\",\"digest\":\"sha256:%s\"}]}",
	         manifest_name, manifest_digest, package_name, package_digest);
	assert(!update_release_digests(json, strlen(json), manifest_name, package_name, manifest_hash, package_hash));
}

static void hash_and_status_tests(void) {
	update_sha256 hash;
	unsigned char digest[32];
	char hex[65];
	update_sha256_init(&hash);
	update_sha256_add(&hash, "abc", 3);
	update_sha256_finish(&hash, digest);
	update_sha256_hex(digest, hex);
	assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
	update_file latest = { 0 }, old = { 0 };
	assert(update_classify(&latest, &old, 1, 1) == UPDATE_OK);
	assert(update_classify(&latest, &old, 1, 0) == UPDATE_MODIFIED);
	assert(update_classify(&latest, &old, 0, 0) == UPDATE_MISSING);
	assert(update_classify(&latest, NULL, 0, 0) == UPDATE_NEW);
	assert(update_classify(&latest, NULL, 1, 0) == UPDATE_CONFLICT);
	assert(update_classify(&latest, NULL, 1, 1) == UPDATE_OK);
	assert(update_classify(NULL, &old, 1, 1) == UPDATE_REMOVE);
	assert(update_classify(NULL, &old, 1, 0) == UPDATE_KEEP);
	assert(update_classify(NULL, &old, 0, 0) == UPDATE_KEEP);
	char path[128];
#ifdef _WIN32
	snprintf(path, sizeof(path), "update-file-test-%lu.tmp", GetCurrentProcessId());
#else
	snprintf(path, sizeof(path), "update-file-test-%ld.tmp", (long)getpid());
#endif
	FILE* file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite("abc", 1, 3, file) == 3);
	assert(fclose(file) == 0);
	latest.size = 3;
	strcpy(latest.hash, hex);
	int exists;
	assert(update_file_matches(path, &latest, &exists) && exists);
	file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite("abd", 1, 3, file) == 3);
	assert(fclose(file) == 0);
	assert(!update_file_matches(path, &latest, &exists) && exists);
	assert(remove(path) == 0);
	assert(!update_file_matches(path, &latest, &exists) && !exists);
}

static void path_tests(void) {
	char platform[24], root[256], target[256];
	const char* executable;
	assert(update_archive_path("hashlink-build/hl.exe", "win64", target, sizeof(target)));
	assert(strcmp(target, "hl.exe") == 0);
	assert(update_archive_path("hashlink-build/bin/libhl.dll", "win64", target, sizeof(target)));
	assert(strcmp(target, "libhl.dll") == 0);
	assert(update_archive_path("hashlink-build/bin/hl", "linux-amd64", target, sizeof(target)));
	assert(strcmp(target, "hl") == 0);
	assert(update_archive_path("hashlink-build/lib/libhl.so", "linux-amd64", target, sizeof(target)));
	assert(strcmp(target, "libhl.so") == 0);
	assert(!update_archive_path("hashlink-build/../escape", "linux-amd64", target, sizeof(target)));
#if defined(_WIN32) || defined(__linux__)
	assert(update_select_platform(platform, &executable));
#ifdef _WIN32
	assert(strcmp(platform, "win64") == 0);
	assert(update_resolve_root("C:/Tools/hashlink/hl.exe", root, sizeof(root), (int[1]){ 0 }));
	assert(strcmp(root, "C:/Tools/hashlink") == 0);
#else
	assert(strcmp(platform, "linux-amd64") == 0 || strcmp(platform, "linux-arm64") == 0);
#endif
#endif
	int prefix;
	assert(update_resolve_root("/usr/local/bin/hl", root, sizeof(root), &prefix));
#ifndef _WIN32
	assert(prefix && strcmp(root, "/usr/local") == 0);
	assert(update_install_path(root, prefix, "fmt.hdll", target, sizeof(target)));
	assert(strcmp(target, "/usr/local/lib/fmt.hdll") == 0);
	assert(update_install_path(root, 2, "fmt.hdll", target, sizeof(target)));
	assert(strcmp(target, "/usr/local/lib64/fmt.hdll") == 0);
	assert(update_install_path(root, 1, "bin/hl-tool", target, sizeof(target)));
	assert(strcmp(target, "/usr/local/bin/hl-tool") == 0);
#endif
	assert(update_resolve_root("/opt/hashlink/hl", root, sizeof(root), &prefix));
	assert(!prefix);
	assert(update_install_path(root, prefix, "fmt.hdll", target, sizeof(target)));
	assert(strcmp(target, "/opt/hashlink/fmt.hdll") == 0);
	assert(update_install_path(root, prefix, "bin/hl-tool", target, sizeof(target)));
	assert(strcmp(target, "/opt/hashlink/hl-tool") == 0);
	assert(!update_install_path(root, prefix, "../escape", target, sizeof(target)));
#if defined(_WIN32) || defined(__linux__)
	char running[4096];
	assert(update_executable_path(running, sizeof(running)));
	assert(strstr(running, "update_test") != NULL);
	assert(update_resolve_root(running, root, sizeof(root), &prefix));
#endif
}

static void write_zip(const char* archive, const char* name, const char* contents) {
	void* writer = mz_zip_writer_create();
	assert(writer != NULL);
	assert(mz_zip_writer_open_file(writer, archive, 0, 0) == MZ_OK);
	mz_zip_file directory = { 0 };
	directory.filename = "hashlink-build/";
	directory.filename_size = (uint16_t)strlen(directory.filename);
	directory.compression_method = MZ_COMPRESS_METHOD_STORE;
	assert(mz_zip_writer_add_buffer(writer, "", 0, &directory) == MZ_OK);
	mz_zip_file entry = { 0 };
	entry.filename = name;
	entry.filename_size = (uint16_t)strlen(name);
	entry.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
	entry.flag = MZ_ZIP_FLAG_UTF8;
	assert(mz_zip_writer_add_buffer(writer, contents, (int32_t)strlen(contents), &entry) == MZ_OK);
	assert(mz_zip_writer_close(writer) == MZ_OK);
	mz_zip_writer_delete(&writer);
}

static void archive_tests(void) {
	char archive[128], stage[128], extracted[160];
#ifdef _WIN32
	const char *managed = "hl.exe", *entry = "hashlink-build/hl.exe", *platform = "win64";
#else
	const char *managed = "hl", *entry = "hashlink-build/bin/hl", *platform = "linux-amd64";
#endif
#ifdef _WIN32
	unsigned long pid = GetCurrentProcessId();
	snprintf(archive, sizeof(archive), "update-archive-%lu.zip", pid);
	snprintf(stage, sizeof(stage), "update-stage-%lu", pid);
	assert(_mkdir(stage) == 0);
#else
	long pid = (long)getpid();
	snprintf(archive, sizeof(archive), "update-archive-%ld.zip", pid);
	snprintf(stage, sizeof(stage), "update-stage-%ld", pid);
	assert(mkdir(stage, 0700) == 0);
#endif
	snprintf(extracted, sizeof(extracted), "%s/%s", stage, managed);
	update_file file = { 0 };
	file.path = (char*)managed;
	file.size = 3;
	strcpy(file.hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	update_manifest manifest = { 0 };
	manifest.files = &file;
	manifest.count = 1;
	strcpy(manifest.platform, platform);
	write_zip(archive, entry, "abc");
	assert(update_extract_package(archive, stage, &manifest));
	assert(remove(extracted) == 0);
	assert(remove(archive) == 0);
	write_zip(archive, entry, "abd");
	assert(!update_extract_package(archive, stage, &manifest));
	assert(remove(extracted) == 0);
	assert(remove(archive) == 0);
	write_zip(archive, "../evil", "abc");
	assert(!update_extract_package(archive, stage, &manifest));
	assert(remove(archive) == 0);
#ifdef _WIN32
	assert(_rmdir(stage) == 0);
#else
	assert(rmdir(stage) == 0);
#endif
}

#ifdef __linux__
static void tar_entry(gzFile archive, const char* path, char type, const char* link, const char* content) {
	unsigned char header[512] = { 0 };
	size_t size = content ? strlen(content) : 0;
	assert(strlen(path) < 100);
	memcpy(header, path, strlen(path));
	snprintf((char*)header + 100, 8, "%07o", 0755);
	snprintf((char*)header + 124, 12, "%011llo", (unsigned long long)size);
	memset(header + 148, ' ', 8);
	header[156] = (unsigned char)type;
	if (link) {
		assert(strlen(link) < 100);
		memcpy(header + 157, link, strlen(link));
	}
	memcpy(header + 257, "ustar", 5);
	unsigned checksum = 0;
	for (size_t i = 0; i < sizeof(header); i++)
		checksum += header[i];
	snprintf((char*)header + 148, 8, "%06o", checksum);
	assert(gzwrite(archive, header, sizeof(header)) == sizeof(header));
	if (size) {
		unsigned char block[512] = { 0 };
		memcpy(block, content, size);
		assert(gzwrite(archive, block, sizeof(block)) == sizeof(block));
	}
}

static void tar_archive_tests(void) {
	char archive[128], stage[128], extracted[160];
	long pid = (long)getpid();
	snprintf(archive, sizeof(archive), "update-tar-%ld.tar.gz", pid);
	snprintf(stage, sizeof(stage), "update-tar-stage-%ld", pid);
	assert(mkdir(stage, 0700) == 0);
	update_file files[2] = { { 0 }, { 0 } };
	files[0].path = "libhl.so.2";
	files[1].path = "libhl.so";
	for (size_t i = 0; i < 2; i++) {
		files[i].size = 3;
		strcpy(files[i].hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	}
	update_manifest manifest = { 0 };
	manifest.files = files;
	manifest.count = 2;
	strcpy(manifest.platform, "linux-amd64");
	gzFile writer = gzopen(archive, "wb");
	assert(writer != NULL);
	tar_entry(writer, "hashlink-build/lib/libhl.so.2", '0', NULL, "abc");
	tar_entry(writer, "hashlink-build/lib/libhl.so", '2', "libhl.so.2", NULL);
	unsigned char end[1024] = { 0 };
	assert(gzwrite(writer, end, sizeof(end)) == sizeof(end));
	assert(gzclose(writer) == Z_OK);
	assert(update_extract_package(archive, stage, &manifest));
	for (size_t i = 0; i < 2; i++) {
		int exists;
		snprintf(extracted, sizeof(extracted), "%s/%s", stage, files[i].path);
		assert(update_file_matches(extracted, &files[i], &exists) && exists);
		assert(remove(extracted) == 0);
	}
	assert(remove(archive) == 0);
	writer = gzopen(archive, "wb");
	assert(writer != NULL);
	tar_entry(writer, "hashlink-build/../escape", '0', NULL, "abc");
	assert(gzwrite(writer, end, sizeof(end)) == sizeof(end));
	assert(gzclose(writer) == Z_OK);
	assert(!update_extract_package(archive, stage, &manifest));
	assert(remove(archive) == 0);
	assert(rmdir(stage) == 0);
}
#endif

static void write_test_file(const char* path, const char* text) {
	FILE* file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(text, 1, strlen(text), file) == strlen(text));
	assert(fclose(file) == 0);
}

static void installation_tests(void) {
	char root[128], stage[128], path[256], platform[24];
	const char* executable;
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
	assert(update_select_platform(platform, &executable));
#else
	assert(!update_select_platform(platform, &executable));
	return;
#endif
#ifdef _WIN32
	unsigned long pid = GetCurrentProcessId();
	snprintf(root, sizeof(root), "update-install-%lu", pid);
	snprintf(stage, sizeof(stage), "update-source-%lu", pid);
	assert(_mkdir(root) == 0 && _mkdir(stage) == 0);
#else
	long pid = (long)getpid();
	snprintf(root, sizeof(root), "update-install-%ld", pid);
	snprintf(stage, sizeof(stage), "update-source-%ld", pid);
	assert(mkdir(root, 0700) == 0 && mkdir(stage, 0700) == 0);
#endif
	const char* hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	update_file old_files[3] = { { 0 }, { 0 }, { 0 } }, new_files[2] = { { 0 }, { 0 } };
	old_files[0].path = (char*)executable;
	old_files[1].path = "oldcodec.hdll";
	old_files[2].path = "modified.hdll";
	for (int i = 0; i < 3; i++) {
		strcpy(old_files[i].hash, hash);
		old_files[i].size = 3;
	}
	old_files[0].executable = 1;
	new_files[0] = old_files[0];
	new_files[1] = old_files[1];
	new_files[1].path = "fmt.hdll";
	update_manifest previous = { 0 }, latest = { 0 };
	previous.files = old_files;
	previous.count = 3;
	latest.files = new_files;
	latest.count = 2;
	snprintf(path, sizeof(path), "%s/%s", root, executable);
	write_test_file(path, "abc");
#ifndef _WIN32
	assert(chmod(path, 0755) == 0);
#endif
	snprintf(path, sizeof(path), "%s/oldcodec.hdll", root);
	write_test_file(path, "abc");
	snprintf(path, sizeof(path), "%s/modified.hdll", root);
	write_test_file(path, "abd");
	snprintf(path, sizeof(path), "%s/custom_plugin.hdll", root);
	write_test_file(path, "custom");
	snprintf(path, sizeof(path), "%s/fmt.hdll", stage);
	write_test_file(path, "abc");
	snprintf(path, sizeof(path), "%s/.hl-manifest", stage);
	write_test_file(path, "new manifest");
	assert(update_install_release(root, 0, stage, &latest, &previous, 0) == 0);
	int exists;
	snprintf(path, sizeof(path), "%s/fmt.hdll", root);
	assert(update_file_matches(path, &new_files[1], &exists) && exists);
	assert(remove(path) == 0);
	snprintf(path, sizeof(path), "%s/oldcodec.hdll", root);
	assert(!update_file_matches(path, &old_files[1], &exists) && !exists);
	snprintf(path, sizeof(path), "%s/modified.hdll", root);
	assert(!update_file_matches(path, &old_files[2], &exists) && exists);
	assert(remove(path) == 0);
	snprintf(path, sizeof(path), "%s/custom_plugin.hdll", root);
	FILE* file = fopen(path, "rb");
	assert(file != NULL);
	assert(fclose(file) == 0);
	assert(remove(path) == 0);
	snprintf(path, sizeof(path), "%s/.hl-manifest", root);
	file = fopen(path, "rb");
	assert(file != NULL);
	assert(fclose(file) == 0);
	assert(remove(path) == 0);
	snprintf(path, sizeof(path), "%s/%s", root, executable);
	assert(remove(path) == 0);
	snprintf(path, sizeof(path), "%s/fmt.hdll", stage);
	assert(remove(path) == 0);
	snprintf(path, sizeof(path), "%s/.hl-manifest", stage);
	assert(remove(path) == 0);
#ifdef _WIN32
	assert(_rmdir(root) == 0 && _rmdir(stage) == 0);
#else
	assert(rmdir(root) == 0 && rmdir(stage) == 0);
#endif
}

#ifdef __MINGW32__
int wmain(void) {
#else
int main(void) {
#endif
	manifest_tests();
	release_digest_tests();
	hash_and_status_tests();
	path_tests();
	archive_tests();
#ifdef __linux__
	tar_archive_tests();
#endif
	installation_tests();
	puts("updater core tests passed");
	return 0;
}
