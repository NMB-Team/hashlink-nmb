#include "update_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "../hl.h"

static const uint32_t constants[64] = { 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	                                    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	                                    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	                                    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

static uint32_t rotate(uint32_t x, unsigned n) {
	return (x >> n) | (x << (32 - n));
}

static void transform(update_sha256* hash, const unsigned char block[64]) {
	uint32_t words[64], a, b, c, d, e, f, g, h;
	for (int i = 0; i < 16; i++)
		words[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
	for (int i = 16; i < 64; i++) {
		uint32_t x = words[i - 15], y = words[i - 2];
		words[i] = words[i - 16] + (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) + words[i - 7] + (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
	}
	a = hash->state[0];
	b = hash->state[1];
	c = hash->state[2];
	d = hash->state[3];
	e = hash->state[4];
	f = hash->state[5];
	g = hash->state[6];
	h = hash->state[7];
	for (int i = 0; i < 64; i++) {
		uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
		uint32_t choice = (e & f) ^ (~e & g);
		uint32_t t1 = h + s1 + choice + constants[i] + words[i];
		uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
		uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
		uint32_t t2 = s0 + majority;
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	hash->state[0] += a;
	hash->state[1] += b;
	hash->state[2] += c;
	hash->state[3] += d;
	hash->state[4] += e;
	hash->state[5] += f;
	hash->state[6] += g;
	hash->state[7] += h;
}

void update_sha256_init(update_sha256* hash) {
	static const uint32_t initial[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	memcpy(hash->state, initial, sizeof(initial));
	hash->length = 0;
	hash->used = 0;
}

void update_sha256_add(update_sha256* hash, const void* data, size_t length) {
	const unsigned char* bytes = data;
	hash->length += (uint64_t)length * 8;
	while (length) {
		size_t count = 64 - hash->used;
		if (count > length)
			count = length;
		memcpy(hash->block + hash->used, bytes, count);
		hash->used += count;
		bytes += count;
		length -= count;
		if (hash->used == 64) {
			transform(hash, hash->block);
			hash->used = 0;
		}
	}
}

void update_sha256_finish(update_sha256* hash, unsigned char digest[32]) {
	uint64_t length = hash->length;
	hash->block[hash->used++] = 0x80;
	if (hash->used > 56) {
		memset(hash->block + hash->used, 0, 64 - hash->used);
		transform(hash, hash->block);
		hash->used = 0;
	}
	memset(hash->block + hash->used, 0, 56 - hash->used);
	for (int i = 0; i < 8; i++)
		hash->block[56 + i] = (unsigned char)(length >> (56 - i * 8));
	transform(hash, hash->block);
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 4; j++)
			digest[i * 4 + j] = (unsigned char)(hash->state[i] >> (24 - j * 8));
}

int update_sha256_file(const char* path, unsigned char digest[32], uint64_t* size) {
#ifdef _WIN32
	int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
	if (!length)
		return 0;
	wchar_t* wide = malloc((size_t)length * sizeof(wchar_t));
	if (!wide)
		return 0;
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, length)) {
		free(wide);
		return 0;
	}
	FILE* file = _wfopen(wide, L"rb");
	free(wide);
#else
	FILE* file = fopen(path, "rb");
#endif
	unsigned char buffer[65536];
	update_sha256 hash;
	size_t count;
	if (file == nullptr)
		return 0;
	update_sha256_init(&hash);
	*size = 0;
	while ((count = fread(buffer, 1, sizeof(buffer), file)) != 0) {
		update_sha256_add(&hash, buffer, count);
		*size += count;
	}
	int ok = !ferror(file);
	if (fclose(file) != 0)
		ok = 0;
	if (ok)
		update_sha256_finish(&hash, digest);
	return ok;
}

void update_sha256_hex(const unsigned char digest[32], char hex[65]) {
	static const char digits[] = "0123456789abcdef";
	for (int i = 0; i < 32; i++) {
		hex[i * 2] = digits[digest[i] >> 4];
		hex[i * 2 + 1] = digits[digest[i] & 15];
	}
	hex[64] = 0;
}
