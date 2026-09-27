#ifndef HL_TRACE_H
#define HL_TRACE_H

#include <hl.h>

HL_API bool hl_trace_enabled(void);
HL_API bool hl_trace_start(const char *filename);
HL_API void hl_trace_end(void);
HL_API void hl_trace_begin(const char *category, const char *name);
HL_API void hl_trace_end_event(const char *category);
HL_API void hl_trace_instant(const char *category, const char *name);
HL_API void hl_trace_counter(const char *category, const char *name, int64 value);
HL_API void hl_trace_counter_float(const char *category, const char *name, double value);
HL_API double hl_trace_now(void);
HL_API void hl_trace_duration(const char *category, const char *name, double timestamp_us, double duration_us);
HL_API void hl_trace_set_thread_name(const char *name);
HL_API void hl_trace_set_thread_name_id(int tid, const char *name);
HL_API void hl_trace_sample(int tid, const char *stack);
HL_API void hl_trace_jit_begin(int findex, const uchar *name);

#endif
