package sdl;

enum abstract AngleBackend(Int) from Int to Int {
	final Auto = 0;
	final Vulkan = 1;
	final Metal = 2;
}
