@:result(20000000)
class JitLoops {
	static function main() {
		var sum = 0;
		for (i in 0...20000000) {
			var a = i & 1;
			var b = i & 1;
			sum += a + b;
		}

		Benchs.result(sum);
	}
}
