import hashlib
import pathlib
import sys
import tarfile
import zipfile

def entries(archive):
	if archive.name.endswith(".tar.gz"):
		with tarfile.open(archive, "r:gz") as package:
			for member in package:
				if member.isdir():
					continue
				if not (member.isfile() or member.issym() or member.islnk()):
					raise ValueError(f"unsupported archive member: {member.name}")
				source = package.extractfile(member)
				if source is None:
					raise ValueError(f"unreadable archive member: {member.name}")
				yield member.name, source, bool(member.mode & 0o111)
				source.close()
	else:
		with zipfile.ZipFile(archive) as package:
			for member in package.infolist():
				if member.is_dir():
					continue
				with package.open(member) as source:
					yield member.filename, source, False

def relative_path(name, platform):
	parts = name.split("/")
	if (len(parts) < 2 or any(not part or part in (".", "..") for part in parts)
			or not name.isascii() or any(ord(ch) < 32 or ord(ch) > 126 or ch in '\\:<>"|?*' for ch in name)):
		raise ValueError(f"unsafe archive path: {name}")
	parts = parts[1:]
	if parts[0] in ("lib", "lib64") or (parts[0] == "bin" and (platform == "win64" or parts[1:] == ["hl"])):
		parts = parts[1:]
	if not parts:
		raise ValueError(f"empty archive path: {name}")
	path = "/".join(parts)
	if (len(path) > 512 or not path.isascii() or any(ord(ch) < 32 or ord(ch) > 126 or ch in '\\:<>"|?*' for ch in path)
			or path.startswith(".hl-")):
		raise ValueError(f"unsupported updater path: {path}")
	return path

def main():
	archive, platform, commit, output = sys.argv[1:]
	archive = pathlib.Path(archive)
	output = pathlib.Path(output)
	output.mkdir(parents=True, exist_ok=True)
	if platform not in ("win64", "linux-amd64", "linux-arm64"):
		raise ValueError("unsupported updater platform")
	if len(commit) != 40 or any(ch not in "0123456789abcdef" for ch in commit):
		raise ValueError("release commit must be a lowercase full SHA")

	manifest_path = output / f"hashlink-latest-{platform}.manifest"
	records = []
	seen = set()
	install_paths = set()
	for original, source, executable in entries(archive):
		path = relative_path(original, platform)
		if path in seen:
			raise ValueError(f"duplicate packaged path: {path}")
		install_path = path[4:] if path.startswith("bin/") else path
		if install_path in install_paths:
			raise ValueError(f"duplicate portable install path: {install_path}")
		seen.add(path)
		install_paths.add(install_path)
		executable = executable or path in ("hl", "hl.exe")
		digest = hashlib.sha256()
		size = 0
		while chunk := source.read(1024 * 1024):
			digest.update(chunk)
			size += len(chunk)
		records.append((path, digest.hexdigest(), size, "executable" if executable else "-"))

	if ("hl.exe" if platform == "win64" else "hl") not in seen:
		raise ValueError("package has no hl executable")
	with manifest_path.open("w", encoding="utf-8", newline="\n") as manifest:
		manifest.write(f"format\t1\nplatform\t{platform}\ncommit\t{commit}\n\n")
		for path, digest, size, flags in sorted(records):
			manifest.write(f"{digest}\t{size}\t{flags}\t{path}\n")

if __name__ == "__main__":
	main()
