import hashlib
import io
import pathlib
import subprocess
import sys
import tarfile
import tempfile
import zipfile

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "tools" / "make_update_manifest.py"
COMMIT = "0123456789abcdef0123456789abcdef01234567"

def verify(output, platform, expected):
	name = f"hashlink-latest-{platform}.manifest"
	manifest = (output / name).read_text(encoding="utf-8").splitlines()
	assert manifest[:4] == ["format\t1", f"platform\t{platform}", f"commit\t{COMMIT}", ""]
	records = {}
	for line in manifest[4:]:
		digest, size, flags, path = line.split("\t")
		records[path] = (digest, int(size), flags)
	assert set(records) == set(expected)
	for path, content in expected.items():
		digest, size, _ = records[path]
		assert digest == hashlib.sha256(content).hexdigest()
		assert size == len(content)
	assert [path.name for path in output.iterdir()] == [name]

with tempfile.TemporaryDirectory() as directory:
	directory = pathlib.Path(directory)
	archive = directory / "windows.zip"
	with zipfile.ZipFile(archive, "w") as package:
		package.writestr("hashlink-build/bin/hl.exe", b"windows executable")
		package.writestr("hashlink-build/bin/libhl.dll", b"windows library")
		package.writestr("hashlink-build/include/hl.h", b"header")
	output = directory / "win-out"
	subprocess.run([sys.executable, str(SCRIPT), str(archive), "win64", COMMIT, str(output)], check=True)
	verify(output, "win64", {"hl.exe": b"windows executable", "libhl.dll": b"windows library", "include/hl.h": b"header"})

	archive = directory / "linux.tar.gz"
	with tarfile.open(archive, "w:gz") as package:
		for name, content in (("hashlink-build/bin/hl", b"linux executable"), ("hashlink-build/bin/hl-tool", b"utility"), ("hashlink-build/lib/libhl.so.2", b"linux library")):
			member = tarfile.TarInfo(name)
			member.size = len(content)
			member.mode = 0o755
			package.addfile(member, io.BytesIO(content))
		link = tarfile.TarInfo("hashlink-build/lib/libhl.so")
		link.type = tarfile.SYMTYPE
		link.linkname = "libhl.so.2"
		package.addfile(link)
	output = directory / "linux-out"
	subprocess.run([sys.executable, str(SCRIPT), str(archive), "linux-arm64", COMMIT, str(output)], check=True)
	verify(output, "linux-arm64", {"hl": b"linux executable", "bin/hl-tool": b"utility", "libhl.so.2": b"linux library", "libhl.so": b"linux library"})

	archive = directory / "unsafe.zip"
	with zipfile.ZipFile(archive, "w") as package:
		package.writestr("hashlink-build/bin/hl.exe", b"windows executable")
		package.writestr("hashlink-build/../escape", b"unsafe")
	result = subprocess.run([sys.executable, str(SCRIPT), str(archive), "win64", COMMIT, str(directory / "unsafe-out")], capture_output=True)
	assert result.returncode != 0

print("updater package tests passed")
