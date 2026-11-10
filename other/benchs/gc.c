#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <hl.h>

static hl_semaphore *done;
static int iterations;
static int object_size;
static int retain;
static void *live[64][4096];

static double now() {
#ifdef HL_WIN
	LARGE_INTEGER t, f;
	QueryPerformanceCounter(&t);
	QueryPerformanceFrequency(&f);
	return (double)t.QuadPart / f.QuadPart;
#else
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
#endif
}

static void worker(void *unused) {
	hl_register_thread(&unused);

	for( int i = 0; i < iterations; i++ ) {
		unsigned char *p = hl_gc_alloc_gen(nullptr, object_size, MEM_KIND_NOPTR | MEM_ZERO);
		p[0] = (unsigned char)i;
		if( retain ) live[(int)(int_val)unused][i & 4095] = p;
	}

	hl_unregister_thread();
	hl_semaphore_release(done);
}

int main(int argc, char **argv) {
	int threads = argc > 1 ? atoi(argv[1]) : 1;
	iterations = argc > 2 ? atoi(argv[2]) : 2000000;
	object_size = argc > 3 ? atoi(argv[3]) : 24;
	retain = argc > 4 ? atoi(argv[4]) : 0;
	if( threads < 1 || threads > 64 || iterations < 1 || object_size < 1 ) return 1;

	hl_global_init();
	hl_register_thread(&argc);
	for( int i = 0; i < threads; i++ )
		for( int j = 0; j < 4096; j++ ) hl_add_root(&live[i][j]);

	done = hl_semaphore_alloc(0);
	hl_add_root((void**)&done);
	double start = now();
	for( int i = 0; i < threads; i++ )
		if( !hl_thread_start(worker, (void*)(int_val)i, false) ) hl_fatal("Failed to start benchmark worker");

	for( int i = 0; i < threads; i++ ) hl_semaphore_acquire(done);
	double elapsed = now() - start;
	double allocated, count, memory;
	hl_gc_stats(&allocated, &count, &memory);
	printf("threads=%d size=%d throughput=%.3f Malloc/s heap=%.3f MiB\n", threads, object_size,
		threads * (double)iterations / elapsed / 1e6, memory / 1048576.);

	for( int i = 0; i < 20; i++ ) {
		start = now();
		hl_gc_major();
		printf("pause_ms=%.6f\n", (now() - start) * 1000.);
	}

	for( int i = 0; i < threads; i++ )
		for( int j = 0; j < 4096; j++ ) hl_remove_root(&live[i][j]);

	hl_remove_root((void**)&done);
	hl_semaphore_free(done);
	hl_unregister_thread();
	hl_global_free();
	return 0;
}
