#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <hl.h>
HL_API bool hl_obj_delete_field(vdynamic *obj, int hash);
HL_API varray *hl_obj_fields(vdynamic *obj);
HL_API vdynamic *hl_obj_copy(vdynamic *obj);

static double now(void) {
#ifdef _WIN32
	LARGE_INTEGER ticks, frequency;
	QueryPerformanceCounter(&ticks);
	QueryPerformanceFrequency(&frequency);
	return (double)ticks.QuadPart / frequency.QuadPart;
#else
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

static int hashes[100];

static void populate(vdynobj *o, int count, int pattern) {
	for( int i = 0; i < count; i++ ) {
		if( pattern == 1 || (pattern == 2 && i % 2) )
			hl_dyn_setp((vdynamic*)o, hashes[i], &hlt_dyn, hl_alloc_dynamic(&hlt_i32));
		else
			hl_dyn_seti((vdynamic*)o, hashes[i], &hlt_i32, i);
	}
}

static void check(void) {
	hl_module_context ctx = {0};
	hl_obj_field fields[2] = {{0}};
	fields[0].hashed_name = hashes[0];
	fields[0].t = &hlt_i32;
	fields[1].hashed_name = hashes[1];
	fields[1].t = &hlt_dyn;
	hl_type_virtual virt = {0};
	virt.nfields = 2;
	virt.fields = fields;
	hl_type vt = {0};
	vt.kind = HVIRTUAL;
	vt.virt = &virt;
	hl_init_virtual(&vt, &ctx);

	vdynobj *o = hl_alloc_dynobj();
	hl_dyn_seti((vdynamic*)o, hashes[0], &hlt_i32, 42);
	hl_dyn_setp((vdynamic*)o, hashes[1], &hlt_dyn, hl_alloc_dynamic(&hlt_i32));
	vvirtual *v = hl_to_virtual(&vt, (vdynamic*)o);

	for( int i = 2; i < 100; i++ ) {
		if( i % 2 )
			hl_dyn_setp((vdynamic*)o, hashes[i], &hlt_dyn, hl_alloc_dynamic(&hlt_i32));
		else
			hl_dyn_setd((vdynamic*)o, hashes[i], i + 0.5);

		hl_gc_major();
		assert(*(int*)hl_vfields(v)[0] == 42);
		assert(*(vdynamic**)hl_vfields(v)[1] == hl_dyn_getp((vdynamic*)o, hashes[1], &hlt_dyn));
	}

	varray *names = hl_obj_fields((vdynamic*)o);
	assert(names->size == 100);
	for( int i = 0; i < 100; i++ )
		assert(hl_hash(hl_aptr(names, vbyte*)[i]) == hashes[i]);

	vdynobj *copy = (vdynobj*)hl_obj_copy((vdynamic*)o);
	hl_dyn_seti((vdynamic*)copy, 1234567, &hlt_i32, 7);
	assert(copy->nfields == 101 && o->nfields == 100);

	for( int round = 0; round < 100; round++ ) {
		assert(hl_obj_delete_field((vdynamic*)o, hashes[2]));
		hl_dyn_seti((vdynamic*)o, hashes[2], &hlt_i32, round);
		hl_dyn_setd((vdynamic*)o, hashes[2], round + 0.5);
		assert(hl_dyn_getd((vdynamic*)o, hashes[2]) == round + 0.5);
		hl_dyn_setp((vdynamic*)o, hashes[2], &hlt_dyn, (vdynamic*)copy);
		assert(hl_dyn_getp((vdynamic*)o, hashes[2], &hlt_dyn) == copy);
		assert(*(int*)hl_vfields(v)[0] == 42);
		assert(*(vdynamic**)hl_vfields(v)[1] == hl_dyn_getp((vdynamic*)o, hashes[1], &hlt_dyn));
	}

	hl_dyn_setf((vdynamic*)o, hashes[0], 3.5f);
	assert(hl_vfields(v)[0] == nullptr);
	assert(hl_dyn_getd((vdynamic*)o, hashes[0]) == 3.5);
	hl_dyn_seti((vdynamic*)o, hashes[0], &hlt_i32, 42);
	assert(*(int*)hl_vfields(v)[0] == 42);
	assert(hl_obj_delete_field((vdynamic*)o, hashes[1]));
	assert(hl_vfields(v)[1] == nullptr);
	hl_dyn_setp((vdynamic*)o, hashes[1], &hlt_dyn, (vdynamic*)copy);
	assert(*(vdynamic**)hl_vfields(v)[1] == (vdynamic*)copy);

	vvirtual *empty = hl_alloc_virtual(&vt);
	*(int*)hl_vfields(empty)[0] = 17;
	vdynobj *materialized = (vdynobj*)hl_virtual_make_value(empty);
	populate(materialized, 100, 2);
	assert(*(int*)hl_vfields(empty)[0] == 0);
	hl_gc_major();
	assert(hl_dyn_getp((vdynamic*)o, hashes[1], &hlt_dyn) == copy);

	hl_free(&ctx.alloc);
}

static void bench(int count, int pattern) {
	const int rounds = 20000;
	hl_gc_major();
	double start = now();
	for( int r = 0; r < rounds; r++ ) {
		vdynobj *o = hl_alloc_dynobj();
		populate(o, count, pattern);
		assert(o->nfields == count);
	}
	double elapsed = now() - start;
	vdynobj *o = hl_alloc_dynobj();
	int growths = 0;
	for( int i = 0; i < count; i++ ) {
		void *lookup = o->lookup, *raw = o->raw_data, *values = o->values;
		if( pattern == 1 || (pattern == 2 && i % 2) )
			hl_dyn_setp((vdynamic*)o, hashes[i], &hlt_dyn, nullptr);
		else
			hl_dyn_seti((vdynamic*)o, hashes[i], &hlt_i32, i);

		growths += lookup != o->lookup;
		growths += raw != o->raw_data;
		growths += values != o->values;
	}
	printf("%3d fields %-8s %8.1f ns/object %d storage allocations\n", count,
		pattern == 0 ? "scalar" : pattern == 1 ? "pointer" : "mixed", elapsed * 1e9 / rounds, growths);
}

int main(int argc, char **argv) {
	hl_global_init();
	hl_register_thread(&argc);
	for( int i = 0; i < 100; i++ ) {
		uchar name[16];
		memcpy(name, USTR("field00"), 8 * sizeof(uchar));
		name[5] = '0' + i / 10;
		name[6] = '0' + i % 10;
		hashes[i] = hl_hash((vbyte*)name);
	}

	check();
	if( argc == 1 )
		for( int pattern = 0; pattern < 3; pattern++ ) {
			bench(5, pattern);
			bench(20, pattern);
			bench(100, pattern);
		}

	hl_unregister_thread();
	hl_global_free();
	return 0;
}
