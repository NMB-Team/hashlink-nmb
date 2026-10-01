#ifdef NDEBUG
#undef NDEBUG
#endif

#include <jit.h>
#include <assert.h>

typedef enum { INTEGERS, FLOATS, ALTERNATING, OBJECTS, PATTERN_COUNT } arg_pattern;
static const char *pattern_names[] = { "integer", "float", "alternating int/float", "object/int/float" };

static void make_functions( hl_code *code, int index, int count, arg_pattern pattern, hl_type *object_type ) {
	hl_type **args = hl_malloc(&code->alloc, sizeof(hl_type*) * count);
	for(int i=0;i<count;i++) {
		switch( pattern ) {
		case INTEGERS: args[i] = &hlt_i32; break;
		case FLOATS: args[i] = &hlt_f64; break;
		case ALTERNATING: args[i] = i % 2 ? &hlt_f64 : &hlt_i32; break;
		case OBJECTS: args[i] = i % 3 == 0 ? object_type : i % 3 == 1 ? &hlt_i32 : &hlt_f64; break;
		case PATTERN_COUNT: assert(false); break;
		}
	}
	hl_type_fun *signature = hl_zalloc(&code->alloc, sizeof(hl_type_fun));
	signature->args = args;
	signature->nargs = count;
	signature->ret = &hlt_i32;
	hl_type *type = hl_zalloc(&code->alloc, sizeof(hl_type));
	type->kind = HFUN;
	type->fun = signature;

	hl_function *callee = code->functions + index;
	callee->findex = index;
	callee->type = type;
	callee->nregs = count + 3;
	callee->regs = hl_malloc(&code->alloc, sizeof(hl_type*) * callee->nregs);
	memcpy(callee->regs, args, sizeof(hl_type*) * count);
	for(int i=count;i<callee->nregs;i++) callee->regs[i] = &hlt_i32;
	callee->ops = hl_malloc(&code->alloc, sizeof(hl_opcode) * (count * 4 + 3));
	int pos = 0;
	callee->ops[pos++] = (hl_opcode){ OInt, count, 0 };
	callee->ops[pos++] = (hl_opcode){ OInt, count + 1, 1 };
	for(int i=0;i<count;i++) {
		int value = i;
		if( args[i]->kind == HF64 ) {
			callee->ops[pos++] = (hl_opcode){ OToInt, count + 2, i };
			value = count + 2;
		} else if( args[i]->kind == HOBJ ) {
			callee->ops[pos++] = (hl_opcode){ OField, count + 2, i, 0 };
			value = count + 2;
		}
		callee->ops[pos++] = (hl_opcode){ OMul, count + 2, value, count + 1 };
		callee->ops[pos++] = (hl_opcode){ OAdd, count, count, count + 2 };
		callee->ops[pos++] = (hl_opcode){ OIncr, count + 1 };
	}
	callee->ops[pos++] = (hl_opcode){ ORet, count };
	callee->nops = pos;

	hl_type_fun *entry_signature = hl_zalloc(&code->alloc, sizeof(hl_type_fun));
	entry_signature->ret = &hlt_i32;
	hl_type *entry_type = hl_zalloc(&code->alloc, sizeof(hl_type));
	entry_type->kind = HFUN;
	entry_type->fun = entry_signature;
	hl_function *caller = callee + 1;
	caller->findex = index + 1;
	caller->type = entry_type;
	caller->nregs = count + 3;
	caller->regs = hl_malloc(&code->alloc, sizeof(hl_type*) * caller->nregs);
	memcpy(caller->regs, args, sizeof(hl_type*) * count);
	caller->regs[count] = &hlt_i32;
	caller->regs[count + 1] = &hlt_i32;
	caller->regs[count + 2] = type;
	caller->ops = hl_malloc(&code->alloc, sizeof(hl_opcode) * (count + 7));
	int *arg_regs = hl_malloc(&code->alloc, sizeof(int) * count);
	pos = 0;
	for(int i=0;i<count;i++) {
		arg_regs[i] = i;
		if( args[i]->kind == HOBJ )
			caller->ops[pos++] = (hl_opcode){ OGetGlobal, i, i };
		else
			caller->ops[pos++] = (hl_opcode){ args[i]->kind == HF64 ? OFloat : OInt, i, i + 2 };
	}
	caller->ops[pos++] = (hl_opcode){ OCallN, count, index, count, arg_regs };
	caller->ops[pos++] = (hl_opcode){ OCallN, count + 1, index, count, arg_regs };
	caller->ops[pos++] = (hl_opcode){ OAdd, count, count, count + 1 };

	if( count < MAX_CALL_ARGS ) {
		caller->ops[pos++] = (hl_opcode){ OStaticClosure, count + 2, index };
		caller->ops[pos++] = (hl_opcode){ OCallClosure, count + 1, count + 2, count, arg_regs };
		caller->ops[pos++] = (hl_opcode){ OAdd, count, count, count + 1 };
	}
	caller->ops[pos++] = (hl_opcode){ ORet, count };
	caller->nops = pos;
}

#ifdef __MINGW32__
int wmain(void) {
#else
int main(void) {
#endif
	const int counts[] = { 1, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 31, 32, 33, 40, 64, 113, 255, 256, 33, 32 };
	const int cases = sizeof(counts) / sizeof(counts[0]);
	hl_code code = {0};
	hl_global_init();
	hl_register_thread(&code);
	hl_alloc_init(&code.alloc);
	code.nints = MAX_CALL_ARGS + 2;
	code.nfloats = code.nints;
	code.ints = hl_malloc(&code.alloc, sizeof(int) * code.nints);
	code.floats = hl_malloc(&code.alloc, sizeof(double) * code.nfloats);
	code.ints[0] = 0;
	code.ints[1] = 1;
	for(int i=2;i<code.nints;i++) {
		code.ints[i] = i + 15;
		code.floats[i] = code.ints[i];
	}
	code.nfunctions = cases * PATTERN_COUNT * 2;
	code.functions = hl_zalloc(&code.alloc, sizeof(hl_function) * code.nfunctions);
	hl_module module = {0};
	module.code = &code;
	hl_alloc_init(&module.ctx.alloc);
	hl_obj_field field = { .name = USTR("value"), .t = &hlt_i32 };
	field.hashed_name = hl_hash_gen(field.name, true);
	hl_type_obj object = { .nfields = 1, .name = USTR("Argument"), .fields = &field, .m = &module.ctx };
	hl_type object_type = { .kind = HOBJ, .obj = &object };
	hl_runtime_obj *object_rt = hl_get_obj_rt(&object_type);
	code.nglobals = MAX_CALL_ARGS;
	code.globals = hl_malloc(&code.alloc, sizeof(hl_type*) * code.nglobals);
	module.globals_indexes = hl_malloc(&code.alloc, sizeof(int) * code.nglobals);
	module.globals_data = hl_zalloc(&code.alloc, sizeof(void*) * code.nglobals);
	for(int i=0;i<code.nglobals;i++) {
		code.globals[i] = &object_type;
		module.globals_indexes[i] = i * sizeof(void*);
		vdynamic **slot = (vdynamic**)(module.globals_data + module.globals_indexes[i]);
		*slot = hl_alloc_obj(&object_type);
		hl_add_root(slot);
		*(int*)((unsigned char*)*slot + object_rt->fields_indexes[0]) = i + 17;
	}
	for(int pattern=0;pattern<PATTERN_COUNT;pattern++)
		for(int i=0;i<cases;i++)
			make_functions(&code, (pattern * cases + i) * 2, counts[i], (arg_pattern)pattern, &object_type);
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
		printf("Compiling %s with %d arguments (%s)\n", i % 2 ? "caller" : "callee", counts[(i / 2) % cases], pattern_names[i / (cases * 2)]);
		fflush(stdout);
		int offset = hl_jit_function(jit, &module, code.functions + i);
		assert(offset >= 0);
		module.functions_ptrs[i] = (void*)(int_val)offset;
	}
	module.jit_code = hl_jit_code(jit, &module, &module.codesize, &module.jit_debug, nullptr);
	assert(module.jit_code != nullptr);
	for(int i=0;i<code.nfunctions;i++)
		module.functions_ptrs[i] = (unsigned char*)module.jit_code + (int_val)module.functions_ptrs[i];
	hl_jit_free(jit, false);
	for(int pattern=0;pattern<PATTERN_COUNT;pattern++) {
		for(int i=0;i<cases;i++) {
			int count = counts[i];
			int expected = 0;
			for(int k=0;k<count;k++) expected += (k + 1) * (k + 17);
			expected *= count < MAX_CALL_ARGS ? 3 : 2;
			int (*call)(void) = module.functions_ptrs[(pattern * cases + i) * 2 + 1];
			int result = call();
			printf("%d arguments (%s): got %d, expected %d\n", count, pattern_names[pattern], result, expected);
			fflush(stdout);
			assert(result == expected);
		}
	}

	for(int i=0;i<code.nglobals;i++)
		hl_remove_root(module.globals_data + module.globals_indexes[i]);
	hl_free_executable_memory(module.jit_code, module.codesize);
	hl_free(&module.ctx.alloc);
	hl_free(&code.alloc);
	hl_unregister_thread();
	hl_global_free();
	return 0;
}
