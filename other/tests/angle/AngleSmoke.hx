import sdl.Angle;
import sdl.AngleBackend;
import sdl.GL;
import sdl.GLContextProvider;
import sdl.Sdl;
import sdl.Window;

class AngleSmoke {
	static function main():Void {
		final backend:AngleBackend = Sys.systemName() == "Mac" ? Metal : Vulkan;
		Sdl.configureGLProvider(GLContextProvider.Angle, backend);

		if (Sys.getEnv("ANGLE_SMOKE_SKIP_CONTEXT") == "1") {
			if (!sdl.Angle.isAvailable() || !sdl.Angle.isEnabled())
				throw "ANGLE support was not enabled.";
			if (sdl.Angle.getActiveBackend() != backend)
				throw "ANGLE did not select the requested backend.";

			final revision = sdl.Angle.getRevision();
			if (revision == null || revision.length == 0 || revision == "unknown")
				throw "ANGLE revision metadata is unavailable.";

			Sys.println('ANGLE revision: $revision');
			Sys.println('ANGLE backend: ${backendName(sdl.Angle.getActiveBackend())}');
			return;
		}

		Sdl.init();

		final window = new Window("ANGLE smoke test", 320, 240, Window.SDL_WINDOWPOS_CENTERED, Window.SDL_WINDOWPOS_CENTERED, Window.SDL_WINDOW_HIDDEN);
		final capabilities = GL.getCapabilities();

		Sys.println('ANGLE revision: ${sdl.Angle.getRevision()}');
		Sys.println('ANGLE backend: ${backendName(sdl.Angle.getActiveBackend())}');
		Sys.println('GL_VENDOR: ${GL.getParameter(GL.VENDOR)}');
		Sys.println('GL_RENDERER: ${GL.getParameter(GL.RENDERER)}');
		Sys.println('GL_VERSION: ${GL.getParameter(GL.VERSION)}');
		Sys.println('GL_SHADING_LANGUAGE_VERSION: ${GL.getParameter(GL.SHADING_LANGUAGE_VERSION)}');

		if (!capabilities.isGLES || !capabilities.isANGLE)
			throw "The smoke test did not create an ANGLE GLES context.";

		GL.clearColor(0.1, 0.2, 0.3, 1.0);
		for (_ in 0...60) {
			GL.clear(GL.COLOR_BUFFER_BIT | GL.DEPTH_BUFFER_BIT);
			final error = GL.getError();
			if (error != 0)
				throw 'ANGLE smoke test failed with GL error $error.';
			window.present();
		}

		window.destroy();
		Sdl.quit();
	}

	private static function backendName(backend:AngleBackend):String {
		return switch (backend) {
			case Vulkan: "Vulkan";
			case Metal: "Metal";
			case Auto: "Auto";
			default: "Unknown";
		};
	}
}
