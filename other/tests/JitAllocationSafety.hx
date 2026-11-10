private class AllocationObject {
	public var value:Int;
	public var child:AllocationObject;
	public inline function new(value:Int) {
		this.value = value;
	}
}

class JitAllocationSafety {
	static var saved:Dynamic;

	@:neverInline static function keep(value:Dynamic):Dynamic {
		saved = value;
		hl.Gc.major();
		return value;
	}

	static function check(value:Bool):Void {
		if (!value) throw "allocation safety check failed";
	}

	static function main() {
		var object = new AllocationObject(42);
		var alias = object;
		check(keep(object) == alias);
		alias.value = 19;
		check(object.value == 19);
		object.child = new AllocationObject(7);
		keep(object);
		check(object.child.value == 7);
		check(new AllocationObject(1) != new AllocationObject(1));

		var box:Dynamic = 42;
		check(keep(box) == 42);
		var failed = false;
		try {
			var invalid:AllocationObject = cast box;
			keep(invalid);
		} catch (_:Dynamic) {
			failed = true;
		}
		check(failed);
		check((box:Int) == 42);
		Sys.println("allocation safety passed");
	}
}
