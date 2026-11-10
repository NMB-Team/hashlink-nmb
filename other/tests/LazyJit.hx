class LazyJit {
	static function sum(a:Int, b:Float, c:Int, d:Float, e:Int, f:Float, g:Int, h:Float):Float {
		return a + b + c + d + e + f + g + h;
	}
	static function work(x:Int):Int {
		for (i in 0...10) x = (x * 3 + i) & 65535;
		return x;
	}
	static function fail() { throw "expected"; }
	static function cold(x:Int):Int { return work(x) + 7; }
	static function main() {
		Sys.println("ready");
		var start = haxe.Timer.stamp();
		var value = work(17);
		Sys.println("first=" + (haxe.Timer.stamp() - start));
		start = haxe.Timer.stamp();
		var result = 0;
		for (i in 0...1000000) result ^= work(i);
		Sys.println("steady=" + (haxe.Timer.stamp() - start));
		var callback = sum;
		if (callback(1,2,3,4,5,6,7,8) != 36) throw "callback arguments";
		for (i in 0...100) if (sum(i,2,3,4,5,6,7,8) != i + 35) throw "cached arguments";
		var recursive:Int->Int = null;
		recursive = function(n) { return n == 0 ? 1 : n * recursive(n-1); };
		if (recursive(6) != 720) throw "recursion";
		try { fail(); } catch (e:Dynamic) {
			if (haxe.CallStack.exceptionStack().length == 0) throw "exception stack";
		}
		var startGate = new sys.thread.Lock();
		var done = new sys.thread.Lock();
		for (i in 0...8) sys.thread.Thread.create(function() {
			startGate.wait();
			if (cold(i) != work(i) + 7) throw "concurrent first call";
			done.release();
		});
		for (i in 0...8) startGate.release();
		for (i in 0...8) if (!done.wait(10)) throw "thread timeout";
		Sys.println("passed=" + result + ":" + value);
	}
}

