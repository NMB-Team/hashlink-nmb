import hashlib
import io
import json
import os
import pathlib
import platform as host
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile

RUNNER = pathlib.Path(sys.argv[1]).resolve()
COMMIT = "0123456789abcdef0123456789abcdef01234567"
PLATFORM = subprocess.check_output([str(RUNNER), "--platform"], text=True).strip()
EXPECTED = ("windows-x64-mingw" if sys.argv[2] == "1" else "windows-x64") if os.name == "nt" else (
	("macos" if sys.platform == "darwin" else "linux") + ("-arm64" if host.machine().lower() in ("aarch64", "arm64") else "-x64")
)
assert PLATFORM == EXPECTED, (PLATFORM, EXPECTED)

def manifest(files, commit=COMMIT, platform=PLATFORM):
	return f"format\t1\nplatform\t{platform}\ncommit\t{commit}\n\n" + "".join(
		f"{hashlib.sha256(data).hexdigest()}\t{len(data)}\t-\t{name}\n" for name, data in files.items()
	)

with tempfile.TemporaryDirectory() as temporary:
	base = pathlib.Path(temporary)
	root = base / "HashLink root"
	root.mkdir()
	release = base / "release"
	release.mkdir()
	executable = root / ("hl.exe" if os.name == "nt" else "hl")
	shutil.copy2(RUNNER, executable)
	env = dict(os.environ, HL_TEST_RELEASE=str(release))
	hl_platform = "win64" if os.name == "nt" else "linux-arm64" if PLATFORM == "linux-arm64" else "linux-amd64"
	hl_manifest = manifest({executable.name: executable.read_bytes()}, platform=hl_platform).replace("\t-\t", "\texecutable\t")
	(root / ".hl-manifest").write_text(hl_manifest, encoding="utf-8", newline="\n")

	def run(*args, code=0, contains=None):
		result = subprocess.run([str(executable), *args], env=env, capture_output=True, text=True)
		assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
		if contains:
			assert contains in result.stdout + result.stderr, (args, result.stdout, result.stderr)
		assert (root / ".hl-manifest").read_text() == hl_manifest
		return result

	def publish(files, commit=COMMIT, text=None, archive_files=None, bad_digest=None, symlink=False):
		name = f"limen-{PLATFORM}"
		package = release / (name + (".zip" if os.name == "nt" else ".tar.gz"))
		entries = files if archive_files is None else archive_files
		if os.name == "nt":
			with zipfile.ZipFile(package, "w") as archive:
				for path, data in entries.items():
					if symlink:
						member = zipfile.ZipInfo(path)
						member.create_system = 3
						member.external_attr = (stat.S_IFLNK | 0o777) << 16
						archive.writestr(member, data)
					else:
						archive.writestr(path, data)
		else:
			with tarfile.open(package, "w:gz", format=tarfile.USTAR_FORMAT) as archive:
				for path, data in entries.items():
					member = tarfile.TarInfo("./" + path)
					if symlink:
						member.type = tarfile.SYMTYPE
						member.linkname = "outside"
						archive.addfile(member)
					else:
						member.size = len(data)
						archive.addfile(member, io.BytesIO(data))
		metadata = release / (name + ".manifest")
		metadata.write_text(manifest(files, commit) if text is None else text, encoding="utf-8", newline="\n")
		assets = []
		for asset in (metadata, package):
			digest = hashlib.sha256(asset.read_bytes()).hexdigest()
			if bad_digest == asset.suffix or bad_digest == "both":
				digest = "0" * 64
			assets.append({"name": asset.name, "digest": "sha256:" + digest})
		(release / "release.json").write_text(json.dumps({"assets": assets}))

	print("CLI parsing and platform selection")
	for args in ((), ("limen",), ("limen", "unknown"), ("limen", "status", "extra"), ("limen", "remove", "--force")):
		run(*args, code=1, contains="usage")
	run("limen", "status", contains="not installed")
	run("limen", "update", code=1, contains="not installed")
	run("limen", "reinstall", code=1, contains="not installed")
	run("limen", "remove", code=1, contains="not installed")

	print("Install and repair")
	first = {"limen.hdll": b"Limen v1", "opengl.limen": b"renderer", "old.limen": b"obsolete"}
	publish(first)
	run("limen", "install")
	run("limen", "install", code=1, contains="already installed")
	run("limen", "update", contains="up to date")
	run("limen", "status", contains=COMMIT)
	for name, data in first.items():
		assert (root / name).read_bytes() == data
	(root / "opengl.limen").unlink()
	(root / "limen.hdll").write_bytes(b"corrupted")
	status = run("limen", "status").stdout
	assert "Missing: 1" in status and "Modified: 1" in status
	run("limen", "update")
	assert (root / "opengl.limen").read_bytes() == first["opengl.limen"]
	assert (root / "limen.hdll").read_bytes() == first["limen.hdll"]
	run("limen", "update", contains="up to date")
	run("limen", "reinstall", contains="reinstalled")
	assert (root / "limen.hdll").read_bytes() == first["limen.hdll"]

	if sys.platform != "darwin":
		print("HashLink updater preserves Limen ownership")
		def publish_hashlink(files):
			name = f"hashlink-latest-{hl_platform}"
			metadata = release / (name + ".manifest")
			metadata.write_text(manifest(files, platform=hl_platform).replace(f"\t-\t{executable.name}\n", f"\texecutable\t{executable.name}\n"), newline="\n")
			package = release / (name + (".zip" if os.name == "nt" else ".tar.gz"))
			if os.name == "nt":
				with zipfile.ZipFile(package, "w") as archive:
					for path, data in files.items():
						archive.writestr("hashlink-build/bin/" + path, data)
			else:
				with tarfile.open(package, "w:gz", format=tarfile.USTAR_FORMAT) as archive:
					for path, data in files.items():
						member = tarfile.TarInfo("hashlink-build/bin/" + path if path == "hl" else "hashlink-build/lib/" + path)
						member.size = len(data)
						archive.addfile(member, io.BytesIO(data))
			(release / "release.json").write_text(json.dumps({"assets": [
				{"name": asset.name, "digest": "sha256:" + hashlib.sha256(asset.read_bytes()).hexdigest()}
				for asset in (metadata, package)
			]}))
		limen_state = (root / ".limen-manifest").read_bytes()
		publish_hashlink({executable.name: executable.read_bytes()})
		run("update", "--check")
		publish_hashlink({executable.name: executable.read_bytes(), "limen.hdll": b"HashLink conflict"})
		run("update", "--check", code=1, contains="ownership conflict")
		assert (root / ".limen-manifest").read_bytes() == limen_state
		assert (root / "limen.hdll").read_bytes() == first["limen.hdll"]

	print("Update and obsolete file preservation")
	(root / "old.limen").write_bytes(b"user changes")
	second = {"limen.hdll": b"Limen v2", "vulkan.limen": b"new renderer"}
	publish(second, commit="a" * 40)
	run("limen", "update")
	assert (root / "old.limen").read_bytes() == b"user changes"
	assert not (root / "opengl.limen").exists()
	run("limen", "status", contains="a" * 40)
	run("limen", "update", contains="up to date")
	(root / "vulkan.limen").write_bytes(b"modified before reinstall")
	(root / "unmanaged.limen").write_bytes(b"user file")
	run("limen", "reinstall", contains="reinstalled")
	assert (root / "vulkan.limen").read_bytes() == second["vulkan.limen"]
	assert (root / "unmanaged.limen").read_bytes() == b"user file"

	print("Removal preserves modified and unmanaged files")
	(root / "vulkan.limen").write_bytes(b"user changes")
	(root / "custom.limen").write_bytes(b"unmanaged")
	run("limen", "remove", code=2, contains="manifest retained")
	assert not (root / "limen.hdll").exists()
	assert (root / ".limen-manifest").exists()
	assert (root / "vulkan.limen").read_bytes() == b"user changes"
	(root / "vulkan.limen").unlink()
	run("limen", "remove")
	assert not (root / ".limen-manifest").exists()
	assert (root / "custom.limen").read_bytes() == b"unmanaged"
	assert (root / "old.limen").read_bytes() == b"user changes"

	print("Ownership conflicts and invalid releases")
	for files in ({"limen.hdll": b"valid", executable.name: b"bad"}, {"limen.hdll": b"valid", "bin/" + executable.name: b"bad"}, {"limen.hdll": b"valid", ".hl-manifest": b"bad"}):
		publish(files)
		run("limen", "install", code=1, contains="conflict" if ".hl-manifest" not in files else "verification failed")
		assert not (root / "limen.hdll").exists()
	(root / "limen.hdll").write_bytes(b"unmanaged")
	publish({"limen.hdll": b"valid"})
	run("limen", "install", code=1)
	assert (root / "limen.hdll").read_bytes() == b"unmanaged"
	(root / "limen.hdll").unlink()
	for kwargs in ({"text": "invalid\n"}, {"bad_digest": ".manifest"}, {"bad_digest": ".zip" if os.name == "nt" else ".gz"},
				{"archive_files": {"../escape": b"bad"}}, {"symlink": True}, {"archive_files": {"limen.hdll": b"wrong"}}, {"text": manifest({"limen.hdll": b"valid"}, platform="wrong")},
				{"text": manifest({".limen-manifest": b"bad", "limen.hdll": b"valid"})}):
		publish({"limen.hdll": b"valid"}, **kwargs)
		run("limen", "install", code=1)
		assert not (root / ".limen-manifest").exists()
		assert not (root / "limen.hdll").exists()
		assert not (base / "escape").exists()
	for protected in ("bin/.limen-manifest", "bin/.hl-manifest", ".LIMEN-manifest" if os.name == "nt" else ".limen-other"):
		publish({"limen.hdll": b"valid", protected: b"bad"})
		run("limen", "install", code=1)
		assert not (root / ".limen-manifest").exists()
	publish({"limen.hdll": b"valid"}, text=manifest({"../escape": b"bad"}))
	run("limen", "install", code=1)
	(root / ".limen-manifest").write_text("invalid\n")
	run("limen", "remove", code=1, contains="invalid installed manifest")

print("Limen lifecycle tests passed")
