typedef Config = {
	version:Int,
	libs:Array<String>,
	defines:haxe.DynamicAccess<String>,
	files:Array<String>
}

enum abstract BuildTemplate(String) to String {
	final CMake = "cmake";
	final Make = "make";
	final Vs2022 = "vs2022";
	final Vs2026 = "vs2026";
	final HxCpp = "hxcpp";

	public static function parse(value:Null<String>):Null<BuildTemplate> {
		return switch value {
			case "cmake": CMake;
			case "make": Make;
			case "vs2022": Vs2022;
			case "vs2026": Vs2026;
			case "hxcpp": HxCpp;
			case _: null;
		};
	}

	public inline function isLegacy():Bool {
		return this == Make || this == Vs2022 || this == Vs2026;
	}
}

final class Build {
	var output:String;
	var name:String;
	var sourcesDir:String;
	var targetDir:String;
	var dataPath:String;
	var config:Config;

	public function new(dataPath, output, config) {
		this.output = output;
		this.config = config;
		this.dataPath = dataPath;

		final path = new haxe.io.Path(output);
		this.name = path.file;
		this.sourcesDir = path.dir + "/";
		this.targetDir = this.sourcesDir;
	}

	private function log(message:String) {
		if (config.defines.get("hlgen.silent") == null)
			Sys.println(message);
	}

	private function warnLegacyTemplate(tpl:BuildTemplate) {
		log('Warning: build template "$tpl" is deprecated; use "${CMake}" instead.');
	}

	static function getDefaultTemplate():BuildTemplate {
		return CMake;
	}

	public function generate() {
		var tpl = config.defines.get("hlgen.makefile");
		if (tpl != null) {
			if (tpl == "1") {
				final defaultSystem = getDefaultTemplate();
				tpl = defaultSystem;
				config.defines["hlgen.makefile"] = tpl;
			}

			final tplParse = BuildTemplate.parse(tpl);

			if (tplParse != null && tplParse.isLegacy())
				warnLegacyTemplate(tplParse);

			generateTemplates(tpl);
		}

		log('Code generated in $output');
	}

	public function compile() {
		final tpl = config.defines.get("hlgen.makefile");
		if (tpl == null) {
			log('Set -D hlgen.makefile for automatic native compilation');
			return 0;
		}

		final tplParse = BuildTemplate.parse(tpl);
		if (tplParse == null) {
			log('Automatic native compilation not yet implemented for $tpl');
			return 0;
		}

		return switch tplParse {
			case CMake:
				compileCmake();
			case Make:
				compileMake();
			case HxCpp:
				compileHxcpp();
			case Vs2022, Vs2026:
				compileVs(tplParse);
		};
	}

	private function compileCmake() {
		final configuration = config.defines.exists("debug") ? "Debug" : "Release";
		final system = Sys.systemName();
		final isWindows = system == "Windows";
		final isMac = system == "Mac" || system == "MacOS";

		final requestedArch = config.defines.get("hlgen.build.architecture");
		final cmakeArch:String = switch requestedArch {
			case "x86_64", "x64":
				"x86_64";
			case "arm64", "aarch64":
				"arm64";
			case null:
				isWindows ? "x86_64" : "native";
			case unsupported:
				log('Unsupported hlgen.build.architecture=$unsupported for CMake');
				return 1;
		}

		final sourceDir = sys.FileSystem.absolutePath(targetDir).split("\\").join("/");

		var cmakeCommand = config.defines.get("hlgen.cmake.command");
		if (cmakeCommand == null || cmakeCommand.length == 0)
			cmakeCommand = "cmake";

		var configuredGenerator = config.defines.get("hlgen.cmake.generator");
		final requestedVsVersion = config.defines.get("hlgen.cmake.vsversion");

		if (requestedVsVersion != null && requestedVsVersion.length > 0) {
			if (!isWindows) {
				log("hlgen.cmake.vsversion is only supported on Windows");
				return 1;
			}
			if (configuredGenerator != null && configuredGenerator.length > 0) {
				log("hlgen.cmake.vsversion cannot be combined with hlgen.cmake.generator");
				return 1;
			}

			configuredGenerator = switch requestedVsVersion {
				case "2022", "17", "17.0":
					"Visual Studio 17 2022";
				case "2026", "18", "18.0":
					"Visual Studio 18 2026";
				case unsupported:
					log('Unsupported hlgen.cmake.vsversion=$unsupported; expected 2022 or 2026');
					return 1;
			}
		}
		var generator = configuredGenerator;
		if (generator == null || generator.length == 0)
			generator = Sys.getEnv("CMAKE_GENERATOR");

		final isMultiConfig = (generator != null
			&& (StringTools.startsWith(generator, "Visual Studio") || generator == "Xcode" || generator == "Ninja Multi-Config"))
			|| (isWindows && (generator == null || generator.length == 0));
		final generatorName = switch generator {
			case null | "":
				"default";
			case g:
				~/[^A-Za-z0-9._-]+/g.replace(g.toLowerCase(), "-");
		};
		final toolchain = config.defines.get("hlgen.cmake.toolchain");

		final toolchainName = toolchain == null
			|| toolchain.length == 0 ? "native" : "toolchain-" + haxe.crypto.Sha1.encode(normalizePath(toolchain)).substr(0, 8);
		final buildDir = sourceDir + "/.cmake-build/" + cmakeArch + "/" + configuration.toLowerCase() + "/" + generatorName + "/" + toolchainName;

		final configureArgs = ["-S", sourceDir, "-B", buildDir, "-DHLC_ARCH=" + cmakeArch,];
		if (!isMultiConfig)
			configureArgs.push("-DCMAKE_BUILD_TYPE=" + configuration);
		if (configuredGenerator != null && configuredGenerator.length > 0) {
			configureArgs.push("-G");
			configureArgs.push(configuredGenerator);
		}
		final hashlink = Sys.getEnv("HASHLINK");
		if (hashlink != null && hashlink.length > 0) {
			final hashlinkPaths = [];
			for (path in hashlink.split(";")) {
				if (path.length == 0)
					continue;

				hashlinkPaths.push(sys.FileSystem.absolutePath(path).split("\\").join("/"));
			}

			configureArgs.push("-DHASHLINK=" + hashlinkPaths.join(";"));
		}

		final nativePath = config.defines.get("hlgen.cmake.nativePath");
		if (nativePath != null && nativePath.length > 0)
			configureArgs.push("-DHLC_NATIVE_PATH=" + nativePath);
		if (toolchain != null && toolchain.length > 0)
			configureArgs.push("-DCMAKE_TOOLCHAIN_FILE=" + toolchain);

		if (isWindows) {
			final visualStudioGenerator = generator == null || generator.length == 0 || StringTools.startsWith(generator, "Visual Studio");
			if (visualStudioGenerator) {
				configureArgs.push("-A");
				configureArgs.push(switch cmakeArch {
					case "arm64": "ARM64";
					case _: "x64";
				});
			}
		} else if (isMac) {
			switch cmakeArch {
				case "x86_64":
					configureArgs.push("-DCMAKE_OSX_ARCHITECTURES=x86_64");

				case "arm64":
					configureArgs.push("-DCMAKE_OSX_ARCHITECTURES=arm64");
				case _:
			}
		}
		final configureStamp = buildDir + "/.hlgen-configure";

		final configureKey = haxe.crypto.Sha1.encode(cmakeCommand + "\n" + (generator ?? "") + "\n" + (toolchain ?? "") + "\n" + configureArgs.join("\n"));
		final cachedKey = try sys.io.File.getContent(configureStamp) catch (e:Dynamic) null;
		final cmakeCacheExists = sys.FileSystem.exists(buildDir + "/CMakeCache.txt");
		final needsConfigure = !cmakeCacheExists || cachedKey != configureKey;
		if (needsConfigure) {
			log(cmakeCommand + " " + configureArgs.join(" "));
			final code = Sys.command(cmakeCommand, configureArgs);
			if (code != 0)
				return code;
			sys.io.File.saveContent(configureStamp, configureKey);
		} else
			log("CMake configuration unchanged, skipping configure");

		final buildArgs = ["--build", buildDir, "--parallel", "--target", "hlc_app",];
		if (isMultiConfig) {
			buildArgs.push("--config");
			buildArgs.push(configuration);
		}
		log(cmakeCommand + " " + buildArgs.join(" "));
		return Sys.command(cmakeCommand, buildArgs);
	}

	private function compileVs(system:BuildTemplate) {
		final versions = switch (system) {
			case Vs2022: ["[17.0,18.0]", "[18.0,19.0]"];
			case Vs2026: ["[18.0,19.0]"];
			case _: throw 'Invalid Visual Studio build system: $system';
		}
		var code = 0;
		var msbuild = "";
		var selectedVersion = "";
		var vswhereError = "";
		for (version in versions) {
			final vswhereProc = new sys.io.Process("C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe", [
				"-requires",
				"Microsoft.Component.MSBuild",

				"-products",
				"*",
				"-latest",

				"-find",
				"MSBuild\\Current\\Bin\\MSBuild.exe",
				"-version",
				version
			]);
			if (vswhereProc.exitCode() == 0) {
				msbuild = StringTools.trim(try vswhereProc.stdout.readLine().toString() catch (e:haxe.io.Eof) "");
				if (msbuild.length > 0) {
					selectedVersion = version;
					vswhereProc.close();
					break;
				}
			} else
				vswhereError += vswhereProc.stdout.readAll().toString() + vswhereProc.stderr.readAll().toString();

			vswhereProc.close();
		}

		if (msbuild.length > 0) {
			final prevCwd = Sys.getCwd();
			final platform = 'x64';
			final configuration = config.defines.exists("debug") ? "Debug" : "Release";

			final msbuildArgs = [
				'$name.sln',
				'-t:$name',
				"-nologo",
				"-verbosity:minimal",

				'-property:Configuration=$configuration',
				'-property:Platform=$platform'
			];
			if (system == Vs2022 && selectedVersion == versions[1])
				msbuildArgs.push("-property:PlatformToolset=v145");
			log('"$msbuild"' + " " + msbuildArgs.join(" "));

			Sys.setCwd(targetDir);
			code = Sys.command(msbuild, msbuildArgs);
			Sys.setCwd(prevCwd);
		} else {
			log('Failed to find a valid MSbuild installation for template $system.');
			if (vswhereError.length > 0)
				log("vswhere error: " + vswhereError);

			code = 1;
		}
		return code;
	}

	private function compileMake() {
		return Sys.command("make", ["-j", "-C", targetDir].concat(config.defines.exists("debug") ? ["DEBUG=1"] : []));
	}

	private function compileHxcpp() {
		return Sys.command("haxelib", ["--cwd", targetDir, "run", "hxcpp", "Build.xml"].concat(config.defines.exists("debug") ? ["-Ddebug"] : []));
	}

	private function isBinary(bytes:haxe.io.Bytes):Bool {
		var i = 0;
		if (bytes.length >= 3 && bytes.get(0) == 0xEF && bytes.get(1) == 0xBB && bytes.get(2) == 0xBF)
			i = 3;
		final len = bytes.length;
		while (i < len)
			if (bytes.get(i++) == 0)
				return true;

		return false;
	}

	private function generateTemplates(?tpl) {
		var srcDir = tpl;
		final jumboBuild = switch config.defines.get("hlgen.makefile.jumbo") {
			case "1": "true";
			case value: value;
		};

		var relDir = "";
		if (config.defines["hlgen.makefilepath"] != null) {
			targetDir = config.defines.get("hlgen.makefilepath");
			if (!StringTools.endsWith(targetDir, "/") && !StringTools.endsWith(targetDir, "\\"))
				targetDir += "/";

			final sourcesAbs = normalizePath(sourcesDir);
			final targetAbs = normalizePath(targetDir);

			if (sourcesAbs == targetAbs) {
				relDir = "";
			} else if (StringTools.startsWith(sourcesAbs, targetAbs + "/")) {
				relDir = sourcesAbs.substr(targetAbs.length + 1) + "/";
			} else {
				relDir = sourcesAbs + "/";
			}
		}
		if (!sys.FileSystem.exists(srcDir)) {
			srcDir = dataPath + "templates/" + tpl;
			if (!sys.FileSystem.exists(srcDir))
				throw "Failed to find make template '" + tpl + "'";
		}

		if (isPathInside(targetDir, srcDir))
			throw 'Template $tpl contains $targetDir, can cause recursive generation';

		final fileSet = new Map<String, Bool>();
		for (f in config.files)
			fileSet.set(f, true);

		for (f in config.files) {
			if (!StringTools.endsWith(f, ".c"))
				continue;
			final h = f.substr(0, -2) + ".h";

			if (sys.FileSystem.exists(sourcesDir + h))
				fileSet.set(h, true);
		}
		final allFiles = [for (f in fileSet.keys()) f];

		allFiles.sort(Reflect.compare);
		final files = [for (f in allFiles) {path: f, directory: new haxe.io.Path(f).dir}];
		final directories = new Map();
		for (f in files)
			if (f.directory != null)
				directories.set(f.directory, true);

		for (k in directories.keys()) {
			final dir = k.split("/");
			dir.pop();
			while (dir.length > 0) {
				final p = dir.join("/");
				directories.set(p, true);
				dir.pop();
			}
		}

		final directories = [for (k in directories.keys()) {path: k}];
		directories.sort(function(a, b) return Reflect.compare(a.path, b.path));

		final libraries = [
			for (l in config.libs)
				if (l != "std") {name: l}

		];
		final cfiles = [
			for (f in files)
				if (StringTools.endsWith(f.path, ".c")) f

		];
		final hfiles = [
			for (f in files)
				if (StringTools.endsWith(f.path, ".h")) f
		];
		function genRec(path:String) {
			final dir = srcDir + "/" + path;
			for (f in sys.FileSystem.readDirectory(dir)) {
				final parts = f.split(".");
				var isBin = parts.length >= 2 && parts[parts.length - 2] == "bin";
				var isOnce = parts.length >= 2 && parts[parts.length - 2] == "once";

				final srcPath = dir + "/" + f;
				final f = (isBin || isOnce) ? {parts.splice(parts.length - 2, 1); parts.join(".");} : f;
				final targetPath = targetDir + path + "/" + f.split("__file__").join(name);
				if (sys.FileSystem.isDirectory(srcPath)) {
					try sys.FileSystem.createDirectory(targetPath) catch (e:Dynamic) {};
					genRec(path + "/" + f);
					continue;
				}
				final bytes = sys.io.File.getBytes(srcPath);
				if (!isBin && isBinary(bytes))
					isBin = true;
				if (isOnce && sys.FileSystem.exists(targetPath))
					continue;
				if (isBin) {
					sys.io.File.copy(srcPath, targetPath);
					continue;
				}
				var content = bytes.toString();
				final tpl = new haxe.Template(content);

				final context = {
					name: this.name,

					libraries: libraries,
					files: files,
					relDir: relDir,
					directories: directories,
					cfiles: cfiles,
					hfiles: hfiles,

					jumboBuild: jumboBuild,
				};

				final macros = {
					makeUID: (_, s:String) -> {
						final sha1 = haxe.crypto.Sha1.encode(s).toUpperCase();

						return sha1.substr(0, 8)
							+ "-"
							+ sha1.substr(8, 4)

							+ "-"
							+ sha1.substr(12, 4)
							+ "-"

							+ sha1.substr(16, 4)
							+ "-"
							+ sha1.substr(20, 12);
					},
					makePath: (_, dir:String) -> return dir == "" ? "." : (StringTools.endsWith(dir, "/") || StringTools.endsWith(dir, "\\")) ? dir : dir
						+ "/",
					upper: (_, s:String) -> return s.charAt(0).toUpperCase() + s.substr(1),
					winPath: (_, s:String) -> return s.split("/").join("\\"),
					getEnv: (_, s:String) -> return Sys.getEnv(s),
					setDefaultJumboBuild: (_, b:String) -> {
						context.jumboBuild ??= b;
						return "";
					}
				};
				content = tpl.execute(context, macros);
				final prevContent = try sys.io.File.getContent(targetPath) catch (e:Dynamic) null;
				if (prevContent != content)
					sys.io.File.saveContent(targetPath, content);
			}
		}
		genRec(".");
	}

	private static function normalizePath(path:String):String {
		var result = sys.FileSystem.absolutePath(path).split("\\").join("/");
		while (result.length > 1 && StringTools.endsWith(result, "/"))
			result = result.substr(0, result.length - 1);

		if (Sys.systemName() == "Windows")
			result = result.toLowerCase();
		return result;
	}

	private static function isPathInside(path:String, parent:String):Bool {
		final normalizedPath = normalizePath(path);
		final normalizedParent = normalizePath(parent);
		return normalizedPath == normalizedParent || StringTools.startsWith(normalizedPath, normalizedParent + "/");
	}
}

class Run {
	static function applyCliDefines(config:Config, args:Array<String>) {
		while (args.length > 0) {
			switch args.shift() {
				case '-D' | '--define':
					final pair = args.shift();
					final equalsPosition = pair.indexOf("=");
					if (equalsPosition == -1) {
						config.defines.set(pair, "1");
					} else {
						final name = pair.substr(0, equalsPosition);
						final value = pair.substr(equalsPosition + 1);
						config.defines.set(name, value);
					}
				case unknown:
					Sys.stderr().writeString('Warning: Unrecognised argument $unknown\n');
					Sys.stderr().flush();
			}
		}
	}

	static function main() {
		final args = Sys.args();
		final originalPath = args.pop();
		final haxelibPath = Sys.getCwd() + "/";
		Sys.setCwd(originalPath);
		switch (args.shift()) {
			case "build":
				final output = args.shift();
				final path = new haxe.io.Path(output);
				path.file = "hlc";
				path.ext = "json";
				final config:Config = haxe.Json.parse(sys.io.File.getContent(path.toString()));
				applyCliDefines(config, args);
				final build = new Build(haxelibPath, output, config);
				build.generate();
				Sys.exit(build.compile());
			case "run":
				final output = args.shift();
				if (StringTools.endsWith(output, ".c"))
					return;
				Sys.exit(Sys.command("hl", [output]));
			case cmd:
				Sys.println("Unknown command " + cmd);
				Sys.exit(1);
		}
	}
}
