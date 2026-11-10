@:result(40000000)
class JitSmallCalls {
	@:neverInline static function add(a:Int, b:Int):Int {
		return a + b;
	}

	static function main() {
		var sum = 0;
		for (i in 0...20000000)
			sum = add(sum, 2);

		Benchs.result(sum);
	}
}
