import argparse
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("runtime", type=Path)
parser.add_argument("--haxe", default="haxe")
parser.add_argument("--runs", type=int, default=7)
args = parser.parse_args()
runtime = args.runtime.resolve()
fixture = Path(__file__).with_name("LazyJit.hx")

with tempfile.TemporaryDirectory(prefix="hl-lazy-") as directory:
	root = Path(directory)
	source = fixture.read_text()
	unused = "\n".join(
		f" @:keep public static function unused{i}(x:Int):Int {{ for (j in 0...10) x += j; return x + {i}; }}"
		for i in range(2000)
	)
	(root / "LazyJit.hx").write_text(source.replace(" static function main()", unused + "\n static function main()"))
	bytecode = root / "test.hl"
	subprocess.run([args.haxe, "-cp", str(root), "-main", "LazyJit", "-hl", str(bytecode)], check=True, timeout=60)
	results = {}
	for mode in ("eager", "lazy"):
		samples = []
		for _ in range(args.runs):
			start = time.perf_counter()
			process = subprocess.Popen([str(runtime), f"--{mode}-jit", str(bytecode)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
			assert process.stdout.readline().strip() == "ready", mode
			startup = time.perf_counter() - start
			output, errors = process.communicate(timeout=30)
			assert process.returncode == 0, (mode, output, errors)
			fields = dict(line.split("=", 1) for line in output.splitlines() if "=" in line)
			assert "passed" in fields, (mode, output, errors)
			samples.append((startup, float(fields["first"]), float(fields["steady"])))
		results[mode] = tuple(statistics.median(row[i] for row in samples) for i in range(3))
		print(f"{mode}: startup={results[mode][0]*1000:.3f} ms, first call={results[mode][1]*1000:.3f} ms, steady={results[mode][2]*1000:.3f} ms")
	print("All argument, closure, recursion, exception stack, and thread checks passed")
