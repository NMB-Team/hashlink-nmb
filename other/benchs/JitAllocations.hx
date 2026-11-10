private class AllocationValue {
	public var value:Int;
	public inline function new(value:Int) {
		this.value = value;
	}
}

class JitAllocations {
	@:neverInline static function objectValue(value:Int):Int {
		var object = new AllocationValue(value);
		return object.value;
	}

	@:neverInline static function boxedValue(value:Int):Int {
		var box:Dynamic = value;
		return (box:Int);
	}

	static function main() {
		var sum = 0;
		for (i in 0...10000000) {
			sum += objectValue(1);
			sum += boxedValue(1);
		}
		if (sum != 20000000) throw "invalid allocation benchmark result";
		Sys.println(sum);
	}
}
