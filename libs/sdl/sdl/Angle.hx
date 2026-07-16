package sdl;

class Angle {
	public static function configure(backend:AngleBackend = Auto, debugLayers:Bool = false):Void {
		if (!isAvailable())
			throw "ANGLE support is not available in this sdl.hdll build.";
		_configure(backend, debugLayers);
	}

	public static function isAvailable():Bool {
		return _isAvailable();
	}

	public static function isEnabled():Bool {
		return isAvailable() && _isEnabled();
	}

	public static function getRequestedBackend():AngleBackend {
		return isAvailable() ? _getRequestedBackend() : Auto;
	}

	public static function getActiveBackend():AngleBackend {
		return isAvailable() ? _getActiveBackend() : Auto;
	}

	public static function getLastError():Null<String> {
		return isAvailable() ? fromBytes(_getLastError()) : null;
	}

	public static function getRevision():Null<String> {
		return isAvailable() ? fromBytes(_getRevision()) : null;
	}

	private static function fromBytes(value:hl.Bytes):Null<String> {
		return value == null ? null : @:privateAccess String.fromUTF8(value);
	}

	@:hlNative("?sdl", "angle_configure")
	private static function _configure(backend:AngleBackend, debugLayers:Bool):Void {
	}

	@:hlNative("?sdl", "angle_is_available")
	private static function _isAvailable():Bool {
		return false;
	}

	@:hlNative("?sdl", "angle_is_enabled")
	private static function _isEnabled():Bool {
		return false;
	}

	@:hlNative("?sdl", "angle_get_requested_backend")
	private static function _getRequestedBackend():AngleBackend {
		return Auto;
	}

	@:hlNative("?sdl", "angle_get_active_backend")
	private static function _getActiveBackend():AngleBackend {
		return Auto;
	}

	@:hlNative("?sdl", "angle_get_last_error")
	private static function _getLastError():hl.Bytes {
		return null;
	}

	@:hlNative("?sdl", "angle_get_revision")
	private static function _getRevision():hl.Bytes {
		return null;
	}
}
