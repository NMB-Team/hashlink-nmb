#include "limen_update.h"
#include "update_util.h"
#include "../hl.h"

#include <stdio.h>
#include <string.h>

#define LIMEN_MANIFEST ".limen-manifest"
#define LIMEN_API "https://api.github.com/repos/NMB-Team/Limen/releases/tags/latest"
#define LIMEN_BASE "https://github.com/NMB-Team/Limen/releases/download/latest/"

static int fail(const char* message) {
	fprintf(stderr, "Limen: %s\n", message);
	return 1;
}

int limen_select_platform(char platform[24]) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#ifdef __MINGW32__
	strcpy(platform, "windows-x64-mingw");
#else
	strcpy(platform, "windows-x64");
#endif
#elif defined(__linux__) && !defined(__ANDROID__) && defined(__x86_64__)
	strcpy(platform, "linux-x64");
#elif defined(__linux__) && !defined(__ANDROID__) && defined(__aarch64__)
	strcpy(platform, "linux-arm64");
#elif defined(__APPLE__) && defined(__x86_64__)
	strcpy(platform, "macos-x64");
#elif defined(__APPLE__) && defined(__aarch64__)
	strcpy(platform, "macos-arm64");
#else
	(void)platform;
	return 0;
#endif
	return 1;
}

static int load_installed(const char* root, const char* name, update_manifest* manifest, int* exists) {
	char path[UPDATE_PATH_MAX];
	uint64_t size;
	int regular;
	if (!update_path_join(path, sizeof(path), root, name) || !update_safe_target(root, path))
		return 0;
	*exists = update_file_info(path, &size, &regular);
	return !*exists || (regular && update_load_manifest(path, manifest));
}

static int limen_manage(const char* root, int prefix, const char* platform, const char* command) {
	update_manifest previous = { 0 }, latest = { 0 }, hashlink = { 0 };
	int installed, hl_installed, result = 1;
	char stage[UPDATE_PATH_MAX] = "";
	if (!load_installed(root, LIMEN_MANIFEST, &previous, &installed) ||
	    !load_installed(root, ".hl-manifest", &hashlink, &hl_installed)) {
		fail("invalid installed manifest or unsafe manifest path");
		goto done;
	}
	if (installed && strcmp(previous.platform, platform)) {
		fail("installed platform does not match this executable");
		goto done;
	}
	if (!update_package_owned(root, prefix, &previous, &hashlink)) {
		fail("unsafe managed path or HashLink ownership conflict");
		goto done;
	}
	if (strcmp(command, "status") == 0) {
		if (!installed) {
			puts("Limen: not installed");
			result = 0;
			goto done;
		}
		printf("Limen: installed\nCommit: %s\nPlatform: %s\n", previous.commit, previous.platform);
		size_t missing = 0, modified = 0;
		for (size_t i = 0; i < previous.count; i++) {
			char target[UPDATE_PATH_MAX];
			int exists;
			if (!update_install_path(root, prefix, previous.files[i].path, target, sizeof(target)))
				goto done;
			if (!update_file_matches(target, &previous.files[i], &exists)) {
				printf("%s %s\n", exists ? "MODIFIED" : "MISSING", previous.files[i].path);
				missing += !exists;
				modified += exists;
			}
		}
		printf("Missing: %zu\nModified: %zu\n", missing, modified);
		result = 0;
		goto done;
	}
	if (strcmp(command, "install") != 0 && !installed) {
		fail("not installed; run hl limen install first");
		goto done;
	}
	if (strcmp(command, "install") == 0 && installed) {
		fail("already installed; run hl limen update");
		goto done;
	}
	if (strcmp(command, "remove") == 0) {
		result = update_remove_package(root, prefix, &previous, LIMEN_MANIFEST);
		goto done;
	}
	if (!update_create_stage(stage)) {
		fail("could not create staging directory");
		goto done;
	}
	char manifest_name[128], package_name[128], archive[UPDATE_PATH_MAX], package_hash[65];
	snprintf(manifest_name, sizeof(manifest_name), "limen-%s.manifest", platform);
	snprintf(package_name, sizeof(package_name), "limen-%s.%s", platform, strncmp(platform, "windows-", 8) == 0 ? "zip" : "tar.gz");
	if (!update_fetch_manifest(LIMEN_API, LIMEN_BASE, manifest_name, package_name, stage, LIMEN_MANIFEST, &latest, package_hash)) {
		fail("release download, SHA-256, or manifest verification failed");
		goto done;
	}
	if (strcmp(latest.platform, platform) || !update_find_file(&latest, "limen.hdll") ||
	    !update_package_owned(root, prefix, &latest, &hashlink)) {
		fail("invalid release platform, unsafe path, or HashLink ownership conflict");
		goto done;
	}
	if (strcmp(command, "reinstall") != 0 && installed && update_manifests_same(&latest, &previous)) {
		int current = 1;
		for (size_t i = 0; i < latest.count; i++) {
			char target[UPDATE_PATH_MAX];
			int exists;
			if (!update_install_path(root, prefix, latest.files[i].path, target, sizeof(target)) ||
			    !update_file_matches(target, &latest.files[i], &exists)) {
				current = 0;
				break;
			}
		}
		if (current) {
			printf("Limen is up to date: %s\n", latest.commit);
			result = 0;
			goto done;
		}
	}
	if (!update_path_join(archive, sizeof(archive), stage, package_name) ||
	    !update_download_verified(LIMEN_BASE, package_name, archive, 2ULL * 1024 * 1024 * 1024, package_hash) ||
	    !update_extract_archive(archive, stage, &latest, 1)) {
		fail("archive SHA-256, extraction, or staged file verification failed");
		goto done;
	}
	result = update_apply_release(root, prefix, stage, &latest, &previous, strcmp(command, "reinstall") == 0, LIMEN_MANIFEST, nullptr);
	if (result == 0)
		printf("Limen %s: %s\n", strcmp(command, "reinstall") == 0 ? "reinstalled" : "installed", latest.commit);
done:
	update_free_manifest(&previous);
	update_free_manifest(&latest);
	update_free_manifest(&hashlink);
	if (stage[0] && !update_remove_tree(stage)) {
		fail("staging cleanup failed");
		result = 1;
	}
	return result;
}

int hl_limen_command(int argc,
#ifdef _WIN32
                     const wchar_t* const* argv
#else
                     const char* const* argv
#endif
) {
	const char* command = nullptr;
	const char* commands[] = { "install", "update", "reinstall", "status", "remove" };
	if (argc == 3) {
		for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
#ifdef _WIN32
			const wchar_t* wide[] = { L"install", L"update", L"reinstall", L"status", L"remove" };
			if (wcscmp(argv[1], L"limen") == 0 && wcscmp(argv[2], wide[i]) == 0)
#else
			if (strcmp(argv[1], "limen") == 0 && strcmp(argv[2], commands[i]) == 0)
#endif
				command = commands[i];
		}
	}
	if (!command)
		return fail("usage: hl limen install | update | reinstall | status | remove");
	char platform[24], root[UPDATE_PATH_MAX];
	int prefix;
	if (!limen_select_platform(platform))
		return fail("unsupported platform");
	if (!update_current_root(root, &prefix))
		return fail("could not resolve the HashLink installation root");
	return limen_manage(root, prefix, platform, command);
}
