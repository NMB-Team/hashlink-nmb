#ifndef HL_UPDATE_SHA256_H
#define HL_UPDATE_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint32_t state[8];
	uint64_t length;
	unsigned char block[64];
	size_t used;
} update_sha256;

void update_sha256_init(update_sha256* hash);
void update_sha256_add(update_sha256* hash, const void* data, size_t length);
void update_sha256_finish(update_sha256* hash, unsigned char digest[32]);
int update_sha256_file(const char* path, unsigned char digest[32], uint64_t* size);
void update_sha256_hex(const unsigned char digest[32], char hex[65]);

#endif
