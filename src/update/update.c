#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "update.h"
#include "update_util.h"
#include "update_http.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#endif

#include "../hl.h"

#define UPDATE_BASE "https://github.com/NMB-Team/hashlink-nmb/releases/download/latest/"
#define UPDATE_RELEASE_API "https://api.github.com/repos/NMB-Team/hashlink-nmb/releases/tags/latest"

static int fail(const char* message) {
	fprintf(stderr, "Update failed: %s\n", message);
	return 1;
}

static int hashlink_owned(const char* root, int prefix, const update_manifest* latest, const update_manifest* previous) {
	update_manifest limen = { 0 };
	char path[UPDATE_PATH_MAX];
	uint64_t size;
	int regular;
	if (!update_path_join(path, sizeof(path), root, ".limen-manifest") || !update_safe_target(root, path))
		return 0;
	if (update_file_info(path, &size, &regular) && (!regular || !update_load_manifest(path, &limen)))
		return 0;
	int owned = update_package_owned(root, prefix, latest, &limen) && update_package_owned(root, prefix, previous, &limen);
	update_free_manifest(&limen);
	return owned;
}

int update_install_release(const char* root, int prefix, const char* stage, const update_manifest* latest, const update_manifest* previous, int force) {
	char platform[24];
	const char* executable;
	if (!update_select_platform(platform, &executable))
		return fail("unsupported platform");
	if (!hashlink_owned(root, prefix, latest, previous))
		return fail("Limen ownership conflict, invalid manifest, or unsafe managed path");
	return update_apply_release(root, prefix, stage, latest, previous, force, ".hl-manifest", executable);
}

#ifdef _WIN32
static int spawn_helper(const char* executable, const char* command, unsigned pid, const char* root, const char* stage) {
	wchar_t *program = update_to_wide(executable), *target = update_to_wide(root), *temporary = update_to_wide(stage);
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
	wchar_t* wide_command = update_to_wide(command);
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
	if (!update_executable_path(executable, sizeof(executable)) || !update_path_join(expected, sizeof(expected), cleanup ? root : stage, "hl.exe"))
		return 0;
	if (_stricmp(executable, expected) != 0)
		return 0;
	if (_stricmp(root, stage) == 0)
		return 0;
	wchar_t temp[UPDATE_PATH_MAX];
	DWORD length = GetTempPathW(UPDATE_PATH_MAX, temp);
	char* temporary = length && length < UPDATE_PATH_MAX ? update_to_utf8(temp) : nullptr;
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
	if (!update_path_join(owner_path, sizeof(owner_path), stage, ".hl-update-owner") || !update_read_file(owner_path, &owner, UPDATE_PATH_MAX, &owner_length))
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

	if (!update_path_join(executable, sizeof(executable), root, "hl.exe"))
		return 0;

	wchar_t* program = update_to_wide(executable);
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
	char *root = update_to_utf8(argv[4]), *stage = update_to_utf8(argv[5]);
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
		int cleanup_ok = update_remove_tree(stage);
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
	if (!update_path_join(manifest_path, sizeof(manifest_path), stage, ".hl-manifest") || !update_load_manifest(manifest_path, &latest) || !update_select_platform(platform, &executable) || strcmp(latest.platform, platform) || !update_verify_stage(stage, &latest))
		goto done;
	const update_file* staged_executable = update_find_file(&latest, executable);
	if (!staged_executable || !staged_executable->executable)
		goto done;
	if (!update_path_join(manifest_path, sizeof(manifest_path), root, ".hl-manifest"))
		goto done;
	uint64_t size;
	int regular;
	if (update_file_info(manifest_path, &size, &regular) && (!regular || !update_load_manifest(manifest_path, &previous)))
		goto done;
	if (previous.count && (strcmp(previous.platform, platform) != 0 || !update_find_file(&previous, executable)))
		goto done;
	int prefix = 0;
	result = update_install_release(root, prefix, stage, &latest, &previous, 1);
	if (result == 0) {
		char installed[UPDATE_PATH_MAX];
		if (!update_path_join(installed, sizeof(installed), root, "hl.exe") || !spawn_helper(installed, "--internal-update-cleanup", GetCurrentProcessId(), root, stage))
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
	char platform[24], root[UPDATE_PATH_MAX], stage[UPDATE_PATH_MAX];
	const char* executable;
	int prefix;
	if (!update_select_platform(platform, &executable))
		return fail("self-update is not supported on this platform");
	if (!update_current_root(root, &prefix))
		return fail("could not resolve the running hl executable");
	if (!update_create_stage(stage))
		return fail("could not create a staging directory");
	if (!update_write_stage_owner(stage, root)) {
		update_remove_tree(stage);
		return fail("could not prepare staging directory");
	}
	int result = 1;
	update_manifest latest = { 0 }, previous = { 0 };
	char name[128], package_name[128], manifest_path[UPDATE_PATH_MAX], local_path[UPDATE_PATH_MAX];
	char package_hash[65];
	snprintf(name, sizeof(name), "hashlink-latest-%s.manifest", platform);
	snprintf(package_name, sizeof(package_name), "hashlink-latest-%s.%s", platform, strcmp(platform, "win64") == 0 ? "zip" : "tar.gz");
	if (!update_path_join(manifest_path, sizeof(manifest_path), stage, ".hl-manifest") ||
	    !update_fetch_manifest(UPDATE_RELEASE_API, UPDATE_BASE, name, package_name, stage, ".hl-manifest", &latest, package_hash)) {
		fail("release metadata, manifest download, SHA-256, or format verification failed");
		goto done;
	}
	const update_file* latest_executable = update_find_file(&latest, executable);
	if (strcmp(latest.platform, platform) != 0 || !latest_executable || !latest_executable->executable) {
		fail("release manifest does not match this platform");
		goto done;
	}
	if (!update_path_join(local_path, sizeof(local_path), root, ".hl-manifest"))
		goto done;
	uint64_t size;
	int regular;
	if (update_file_info(local_path, &size, &regular) && (!regular || !update_load_manifest(local_path, &previous))) {
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
	if (!hashlink_owned(root, prefix, &latest, &previous)) {
		fail("Limen ownership conflict, invalid manifest, or unsafe managed path");
		goto done;
	}
	printf("HashLink NMB updater\n\nCurrent: %.7s\nLatest:  %.7s\n\nChecking installation...\n", current_commit, latest.commit);
	size_t changes = 0;
	size_t conflicts = 0;
	for (size_t i = 0; i < latest.count; i++) {
		const update_file* file = &latest.files[i];
		char path[UPDATE_PATH_MAX];
		int exists;
		if (!update_install_path(root, prefix, file->path, path, sizeof(path)) || !update_safe_target(root, path)) {
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
		if (!update_install_path(root, prefix, file->path, path, sizeof(path)) || !update_safe_target(root, path)) {
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
		if (!check && !update_manifests_same(&latest, &previous) && !update_save_manifest(root, manifest_path, local_path)) {
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
	if (!update_path_join(package_path, sizeof(package_path), stage, package_name))
		goto done;
	printf("Downloading %s...\n", package_name);
	if (!update_download_verified(UPDATE_BASE, package_name, package_path, 2ULL * 1024 * 1024 * 1024, package_hash)) {
		fail("package download or SHA-256 verification failed");
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

	if (!update_path_join(staged_executable, sizeof(staged_executable), stage, "hl.exe") || !spawn_helper(staged_executable, "--internal-update-apply", GetCurrentProcessId(), root, stage)) {
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
		update_remove_tree(stage);
	return result;
}
