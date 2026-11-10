@:result(120000000)
class JitArithmetic {
	@:neverInline static function arithmetic(a:Int, b:Int):Int {
		var x = a * b;
		var y = a * b;
		return x + y;
	}

	static function main() {
		var sum = 0;
		for (i in 0...20000000)
			sum += arithmetic(i & 1, 6);

		Benchs.result(sum);
	}
}
