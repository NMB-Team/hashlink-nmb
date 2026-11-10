#ifdef NDEBUG
#undef NDEBUG
#endif
#include <jit.h>
#include <assert.h>
#include <time.h>

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
	hl_module module = { .code = &code };
	hl_alloc_init(&module.ctx.alloc);
	hl_obj_field field = { .name = USTR("value"), .t = &hlt_i32 };
	hl_type_obj layout = { .name = USTR("Scalar"), .nfields = 1, .fields = &field, .m = &module.ctx };
	hl_type object = { .kind = HOBJ, .obj = &layout };
	hl_type *regs[] = { &hlt_i32, &object, &hlt_dyn, &hlt_i32, &hlt_i32, &object };
	hl_type_fun signature = { .args = regs, .nargs = 1, .ret = &hlt_i32 };
	hl_type type = { .kind = HFUN, .fun = &signature };
	hl_opcode bodies[][10] = {
		{ { ONew, 1 }, { OSetField, 1, 0, 0 }, { OIncr, 0 }, { OField, 3, 1, 0 }, { ORet, 3 } },
		{ { ONew, 1 }, { OField, 3, 1, 0 }, { ORet, 3 } },
		{ { OToDyn, 2, 0 }, { OIncr, 0 }, { OSafeCast, 3, 2 }, { OSafeCast, 4, 2 }, { OAdd, 3, 3, 4 }, { ORet, 3 } },
		{ { ONew, 1 }, { OMov, 5, 1 }, { OSetField, 1, 0, 0 }, { OField, 3, 1, 0 }, { ORet, 3 } },
		{ { OToDyn, 2, 0 }, { OMov, 2, 2 }, { OSafeCast, 3, 2 }, { ORet, 3 } },
		{ { ONew, 1 }, { ONullCheck, 1 }, { OSetField, 1, 0, 0 }, { OField, 3, 1, 0 }, { OSetField, 1, 0, 3 }, { ORet, 3 } }
	};
	int sizes[] = { 5, 3, 6, 5, 4, 6 };
	const int count = 12;
	code.nfunctions = count;
	code.functions = hl_zalloc(&code.alloc, count * sizeof(hl_function));
	module.functions_indexes = hl_malloc(&code.alloc, count * sizeof(int));
	module.functions_ptrs = hl_zalloc(&code.alloc, count * sizeof(void*));

	for(int i=0;i<count;i++) {
		int n = sizes[i / 2];
		hl_opcode *ops = hl_zalloc(&code.alloc, (n + 1) * sizeof(hl_opcode));
		// a label forces the unmodified pipeline for differential checks
		if( i % 2 ) ops[0].op = OLabel;
		memcpy(ops + i % 2, bodies[i / 2], n * sizeof(hl_opcode));
		code.functions[i] = (hl_function){ .findex = i, .type = &type, .nregs = 6, .regs = regs,
			.nops = n + i % 2, .ops = ops };
		module.functions_indexes[i] = i;
	}
#ifdef WIN64_UNWIND_TABLES
	module.unwind_table_size = count + 10;
	module.unwind_table = hl_zalloc(&code.alloc, module.unwind_table_size * sizeof(RUNTIME_FUNCTION));
#endif
	jit_ctx *jit = hl_jit_alloc();
	hl_jit_init(jit, &module);
	for(int i=0;i<count;i++) {
		int offset = hl_jit_function(jit, &module, code.functions + i);
		assert(offset >= 0);
		module.functions_ptrs[i] = (void*)(int_val)offset;
	}
	module.jit_code = hl_jit_code(jit, &module, &module.codesize, &module.jit_debug, nullptr);
	assert(module.jit_code);
	for(int i=0;i<count;i++)
		module.functions_ptrs[i] = (char*)module.jit_code + (int_val)module.functions_ptrs[i];
	hl_jit_free(jit, false);

	for(int i=0;i<count;i+=2) {
		int (*optimized)(int) = module.functions_ptrs[i];
		int (*baseline)(int) = module.functions_ptrs[i + 1];
		for(int v=-100;v<100;v++) {
			int expected = i == 2 ? 0 : i == 4 ? v * 2 : v;
			assert(optimized(v) == expected);
			assert(baseline(v) == expected);
		}
		hl_gc_major();
		assert(optimized(42) == baseline(42));
	}

	for(int i=0;i<count;i++) {
		int (*run)(int) = module.functions_ptrs[i];
		double bytes, before, after, memory;
		hl_gc_stats(&bytes, &before, &memory);
		clock_t start = clock();
		volatile int result = 0;
		for(int n=0;n<200000;n++) result = run(n & 1023);
		double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
		hl_gc_stats(&bytes, &after, &memory);
		bool eliminated = i % 2 == 0 && (i < 6 || i == 10);
		assert(eliminated ? after == before : after > before);
		printf("case %d: %.3fs, %.0f slow-path allocations, result %d\n", i, seconds, after - before, result);
	}

	hl_free_executable_memory(module.jit_code, module.codesize);
	hl_free(&module.ctx.alloc);
	hl_free(&code.alloc);
	hl_unregister_thread();
	hl_global_free();
	return 0;
}
