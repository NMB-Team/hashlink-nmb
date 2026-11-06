#define HL_NAME(n) fmt_##n
#include <hl.h>
#include <limits.h>

enum { QOI_HEADER_SIZE = 14, QOI_END_SIZE = 8 };

static const vbyte qoi_end[QOI_END_SIZE] = {0, 0, 0, 0, 0, 0, 0, 1};

typedef union {
	uint32_t value;
	vbyte rgba[4];
} qoi_pixel;

static int qoi_hash( qoi_pixel pixel ) {
	return (pixel.rgba[0] * 3 + pixel.rgba[1] * 5 + pixel.rgba[2] * 7 + pixel.rgba[3] * 11) & 63;
}

static uint32_t qoi_read_u32( const vbyte *data ) {
	return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

static void qoi_write_u32( vbyte *data, uint32_t value ) {
	data[0] = (vbyte)(value >> 24);
	data[1] = (vbyte)(value >> 16);
	data[2] = (vbyte)(value >> 8);
	data[3] = (vbyte)value;
}

HL_PRIM vbyte *HL_NAME(qoi_decode)( const vbyte *data, int length, int *width, int *height ) {
	qoi_pixel index[64] = {{0}};
	qoi_pixel pixel;
	uint32_t w, h;
	const vbyte *src, *end;
	vbyte *output, *dst;
	int remaining;

	if( !data || length < QOI_HEADER_SIZE + QOI_END_SIZE || memcmp(data, "qoif", 4) != 0 )
		return NULL;
	w = qoi_read_u32(data + 4);
	h = qoi_read_u32(data + 8);
	if( !w || !h || w > (uint32_t)(INT_MAX / 4) / h || (data[12] != 3 && data[12] != 4) || data[13] > 1 )
		return NULL;
	remaining = (int)(w * h);
	src = data + QOI_HEADER_SIZE;
	end = data + length - QOI_END_SIZE;
	if( memcmp(end, qoi_end, QOI_END_SIZE) != 0 || (remaining + 61) / 62 > end - src )
		return NULL;

	dst = output = hl_alloc_bytes(remaining * 4);
	pixel.value = 0;
	pixel.rgba[3] = 255;
	while( remaining > 0 ) {
		int tag, run = 1;
		if( src == end )
			return NULL;
		tag = *src++;
		if( tag == 0xFE || tag == 0xFF ) {
			int channels = tag == 0xFE ? 3 : 4;
			if( end - src < channels )
				return NULL;
			memcpy(pixel.rgba, src, channels);
			src += channels;
		} else {
			switch( tag & 0xC0 ) {
			case 0x00:
				pixel = index[tag & 63];
				break;
			case 0x40:
				pixel.rgba[0] = (vbyte)(pixel.rgba[0] + ((tag >> 4) & 3) - 2);
				pixel.rgba[1] = (vbyte)(pixel.rgba[1] + ((tag >> 2) & 3) - 2);
				pixel.rgba[2] = (vbyte)(pixel.rgba[2] + (tag & 3) - 2);
				break;
			case 0x80: {
				int diff, dg = (tag & 63) - 32;
				if( src == end )
					return NULL;
				diff = *src++;
				pixel.rgba[0] = (vbyte)(pixel.rgba[0] + dg + (diff >> 4) - 8);
				pixel.rgba[1] = (vbyte)(pixel.rgba[1] + dg);
				pixel.rgba[2] = (vbyte)(pixel.rgba[2] + dg + (diff & 15) - 8);
				break;
			}
			case 0xC0:
				run = (tag & 63) + 1;
				break;
			}
		}
		if( run > remaining )
			return NULL;
		index[qoi_hash(pixel)] = pixel;
		remaining -= run;
		while( run-- > 0 ) {
			memcpy(dst, pixel.rgba, 4);
			dst += 4;
		}
	}
	if( src != end )
		return NULL;
	*width = (int)w;
	*height = (int)h;
	return output;
}

HL_PRIM vbyte *HL_NAME(qoi_encode)( const vbyte *data, int length, int offset, int width, int height, int colorspace, int *size ) {
	qoi_pixel index[64] = {{0}};
	qoi_pixel previous;
	const vbyte *src, *end;
	vbyte *buffer, *dst, *output;
	int count, run = 0;

	if( !data || offset < 0 || offset > length || width <= 0 || height <= 0 ||
		width > (INT_MAX - QOI_HEADER_SIZE - QOI_END_SIZE) / 5 / height || colorspace < 0 || colorspace > 1 )
		return NULL;
	count = width * height;
	if( count > (length - offset) / 4 )
		return NULL;
	buffer = malloc(QOI_HEADER_SIZE + count * 5 + QOI_END_SIZE);
	if( !buffer )
		return NULL;
	memcpy(buffer, "qoif", 4);
	qoi_write_u32(buffer + 4, width);
	qoi_write_u32(buffer + 8, height);
	buffer[12] = 4;
	buffer[13] = (vbyte)colorspace;

	dst = buffer + QOI_HEADER_SIZE;
	src = data + offset;
	end = src + count * 4;
	previous.value = 0;
	previous.rgba[3] = 255;
	while( src < end ) {
		qoi_pixel pixel;
		int hash;
		memcpy(pixel.rgba, src, 4);
		src += 4;
		if( pixel.value == previous.value ) {
			if( ++run == 62 || src == end ) {
				*dst++ = (vbyte)(0xC0 | (run - 1));
				run = 0;
			}
			continue;
		}
		if( run > 0 ) {
			*dst++ = (vbyte)(0xC0 | (run - 1));
			run = 0;
		}
		hash = qoi_hash(pixel);
		if( index[hash].value == pixel.value ) {
			*dst++ = (vbyte)hash;
		} else {
			index[hash] = pixel;
			if( pixel.rgba[3] != previous.rgba[3] ) {
				*dst++ = 0xFF;
				memcpy(dst, pixel.rgba, 4);
				dst += 4;
			} else {
				int dr = pixel.rgba[0] - previous.rgba[0];
				int dg = pixel.rgba[1] - previous.rgba[1];
				int db = pixel.rgba[2] - previous.rgba[2];
				int drg = dr - dg, dbg = db - dg;
				if( dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1 ) {
					*dst++ = (vbyte)(0x40 | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2));
				} else if( dg >= -32 && dg <= 31 && drg >= -8 && drg <= 7 && dbg >= -8 && dbg <= 7 ) {
					*dst++ = (vbyte)(0x80 | (dg + 32));
					*dst++ = (vbyte)(((drg + 8) << 4) | (dbg + 8));
				} else {
					*dst++ = 0xFE;
					memcpy(dst, pixel.rgba, 3);
					dst += 3;
				}
			}
		}
		previous = pixel;
	}
	memcpy(dst, qoi_end, QOI_END_SIZE);
	*size = (int)(dst - buffer) + QOI_END_SIZE;
	output = hl_copy_bytes(buffer, *size);
	free(buffer);
	return output;
}

DEFINE_PRIM(_BYTES, qoi_decode, _BYTES _I32 _REF(_I32) _REF(_I32));
DEFINE_PRIM(_BYTES, qoi_encode, _BYTES _I32 _I32 _I32 _I32 _I32 _REF(_I32));
