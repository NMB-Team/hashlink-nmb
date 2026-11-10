#ifdef NDEBUG
#undef NDEBUG
#endif
#include <jit.h>
#include <assert.h>
#include <time.h>

HL_API bool hl_obj_delete_field( vdynamic *obj, int hash );

#ifdef __MINGW32__
int wmain(void) {
#else
int main(void) {
#endif
	hl_global_init();
	int stack_top;
	hl_register_thread(&stack_top);
	hl_code code = {0};
	hl_alloc_init(&code.alloc);
	code.nfunctions = 10;
	code.nstrings = 1;
	char *strings[] = { "value" };
	int lengths[] = { 5 };
	code.strings = strings;
	uchar *ustrings[] = { nullptr };
	code.ustrings = ustrings;
	code.strings_lens = lengths;
	code.functions = hl_zalloc(&code.alloc, 10 * sizeof(hl_function));
	hl_type *values[] = { &hlt_i32, &hlt_i32, &hlt_f64, &hlt_bytes, &hlt_i64 };
	hl_type *regs[10][2];
	hl_type_fun signatures[10] = {0};
	hl_type types[10] = {0};
	hl_opcode get[] = { { ODynGet, 1, 0, 0 }, { ORet, 1 } };
	hl_opcode set[] = { { ODynSet, 0, 0, 1 }, { ODynGet, 1, 0, 0 }, { ORet, 1 } };
	for(int i=0;i<10;i++) {
		regs[i][0] = &hlt_dyn;
		regs[i][1] = values[i / 2];
		signatures[i] = (hl_type_fun){ .args = regs[i], .ret = regs[i][1], .nargs = i % 2 ? 2 : 1 };
		types[i] = (hl_type){ .kind = HFUN, .fun = signatures + i };
		code.functions[i] = (hl_function){ .findex = i, .type = types + i, .nregs = 2, .regs = regs[i],
			.nops = i % 2 ? 3 : 2, .ops = i % 2 ? set : get };
	}
	hl_module module = {0};
	module.code = &code;
	hl_alloc_init(&module.ctx.alloc);
	module.functions_indexes = hl_malloc(&code.alloc, sizeof(int) * code.nfunctions);
	module.functions_ptrs = hl_zalloc(&code.alloc, sizeof(void*) * code.nfunctions);
	for(int i=0;i<code.nfunctions;i++) module.functions_indexes[i] = i;
#ifdef WIN64_UNWIND_TABLES
	module.unwind_table_size = code.nfunctions + 10;
	module.unwind_table = hl_zalloc(&code.alloc, sizeof(RUNTIME_FUNCTION) * module.unwind_table_size);
#endif
	jit_ctx *jit = hl_jit_alloc();
	hl_jit_init(jit, &module);
	for(int i=0;i<code.nfunctions;i++) {
		int offset = hl_jit_function(jit, &module, code.functions + i);
		assert(offset >= 0);
		module.functions_ptrs[i] = (void*)(int_val)offset;
	}
	module.jit_code = hl_jit_code(jit, &module, &module.codesize, &module.jit_debug, nullptr);
	assert(module.jit_code != nullptr);
	for(int i=0;i<code.nfunctions;i++)
		module.functions_ptrs[i] = (unsigned char*)module.jit_code + (int_val)module.functions_ptrs[i];
	hl_jit_free(jit, false);
	int (*read_field)(vdynamic*) = module.functions_ptrs[0];
	int (*write_field)(vdynamic*,int) = module.functions_ptrs[1];
	vdynamic *obj = (vdynamic*)hl_alloc_dynobj();
	hl_add_root(&obj);
	int hash = hl_hash_utf8("value");
	assert(read_field(obj) == 0);
	assert(write_field(obj, 17) == 17);
	for(int i=0;i<100;i++) assert(write_field(obj, i) == i);

	// deleting and retyping fields can shift storage without replacing the lookup table
	for(int i=0;i<100;i++) {
		hl_dyn_setd(obj, hash, 12.5);
		assert(read_field(obj) == 12);
		assert(write_field(obj, i) == i);
		hl_obj_delete_field(obj, hash);
		assert(read_field(obj) == 0);
		assert(write_field(obj, i + 1) == i + 1);
	}
	for(int i=0;i<100;i++) hl_dyn_seti(obj, i + 1, &hlt_i32, i);
	assert(write_field(obj, 42) == 42);
	hl_gc_major();
	assert(read_field(obj) == 42);

	// a new class layout must miss a cache warmed by a dynamic object
	hl_obj_field field = { .name = USTR("value"), .hashed_name = hash, .t = &hlt_i32 };
	hl_type_obj layout = { .name = USTR("CacheTest"), .nfields = 1, .fields = &field, .m = &module.ctx };
	hl_type type = { .kind = HOBJ, .obj = &layout };
	vdynamic *instance = hl_alloc_obj(&type);
	hl_add_root(&instance);
	assert(write_field(instance, 91) == 91);
	assert(read_field(instance) == 91);
	int (*read_class)(vdynamic*) = module.functions_ptrs[2];
	int (*write_class)(vdynamic*,int) = module.functions_ptrs[3];
	assert(read_class(instance) == 91);
	for(int i=0;i<100;i++) assert(write_class(instance, i) == i);

	// hot reload can replace obj metadata while retaining the type identity
	hl_obj_field reloaded_fields[] = {
		{ .name = USTR("prefix"), .hashed_name = hl_hash_utf8("prefix"), .t = &hlt_f64 }, field
	};
	hl_type_obj reloaded_layout = { .name = USTR("CacheTest"), .nfields = 2,
		.fields = reloaded_fields, .m = &module.ctx };
	type.obj = &reloaded_layout;
	instance = hl_alloc_obj(&type);
	assert(write_class(instance, 123) == 123);
	assert(read_class(instance) == 123);
	assert(write_class(obj, 42) == 42);


	double (*read_double)(vdynamic*) = module.functions_ptrs[4];
	double (*write_double)(vdynamic*,double) = module.functions_ptrs[5];
	void *(*read_pointer)(vdynamic*) = module.functions_ptrs[6];
	void *(*write_pointer)(vdynamic*,void*) = module.functions_ptrs[7];
	int64 (*read_int64)(vdynamic*) = module.functions_ptrs[8];
	int64 (*write_int64)(vdynamic*,int64) = module.functions_ptrs[9];
	for(int i=0;i<100;i++) assert(write_double(obj, i + 0.5) == i + 0.5);
	assert(read_double(obj) == 99.5);
	assert(write_pointer(obj, nullptr) == nullptr);
	for(int i=0;i<100;i++) assert(write_pointer(obj, strings[0]) == strings[0]);
	assert(read_pointer(obj) == strings[0]);
	const int64 large = (int64)1 << 50;
	for(int i=0;i<100;i++) assert(write_int64(obj, large + i) == large + i);
	assert(read_int64(obj) == large + 99);

	obj = (vdynamic*)hl_alloc_dynobj();
	hl_dyn_seti(obj, hash, &hlt_i32, 42);
	assert(read_field(obj) == 42);
	assert(write_field(obj, 42) == 42);

	const int iterations = 5000000;
	volatile int sum = 0;
	clock_t start = clock();
	for(int i=0;i<iterations;i++) sum = read_field(obj);
	double jit_seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
	start = clock();
	for(int i=0;i<iterations;i++) sum = hl_dyn_geti(obj, hash, &hlt_i32);
	printf("dynamic get: JIT %.3fs, runtime %.3fs (%d iterations, result %d)\n",
		jit_seconds, (double)(clock() - start) / CLOCKS_PER_SEC, iterations, sum);
	start = clock();
	for(int i=0;i<iterations;i++) sum = write_field(obj, i);
	printf("dynamic set/get: JIT %.3fs\n", (double)(clock() - start) / CLOCKS_PER_SEC);

	start = clock();
	for(int i=0;i<iterations;i++) {
		hl_dyn_seti(obj, hash, &hlt_i32, i);
		sum = hl_dyn_geti(obj, hash, &hlt_i32);
	}
	printf("dynamic set/get: runtime %.3fs\n", (double)(clock() - start) / CLOCKS_PER_SEC);

	hl_remove_root(&instance);
	hl_remove_root(&obj);
	hl_free_executable_memory(module.jit_code, module.codesize);
	hl_free(&module.ctx.alloc);
	hl_free(&code.alloc);
	hl_unregister_thread();
	hl_global_free();
	return 0;
}
