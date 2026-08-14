/*
 * Copyright (C)2015-2016 Haxe Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <jit.h>
#include "data_struct.h"

static jit_ctx *current_ctx = NULL;

int hl_jit_trampoline = -1;

static hl_alloc callback_alloc = {0};
static ptr_set callback_natives = {0};

void hl_jit_tag_callback( void *native ) {
	ptr_set_add_impl(&callback_alloc,&callback_natives,native);
}

bool hl_jit_is_callback( void *native ) {
	return ptr_set_exists(callback_natives,native);
}

void hl_jit_error( const char *msg, const char *func, int line ) {
	printf("*** JIT ERROR %s:%d (%s)****\n", func, line, msg);
	if( current_ctx  ) {
		jit_ctx *ctx = current_ctx;
		current_ctx = NULL;
		hl_emit_dump(ctx);
	}
	fflush(stdout);
}

void hl_jit_null_field_access( int fhash ) {
	vbyte *field = hl_field_name(fhash);
	hl_buffer *b = hl_alloc_buffer();
	hl_buffer_str(b, USTR("Null access ."));
	hl_buffer_str(b, (uchar*)field);
	vdynamic *d = hl_alloc_dynamic(&hlt_bytes);
	d->v.ptr = hl_buffer_content(b,NULL);
	hl_throw(d);
}

void hl_jit_assert() {
	vdynamic *d = hl_alloc_dynamic(&hlt_bytes);
	d->v.ptr = USTR("Assert");
	hl_throw(d);
}

void hl_emit_alloc( jit_ctx *jit );
void hl_emit_free( jit_ctx *jit );
void hl_emit_function( jit_ctx *jit );
void hl_emit_final( jit_ctx *jit );

void hl_regs_alloc( jit_ctx *jit );
void hl_regs_free( jit_ctx *jit );
void hl_regs_function( jit_ctx *jit );

void hl_codegen_alloc( jit_ctx *jit );
void hl_codegen_init( jit_ctx *jit );
void hl_codegen_free( jit_ctx *jit );
void hl_codegen_flush_consts( jit_ctx *jit );
void hl_codegen_function( jit_ctx *jit );
void hl_codegen_final( jit_ctx *jit );

void hl_jit_init_regs( regs_config *cfg );

jit_ctx *hl_jit_alloc() {
	jit_ctx *ctx = (jit_ctx*)malloc(sizeof(jit_ctx));
	memset(ctx,0,sizeof(jit_ctx));
	hl_jit_init_regs(&ctx->cfg);
	hl_alloc_init(&ctx->falloc);
	hl_emit_alloc(ctx);
	hl_regs_alloc(ctx);
	hl_codegen_alloc(ctx);
	return ctx;
}

void hl_jit_define_function( jit_ctx *ctx, int start, int size ) {
#ifdef WIN64_UNWIND_TABLES
	int fid = ctx->fdef_index++;
	if( fid >= ctx->mod->unwind_table_size ) jit_assert();
	ctx->mod->unwind_table[fid].BeginAddress = start;
	ctx->mod->unwind_table[fid].EndAddress = start + size;
#endif
}

static bool jit_code_reserve( jit_ctx *ctx, int size ) {
	int pos = ctx->out_pos;
	if( pos + size > ctx->out_max ) {
		int nsize = ctx->out_max ? ctx->out_max * 3 : 4096;
		while( pos + ctx->code_size > nsize ) nsize *= 3;
		unsigned char *nout = malloc(nsize);
		if( !nout ) return false;
		memcpy(nout,ctx->output,pos);
		free(ctx->output);
		ctx->output = nout;
		ctx->out_max = nsize;
	}
	return true;
}

static bool jit_code_append( jit_ctx *ctx ) {
	if( !jit_code_reserve(ctx,ctx->code_size) )
		return false;
	int pos = ctx->out_pos;
	memcpy(ctx->output + pos, ctx->code_instrs, ctx->code_size);
	ctx->out_pos += ctx->code_size;
	return true;
}

void hl_jit_init( jit_ctx *ctx, hl_module *m ) {
	ctx->mod = m;
	hl_jit_tag_callback(hl_dyn_call);
	hl_jit_tag_callback(hl_dyn_call_obj);
#ifdef WIN64_UNWIND_TABLES
	unsigned char version = 1;
	unsigned char flags = 0;
	unsigned char CountOfCodes = 2;
	unsigned char SizeOfProlog = 4;
	unsigned char FrameRegister = 5; // RBP
	unsigned char FrameOffset = 0;
	jit_code_reserve(ctx,64);
#	define B(v)	ctx->output[ctx->out_pos++] = v
#	define UW(offs,code,inf)	B(offs); B((code) | (inf) << 4)
	B((version) | (flags) << 3);
	B(SizeOfProlog);
	B(CountOfCodes);
	B((FrameRegister) | (FrameOffset) << 4);
	UW(4, 3 /*UWOP_SET_FPREG*/, 0);
	UW(1, 0 /*UWOP_PUSH_NONVOL*/, 5);
	while( ctx->out_pos & 15 ) B(0);
#endif
	hl_codegen_init(ctx);
	jit_code_append(ctx);
	if( m->code->hasdebug ) {
		m->jit_debug = (hl_debug_infos*)malloc(sizeof(hl_debug_infos) * m->code->nfunctions);
		memset(m->jit_debug, 0, sizeof(hl_debug_infos) * m->code->nfunctions);
	}
}

void hl_jit_free( jit_ctx *ctx, h_bool can_reset ) {
	hl_codegen_free(ctx);
	hl_regs_free(ctx);
	hl_emit_free(ctx);
	hl_free(&ctx->falloc);
	free(ctx);
}

void hl_jit_reset( jit_ctx *ctx, hl_module *m ) {
}

int hl_jit_function( jit_ctx *ctx, hl_module *m, hl_function *f ) {
	hl_free(&ctx->falloc);
	ctx->mod = m;
	ctx->fun = f;
	ctx->reg_instr_count = 0;
	ctx->code_size = 0;
	current_ctx = ctx;
	hl_emit_function(ctx);
	hl_regs_function(ctx);
	hl_codegen_function(ctx);
	int pos = ctx->out_pos;
	hl_jit_define_function(ctx, pos, ctx->code_size);
	if( m->jit_debug && ctx->code_pos_map ) {
		bool compact = ctx->code_size < 0xFFFF;
		void *debug = malloc((compact ? sizeof(unsigned short) : sizeof(int)) * (f->nops + 1));
		for(int i=0;i<=f->nops;i++) {
			int ipos = ctx->emit_pos_map[i];
			int rpos = ctx->reg_pos_map[ipos];
			int cpos = ctx->code_pos_map[rpos];
			if( compact )
				((unsigned short*)debug)[i] = (unsigned short)cpos;
			else
				((int*)debug)[i] = cpos;
		}
		int fid = (int)(f - m->code->functions);
		hl_debug_infos *dbg = &m->jit_debug[fid];
		dbg->start = pos;
		dbg->offsets = debug;
		dbg->large = !compact;
		dbg->vars_size = ctx->regs_track_count * sizeof(int);
		dbg->vars = malloc(dbg->vars_size);
		for(int i=0;i<ctx->regs_track_count>>2;i++) {
			if( ctx->regs_track[i<<2] < 0 ) continue; // saved register, not a code range
			ctx->regs_track[(i<<2)|1] = ctx->code_pos_map[ctx->regs_track[(i<<2)|1] + 1];
			ctx->regs_track[(i<<2)|2] = ctx->code_pos_map[ctx->regs_track[(i<<2)|2]];
		}
		memcpy(dbg->vars,ctx->regs_track,dbg->vars_size);
	}
	if( !jit_code_append(ctx) )
		return -1;
	current_ctx = NULL;
	return pos;
}

static void *call_jit_c2hl = hl_jit_assert;
static void *call_jit_hl2c = hl_jit_assert;
static int arg_reg_count = 0;
static int arg_fp_count = 0;

static int get_next_reg( hl_type *t, int *rp, int *fp ) {
	if( t->kind == HF32 || t->kind == HF64 ) {
		if( *fp < arg_fp_count ) {
			int r = (*fp)++;
			if( IS_WINCALL64 ) (*rp)++;
			return r;
		}
		return -1;
	}
	if( *rp < arg_fp_count ) {
		int r = (*rp)++;
		if( IS_WINCALL64 ) (*fp)++;
		return r;
	}
	return -1;
}

static void *default_wrapper( hl_type *ft ) {
	return call_jit_hl2c;
}

static void *callback_c2hl( void *f, hl_type *t, void **args, vdynamic *ret ) {
	int nargs = t->fun->nargs;
	if( nargs > MAX_ARGS )
		hl_error("Too many arguments for dynamic call");
	struct {
		void *regs[MAX_ARGS];
		void *stack[MAX_ARGS];
	} vargs;
	int rp = 0, fp = 0, sp = 0;
	for(int i=0;i<t->fun->nargs;i++) {
		hl_type *at = t->fun->args[i];
		void *v = args[i];
		int r = get_next_reg(at,&rp,&fp);
		int_val iv;
		switch( at->kind ) {
		case HBOOL:
		case HUI8:
		case HUI16:
		case HI32:
		case HF32:
			iv = *(int*)v;
			break;
		case HI64:
		case HGUID:
		case HF64:
			iv = *(int_val*)v;
			break;
		default:
			iv = (int_val)v;
			break;
		}
		if( r >= 0 )
			vargs.regs[r + (at->kind == HF32 || at->kind == HF64 ? arg_reg_count : 0)] = (void*)iv;
		else
			vargs.stack[sp++] = (void*)iv;
	}
	if( sp & 1 ) sp++; // align stack
	switch( t->fun->ret->kind ) {
	case HUI8:
	case HUI16:
	case HI32:
	case HBOOL:
		ret->v.i = ((int (*)(void *, void *, int))call_jit_c2hl)(f, &vargs, sp);
		return &ret->v.i;
	case HI64:
	case HGUID:
		ret->v.i64 = ((int64 (*)(void *, void *, int))call_jit_c2hl)(f, &vargs, sp);
		return &ret->v.i64;
	case HF32:
		ret->v.f = ((float (*)(void *, void *, int))call_jit_c2hl)(f, &vargs, sp);
		return &ret->v.f;
	case HF64:
		ret->v.d = ((double (*)(void *, void *, int))call_jit_c2hl)(f, &vargs, sp);
		return &ret->v.d;
	default:
		return ((void *(*)(void *, void *, int))call_jit_c2hl)(f, &vargs, sp);
	}
}

static vdynamic *callback_hl2c( vclosure_wrapper *c, char *stack_args, void **regs ) {
	vdynamic *args[MAX_ARGS];
	int nargs = c->cl.t->fun->nargs;
	if( nargs > MAX_ARGS )
		hl_error("Too many arguments for wrapped call");
	int rp = 0, fp = 0;
	rp++; // skip fptr in HL64 - was passed as arg0
	if( IS_WINCALL64 ) fp++;
	for(int i=0;i<nargs;i++) {
		hl_type *t = c->cl.t->fun->args[i];
		int creg = get_next_reg(t,&rp,&fp);
		if( creg < 0 ) {
			args[i] = hl_is_dynamic(t) ? *(vdynamic**)stack_args : hl_make_dyn(stack_args,t);
			stack_args += (t->kind == HF64 ? 8 : HL_WSIZE);
		} else if( hl_is_dynamic(t) ) {
			args[i] = *(vdynamic**)(regs + creg);
		} else if( t->kind == HF32 || t->kind == HF64 ) {
			args[i] = hl_make_dyn(regs + arg_reg_count + creg,&hlt_f64);
		} else {
			args[i] = hl_make_dyn(regs + creg,t);
		}
	}
	return hl_dyn_call(c->wrappedFun,args,nargs);
}

void *hl_jit_wrapper_ptr( vclosure_wrapper *c, char *stack_args, void **regs ) {
	vdynamic *ret = callback_hl2c(c, stack_args, regs);
	hl_type *tret = c->cl.t->fun->ret;
	switch( tret->kind ) {
	case HVOID:
		return nullptr;
	case HUI8:
	case HUI16:
	case HI32:
	case HBOOL:
		return (void*)(int_val)hl_dyn_casti(&ret,&hlt_dyn,tret);
	case HI64:
	case HGUID:
		return (void*)(int_val)hl_dyn_casti64(&ret,&hlt_dyn);
	default:
		return hl_dyn_castp(&ret,&hlt_dyn,tret);
	}
}

double hl_jit_wrapper_d( vclosure_wrapper *c, char *stack_args, void **regs ) {
	vdynamic *ret = callback_hl2c(c, stack_args, regs);
	return hl_dyn_castd(&ret,&hlt_dyn);
}

static void jit_hl2c( jit_ctx *ctx ) {
	// create a function that is called with a vclosure_wrapper* and native args
	// and pack and pass the args to callback_hl2c
	preg p;
	int jfloat1, jfloat2, jexit;
	hl_type_fun *ft = nullptr;
	int size;
#	ifdef HL_64
	preg *cl = REG_AT(CALL_REGS[0]);
	preg *tmp = REG_AT(CALL_REGS[1]);
#	else
	preg *cl = REG_AT(Ecx);
	preg *tmp = REG_AT(Edx);
#	endif

	op64(ctx,PUSH,PEBP,UNUSED);
	op64(ctx,MOV,PEBP,PESP);

#	ifdef HL_64
	// push registers
	int i;
	op64(ctx,SUB,PESP,pconst(&p,CALL_NREGS*8));
	for(i=0;i<CALL_NREGS;i++)
		op64(ctx,MOVSD,pmem(&p,Esp,i*8),REG_AT(XMM(i)));
	for(i=0;i<CALL_NREGS;i++)
		op64(ctx,PUSH,REG_AT(CALL_REGS[CALL_NREGS - 1 - i]),UNUSED);
#	endif

	// opcodes for:
	//		switch( arg0->t->fun->ret->kind ) {
	//		case HF32: case HF64: return jit_wrapper_d(arg0,&args);
	//		default: return jit_wrapper_ptr(arg0,&args);
	//		}
	if( !IS_64 )
		op64(ctx,MOV,cl,pmem(&p,Ebp,HL_WSIZE*2)); // load arg0
	op64(ctx,MOV,tmp,pmem(&p,cl->id,0)); // ->t
	op64(ctx,MOV,tmp,pmem(&p,tmp->id,HL_WSIZE)); // ->fun
	op64(ctx,MOV,tmp,pmem(&p,tmp->id,(int)(int_val)&ft->ret)); // ->ret
	op32(ctx,MOV,tmp,pmem(&p,tmp->id,0)); // -> kind

	op32(ctx,CMP,tmp,pconst(&p,HF64));
	XJump_small(JEq,jfloat1);
	op32(ctx,CMP,tmp,pconst(&p,HF32));
	XJump_small(JEq,jfloat2);

	// 64 bits : ESP + EIP (+WIN64PAD)
	// 32 bits : ESP + EIP + PARAM0
	int args_pos = IS_64 ? ((IS_WINCALL64 ? 32 : 0) + HL_WSIZE * 2) : (HL_WSIZE*3);

	size = begin_native_call(ctx,3);
	op64(ctx, LEA, tmp, pmem(&p,Ebp,-HL_WSIZE*CALL_NREGS*2));
	set_native_arg(ctx, tmp);
	op64(ctx, LEA, tmp, pmem(&p,Ebp,args_pos));
	set_native_arg(ctx, tmp);
	set_native_arg(ctx, cl);
	call_native(ctx, jit_wrapper_ptr, size);
	XJump_small(JAlways, jexit);

	patch_jump(ctx,jfloat1);
	patch_jump(ctx,jfloat2);
	size = begin_native_call(ctx,3);
	op64(ctx, LEA, tmp, pmem(&p,Ebp,-HL_WSIZE*CALL_NREGS*2));
	set_native_arg(ctx, tmp);
	op64(ctx, LEA, tmp, pmem(&p,Ebp,args_pos));
	set_native_arg(ctx, tmp);
	set_native_arg(ctx, cl);
	call_native(ctx, jit_wrapper_d, size);

	patch_jump(ctx,jexit);
	op64(ctx,MOV,PESP,PEBP);
	op64(ctx,POP,PEBP, UNUSED);
	op64(ctx,RET,UNUSED,UNUSED);
}

#ifdef JIT_CUSTOM_LONGJUMP
// Win64 debug CRT performs a Rtl stack check in debug mode, preventing from
// using longjump. This in an alternate implementation that follows the native
// setjump storage.
//
// Another more reliable way of handling this would be to use RtlAddFunctionTable
// but some platform does not have it.
static void jit_longjump( jit_ctx *ctx ) {
	preg *buf = REG_AT(CALL_REGS[0]);
	preg *ret = REG_AT(CALL_REGS[1]);
	preg p;
	int i;
	op64(ctx,MOV,PEAX,ret); // return value
	op64(ctx,MOV,REG_AT(Edx),pmem(&p,buf->id,0x0));
	op64(ctx,MOV,REG_AT(Ebx),pmem(&p,buf->id,0x8));
	op64(ctx,MOV,REG_AT(Esp),pmem(&p,buf->id,0x10));
	op64(ctx,MOV,REG_AT(Ebp),pmem(&p,buf->id,0x18));
	op64(ctx,MOV,REG_AT(Esi),pmem(&p,buf->id,0x20));
	op64(ctx,MOV,REG_AT(Edi),pmem(&p,buf->id,0x28));
	op64(ctx,MOV,REG_AT(R12),pmem(&p,buf->id,0x30));
	op64(ctx,MOV,REG_AT(R13),pmem(&p,buf->id,0x38));
	op64(ctx,MOV,REG_AT(R14),pmem(&p,buf->id,0x40));
	op64(ctx,MOV,REG_AT(R15),pmem(&p,buf->id,0x48));
	op64(ctx,LDMXCSR,pmem(&p,buf->id,0x58), UNUSED);
	op64(ctx,FLDCW,pmem(&p,buf->id,0x5C), UNUSED);
	for(i=0;i<10;i++)
		op64(ctx,MOVSD,REG_AT(XMM(i+6)),pmem(&p,buf->id,0x60 + i * 16));
	op64(ctx,PUSH,pmem(&p,buf->id,0x50),UNUSED);
	op64(ctx,RET,UNUSED,UNUSED);
}
#endif

static void jit_fail( uchar *msg ) {
	if( msg == nullptr ) {
		hl_debug_break();
		msg = USTR("assert");
	}
	vdynamic *d = hl_alloc_dynamic(&hlt_bytes);
	d->v.ptr = msg;
	hl_throw(d);
}

static void jit_null_access( jit_ctx *ctx ) {
	op64(ctx,PUSH,PEBP,UNUSED);
	op64(ctx,MOV,PEBP,PESP);
	int_val arg = (int_val)USTR("Null access");
	call_native_consts(ctx, jit_fail, &arg, 1);
}

static void jit_null_fail( int fhash ) {
	vbyte *field = hl_field_name(fhash);
	hl_buffer *b = hl_alloc_buffer();
	hl_buffer_str(b, USTR("Null access ."));
	hl_buffer_str(b, (uchar*)field);
	vdynamic *d = hl_alloc_dynamic(&hlt_bytes);
	d->v.ptr = hl_buffer_content(b,nullptr);
	hl_throw(d);
}

static void jit_null_field_access( jit_ctx *ctx ) {
	preg p;
	op64(ctx,PUSH,PEBP,UNUSED);
	op64(ctx,MOV,PEBP,PESP);
	int size = begin_native_call(ctx, 1);
	int args_pos = (IS_WINCALL64 ? 32 : 0) + HL_WSIZE*2;
	set_native_arg(ctx, pmem(&p,Ebp,args_pos));
	call_native(ctx,jit_null_fail,size);
}

static void jit_assert( jit_ctx *ctx ) {
	op64(ctx,PUSH,PEBP,UNUSED);
	op64(ctx,MOV,PEBP,PESP);
	int_val arg = 0;
	call_native_consts(ctx, jit_fail, &arg, 1);
}

static int jit_build( jit_ctx *ctx, void (*fbuild)( jit_ctx *) ) {
	int pos;
	jit_buf(ctx);
	jit_nops(ctx);
	pos = BUF_POS();
	fbuild(ctx);
	int endPos = BUF_POS();
	jit_nops(ctx);
#ifdef WIN64_UNWIND_TABLES
	int fid = ctx->nunwind++;
	ctx->unwind_table[fid].BeginAddress = pos;
	ctx->unwind_table[fid].EndAddress = endPos;
	ctx->unwind_table[fid].UnwindData = ctx->unwind_offset;
#endif
	return pos;
}

static void hl_jit_init_module( jit_ctx *ctx, hl_module *m ) {
	int i;
	ctx->m = m;
	if( m->code->hasdebug ) {
		ctx->debug = (hl_debug_infos*)malloc(sizeof(hl_debug_infos) * m->code->nfunctions);
		memset(ctx->debug, -1, sizeof(hl_debug_infos) * m->code->nfunctions);
	}
	for(i=0;i<m->code->nfloats;i++) {
		jit_buf(ctx);
		*ctx->buf.d++ = m->code->floats[i];
	}
#ifdef WIN64_UNWIND_TABLES
	jit_buf(ctx);
	ctx->unwind_offset = BUF_POS();
	write_unwind_data(ctx);

	ctx->unwind_table = malloc(sizeof(RUNTIME_FUNCTION) * (m->code->nfunctions + 10));
	memset(ctx->unwind_table, 0, sizeof(RUNTIME_FUNCTION) * (m->code->nfunctions + 10));
#endif
}

void hl_jit_init( jit_ctx *ctx, hl_module *m ) {
	hl_jit_init_module(ctx,m);
	ctx->c2hl = jit_build(ctx, jit_c2hl);
	ctx->hl2c = jit_build(ctx, jit_hl2c);
#	ifdef JIT_CUSTOM_LONGJUMP
	ctx->longjump = jit_build(ctx, jit_longjump);
#	endif
	ctx->static_functions[0] = (void*)(int_val)jit_build(ctx,jit_null_access);
	ctx->static_functions[1] = (void*)(int_val)jit_build(ctx,jit_assert);
	ctx->static_functions[2] = (void*)(int_val)jit_build(ctx,jit_null_field_access);
}

void hl_jit_reset( jit_ctx *ctx, hl_module *m ) {
	ctx->debug = nullptr;
	hl_jit_init_module(ctx,m);
}

static void *get_dyncast( hl_type *t ) {
	switch( t->kind ) {
	case HF32:
		return hl_dyn_castf;
	case HF64:
		return hl_dyn_castd;
	case HI64:
	case HGUID:
		return hl_dyn_casti64;
	case HI32:
	case HUI16:
	case HUI8:
	case HBOOL:
		return hl_dyn_casti;
	default:
		return hl_dyn_castp;
	}
}

static void *get_dynset( hl_type *t ) {
	switch( t->kind ) {
	case HF32:
		return hl_dyn_setf;
	case HF64:
		return hl_dyn_setd;
	case HI64:
	case HGUID:
		return hl_dyn_seti64;
	case HI32:
	case HUI16:
	case HUI8:
	case HBOOL:
		return hl_dyn_seti;
	default:
		return hl_dyn_setp;
	}
}

static void *get_dynget( hl_type *t ) {
	switch( t->kind ) {
	case HF32:
		return hl_dyn_getf;
	case HF64:
		return hl_dyn_getd;
	case HI64:
	case HGUID:
		return hl_dyn_geti64;
	case HI32:
	case HUI16:
	case HUI8:
	case HBOOL:
		return hl_dyn_geti;
	default:
		return hl_dyn_getp;
	}
}

static double uint_to_double( unsigned int v ) {
	return v;
}

static vclosure *alloc_static_closure( jit_ctx *ctx, int fid ) {
	hl_module *m = ctx->m;
	vclosure *c = hl_malloc(&m->ctx.alloc,sizeof(vclosure));
	int fidx = m->functions_indexes[fid];
	c->hasValue = 0;
	if( fidx >= m->code->nfunctions ) {
		// native
		c->t = m->code->natives[fidx - m->code->nfunctions].t;
		c->fun = m->functions_ptrs[fid];
		c->value = nullptr;
	} else {
		c->t = m->code->functions[fidx].type;
		c->fun = (void*)(int_val)fid;
		c->value = ctx->closure_list;
		ctx->closure_list = c;
	}
	return c;
}

static void make_dyn_cast( jit_ctx *ctx, vreg *dst, vreg *v ) {
	int size;
	preg p;
	preg *tmp;
	if( v->t->kind == HNULL && v->t->tparam->kind == dst->t->kind ) {
		int jnull, jend;
		preg *out;
		switch( dst->t->kind ) {
		case HUI8:
		case HUI16:
		case HI32:
		case HBOOL:
		case HI64:
		case HGUID:
			tmp = alloc_cpu(ctx, v, true);
			op64(ctx, TEST, tmp, tmp);
			XJump_small(JZero, jnull);
			op64(ctx, MOV, tmp, pmem(&p,tmp->id,8));
			XJump_small(JAlways, jend);
			patch_jump(ctx, jnull);
			op64(ctx, XOR, tmp, tmp);
			patch_jump(ctx, jend);
			store(ctx, dst, tmp, true);
			return;
		case HF32:
		case HF64:
			tmp = alloc_cpu(ctx, v, true);
			out = alloc_fpu(ctx, dst, false);
			op64(ctx, TEST, tmp, tmp);
			XJump_small(JZero, jnull);
			op64(ctx, dst->t->kind == HF32 ? MOVSS : MOVSD, out, pmem(&p,tmp->id,8));
			XJump_small(JAlways, jend);
			patch_jump(ctx, jnull);
			op64(ctx, XORPD, out, out);
			patch_jump(ctx, jend);
			store(ctx, dst, out, true);
			return;
		default:
			break;
		}
	}
	switch( dst->t->kind ) {
	case HF32:
	case HF64:
	case HI64:
	case HGUID:
		size = begin_native_call(ctx, 2);
		set_native_arg(ctx, pconst64(&p,(int_val)v->t));
		break;
	default:
		size = begin_native_call(ctx, 3);
		set_native_arg(ctx, pconst64(&p,(int_val)dst->t));
		set_native_arg(ctx, pconst64(&p,(int_val)v->t));
		break;
	}
	tmp = alloc_native_arg(ctx);
	op64(ctx,MOV,tmp,REG_AT(Ebp));
	if( v->stackPos >= 0 )
		op64(ctx,ADD,tmp,pconst(&p,v->stackPos));
	else
		op64(ctx,SUB,tmp,pconst(&p,-v->stackPos));
	set_native_arg(ctx,tmp);
	call_native(ctx,get_dyncast(dst->t),size);
	store_result(ctx, dst);
}

int hl_jit_function( jit_ctx *ctx, hl_module *m, hl_function *f ) {
	int i, size = 0, opCount;
	int codePos = BUF_POS();
	int nargs = f->type->fun->nargs;
	unsigned short *debug16 = nullptr;
	int *debug32 = nullptr;
	call_regs cregs = {0};
	hl_thread_info *tinf = nullptr;
	preg p;
	ctx->f = f;
	ctx->allocOffset = 0;
	if( f->nregs > ctx->maxRegs ) {
		free(ctx->vregs);
		ctx->vregs = (vreg*)malloc(sizeof(vreg) * (f->nregs + 1));
		if( ctx->vregs == nullptr ) {
			ctx->maxRegs = 0;
			return -1;
		}
		ctx->maxRegs = f->nregs;
	}
	if( f->nops > ctx->maxOps ) {
		free(ctx->opsPos);
		ctx->opsPos = (int*)malloc(sizeof(int) * (f->nops + 1));
		if( ctx->opsPos == nullptr ) {
			ctx->maxOps = 0;
			return -1;
		}
		ctx->maxOps = f->nops;
	}
	memset(ctx->opsPos,0,(f->nops+1)*sizeof(int));
	for(i=0;i<f->nregs;i++) {
		vreg *r = R(i);
		r->t = f->regs[i];
		r->size = hl_type_size(r->t);
		r->current = nullptr;
		r->stack.holds = nullptr;
		r->stack.id = i;
		r->stack.kind = RSTACK;
	}
	size = 0;
	int argsSize = 0;
	for(i=0;i<nargs;i++) {
		vreg *r = R(i);
		int creg = select_call_reg(&cregs,r->t,i);
		if( creg < 0 || IS_WINCALL64 ) {
			// use existing stack storage
			r->stackPos = argsSize + HL_WSIZE * 2;
			argsSize += stack_size(r->t);
		} else {
			// make room in local vars
			size += r->size;
			size += hl_pad_size(size,r->t);
			r->stackPos = -size;
		}
	}
	for(i=nargs;i<f->nregs;i++) {
		vreg *r = R(i);
		size += r->size;
		size += hl_pad_size(size,r->t); // align local vars
		r->stackPos = -size;
	}
#	ifdef HL_64
	size += (-size) & 15; // align on 16 bytes
#	else
	size += hl_pad_size(size,&hlt_dyn); // align on word size
#	endif
	ctx->totalRegsSize = size;
	jit_buf(ctx);
	ctx->functionPos = BUF_POS();
	// make sure currentPos is > 0 before any reg allocations happen
	// otherwise `alloc_reg` thinks that all registers are locked
	ctx->currentPos = 1;
	op_enter(ctx);
#	ifdef HL_64
	{
		// store in local var
		for(i=0;i<nargs;i++) {
			vreg *r = R(i);
			preg *p;
			int reg = mapped_reg(&cregs, i);
			if( reg < 0 ) continue;
			p = REG_AT(reg);
			copy(ctx,fetch(r),p,r->size);
			p->holds = r;
			r->current = p;
		}
	}
#	endif
	if( ctx->m->code->hasdebug ) {
		debug16 = (unsigned short*)malloc(sizeof(unsigned short) * (f->nops + 1));
		debug16[0] = (unsigned short)(BUF_POS() - codePos);
	}
	ctx->opsPos[0] = BUF_POS();

	for(opCount=0;opCount<f->nops;opCount++) {
		int jump;
		hl_opcode *o = f->ops + opCount;
		vreg *dst = R(o->p1);
		vreg *ra = R(o->p2);
		vreg *rb = R(o->p3);
		ctx->currentPos = opCount + 1;
		jit_buf(ctx);
#		ifdef JIT_DEBUG
		if( opCount == 0 || f->ops[opCount-1].op != OAsm ) {
			int uid = opCount + (f->findex<<16);
			op32(ctx, PUSH, pconst(&p,uid), UNUSED);
			op64(ctx, ADD, PESP, pconst(&p,HL_WSIZE));
		}
#		endif
		// emit code
		switch( o->op ) {
		case OMov:
		case OUnsafeCast:
			op_mov(ctx, dst, ra);
			break;
		case OInt:
			store_const(ctx, dst, m->code->ints[o->p2]);
			break;
		case OBool:
			store_const(ctx, dst, o->p2);
			break;
		case OGetGlobal:
			{
				void *addr = m->globals_data + m->globals_indexes[o->p2];
#				ifdef HL_64
				preg *tmp = alloc_reg(ctx, RCPU);
				op64(ctx, MOV, tmp, pconst64(&p,(int_val)addr));
				copy_to(ctx, dst, pmem(&p,tmp->id,0));
#				else
				copy_to(ctx, dst, paddr(&p,addr));
#				endif
			}
			break;
		case OSetGlobal:
			{
				void *addr = m->globals_data + m->globals_indexes[o->p1];
#				ifdef HL_64
				preg *tmp = alloc_reg(ctx, RCPU);
				op64(ctx, MOV, tmp, pconst64(&p,(int_val)addr));
				copy_from(ctx, pmem(&p,tmp->id,0), ra);
#				else
				copy_from(ctx, paddr(&p,addr), ra);
#				endif
			}
			break;
		case OCall3:
			{
				int args[3] = { o->p3, o->extra[0], o->extra[1] };
				op_call_fun(ctx, dst, o->p2, 3, args);
			}
			break;
		case OCall4:
			{
				int args[4] = { o->p3, o->extra[0], o->extra[1], o->extra[2] };
				op_call_fun(ctx, dst, o->p2, 4, args);
			}
			break;
		case OCallN:
			op_call_fun(ctx, dst, o->p2, o->p3, o->extra);
			break;
		case OCall0:
			op_call_fun(ctx, dst, o->p2, 0, nullptr);
			break;
		case OCall1:
			op_call_fun(ctx, dst, o->p2, 1, &o->p3);
			break;
		case OCall2:
			{
				int args[2] = { o->p3, (int)(int_val)o->extra };
				op_call_fun(ctx, dst, o->p2, 2, args);
			}
			break;
		case OSub:
		case OAdd:
		case OMul:
		case OSDiv:
		case OUDiv:
		case OShl:
		case OSShr:
		case OUShr:
		case OAnd:
		case OOr:
		case OXor:
		case OSMod:
		case OUMod:
			op_binop(ctx, dst, ra, rb, o->op);
			break;
		case ONeg:
			{
				if( IS_FLOAT(ra) ) {
					preg *pa = alloc_reg(ctx,RFPU);
					preg *pb = alloc_fpu(ctx,ra,true);
					op64(ctx,XORPD,pa,pa);
					op64(ctx,ra->t->kind == HF32 ? SUBSS : SUBSD,pa,pb);
					store(ctx,dst,pa,true);
				} else if( ra->t->kind == HI64 ) {
#					ifdef HL_64
					preg *pa = alloc_reg(ctx,RCPU);
					preg *pb = alloc_cpu(ctx,ra,true);
					op64(ctx,XOR,pa,pa);
					op64(ctx,SUB,pa,pb);
					store(ctx,dst,pa,true);
#					else
					error_i64();
#					endif
				} else {
					preg *pa = alloc_reg(ctx,RCPU);
					preg *pb = alloc_cpu(ctx,ra,true);
					op32(ctx,XOR,pa,pa);
					op32(ctx,SUB,pa,pb);
					store(ctx,dst,pa,true);
				}
			}
			break;
		case ONot:
			{
				preg *v = alloc_cpu(ctx,ra,true);
				op32(ctx,XOR,v,pconst(&p,1));
				store(ctx,dst,v,true);
			}
			break;
		case OJFalse:
		case OJTrue:
		case OJNotNull:
		case OJNull:
			{
				preg *r = dst->t->kind == HBOOL ? alloc_cpu8(ctx, dst, true) : alloc_cpu(ctx, dst, true);
				op64(ctx, dst->t->kind == HBOOL ? TEST8 : TEST, r, r);
				XJump( o->op == OJFalse || o->op == OJNull ? JZero : JNotZero,jump);
				register_jump(ctx,jump,(opCount + 1) + o->p2);
			}
			break;
		case OJEq:
		case OJNotEq:
		case OJSLt:
		case OJSGte:
		case OJSLte:
		case OJSGt:
		case OJULt:
		case OJUGte:
		case OJNotLt:
		case OJNotGte:
			op_jump(ctx,dst,ra,o,(opCount + 1) + o->p3);
			break;
		case OJAlways:
			jump = do_jump(ctx,o->op,false);
			register_jump(ctx,jump,(opCount + 1) + o->p1);
			break;
		case OToDyn:
			if( ra->t->kind == HBOOL ) {
				int size = begin_native_call(ctx, 1);
				set_native_arg(ctx, fetch(ra));
				call_native(ctx, hl_alloc_dynbool, size);
				store(ctx, dst, PEAX, true);
			} else {
				int_val rt = (int_val)ra->t;
				int jskip = 0;
				if( hl_is_ptr(ra->t) ) {
					int jnz;
					preg *a = alloc_cpu(ctx,ra,true);
					op64(ctx,TEST,a,a);
					XJump_small(JNotZero,jnz);
					op64(ctx,XOR,PEAX,PEAX); // will replace the result of alloc_dynamic at jump land
					XJump_small(JAlways,jskip);
					patch_jump(ctx,jnz);
				}
				call_native_consts(ctx, hl_alloc_dynamic, &rt, 1);
				// copy value to dynamic
				if( (IS_FLOAT(ra) || ra->size == 8) && !IS_64 ) {
					preg *tmp = REG_AT(RCPU_SCRATCH_REGS[1]);
					op64(ctx,MOV,tmp,&ra->stack);
					op32(ctx,MOV,pmem(&p,Eax,HDYN_VALUE),tmp);
					if( ra->t->kind == HF64 ) {
						ra->stackPos += 4;
						op64(ctx,MOV,tmp,&ra->stack);
						op32(ctx,MOV,pmem(&p,Eax,HDYN_VALUE+4),tmp);
						ra->stackPos -= 4;
					}
				} else {
					preg *tmp = REG_AT(RCPU_SCRATCH_REGS[1]);
					copy_from(ctx,tmp,ra);
					op64(ctx,MOV,pmem(&p,Eax,HDYN_VALUE),tmp);
				}
				if( hl_is_ptr(ra->t) ) patch_jump(ctx,jskip);
				store(ctx, dst, PEAX, true);
			}
			break;
		case OToSFloat:
			if( ra == dst ) break;
			if (ra->t->kind == HI32 || ra->t->kind == HUI16 || ra->t->kind == HUI8) {
				preg* r = alloc_cpu(ctx, ra, true);
				preg* w = alloc_fpu(ctx, dst, false);
				op32(ctx, dst->t->kind == HF64 ? CVTSI2SD : CVTSI2SS, w, r);
				store(ctx, dst, w, true);
			} else if (ra->t->kind == HI64 ) {
				preg* r = alloc_cpu(ctx, ra, true);
				preg* w = alloc_fpu(ctx, dst, false);
				op64(ctx, dst->t->kind == HF64 ? CVTSI2SD : CVTSI2SS, w, r);
				store(ctx, dst, w, true);
			} else if( ra->t->kind == HF64 && dst->t->kind == HF32 ) {
				preg *r = alloc_fpu(ctx,ra,true);
				preg *w = alloc_fpu(ctx,dst,false);
				op32(ctx,CVTSD2SS,w,r);
				store(ctx, dst, w, true);
			} else if( ra->t->kind == HF32 && dst->t->kind == HF64 ) {
				preg *r = alloc_fpu(ctx,ra,true);
				preg *w = alloc_fpu(ctx,dst,false);
				op32(ctx,CVTSS2SD,w,r);
				store(ctx, dst, w, true);
			} else
				ASSERT(0);
			break;
		case OToUFloat:
			{
				int size;
				size = prepare_call_args(ctx,1,&o->p2,ctx->vregs,0);
				call_native(ctx,uint_to_double,size);
				store_result(ctx,dst);
			}
			break;
		case OToInt:
			if( ra == dst ) break;
			if( ra->t->kind == HF64 ) {
				preg *r = alloc_fpu(ctx,ra,true);
				preg *w = alloc_cpu(ctx,dst,false);
				preg *tmp = alloc_reg(ctx,RCPU);
				op32(ctx,STMXCSR,pmem(&p,Esp,-4),UNUSED);
				op32(ctx,MOV,tmp,&p);
				op32(ctx,OR,tmp,pconst(&p,0x6000)); // set round towards 0
				op32(ctx,MOV,pmem(&p,Esp,-8),tmp);
				op32(ctx,LDMXCSR,&p,UNUSED);
				op32(ctx,CVTSD2SI,w,r);
				op32(ctx,LDMXCSR,pmem(&p,Esp,-4),UNUSED);
				store(ctx, dst, w, true);
			} else if (ra->t->kind == HF32) {
				preg *r = alloc_fpu(ctx, ra, true);
				preg *w = alloc_cpu(ctx, dst, false);
				preg *tmp = alloc_reg(ctx, RCPU);
				op32(ctx, STMXCSR, pmem(&p, Esp, -4), UNUSED);
				op32(ctx, MOV, tmp, &p);
				op32(ctx, OR, tmp, pconst(&p, 0x6000)); // set round towards 0
				op32(ctx, MOV, pmem(&p, Esp, -8), tmp);
				op32(ctx, LDMXCSR, &p, UNUSED);
				op32(ctx, CVTSS2SI, w, r);
				op32(ctx, LDMXCSR, pmem(&p, Esp, -4), UNUSED);
				store(ctx, dst, w, true);
			} else if( (dst->t->kind == HI64 || dst->t->kind == HGUID) && ra->t->kind == HI32 ) {
				if( ra->current != PEAX ) {
					op32(ctx, MOV, PEAX, fetch(ra));
					scratch(PEAX);
				}
#				ifdef HL_64
				op64(ctx, CDQE, UNUSED, UNUSED); // sign-extend Eax into Rax
				store(ctx, dst, PEAX, true);
#				else
				op32(ctx, CDQ, UNUSED, UNUSED); // sign-extend Eax into Eax:Edx
				scratch(REG_AT(Edx));
				op32(ctx, MOV, fetch(dst), PEAX);
				dst->stackPos += 4;
				op32(ctx, MOV, fetch(dst), REG_AT(Edx));
				dst->stackPos -= 4;
			} else if( dst->t->kind == HI32 && ra->t->kind == HI64 ) {
				error_i64();
#				endif
			} else {
				preg *r = alloc_cpu(ctx,dst,false);
				copy_from(ctx, r, ra);
				store(ctx, dst, r, true);
			}
			break;
		case ORet:
			op_ret(ctx, dst);
			break;
		case OIncr:
			{
				if( IS_FLOAT(dst) ) {
					ASSERT(0);
				} else {
					preg *v = fetch32(ctx,dst);
					op32(ctx,INC,v,UNUSED);
					if( v->kind != RSTACK ) store(ctx, dst, v, false);
				}
			}
			break;
		case ODecr:
			{
				if( IS_FLOAT(dst) ) {
					ASSERT(0);
				} else {
					preg *v = fetch32(ctx,dst);
					op32(ctx,DEC,v,UNUSED);
					if( v->kind != RSTACK ) store(ctx, dst, v, false);
				}
			}
			break;
		case OFloat:
			{
				if( m->code->floats[o->p2] == 0 ) {
					preg *f = alloc_fpu(ctx,dst,false);
					op64(ctx,XORPD,f,f);
				} else switch( dst->t->kind ) {
				case HF64:
				case HF32:
#					ifdef HL_64
					op64(ctx,dst->t->kind == HF32 ? CVTSD2SS : MOVSD,alloc_fpu(ctx,dst,false),pcodeaddr(&p,o->p2 * 8));
#					else
					op64(ctx,dst->t->kind == HF32 ? MOVSS : MOVSD,alloc_fpu(ctx,dst,false),paddr(&p,m->code->floats + o->p2));
#					endif
					break;
				default:
					ASSERT(dst->t->kind);
				}
				store(ctx,dst,dst->current,false);
			}
			break;
		case OString:
			op64(ctx,MOV,alloc_cpu(ctx, dst, false),pconst64(&p,(int_val)hl_get_ustring(m->code,o->p2)));
			store(ctx,dst,dst->current,false);
			break;
		case OBytes:
			{
				char *b = m->code->version >= 5 ? m->code->bytes + m->code->bytes_pos[o->p2] : m->code->strings[o->p2];
				op64(ctx,MOV,alloc_cpu(ctx,dst,false),pconst64(&p,(int_val)b));
				store(ctx,dst,dst->current,false);
			}
			break;
		case ONull:
			{
				op64(ctx,XOR,alloc_cpu(ctx, dst, false),alloc_cpu(ctx, dst, false));
				store(ctx,dst,dst->current,false);
			}
			break;
		case ONew:
			{
				int_val args[] = { (int_val)dst->t };
				void *allocFun;
				int nargs = 1;
				switch( dst->t->kind ) {
				case HOBJ:
				case HSTRUCT:
					allocFun = hl_alloc_obj;
					break;
				case HDYNOBJ:
					allocFun = hl_alloc_dynobj;
					nargs = 0;
					break;
				case HVIRTUAL:
					allocFun = hl_alloc_virtual;
					break;
				default:
					ASSERT(dst->t->kind);
				}
				call_native_consts(ctx, allocFun, args, nargs);
				store(ctx, dst, PEAX, true);
			}
			break;
		case OInstanceClosure:
			{
				preg *r = alloc_cpu(ctx, rb, true);
				jlist *j = (jlist*)hl_malloc(&ctx->galloc,sizeof(jlist));
				int size = begin_native_call(ctx,3);
				set_native_arg(ctx,r);

				j->pos = BUF_POS();
				j->target = o->p2;
				j->next = ctx->calls;
				ctx->calls = j;

				set_native_arg(ctx,pconst64(&p,RESERVE_ADDRESS));
				set_native_arg(ctx,pconst64(&p,(int_val)m->code->functions[m->functions_indexes[o->p2]].type));
				call_native(ctx,hl_alloc_closure_ptr,size);
				store(ctx,dst,PEAX,true);
			}
			break;
		case OVirtualClosure:
			{
				int size, i;
				preg *r = alloc_cpu_call(ctx, ra);
				hl_type *t = nullptr;
				hl_type *ot = ra->t;
				while( t == nullptr ) {
					for(i=0;i<ot->obj->nproto;i++) {
						hl_obj_proto *pp = ot->obj->proto + i;
						if( pp->pindex == o->p3 ) {
							t = m->code->functions[m->functions_indexes[pp->findex]].type;
							break;
						}
					}
					ot = ot->obj->super;
				}
				size = begin_native_call(ctx,3);
				set_native_arg(ctx,r);
				// read r->type->vobj_proto[i] for function address
				op64(ctx,MOV,r,pmem(&p,r->id,0));
				op64(ctx,MOV,r,pmem(&p,r->id,HL_WSIZE*2));
				op64(ctx,MOV,r,pmem(&p,r->id,HL_WSIZE*o->p3));
				set_native_arg(ctx,r);
				op64(ctx,MOV,r,pconst64(&p,(int_val)t));
				set_native_arg(ctx,r);
				call_native(ctx,hl_alloc_closure_ptr,size);
				store(ctx,dst,PEAX,true);
			}
			break;
		case OCallClosure:
			if( ra->t->kind == HDYN ) {
				// ASM for {
				//	vdynamic *args[] = {args};
				//  vdynamic *ret = hl_dyn_call(closure,args,nargs);
				//  dst = hl_dyncast(ret,t_dynamic,t_dst);
				// }
				int offset = o->p3 * HL_WSIZE;
				preg *r = alloc_reg(ctx, RCPU_CALL);
				if( offset & 15 ) offset += 16 - (offset & 15);
				op64(ctx,SUB,PESP,pconst(&p,offset));
				op64(ctx,MOV,r,PESP);
				for(i=0;i<o->p3;i++) {
					vreg *a = R(o->extra[i]);
					if( !hl_is_dynamic(a->t) ) ASSERT(0);
					preg *v = alloc_cpu(ctx,a,true);
					op64(ctx,MOV,pmem(&p,r->id,i * HL_WSIZE),v);
					RUNLOCK(v);
				}
#				ifdef HL_64
				int size = begin_native_call(ctx, 3) + offset;
				set_native_arg(ctx, pconst(&p,o->p3));
				set_native_arg(ctx, r);
				set_native_arg(ctx, fetch(ra));
#				else
				int size = pad_before_call(ctx,HL_WSIZE*2 + sizeof(int) + offset);
				op64(ctx,PUSH,pconst(&p,o->p3),UNUSED);
				op64(ctx,PUSH,r,UNUSED);
				op64(ctx,PUSH,alloc_cpu(ctx,ra,true),UNUSED);
#				endif
				call_native(ctx,hl_dyn_call,size);
				if( dst->t->kind != HVOID ) {
					store(ctx,dst,PEAX,true);
					make_dyn_cast(ctx,dst,dst);
				}
			} else {
				int jhasvalue, jend, size;
				// ASM for  if( c->hasValue ) c->fun(value,args) else c->fun(args)
				preg *r = alloc_cpu(ctx,ra,true);
				preg *tmp = alloc_reg(ctx, RCPU);
				op32(ctx,MOV,tmp,pmem(&p,r->id,HL_WSIZE*2));
				op32(ctx,TEST,tmp,tmp);
				scratch(tmp);
				XJump_small(JNotZero,jhasvalue);
				save_regs(ctx);
				size = prepare_call_args(ctx,o->p3,o->extra,ctx->vregs,0);
				preg *rr = r;
				if( rr->holds != ra ) rr = alloc_cpu(ctx, ra, true);
				op_call(ctx, pmem(&p,rr->id,HL_WSIZE), size);
				XJump_small(JAlways,jend);
				patch_jump(ctx,jhasvalue);
				restore_regs(ctx);
#				ifdef HL_64
				{
					int regids[64];
					preg *pc = REG_AT(CALL_REGS[0]);
					vreg *sc = R(f->nregs); // scratch register that we temporary rebind
					if( o->p3 >= 63 ) jit_error("assert");
					memcpy(regids + 1, o->extra, o->p3 * sizeof(int));
					regids[0] = f->nregs;
					sc->size = HL_WSIZE;
					sc->t = &hlt_dyn;
					op64(ctx, MOV, pc, pmem(&p,r->id,HL_WSIZE*3));
					scratch(pc);
					sc->current = pc;
					pc->holds = sc;
					size = prepare_call_args(ctx,o->p3 + 1,regids,ctx->vregs,0);
					if( r->holds != ra ) r = alloc_cpu(ctx, ra, true);
				}
#				else
				size = prepare_call_args(ctx,o->p3,o->extra,ctx->vregs,HL_WSIZE);
				if( r->holds != ra ) r = alloc_cpu(ctx, ra, true);
				op64(ctx, PUSH,pmem(&p,r->id,HL_WSIZE*3),UNUSED); // push closure value
#				endif
				op_call(ctx, pmem(&p,r->id,HL_WSIZE), size);
				discard_regs(ctx,false);
				patch_jump(ctx,jend);
				store_result(ctx, dst);
			}
			break;
		case OStaticClosure:
			{
				vclosure *c = alloc_static_closure(ctx,o->p2);
				preg *r = alloc_reg(ctx, RCPU);
				op64(ctx, MOV, r, pconst64(&p,(int_val)c));
				store(ctx,dst,r,true);
			}
			break;
		case OField:
			{
#				ifndef HL_64
				if( dst->t->kind == HI64 ) {
					error_i64();
					break;
				}
#				endif
				switch( ra->t->kind ) {
				case HOBJ:
				case HSTRUCT:
					{
						hl_runtime_obj *rt = hl_get_obj_rt(ra->t);
						preg *rr = alloc_cpu(ctx,ra, true);
						if( dst->t->kind == HSTRUCT ) {
							hl_type *ft = hl_obj_field_fetch(ra->t,o->p3)->t;
							if( ft->kind == HPACKED ) {
								preg *r = alloc_reg(ctx,RCPU);
								op64(ctx,LEA,r,pmem(&p,(CpuReg)rr->id,rt->fields_indexes[o->p3]));
								store(ctx,dst,r,true);
								break;
							}
						}
						copy_to(ctx,dst,pmem(&p, (CpuReg)rr->id, rt->fields_indexes[o->p3]));
					}
					break;
				case HVIRTUAL:
					// ASM for --> if( hl_vfields(o)[f] ) r = *hl_vfields(o)[f]; else r = hl_dyn_get(o,hash(field),vt)
					{
						int jhasfield, jend, size;
						bool need_type = !(IS_FLOAT(dst) || dst->t->kind == HI64);
						preg *v = alloc_cpu_call(ctx,ra);
						preg *r = alloc_reg(ctx,RCPU);
						op64(ctx,MOV,r,pmem(&p,v->id,sizeof(vvirtual)+HL_WSIZE*o->p3));
						op64(ctx,TEST,r,r);
						XJump_small(JNotZero,jhasfield);
						size = begin_native_call(ctx, need_type ? 3 : 2);
						if( need_type ) set_native_arg(ctx,pconst64(&p,(int_val)dst->t));
						set_native_arg(ctx,pconst64(&p,(int_val)ra->t->virt->fields[o->p3].hashed_name));
						set_native_arg(ctx,v);
						call_native(ctx,get_dynget(dst->t),size);
						store_result(ctx,dst);
						XJump_small(JAlways,jend);
						patch_jump(ctx,jhasfield);
						copy_to(ctx, dst, pmem(&p,(CpuReg)r->id,0));
						patch_jump(ctx,jend);
						scratch(dst->current);
					}
					break;
				default:
					ASSERT(ra->t->kind);
					break;
				}
			}
			break;
		case OSetField:
			{
				switch( dst->t->kind ) {
				case HOBJ:
				case HSTRUCT:
					{
						hl_runtime_obj *rt = hl_get_obj_rt(dst->t);
						preg *rr = alloc_cpu(ctx, dst, true);
						if( rb->t->kind == HSTRUCT ) {
							hl_type *ft = hl_obj_field_fetch(dst->t,o->p2)->t;
							if( ft->kind == HPACKED ) {
								hl_runtime_obj *frt = hl_get_obj_rt(ft->tparam);
								preg *prb = alloc_cpu(ctx, rb, true);
								preg *tmp = alloc_reg(ctx, RCPU_CALL);
								int offset = 0;
								while( offset < frt->size ) {
									int remain = frt->size - offset;
									int copy_size = remain >= HL_WSIZE ? HL_WSIZE : (remain >= 4 ? 4 : (remain >= 2 ? 2 : 1));
									copy(ctx, tmp, pmem(&p, (CpuReg)prb->id, offset), copy_size);
									copy(ctx, pmem(&p, (CpuReg)rr->id, rt->fields_indexes[o->p2]+offset), tmp, copy_size);
									offset += copy_size;
								}
								break;
							}
						}
						copy_from(ctx, pmem(&p, (CpuReg)rr->id, rt->fields_indexes[o->p2]), rb);
					}
					break;
				case HVIRTUAL:
					// ASM for --> if( hl_vfields(o)[f] ) *hl_vfields(o)[f] = v; else hl_dyn_set(o,hash(field),vt,v)
					{
						int jhasfield, jend;
						preg *obj = alloc_cpu_call(ctx,dst);
						preg *r = alloc_reg(ctx,RCPU);
						op64(ctx,MOV,r,pmem(&p,obj->id,sizeof(vvirtual)+HL_WSIZE*o->p2));
						op64(ctx,TEST,r,r);
						XJump_small(JNotZero,jhasfield);
#						ifdef HL_64
						switch( rb->t->kind ) {
						case HF64:
						case HF32:
							size = begin_native_call(ctx,3);
							set_native_arg_fpu(ctx, fetch(rb), rb->t->kind == HF32);
							break;
						case HI64:
						case HGUID:
							size = begin_native_call(ctx,3);
							set_native_arg(ctx, fetch(rb));
							break;
						default:
							size = begin_native_call(ctx, 4);
							set_native_arg(ctx, fetch(rb));
							set_native_arg(ctx, pconst64(&p,(int_val)rb->t));
							break;
						}
						set_native_arg(ctx,pconst(&p,dst->t->virt->fields[o->p2].hashed_name));
						set_native_arg(ctx,obj);
#						else
						switch( rb->t->kind ) {
						case HF64:
						case HI64:
						case HGUID:
							size = pad_before_call(ctx,HL_WSIZE*2 + sizeof(double));
							push_reg(ctx,rb);
							break;
						case HF32:
							size = pad_before_call(ctx,HL_WSIZE*2 + sizeof(float));
							push_reg(ctx,rb);
							break;
						default:
							size = pad_before_call(ctx,HL_WSIZE*4);
							op64(ctx,PUSH,fetch32(ctx,rb),UNUSED);
							op64(ctx,MOV,r,pconst64(&p,(int_val)rb->t));
							op64(ctx,PUSH,r,UNUSED);
							break;
						}
						op32(ctx,MOV,r,pconst(&p,dst->t->virt->fields[o->p2].hashed_name));
						op64(ctx,PUSH,r,UNUSED);
						op64(ctx,PUSH,obj,UNUSED);
#						endif
						call_native(ctx,get_dynset(rb->t),size);
						XJump_small(JAlways,jend);
						patch_jump(ctx,jhasfield);
						copy_from(ctx, pmem(&p,(CpuReg)r->id,0), rb);
						patch_jump(ctx,jend);
						scratch(rb->current);
					}
					break;
				default:
					ASSERT(dst->t->kind);
					break;
				}
			}
			break;
		case OGetThis:
			{
				vreg *r = R(0);
				hl_runtime_obj *rt = hl_get_obj_rt(r->t);
				preg *rr = alloc_cpu(ctx,r, true);
				if( dst->t->kind == HSTRUCT ) {
					hl_type *ft = hl_obj_field_fetch(r->t,o->p2)->t;
					if( ft->kind == HPACKED ) {
						preg *r = alloc_reg(ctx,RCPU);
						op64(ctx,LEA,r,pmem(&p,(CpuReg)rr->id,rt->fields_indexes[o->p2]));
						store(ctx,dst,r,true);
						break;
					}
				}
				copy_to(ctx,dst,pmem(&p, (CpuReg)rr->id, rt->fields_indexes[o->p2]));
			}
			break;
		case OSetThis:
			{
				vreg *r = R(0);
				hl_runtime_obj *rt = hl_get_obj_rt(r->t);
				preg *rr = alloc_cpu(ctx, r, true);
				if( ra->t->kind == HSTRUCT ) {
					hl_type *ft = hl_obj_field_fetch(r->t,o->p1)->t;
					if( ft->kind == HPACKED ) {
						hl_runtime_obj *frt = hl_get_obj_rt(ft->tparam);
						preg *pra = alloc_cpu(ctx, ra, true);
						preg *tmp = alloc_reg(ctx, RCPU_CALL);
						int offset = 0;
						while( offset < frt->size ) {
							int remain = frt->size - offset;
							int copy_size = remain >= HL_WSIZE ? HL_WSIZE : (remain >= 4 ? 4 : (remain >= 2 ? 2 : 1));
							copy(ctx, tmp, pmem(&p, (CpuReg)pra->id, offset), copy_size);
							copy(ctx, pmem(&p, (CpuReg)rr->id, rt->fields_indexes[o->p1]+offset), tmp, copy_size);
							offset += copy_size;
						}
						break;
					}
				}
				copy_from(ctx, pmem(&p, (CpuReg)rr->id, rt->fields_indexes[o->p1]), ra);
			}
			break;
		case OCallThis:
			{
				int nargs = o->p3 + 1;
				int *args = (int*)hl_malloc(&ctx->falloc,sizeof(int) * nargs);
				int size;
				preg *r = alloc_cpu(ctx, R(0), true);
				preg *tmp;
				tmp = alloc_reg(ctx, RCPU_CALL);
				op64(ctx,MOV,tmp,pmem(&p,r->id,0)); // read type
				op64(ctx,MOV,tmp,pmem(&p,tmp->id,HL_WSIZE*2)); // read proto
				args[0] = 0;
				for(i=1;i<nargs;i++)
					args[i] = o->extra[i-1];
				size = prepare_call_args(ctx,nargs,args,ctx->vregs,0);
				op_call(ctx,pmem(&p,tmp->id,o->p2*HL_WSIZE),size);
				discard_regs(ctx, false);
				store_result(ctx, dst);
			}
			break;
		case OCallMethod:
			switch( R(o->extra[0])->t->kind ) {
			case HOBJ: {
				int size;
				preg *r = alloc_cpu(ctx, R(o->extra[0]), true);
				preg *tmp;
				tmp = alloc_reg(ctx, RCPU_CALL);
				op64(ctx,MOV,tmp,pmem(&p,r->id,0)); // read type
				op64(ctx,MOV,tmp,pmem(&p,tmp->id,HL_WSIZE*2)); // read proto
				size = prepare_call_args(ctx,o->p3,o->extra,ctx->vregs,0);
				op_call(ctx,pmem(&p,tmp->id,o->p2*HL_WSIZE),size);
				discard_regs(ctx, false);
				store_result(ctx, dst);
				break;
			}
			case HVIRTUAL:
				// ASM for --> if( hl_vfields(o)[f] ) dst = *hl_vfields(o)[f](o->value,args...); else dst = hl_dyn_call_obj(o->value,field,args,&ret)
				{
					int size;
					int paramsSize;
					int jhasfield, jend;
					bool need_dyn;
					bool obj_in_args = false;
					vreg *obj = R(o->extra[0]);
					preg *v = alloc_cpu_call(ctx,obj);
					preg *r = alloc_reg(ctx,RCPU_CALL);
					op64(ctx,MOV,r,pmem(&p,v->id,sizeof(vvirtual)+HL_WSIZE*o->p2));
					op64(ctx,TEST,r,r);
					save_regs(ctx);

					if( o->p3 < 6 ) {
						XJump_small(JNotZero,jhasfield);
					} else {
						XJump(JNotZero,jhasfield);
					}

					need_dyn = !hl_is_ptr(dst->t) && dst->t->kind != HVOID;
					paramsSize = (o->p3 - 1) * HL_WSIZE;
					if( need_dyn ) paramsSize += sizeof(vdynamic);
					if( paramsSize & 15 ) paramsSize += 16 - (paramsSize&15);
					op64(ctx,SUB,PESP,pconst(&p,paramsSize));
					op64(ctx,MOV,r,PESP);

					for(i=0;i<o->p3-1;i++) {
						vreg *a = R(o->extra[i+1]);
						if( hl_is_ptr(a->t) ) {
							op64(ctx,MOV,pmem(&p,r->id,i*HL_WSIZE),alloc_cpu(ctx,a,true));
							if( a->current != v ) {
								RUNLOCK(a->current);
							} else
								obj_in_args = true;
						} else {
							preg *r2 = alloc_reg(ctx,RCPU);
							op64(ctx,LEA,r2,&a->stack);
							op64(ctx,MOV,pmem(&p,r->id,i*HL_WSIZE),r2);
							if( r2 != v ) RUNLOCK(r2);
						}
					}

					jit_buf(ctx);

					if( !need_dyn ) {
						size = begin_native_call(ctx, 5);
						set_native_arg(ctx, pconst(&p,0));
					} else {
						preg *rtmp = alloc_reg(ctx,RCPU);
						op64(ctx,LEA,rtmp,pmem(&p,Esp,paramsSize - sizeof(vdynamic)));
						size = begin_native_call(ctx, 5);
						set_native_arg(ctx,rtmp);
						if( !IS_64 ) RUNLOCK(rtmp);
					}
					set_native_arg(ctx,r);
					set_native_arg(ctx,pconst(&p,obj->t->virt->fields[o->p2].hashed_name)); // fid
					set_native_arg(ctx,pconst64(&p,(int_val)obj->t->virt->fields[o->p2].t)); // ftype
					set_native_arg(ctx,pmem(&p,v->id,HL_WSIZE)); // o->value
					call_native(ctx,hl_dyn_call_obj,size + paramsSize);
					if( need_dyn ) {
						preg *r = IS_FLOAT(dst) ? REG_AT(XMM(0)) : PEAX;
						copy(ctx,r,pmem(&p,Esp,HDYN_VALUE - (int)sizeof(vdynamic)),dst->size);
						store(ctx, dst, r, false);
					} else
						store(ctx, dst, PEAX, false);

					XJump_small(JAlways,jend);
					patch_jump(ctx,jhasfield);
					restore_regs(ctx);

					if( !obj_in_args ) {
						// o = o->value hack
						if( v->holds ) v->holds->current = nullptr;
						obj->current = v;
						v->holds = obj;
						op64(ctx,MOV,v,pmem(&p,v->id,HL_WSIZE));
						size = prepare_call_args(ctx,o->p3,o->extra,ctx->vregs,0);
					} else {
						// keep o->value in R(f->nregs)
						int regids[64];
						preg *pc = alloc_reg(ctx,RCPU_CALL);
						vreg *sc = R(f->nregs); // scratch register that we temporary rebind
						if( o->p3 >= 63 ) jit_error("assert");
						memcpy(regids, o->extra, o->p3 * sizeof(int));
						regids[0] = f->nregs;
						sc->size = HL_WSIZE;
						sc->t = &hlt_dyn;
						op64(ctx, MOV, pc, pmem(&p,v->id,HL_WSIZE));
						scratch(pc);
						sc->current = pc;
						pc->holds = sc;
						size = prepare_call_args(ctx,o->p3,regids,ctx->vregs,0);
					}

					op_call(ctx,r,size);
					discard_regs(ctx, false);
					store_result(ctx, dst);
					patch_jump(ctx,jend);
				}
				break;
			default:
				ASSERT(0);
				break;
			}
			break;
		case ORethrow:
			{
				int size = prepare_call_args(ctx,1,&o->p1,ctx->vregs,0);
				call_native(ctx,hl_rethrow,size);
			}
			break;
		case OThrow:
			{
				int size = prepare_call_args(ctx,1,&o->p1,ctx->vregs,0);
				call_native(ctx,hl_throw,size);
			}
			break;
		case OLabel:
			// NOP for now
			discard_regs(ctx,false);
			break;
		case OGetI8:
		case OGetI16:
			{
				preg *base = alloc_cpu(ctx, ra, true);
				preg *offset = alloc_cpu64(ctx, rb, true);
				preg *r = alloc_reg(ctx,o->op == OGetI8 ? RCPU_8BITS : RCPU);
				op64(ctx,XOR,r,r);
				op32(ctx, o->op == OGetI8 ? MOV8 : MOV16,r,pmem2(&p,base->id,offset->id,1,0));
				store(ctx, dst, r, true);
			}
			break;
		case OGetMem:
			{
				#ifndef HL_64
				if (dst->t->kind == HI64) {
					error_i64();
				}
				#endif
				preg *base = alloc_cpu(ctx, ra, true);
				preg *offset = alloc_cpu64(ctx, rb, true);
				store(ctx, dst, pmem2(&p,base->id,offset->id,1,0), false);
			}
			break;
		case OSetI8:
			{
				preg *base = alloc_cpu(ctx, dst, true);
				preg *offset = alloc_cpu64(ctx, ra, true);
				preg *value = alloc_cpu8(ctx, rb, true);
				op32(ctx,MOV8,pmem2(&p,base->id,offset->id,1,0),value);
			}
			break;
		case OSetI16:
			{
				preg *base = alloc_cpu(ctx, dst, true);
				preg *offset = alloc_cpu64(ctx, ra, true);
				preg *value = alloc_cpu(ctx, rb, true);
				op32(ctx,MOV16,pmem2(&p,base->id,offset->id,1,0),value);
			}
			break;
		case OSetMem:
			{
				preg *base = alloc_cpu(ctx, dst, true);
				preg *offset = alloc_cpu64(ctx, ra, true);
				preg *value;
				switch( rb->t->kind ) {
				case HI32:
					value = alloc_cpu(ctx, rb, true);
					op32(ctx,MOV,pmem2(&p,base->id,offset->id,1,0),value);
					break;
				case HF32:
					value = alloc_fpu(ctx, rb, true);
					op32(ctx,MOVSS,pmem2(&p,base->id,offset->id,1,0),value);
					break;
				case HF64:
					value = alloc_fpu(ctx, rb, true);
					op32(ctx,MOVSD,pmem2(&p,base->id,offset->id,1,0),value);
					break;
				case HI64:
				case HGUID:
					value = alloc_cpu(ctx, rb, true);
					op64(ctx,MOV,pmem2(&p,base->id,offset->id,1,0),value);
					break;
				default:
					ASSERT(rb->t->kind);
					break;
				}
			}
			break;
		case OType:
			{
				op64(ctx,MOV,alloc_cpu(ctx, dst, false),pconst64(&p,(int_val)(m->code->types + o->p2)));
				store(ctx,dst,dst->current,false);
			}
			break;
		case OGetType:
			{
				int jnext, jend;
				preg *r = alloc_cpu(ctx, ra, true);
				preg *tmp = alloc_reg(ctx, RCPU);
				op64(ctx,TEST,r,r);
				XJump_small(JNotZero,jnext);
				op64(ctx,MOV, tmp, pconst64(&p,(int_val)&hlt_void));
				XJump_small(JAlways,jend);
				patch_jump(ctx,jnext);
				op64(ctx, MOV, tmp, pmem(&p,r->id,0));
				patch_jump(ctx,jend);
				store(ctx,dst,tmp,true);
			}
			break;
		case OGetArray:
			{
				preg *rdst = IS_FLOAT(dst) ? alloc_fpu(ctx,dst,false) : alloc_cpu(ctx,dst,false);
				if( ra->t->kind == HABSTRACT ) {
					int osize;
					bool isRead = dst->t->kind != HOBJ && dst->t->kind != HSTRUCT;
					if( isRead )
						osize = sizeof(void*);
					else {
						hl_runtime_obj *rt = hl_get_obj_rt(dst->t);
						osize = rt->size;
					}
					preg *idx = alloc_cpu64(ctx, rb, true);
					op64(ctx, IMUL, idx, pconst(&p,osize));
					op64(ctx, isRead?MOV:LEA, rdst, pmem2(&p,alloc_cpu(ctx,ra, true)->id,idx->id,1,0));
					store(ctx,dst,dst->current,false);
					scratch(idx);
				} else {
					copy(ctx, rdst, pmem2(&p,alloc_cpu(ctx,ra,true)->id,alloc_cpu64(ctx,rb,true)->id,hl_type_size(dst->t),sizeof(varray)), dst->size);
					store(ctx,dst,dst->current,false);
				}
			}
			break;
		case OSetArray:
			{
				if( dst->t->kind == HABSTRACT ) {
					int osize;
					bool isWrite = rb->t->kind != HOBJ && rb->t->kind != HSTRUCT;
					if( isWrite ) {
						osize = sizeof(void*);
					} else {
						hl_runtime_obj *rt = hl_get_obj_rt(rb->t);
						osize = rt->size;
					}
					preg *pdst = alloc_cpu(ctx,dst,true);
					preg *pra = alloc_cpu64(ctx,ra,true);
					op64(ctx, IMUL, pra, pconst(&p,osize));
					op64(ctx, ADD, pdst, pra);
					scratch(pra);
					preg *prb = alloc_cpu(ctx,rb,true);
					preg *tmp = alloc_reg(ctx, RCPU_CALL);
					int offset = 0;
					while( offset < osize ) {
						int remain = osize - offset;
						int copy_size = remain >= HL_WSIZE ? HL_WSIZE : (remain >= 4 ? 4 : (remain >= 2 ? 2 : 1));
						copy(ctx, tmp, pmem(&p, prb->id, offset), copy_size);
						copy(ctx, pmem(&p, pdst->id, offset), tmp, copy_size);
						offset += copy_size;
					}
					scratch(pdst);
				} else  {
					preg *rrb = IS_FLOAT(rb) ? alloc_fpu(ctx,rb,true) : alloc_cpu(ctx,rb,true);
					copy(ctx, pmem2(&p,alloc_cpu(ctx,dst,true)->id,alloc_cpu64(ctx,ra,true)->id,hl_type_size(rb->t),sizeof(varray)), rrb, rb->size);
				}
			}
			break;
		case OArraySize:
			{
				op32(ctx,MOV,alloc_cpu(ctx,dst,false),pmem(&p,alloc_cpu(ctx,ra,true)->id,ra->t->kind == HABSTRACT ? HL_WSIZE + 4 : HL_WSIZE*2));
				store(ctx,dst,dst->current,false);
			}
			break;
		case ORef:
			{
				scratch(ra->current);
				op64(ctx,MOV,alloc_cpu(ctx,dst,false),REG_AT(Ebp));
				if( ra->stackPos < 0 )
					op64(ctx,SUB,dst->current,pconst(&p,-ra->stackPos));
				else
					op64(ctx,ADD,dst->current,pconst(&p,ra->stackPos));
				store(ctx,dst,dst->current,false);
			}
			break;
		case OUnref:
			copy_to(ctx,dst,pmem(&p,alloc_cpu(ctx,ra,true)->id,0));
			break;
		case OSetref:
			copy_from(ctx,pmem(&p,alloc_cpu(ctx,dst,true)->id,0),ra);
			break;
		case ORefData:
			switch( ra->t->kind ) {
			case HARRAY:
				{
					preg *r = fetch(ra);
					preg *d = alloc_cpu(ctx,dst,false);
					op64(ctx,MOV,d,r);
					op64(ctx,ADD,d,pconst(&p,sizeof(varray)));
					store(ctx,dst,dst->current,false);
				}
				break;
			default:
				ASSERT(ra->t->kind);
			}
			break;
		case ORefOffset:
			{
				preg *d = alloc_cpu(ctx,rb,true);
				preg *r2 = alloc_cpu(ctx,dst,false);
				preg *r = fetch(ra);
				int size = hl_type_size(dst->t->tparam);
				op64(ctx,MOV,r2,r);
				switch( size ) {
				case 1:
					break;
				case 2:
					op64(ctx,SHL,d,pconst(&p,1));
					break;
				case 4:
					op64(ctx,SHL,d,pconst(&p,2));
					break;
				case 8:
					op64(ctx,SHL,d,pconst(&p,3));
					break;
				default:
					op64(ctx,IMUL,d,pconst(&p,size));
					break;
				}
				op64(ctx,ADD,r2,d);
				scratch(d);
				store(ctx,dst,dst->current,false);
			}
			break;
		case OToVirtual:
			{
#				ifdef HL_64
				int size = pad_before_call(ctx, 0);
				op64(ctx,MOV,REG_AT(CALL_REGS[1]),fetch(ra));
				op64(ctx,MOV,REG_AT(CALL_REGS[0]),pconst64(&p,(int_val)dst->t));
#				else
				int size = pad_before_call(ctx, HL_WSIZE*2);
				op32(ctx,PUSH,fetch(ra),UNUSED);
				op32(ctx,PUSH,pconst(&p,(int)(int_val)dst->t),UNUSED);
#				endif
				if( ra->t->kind == HOBJ ) hl_get_obj_rt(ra->t); // ensure it's initialized
				call_native(ctx,hl_to_virtual,size);
				store(ctx,dst,PEAX,true);
			}
			break;
		case OMakeEnum:
			{
				hl_enum_construct *c = &dst->t->tenum->constructs[o->p2];
				int_val args[] = { (int_val)dst->t, o->p2 };
				int i;
				call_native_consts(ctx, hl_alloc_enum, args, 2);
				RLOCK(PEAX);
				for(i=0;i<c->nparams;i++) {
					preg *r = fetch(R(o->extra[i]));
					copy(ctx, pmem(&p,Eax,c->offsets[i]),r, R(o->extra[i])->size);
					RUNLOCK(fetch(R(o->extra[i])));
					if ((i & 15) == 0) jit_buf(ctx);
				}
				store(ctx, dst, PEAX, true);
			}
			break;
		case OEnumAlloc:
			{
				int_val args[] = { (int_val)dst->t, o->p2 };
				call_native_consts(ctx, hl_alloc_enum, args, 2);
				store(ctx, dst, PEAX, true);
			}
			break;
		case OEnumField:
			{
				hl_enum_construct *c = &ra->t->tenum->constructs[o->p3];
				preg *r = alloc_cpu(ctx,ra,true);
				copy_to(ctx,dst,pmem(&p,r->id,c->offsets[(int)(int_val)o->extra]));
			}
			break;
		case OSetEnumField:
			{
				hl_enum_construct *c = &dst->t->tenum->constructs[0];
				preg *r = alloc_cpu(ctx,dst,true);
				switch( rb->t->kind ) {
				case HF64:
					{
						preg *d = alloc_fpu(ctx,rb,true);
						copy(ctx,pmem(&p,r->id,c->offsets[o->p2]),d,8);
						break;
					}
				default:
					copy(ctx,pmem(&p,r->id,c->offsets[o->p2]),alloc_cpu(ctx,rb,true),hl_type_size(c->params[o->p2]));
					break;
				}
			}
			break;
		case ONullCheck:
			{
				int jz;
				preg *r = alloc_cpu(ctx,dst,true);
				op64(ctx,TEST,r,r);
				XJump_small(JNotZero,jz);

				hl_opcode *next = f->ops + opCount + 1;
				bool null_field_access = false;
				int hashed_name = 0;
				// skip const and operation between nullcheck and access
				while( (next < f->ops + f->nops - 1) && (next->op >= OInt && next->op <= ODecr) ) {
					next++;
				}
				if( (next->op == OField && next->p2 == o->p1) || (next->op == OSetField && next->p1 == o->p1) ) {
					int fid = next->op == OField ? next->p3 : next->p2;
					hl_obj_field *f = nullptr;
					if( dst->t->kind == HOBJ || dst->t->kind == HSTRUCT )
						f = hl_obj_field_fetch(dst->t, fid);
					else if( dst->t->kind == HVIRTUAL )
						f = dst->t->virt->fields + fid;
					if( f == nullptr ) ASSERT(dst->t->kind);
					null_field_access = true;
					hashed_name = f->hashed_name;
				} else if( (next->op >= OCall1 && next->op <= OCallN) && next->p3 == o->p1 ) {
					int fid = next->p2 < 0 ? -1 : ctx->m->functions_indexes[next->p2];
					hl_function *cf = ctx->m->code->functions + fid;
					const uchar *name = fun_field_name(cf);
					null_field_access = true;
					hashed_name = hl_hash_gen(name, true);
				}

				if( null_field_access ) {
					pad_before_call(ctx, HL_WSIZE);
					if( hashed_name >= 0 && hashed_name < 256 )
						op64(ctx,PUSH8,pconst(&p,hashed_name),UNUSED);
					else
						op32(ctx,PUSH,pconst(&p,hashed_name),UNUSED);
				} else {
					pad_before_call(ctx, 0);
				}

				jlist *j = (jlist*)hl_malloc(&ctx->galloc,sizeof(jlist));
				j->pos = BUF_POS();
				j->target = null_field_access ? -3 : -1;
				j->next = ctx->calls;
				ctx->calls = j;

				op64(ctx,MOV,PEAX,pconst64(&p,RESERVE_ADDRESS));
				op_call(ctx,PEAX,-1);
				patch_jump(ctx,jz);
			}
			break;
		case OSafeCast:
			make_dyn_cast(ctx, dst, ra);
			break;
		case ODynGet:
			{
				int size;
#				ifdef HL_64
				if( IS_FLOAT(dst) || dst->t->kind == HI64 ) {
					size = begin_native_call(ctx,2);
				} else {
					size = begin_native_call(ctx,3);
					set_native_arg(ctx,pconst64(&p,(int_val)dst->t));
				}
				set_native_arg(ctx,pconst64(&p,(int_val)hl_hash_utf8(m->code->strings[o->p3])));
				set_native_arg(ctx,fetch(ra));
#				else
				preg *r;
				r = alloc_reg(ctx,RCPU);
				if( IS_FLOAT(dst) || dst->t->kind == HI64 ) {
					size = pad_before_call(ctx,HL_WSIZE*2);
				} else {
					size = pad_before_call(ctx,HL_WSIZE*3);
					op64(ctx,MOV,r,pconst64(&p,(int_val)dst->t));
					op64(ctx,PUSH,r,UNUSED);
				}
				op64(ctx,MOV,r,pconst64(&p,(int_val)hl_hash_utf8(m->code->strings[o->p3])));
				op64(ctx,PUSH,r,UNUSED);
				op64(ctx,PUSH,fetch(ra),UNUSED);
#				endif
				call_native(ctx,get_dynget(dst->t),size);
				store_result(ctx,dst);
			}
			break;
		case ODynSet:
			{
				int size;
#				ifdef HL_64
				switch( rb->t->kind ) {
				case HF32:
				case HF64:
					size = begin_native_call(ctx, 3);
					set_native_arg_fpu(ctx,fetch(rb),rb->t->kind == HF32);
					set_native_arg(ctx,pconst64(&p,hl_hash_gen(hl_get_ustring(m->code,o->p2),true)));
					set_native_arg(ctx,fetch(dst));
					call_native(ctx,get_dynset(rb->t),size);
					break;
				case HI64:
				case HGUID:
					size = begin_native_call(ctx, 3);
					set_native_arg(ctx,fetch(rb));
					set_native_arg(ctx,pconst64(&p,hl_hash_gen(hl_get_ustring(m->code,o->p2),true)));
					set_native_arg(ctx,fetch(dst));
					call_native(ctx,get_dynset(rb->t),size);
					break;
				default:
					size = begin_native_call(ctx,4);
					set_native_arg(ctx,fetch(rb));
					set_native_arg(ctx,pconst64(&p,(int_val)rb->t));
					set_native_arg(ctx,pconst64(&p,hl_hash_gen(hl_get_ustring(m->code,o->p2),true)));
					set_native_arg(ctx,fetch(dst));
					call_native(ctx,get_dynset(rb->t),size);
					break;
				}
#				else
				switch( rb->t->kind ) {
				case HF32:
					size = pad_before_call(ctx, HL_WSIZE*2 + sizeof(float));
					push_reg(ctx,rb);
					op32(ctx,PUSH,pconst64(&p,hl_hash_gen(hl_get_ustring(m->code,o->p2),true)),UNUSED);
					op32(ctx,PUSH,fetch(dst),UNUSED);
					call_native(ctx,get_dynset(rb->t),size);
					break;
				case HF64:
				case HI64:
				case HGUID:
					size = pad_before_call(ctx, HL_WSIZE*2 + sizeof(double));
					push_reg(ctx,rb);
					op32(ctx,PUSH,pconst64(&p,hl_hash_gen(hl_get_ustring(m->code,o->p2),true)),UNUSED);
					op32(ctx,PUSH,fetch(dst),UNUSED);
					call_native(ctx,get_dynset(rb->t),size);
					break;
				default:
					size = pad_before_call(ctx, HL_WSIZE*4);
					op32(ctx,PUSH,fetch32(ctx,rb),UNUSED);
					op32(ctx,PUSH,pconst64(&p,(int_val)rb->t),UNUSED);
					op32(ctx,PUSH,pconst64(&p,hl_hash_gen(hl_get_ustring(m->code,o->p2),true)),UNUSED);
					op32(ctx,PUSH,fetch(dst),UNUSED);
					call_native(ctx,get_dynset(rb->t),size);
					break;
				}
#				endif
			}
			break;
		case OTrap:
			{
				int size, jenter, jtrap;
				int offset = 0;
				int trap_size = (sizeof(hl_trap_ctx) + 15) & 0xFFF0;
				hl_trap_ctx *t = nullptr;
#				ifndef HL_THREADS
				if( tinf == nullptr ) tinf = hl_get_thread(); // single thread
#				endif

#				ifdef HL_64
				preg *trap = REG_AT(CALL_REGS[0]);
#				else
				preg *trap = PEAX;
#				endif
				RLOCK(trap);

				preg *treg = alloc_reg(ctx, RCPU);
				if( !tinf ) {
					call_native(ctx, hl_get_thread, 0);
					op64(ctx,MOV,treg,PEAX);
					offset = (int)(int_val)&tinf->trap_current;
				} else {
					offset = 0;
					op64(ctx,MOV,treg,pconst64(&p,(int_val)&tinf->trap_current));
				}
				op64(ctx,MOV,trap,pmem(&p,treg->id,offset));
				op64(ctx,SUB,PESP,pconst(&p,trap_size));
				op64(ctx,MOV,pmem(&p,Esp,(int)(int_val)&t->prev),trap);
				op64(ctx,MOV,trap,PESP);
				op64(ctx,MOV,pmem(&p,treg->id,offset),trap);

				/*
					trap E,@catch
					catch g
					catch g2
					...
					@:catch

					// Before haxe 5
					This is a bit hackshish : we want to detect the type of exception filtered by the catch so we check the following
					sequence of HL opcodes:

					trap E,@catch
					...
					@catch:
					global R, _
					call _, ???(R,E)

					??? is expected to be hl.BaseType.check
				*/
				hl_opcode *cat = f->ops + opCount + 1;
				hl_opcode *next = f->ops + opCount + 1 + o->p2;
				hl_opcode *next2 = f->ops + opCount + 2 + o->p2;
				if( cat->op == OCatch || (next->op == OGetGlobal && next2->op == OCall2 && next2->p3 == next->p1 && dst->stack.id == (int)(int_val)next2->extra) ) {
					int gindex = cat->op == OCatch ? cat->p1 : next->p2;
					hl_type *gt = m->code->globals[gindex];
					while( gt->kind == HOBJ && gt->obj->super ) gt = gt->obj->super;
					if( gt->kind == HOBJ && gt->obj->nfields && gt->obj->fields[0].t->kind == HTYPE ) {
						void *addr = m->globals_data + m->globals_indexes[gindex];
#						ifdef HL_64
						op64(ctx,MOV,treg,pconst64(&p,(int_val)addr));
						op64(ctx,MOV,treg,pmem(&p,treg->id,0));
#						else
						op64(ctx,MOV,treg,paddr(&p,addr));
#						endif
					} else
						op64(ctx,MOV,treg,pconst(&p,0));
				} else {
					op64(ctx,MOV,treg,pconst(&p,0));
				}
				op64(ctx,MOV,pmem(&p,Esp,(int)(int_val)&t->tcheck),treg);

				// On Win64 setjmp actually takes two arguments
				// the jump buffer and the frame pointer (or the stack pointer if there is no FP)
#if defined(HL_WIN) && defined(HL_64)
				size = begin_native_call(ctx, 2);
				set_native_arg(ctx, REG_AT(Ebp));
#else
				size = begin_native_call(ctx, 1);
#endif
				set_native_arg(ctx,trap);
#ifdef HL_MINGW
				call_native(ctx,_setjmp,size);
#else
				call_native(ctx,setjmp,size);
#endif
				op64(ctx,TEST,PEAX,PEAX);
				XJump_small(JZero,jenter);
				op64(ctx,ADD,PESP,pconst(&p,trap_size));
				if( !tinf ) {
					call_native(ctx, hl_get_thread, 0);
					op64(ctx,MOV,PEAX,pmem(&p, Eax, (int)(int_val)&tinf->exc_value));
				} else {
					op64(ctx,MOV,PEAX,pconst64(&p,(int_val)&tinf->exc_value));
					op64(ctx,MOV,PEAX,pmem(&p, Eax, 0));
				}
				store(ctx,dst,PEAX,false);

				jtrap = do_jump(ctx,OJAlways,false);
				register_jump(ctx,jtrap,(opCount + 1) + o->p2);
				patch_jump(ctx,jenter);
			}
			break;
		case OEndTrap:
			{
				int trap_size = (sizeof(hl_trap_ctx) + 15) & 0xFFF0;
				hl_trap_ctx *tmp = nullptr;
				preg *addr,*r;
				int offset;
				if (!tinf) {
					call_native(ctx, hl_get_thread, 0);
					addr = PEAX;
					RLOCK(addr);
					offset = (int)(int_val)&tinf->trap_current;
				} else {
					offset = 0;
					addr = alloc_reg(ctx, RCPU);
					op64(ctx, MOV, addr, pconst64(&p, (int_val)&tinf->trap_current));
				}
				r = alloc_reg(ctx, RCPU);
				op64(ctx, MOV, r, pmem(&p,addr->id,offset));
				op64(ctx, MOV, r, pmem(&p,r->id,(int)(int_val)&tmp->prev));
				op64(ctx, MOV, pmem(&p,addr->id, offset), r);
#				ifdef HL_WIN
				// erase eip (prevent false positive)
				{
					_JUMP_BUFFER *b = nullptr;
#					ifdef HL_64
					op64(ctx,MOV,pmem(&p,Esp,(int)(int_val)&(b->Rip)),PEAX);
#					else
					op64(ctx,MOV,pmem(&p,Esp,(int)&(b->Eip)),PEAX);
#					endif
				}
#				endif
				op64(ctx,ADD,PESP,pconst(&p,trap_size));
			}
			break;
		case OEnumIndex:
			{
				preg *r = alloc_reg(ctx,RCPU);
				op64(ctx,MOV,r,pmem(&p,alloc_cpu(ctx,ra,true)->id,HL_WSIZE));
				store(ctx,dst,r,true);
				break;
			}
			break;
		case OSwitch:
			{
				int jdefault;
				int i;
				preg *r = alloc_cpu(ctx, dst, true);
				preg *r2 = alloc_reg(ctx, RCPU);
				op32(ctx, CMP, r, pconst(&p,o->p2));
				XJump(JUGte,jdefault);
				// r2 = r * 5 + eip
#				ifdef HL_64
				op64(ctx, XOR, r2, r2);
#				endif
				op32(ctx, MOV, r2, r);
				op32(ctx, SHL, r2, pconst(&p,2));
				op32(ctx, ADD, r2, r);
#				ifdef HL_64
				preg *tmp = alloc_reg(ctx, RCPU);
				op64(ctx, MOV, tmp, pconst64(&p,RESERVE_ADDRESS));
#				else
				op64(ctx, ADD, r2, pconst64(&p,RESERVE_ADDRESS));
#				endif
				{
					jlist *s = (jlist*)hl_malloc(&ctx->galloc, sizeof(jlist));
					s->pos = BUF_POS() - sizeof(void*);
					s->next = ctx->switchs;
					ctx->switchs = s;
				}
#				ifdef HL_64
				op64(ctx, ADD, r2, tmp);
#				endif
				op64(ctx, JMP, r2, UNUSED);
				for(i=0;i<o->p2;i++) {
					int j = do_jump(ctx,OJAlways,false);
					register_jump(ctx,j,(opCount + 1) + o->extra[i]);
					if( (i & 15) == 0 ) jit_buf(ctx);
				}
				patch_jump(ctx, jdefault);
			}
			break;
		case OGetTID:
			op32(ctx, MOV, alloc_cpu(ctx,dst,false), pmem(&p,alloc_cpu(ctx,ra,true)->id,0));
			store(ctx,dst,dst->current,false);
			break;
		case OAssert:
			{
				pad_before_call(ctx, 0);
				jlist *j = (jlist*)hl_malloc(&ctx->galloc,sizeof(jlist));
				j->pos = BUF_POS();
				j->target = -2;
				j->next = ctx->calls;
				ctx->calls = j;

				op64(ctx,MOV,PEAX,pconst64(&p,RESERVE_ADDRESS));
				op_call(ctx,PEAX,-1);
			}
			break;
		case ONop:
			break;
		case OPrefetch:
			{
				preg *r = alloc_cpu(ctx, dst, true);
				if( o->p2 > 0 ) {
					switch( dst->t->kind ) {
					case HOBJ:
					case HSTRUCT:
						{
							hl_runtime_obj *rt = hl_get_obj_rt(dst->t);
							preg *r2 = alloc_reg(ctx, RCPU);
							op64(ctx, LEA, r2, pmem(&p, r->id, rt->fields_indexes[o->p2-1]));
							r = r2;
						}
						break;
					default:
						ASSERT(dst->t->kind);
						break;
					}
				}
				switch( o->p3 ) {
				case 0:
					op64(ctx, PREFETCHT0, pmem(&p,r->id,0), UNUSED);
					break;
				case 1:
					op64(ctx, PREFETCHT1, pmem(&p,r->id,0), UNUSED);
					break;
				case 2:
					op64(ctx, PREFETCHT2, pmem(&p,r->id,0), UNUSED);
					break;
				case 3:
					op64(ctx, PREFETCHNTA, pmem(&p,r->id,0), UNUSED);
					break;
				case 4:
					op64(ctx, PREFETCHW, pmem(&p,r->id,0), UNUSED);
					break;
				default:
					ASSERT(o->p3);
					break;
				}
			}
			break;
		case OAsm:
			{
				switch( o->p1 ) {
				case 0: // byte output
					B(o->p2);
					break;
				case 1: // scratch cpu reg
					scratch(REG_AT(o->p2));
					break;
				case 2: // read vm reg
					rb--;
					copy(ctx, REG_AT(o->p2), &rb->stack, rb->size);
					scratch(REG_AT(o->p2));
					break;
				case 3: // write vm reg
					rb--;
					copy(ctx, &rb->stack, REG_AT(o->p2), rb->size);
					scratch(rb->current);
					break;
				case 4:
					if( ctx->totalRegsSize != 0 )
						hl_fatal("Asm naked function should not have local variables");
					if( opCount != 0 )
						hl_fatal("Asm naked function should be on first opcode");
					ctx->buf.b -= BUF_POS() - ctx->functionPos; // reset to our function start
					break;
				default:
					ASSERT(o->p1);
					break;
				}
			}
			break;
		case OCatch:
			// Only used by OTrap typing
			break;
		default:
			jit_error(hl_op_name(o->op));
			break;
		}
		// we are landing at this position, assume we have lost our registers
		if( ctx->opsPos[opCount+1] == -1 )
			discard_regs(ctx,true);
		ctx->opsPos[opCount+1] = BUF_POS();

		// write debug infos
		size = BUF_POS() - codePos;
		if( debug16 && size > 0xFF00 ) {
			debug32 = malloc(sizeof(int) * (f->nops + 1));
			for(i=0;i<ctx->currentPos;i++)
				debug32[i] = debug16[i];
			free(debug16);
			debug16 = nullptr;
		}
		if( debug16 ) debug16[ctx->currentPos] = (unsigned short)size; else if( debug32 ) debug32[ctx->currentPos] = size;

	}
	// patch jumps
	{
		jlist *j = ctx->jumps;
		while( j ) {
			*(int*)(ctx->startBuf + j->pos) = ctx->opsPos[j->target] - (j->pos + 4);
			j = j->next;
		}
		ctx->jumps = nullptr;
	}
	int codeEndPos = BUF_POS();
	// add nops padding
	jit_nops(ctx);
	// clear regs
	for(i=0;i<REG_COUNT;i++) {
		preg *r = REG_AT(i);
		r->holds = nullptr;
		r->lock = 0;
	}
	// save debug infos
	if( ctx->debug ) {
		int fid = (int)(f - m->code->functions);
		ctx->debug[fid].start = codePos;
		ctx->debug[fid].offsets = debug32 ? (void*)debug32 : (void*)debug16;
		ctx->debug[fid].large = debug32 != nullptr;
	}
	// unwind info
#ifdef WIN64_UNWIND_TABLES
	int uw_idx = ctx->nunwind++;
	ctx->unwind_table[uw_idx].BeginAddress = codePos;
	ctx->unwind_table[uw_idx].EndAddress = codeEndPos;
	ctx->unwind_table[uw_idx].UnwindData = ctx->unwind_offset;
#endif
	// reset tmp allocator
	hl_free(&ctx->falloc);
	return codePos;
}

static void *get_wrapper( hl_type *t ) {
	return call_jit_hl2c;
}

void hl_jit_patch_method( void *old_fun, void **new_fun_table ) {
	// mov eax, addr
	// jmp [eax]
	unsigned char *b = (unsigned char*)old_fun;
	unsigned long long addr = (unsigned long long)(int_val)new_fun_table;
#	ifdef HL_64
	*b++ = 0x48;
	*b++ = 0xB8;
	*b++ = (unsigned char)addr;
	*b++ = (unsigned char)(addr>>8);
	*b++ = (unsigned char)(addr>>16);
	*b++ = (unsigned char)(addr>>24);
	*b++ = (unsigned char)(addr>>32);
	*b++ = (unsigned char)(addr>>40);
	*b++ = (unsigned char)(addr>>48);
	*b++ = (unsigned char)(addr>>56);
#	else
	*b++ = 0xB8;
	*b++ = (unsigned char)addr;
	*b++ = (unsigned char)(addr>>8);
	*b++ = (unsigned char)(addr>>16);
	*b++ = (unsigned char)(addr>>24);
#	endif
	*b++ = 0xFF;
	*b++ = 0x20;
}

static void missing_closure() {
	hl_error("Missing static closure");
}

void *hl_jit_code( jit_ctx *ctx, hl_module *m, int *codesize, hl_debug_infos **debug, hl_module *previous ) {
	hl_codegen_flush_consts(ctx);
	jit_code_append(ctx);
	int size = ctx->out_pos;
	if( size & 4095 ) size += 4096 - (size&4095);
	unsigned char *code = (unsigned char*)hl_alloc_executable_memory(size);
	if( code == NULL ) return NULL;
	memcpy(code,ctx->output,size);
	*codesize = size;
	*debug = m->jit_debug;
	ctx->final_code = code;
	hl_emit_final(ctx);
	hl_codegen_final(ctx);
	arg_reg_count = ctx->cfg.regs.nargs;
	arg_fp_count = ctx->cfg.floats.nargs;
	call_jit_c2hl = ctx->final_code + ctx->code_funs.c2hl;
	call_jit_hl2c = ctx->final_code + ctx->code_funs.hl2c;
#	ifdef WIN64_UNWIND_TABLES
	ctx->mod->unwind_table_size = ctx->fdef_index;
#	endif
	hl_setup.get_wrapper = default_wrapper;
	hl_setup.static_call = callback_c2hl;
	return code;
}

void hl_jit_patch_method( void*fun, void**newt ) {
	jit_assert();
}
