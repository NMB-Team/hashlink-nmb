#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "trace.h"

#ifdef HL_WIN
#undef _GUID
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HL_API int hl_thread_id(void);

static FILE *trace_file;
static char trace_buffer[65536];
static size_t trace_pos;
static bool trace_first;
#ifdef HL_WIN
static CRITICAL_SECTION trace_lock;
static LARGE_INTEGER trace_frequency;
#define TRACE_LOCK() EnterCriticalSection(&trace_lock)
#define TRACE_UNLOCK() LeaveCriticalSection(&trace_lock)
#else
static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;
#define TRACE_LOCK() pthread_mutex_lock(&trace_lock)
#define TRACE_UNLOCK() pthread_mutex_unlock(&trace_lock)
#endif
static volatile int trace_active;

bool hl_trace_enabled(void) {
#ifdef HL_WIN
	return InterlockedCompareExchange((volatile LONG *)&trace_active,0,0) != 0;
#else
	return __atomic_load_n(&trace_active,__ATOMIC_ACQUIRE) != 0;
#endif
}

static void trace_set_active(int value) {
#ifdef HL_WIN
	InterlockedExchange((volatile LONG *)&trace_active,value);
#else
	__atomic_store_n(&trace_active,value,__ATOMIC_RELEASE);
#endif
}

static uint64_t trace_time(void) {
#ifdef HL_WIN
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (uint64_t)(now.QuadPart / trace_frequency.QuadPart) * 1000000 +
		(uint64_t)(now.QuadPart % trace_frequency.QuadPart) * 1000000 / trace_frequency.QuadPart;
#else
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC,&now);
	return (uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
#endif
}

static void trace_flush(void) {
	if( trace_pos ) fwrite(trace_buffer,1,trace_pos,trace_file);
	trace_pos = 0;
}

static void trace_write(const char *text) {
	size_t length = strlen(text);
	while( length ) {
		size_t count = sizeof(trace_buffer) - trace_pos;
		if( count > length ) count = length;
		memcpy(trace_buffer + trace_pos,text,count);
		trace_pos += count;
		text += count;
		length -= count;
		if( trace_pos == sizeof(trace_buffer) ) trace_flush();
	}
}

static void trace_string(const char *value) {
	static const char hex[] = "0123456789abcdef";
	trace_write("\"");
	for( const unsigned char *p = (const unsigned char *)value; *p; p++ ) {
		char escape[7];
		if( *p == '"' || *p == '\\' ) {
			escape[0] = '\\'; escape[1] = *p; escape[2] = 0;
			trace_write(escape);
		} else if( *p < 0x20 ) {
			escape[0] = '\\'; escape[1] = 'u'; escape[2] = '0'; escape[3] = '0';
			escape[4] = hex[*p >> 4]; escape[5] = hex[*p & 15]; escape[6] = 0;
			trace_write(escape);
		} else {
			escape[0] = *p; escape[1] = 0;
			trace_write(escape);
		}
	}
	trace_write("\"");
}

static void trace_event(const char *category, const char *name, char phase, int tid, const char *arg_name, const char *arg_value, const int64 *number) {
	char fields[128];
	uint64_t timestamp = trace_time();
	if( !hl_trace_enabled() ) return;
	TRACE_LOCK();
	if( !hl_trace_enabled() ) { TRACE_UNLOCK(); return; }
	trace_write(trace_first ? "" : ",\n");
	trace_first = false;
	trace_write("{\"name\":");
	trace_string(name);
	trace_write(",\"cat\":");
	trace_string(category);
	snprintf(fields,sizeof(fields),",\"ph\":\"%c\",\"ts\":%llu,\"pid\":1,\"tid\":%d",phase,(unsigned long long)timestamp,tid);
	trace_write(fields);
	if( arg_name ) {
		trace_write(",\"args\":{");
		trace_string(arg_name);
		trace_write(":");
		if( number ) {
			snprintf(fields,sizeof(fields),"%lld",(long long)*number);
			trace_write(fields);
		} else trace_string(arg_value);
		trace_write("}");
	}
	if( phase == 'I' ) trace_write(",\"s\":\"t\"");
	trace_write("}");
	TRACE_UNLOCK();
}

bool hl_trace_start(const char *filename) {
#ifdef HL_WIN
	int wide_length = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,filename,-1,nullptr,0);
	wchar_t *wide = wide_length ? malloc((size_t)wide_length * sizeof(wchar_t)) : nullptr;
	if( wide ) {
		MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,filename,-1,wide,wide_length);
		trace_file = _wfopen(wide,L"wb");
		free(wide);
	}
#else
	trace_file = fopen(filename,"wb");
#endif
	if( !trace_file ) {
		fprintf(stderr,"Could not open trace file %s\n",filename);
		return false;
	}
#ifdef HL_WIN
	InitializeCriticalSection(&trace_lock);
	QueryPerformanceFrequency(&trace_frequency);
#endif
	trace_pos = 0;
	trace_first = true;
	trace_write("{\"traceEvents\":[\n");
	trace_set_active(1);
	trace_event("metadata","process_name",'M',0,"name","HashLink",nullptr);
	trace_event("metadata","thread_name",'M',-1,"name","GPU (CPU submission anchored)",nullptr);
	return true;
}

void hl_trace_end(void) {
	if( !hl_trace_enabled() ) return;
	TRACE_LOCK();
	trace_set_active(0);
	trace_write("\n]}\n");
	trace_flush();
	fclose(trace_file);
	trace_file = nullptr;
	TRACE_UNLOCK();
}

void hl_trace_begin(const char *category, const char *name) {
	if( !hl_trace_enabled() ) return;
	trace_event(category,name,'B',hl_thread_id(),nullptr,nullptr,nullptr);
}

void hl_trace_end_event(const char *category) {
	if( !hl_trace_enabled() ) return;
	trace_event(category,"",'E',hl_thread_id(),nullptr,nullptr,nullptr);
}

void hl_trace_instant(const char *category, const char *name) {
	if( !hl_trace_enabled() ) return;
	trace_event(category,name,'I',hl_thread_id(),nullptr,nullptr,nullptr);
}

void hl_trace_counter(const char *category, const char *name, int64 value) {
	if( !hl_trace_enabled() ) return;
	trace_event(category,name,'C',hl_thread_id(),"value",nullptr,&value);
}

void hl_trace_counter_float(const char *category, const char *name, double value) {
	char fields[160];
	if( !hl_trace_enabled() ) return;
	TRACE_LOCK();
	if( !hl_trace_enabled() ) { TRACE_UNLOCK(); return; }
	trace_write(trace_first ? "" : ",\n");
	trace_first = false;
	trace_write("{\"name\":");
	trace_string(name);
	trace_write(",\"cat\":");
	trace_string(category);
	snprintf(fields,sizeof(fields),",\"ph\":\"C\",\"ts\":%llu,\"pid\":1,\"tid\":%d,\"args\":{\"value\":%.3f}}",(unsigned long long)trace_time(),hl_thread_id(),value);
	trace_write(fields);
	TRACE_UNLOCK();
}

double hl_trace_now(void) {
	return (double)trace_time();
}

void hl_trace_duration(const char *category, const char *name, double timestamp_us, double duration_us) {
	char fields[160];
	if( !hl_trace_enabled() ) return;
	TRACE_LOCK();
	if( !hl_trace_enabled() ) { TRACE_UNLOCK(); return; }
	trace_write(trace_first ? "" : ",\n");
	trace_first = false;
	trace_write("{\"name\":");
	trace_string(name);
	trace_write(",\"cat\":");
	trace_string(category);
	snprintf(fields,sizeof(fields),",\"ph\":\"X\",\"ts\":%.0f,\"dur\":%.3f,\"pid\":1,\"tid\":-1}",timestamp_us,duration_us);
	trace_write(fields);
	TRACE_UNLOCK();
}

void hl_trace_set_thread_name_id(int tid, const char *name) {
	if( !hl_trace_enabled() ) return;
	trace_event("metadata","thread_name",'M',tid,"name",name,nullptr);
}

void hl_trace_set_thread_name(const char *name) {
	hl_trace_set_thread_name_id(hl_thread_id(),name);
}

void hl_trace_sample(int tid, const char *stack) {
	if( !hl_trace_enabled() ) return;
	trace_event("sample","Stack Sample",'I',tid,"stack",stack,nullptr);
}

void hl_trace_jit_begin(int findex, const uchar *name) {
	char function[512];
	if( !hl_trace_enabled() ) return;
	int pos = 0;
	if( name ) {
		for( const uchar *p = name; *p && pos < (int)sizeof(function) - 32; p++ ) {
			unsigned int c = *p;
			if( c >= 0xD800 && c <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF )
				c = 0x10000 + ((c - 0xD800) << 10) + (*++p - 0xDC00);
			if( c < 0x80 ) function[pos++] = (char)c;
			else if( c < 0x800 ) {
				function[pos++] = (char)(0xC0 | (c >> 6));
				function[pos++] = (char)(0x80 | (c & 63));
			} else if( c < 0x10000 ) {
				function[pos++] = (char)(0xE0 | (c >> 12));
				function[pos++] = (char)(0x80 | ((c >> 6) & 63));
				function[pos++] = (char)(0x80 | (c & 63));
			} else {
				function[pos++] = (char)(0xF0 | (c >> 18));
				function[pos++] = (char)(0x80 | ((c >> 12) & 63));
				function[pos++] = (char)(0x80 | ((c >> 6) & 63));
				function[pos++] = (char)(0x80 | (c & 63));
			}
		}
	}
	snprintf(function + pos,sizeof(function) - pos,"%sfun$%d",pos ? " " : "",findex);
	trace_event("jit","JIT Compile",'B',hl_thread_id(),"function",function,nullptr);
}
