#ifndef HL_UPDATE_HTTP_H
#define HL_UPDATE_HTTP_H

#include <stdint.h>

int update_download(const char* url, const char* destination, uint64_t limit);

#endif
