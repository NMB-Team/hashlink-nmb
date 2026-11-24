class JitThrow {
	static function throwOnly(error:haxe.Exception):Void {
		throw error;
	}

	static function rethrow(error:haxe.Exception):Void {
		try {
			throwOnly(error);
		} catch (caught:haxe.Exception) {
			if (caught != error)
				throw new haxe.Exception("Changed exception");
			throw caught;
		}
	}

	static function main():Void {
		final error = new haxe.Exception("expected");
		try {
			rethrow(error);
			throw new haxe.Exception("Exception was swallowed");
		} catch (caught:haxe.Exception) {
			if (caught != error)
				throw caught;
		}
	}
}
