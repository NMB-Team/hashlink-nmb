#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "../../src/std/maps.c"

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

#define COUNT 4096
#define ROUNDS 32

static vdynamic value;

static void check_maps(void) {
	hl_hi_map *m = hl_hialloc();
	unsigned state = 12345;
	bool present[512] = { false };
	int count = 0;

	for( int i = 0; i < 100000; i++ ) {
		state = state * 1664525u + 1013904223u;
		int index = (state >> 16) & 511;
		int key = index * 1024 - 1;

		if( state & 1 ) {
			hl_hiset(m, key, i & 2 ? &value : nullptr);
			count += !present[index];
			present[index] = true;
		} else {
			assert(hl_hiremove(m, key) == present[index]);
			count -= present[index];
			present[index] = false;
		}

		assert(hl_hisize(m) == count);
		assert(hl_hiexists(m, key) == present[index]);

		if( i % 1024 == 0 ) {
			for( int j = 0; j < 512; j++ )
				assert(hl_hiexists(m, j * 1024 - 1) == present[j]);

			for( int c = 0; c < m->nbuckets; c++ ) {
				if( m->psl[c] >= 0 ) {
					unsigned home = hl_hihash(m->entries[c].key) & (m->nbuckets - 1);
					assert(m->psl[c] == ((c - home) & (m->nbuckets - 1)));
				}
			}
		}
	}

	assert(hl_hikeys(m)->size == count);
	assert(hl_hivalues(m)->size == count);
	hl_hiclear(m);
	assert(hl_hisize(m) == 0);
	assert(!hl_hiexists(m, -1));
	assert(!hl_hiremove(m, -1));

	hl_ho_map *objects = hl_hoalloc();
	hl_mlookup__map *lookup = hl_mlookup_alloc();
	vdynamic *keys = calloc(COUNT, sizeof(vdynamic));
	assert(keys);

	for( int i = 0; i < COUNT; i++ ) {
		keys[i].t = &hlt_dyn;
		hl_hoset(objects, keys + i, &value);
		hl_mlookup_set_impl(lookup, keys + i, i);
	}

	hl_hoset(objects, nullptr, nullptr);
	assert(hl_hoexists(objects, nullptr));
	assert(hl_horemove(objects, nullptr));

	for( int i = 0; i < COUNT; i++ ) {
		assert(hl_hoget(objects, keys + i) == &value);
		assert(*hl_mlookup_find(lookup, keys + i) == i);
		assert(hl_horemove(objects, keys + i));
		assert(!hl_hoexists(objects, keys + i));
	}

	free(keys);

	hl_hi64_map *wide = hl_hi64alloc();
	hl_hb_map *bytes = hl_hballoc();
	uchar strings[256][16] = { 0 };

	for( int i = 0; i < 256; i++ ) {
		strings[i][0] = 'k';
		strings[i][1] = (uchar)(i + 1);
		strings[i][2] = 0;

		hl_hi64set(wide, ((int64)i << 32) | (unsigned)i, &value);
		hl_hbset(bytes, strings[i], &value);
	}

	for( int i = 0; i < 256; i++ ) {
		uchar copy[16];
		memcpy(copy, strings[i], sizeof(copy));
		int64 key = ((int64)i << 32) | (unsigned)i;
		assert(hl_hi64get(wide, key) == &value);
		assert(hl_hbget(bytes, copy) == &value);
		assert(hl_hi64remove(wide, key));
		assert(hl_hbremove(bytes, copy));
		assert(!hl_hi64exists(wide, key));
		assert(!hl_hbexists(bytes, copy));
	}

	puts("map correctness checks passed");
}

#define BENCH(NAME, MAP, ALLOC, SET, GET, REMOVE, KEY) \
static void NAME(void) { \
	double insert = 0, lookup = 0, remove = 0; \
\
	for( int round = 0; round < ROUNDS; round++ ) { \
		MAP *m = ALLOC(); \
\
		double start = now(); \
		for( int i = 0; i < COUNT; i++ ) SET(m, (KEY), &value); \
		insert += (now() - start); \
\
		start = now(); \
		for( int repeat = 0; repeat < 16; repeat++ ) \
			for( int i = 0; i < COUNT; i++ ) assert(GET(m, (KEY)) == &value); \
		lookup += (now() - start); \
\
		start = now(); \
		for( int i = 0; i < COUNT; i++ ) assert(REMOVE(m, (KEY))); \
		remove += (now() - start); \
	} \
\
	double scale = 1e9 / (COUNT * ROUNDS); \
	printf("%s ns/op: insert %.2f lookup %.2f remove %.2f\n", #NAME, insert * scale, lookup * scale / 16, remove * scale); \
}

BENCH(integers, hl_hi_map, hl_hialloc, hl_hiset, hl_higet, hl_hiremove, i)
BENCH(collisions, hl_hi_map, hl_hialloc, hl_hiset, hl_higet, hl_hiremove, i * 8192 - 1)

static vdynamic *pointer_keys;
BENCH(pointers, hl_ho_map, hl_hoalloc, hl_hoset, hl_hoget, hl_horemove, pointer_keys + i * 8)

int main(int argc, char **argv) {
	hl_global_init();
	hl_register_thread(&argc);
	check_maps();

	if( argc == 1 ) {
		pointer_keys = calloc(COUNT * 8, sizeof(vdynamic));
		assert(pointer_keys);

		for( int i = 0; i < COUNT; i++ )
			pointer_keys[i * 8].t = &hlt_dyn;

		integers();
		pointers();
		collisions();
		free(pointer_keys);
	}

	hl_unregister_thread();
	hl_global_free();
	return 0;
}
