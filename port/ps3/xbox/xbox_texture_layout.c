/* The layout of an Xbox texture in memory and its texels for the RSX, from xbox_textures.c without its
OpenGL cache: d3d8_resources.c sizes and locks resources with it, d3d8_rsx.c uploads them. */

#include <string.h>

/* ---------- formats */

enum texel_kind
{
	_texel_unknown,
	_texel_a8r8g8b8, _texel_x8r8g8b8, _texel_r5g6b5, _texel_a1r5g5b5, _texel_x1r5g5b5, _texel_a4r4g4b4,
	_texel_l8, _texel_al8, _texel_a8, _texel_a8l8, _texel_p8, _texel_g8b8, _texel_r8b8, _texel_r6g5b5,
	_texel_l16, _texel_v16u16, _texel_a8b8g8r8, _texel_b8g8r8a8, _texel_r8g8b8a8, _texel_r5g5b5a1,
	_texel_r4g4b4a4, _texel_yuy2, _texel_uyvy, _texel_d24s8, _texel_d16,
	_texel_dxt1, _texel_dxt3, _texel_dxt5,
};

struct format_information
{
	unsigned char kind;
	unsigned char bytes; /* per texel; per 4x4 block for DXT */
	unsigned char linear;
};

static struct format_information format_information(DWORD format)
{
	static const struct format_information table[0x42] =
	{
		[0x00] = { _texel_l8, 1, 0 },
		[0x01] = { _texel_al8, 1, 0 },
		[0x02] = { _texel_a1r5g5b5, 2, 0 },
		[0x03] = { _texel_x1r5g5b5, 2, 0 },
		[0x04] = { _texel_a4r4g4b4, 2, 0 },
		[0x05] = { _texel_r5g6b5, 2, 0 },
		[0x06] = { _texel_a8r8g8b8, 4, 0 },
		[0x07] = { _texel_x8r8g8b8, 4, 0 },
		[0x0b] = { _texel_p8, 1, 0 },
		[0x0c] = { _texel_dxt1, 8, 0 },
		[0x0e] = { _texel_dxt3, 16, 0 },
		[0x0f] = { _texel_dxt5, 16, 0 },
		[0x10] = { _texel_a1r5g5b5, 2, 1 },
		[0x11] = { _texel_r5g6b5, 2, 1 },
		[0x12] = { _texel_a8r8g8b8, 4, 1 },
		[0x13] = { _texel_l8, 1, 1 },
		[0x16] = { _texel_r8b8, 2, 1 },
		[0x17] = { _texel_g8b8, 2, 1 },
		[0x19] = { _texel_a8, 1, 0 },
		[0x1a] = { _texel_a8l8, 2, 0 },
		[0x1b] = { _texel_al8, 1, 1 },
		[0x1c] = { _texel_x1r5g5b5, 2, 1 },
		[0x1d] = { _texel_a4r4g4b4, 2, 1 },
		[0x1e] = { _texel_x8r8g8b8, 4, 1 },
		[0x1f] = { _texel_a8, 1, 1 },
		[0x20] = { _texel_a8l8, 2, 1 },
		[0x24] = { _texel_yuy2, 2, 1 },
		[0x25] = { _texel_uyvy, 2, 1 },
		[0x27] = { _texel_r6g5b5, 2, 0 },
		[0x28] = { _texel_g8b8, 2, 0 },
		[0x29] = { _texel_r8b8, 2, 0 },
		[0x2a] = { _texel_d24s8, 4, 0 },
		[0x2b] = { _texel_d24s8, 4, 0 },
		[0x2c] = { _texel_d16, 2, 0 },
		[0x2d] = { _texel_d16, 2, 0 },
		[0x2e] = { _texel_d24s8, 4, 1 },
		[0x2f] = { _texel_d24s8, 4, 1 },
		[0x30] = { _texel_d16, 2, 1 },
		[0x31] = { _texel_d16, 2, 1 },
		[0x32] = { _texel_l16, 2, 0 },
		[0x33] = { _texel_v16u16, 4, 0 },
		[0x35] = { _texel_l16, 2, 1 },
		[0x36] = { _texel_v16u16, 4, 1 },
		[0x37] = { _texel_r6g5b5, 2, 1 },
		[0x38] = { _texel_r5g5b5a1, 2, 0 },
		[0x39] = { _texel_r4g4b4a4, 2, 0 },
		[0x3a] = { _texel_a8b8g8r8, 4, 0 },
		[0x3b] = { _texel_b8g8r8a8, 4, 0 },
		[0x3c] = { _texel_r8g8b8a8, 4, 0 },
		[0x3d] = { _texel_r5g5b5a1, 2, 1 },
		[0x3e] = { _texel_r4g4b4a4, 2, 1 },
		[0x3f] = { _texel_a8b8g8r8, 4, 1 },
		[0x40] = { _texel_b8g8r8a8, 4, 1 },
		[0x41] = { _texel_r8g8b8a8, 4, 1 },
	};
	struct format_information unknown = { _texel_a8r8g8b8, 4, 0 };

	if (format < sizeof(table) / sizeof(table[0]) && table[format].kind != _texel_unknown)
		return table[format];
	return unknown;
}

static BOOL kind_compressed(unsigned char kind)
{
	return kind == _texel_dxt1 || kind == _texel_dxt3 || kind == _texel_dxt5;
}

/* ---------- geometry of a texture in memory */

static unsigned long level_dimension(unsigned long base, unsigned long level)
{
	unsigned long value = base >> level;

	return value ? value : 1;
}

void xgpu_texture_describe(DWORD format_word, DWORD size_word, struct xgpu_texture_description *description)
{
	struct format_information information;

	memset(description, 0, sizeof(*description));
	description->format = (format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT;
	information = format_information(description->format);
	description->cube_map = (format_word & D3DFORMAT_CUBEMAP) != 0;
	description->compressed = kind_compressed(information.kind);
	if (size_word)
	{
		description->width = (size_word & D3DSIZE_WIDTH_MASK) + 1;
		description->height = ((size_word & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1;
		description->depth = 1;
		description->levels = 1;
		description->pitch = (((size_word & D3DSIZE_PITCH_MASK) >> D3DSIZE_PITCH_SHIFT) + 1) * D3DTEXTURE_PITCH_ALIGNMENT;
		description->linear = TRUE;
	}
	else
	{
		description->width = 1UL << ((format_word & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT);
		description->height = 1UL << ((format_word & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT);
		description->depth = 1UL << ((format_word & D3DFORMAT_PSIZE_MASK) >> D3DFORMAT_PSIZE_SHIFT);
		description->levels = (format_word & D3DFORMAT_MIPMAP_MASK) >> D3DFORMAT_MIPMAP_SHIFT;
		if (!description->levels)
			description->levels = 1;
		description->linear = information.linear;
		description->pitch = description->width * information.bytes;
	}
	if ((format_word & D3DFORMAT_DIMENSION_MASK) >> D3DFORMAT_DIMENSION_SHIFT != 3)
		description->depth = 1;
}

static unsigned long level_bytes(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);

	if (description->compressed)
		return ((width + 3) / 4) * ((height + 3) / 4) * information.bytes * depth;
	if (description->linear)
		return description->pitch * height;
	return width * height * depth * information.bytes;
}

unsigned long xgpu_texture_level_offset(const struct xgpu_texture_description *description, unsigned long level)
{
	unsigned long offset = 0;
	unsigned long index;

	for (index = 0; index < level && index < description->levels; index++)
		offset += level_bytes(description, index);
	return offset;
}

unsigned long xgpu_texture_face_size(const struct xgpu_texture_description *description)
{
	unsigned long size = xgpu_texture_level_offset(description, description->levels);

	if (description->cube_map)
		size = (size + D3DTEXTURE_CUBEFACE_ALIGNMENT - 1) & ~(unsigned long)(D3DTEXTURE_CUBEFACE_ALIGNMENT - 1);
	return size;
}

unsigned long xgpu_texture_level_pitch(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);

	if (description->linear)
		return description->pitch;
	if (description->compressed)
		return ((level_dimension(description->width, level) + 3) / 4) * information.bytes;
	return level_dimension(description->width, level) * information.bytes;
}

/* ---------- texels for the RSX: A8R8G8B8 words in the source's order, DXT blocks as they are */

static DWORD expand5(DWORD v) { return (v << 3) | (v >> 2); }
static DWORD expand6(DWORD v) { return (v << 2) | (v >> 4); }
static DWORD expand4(DWORD v) { return v * 0x11; }

static DWORD argb(DWORD a, DWORD r, DWORD g, DWORD b)
{
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static DWORD clamp_byte(long value)
{
	return (DWORD)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static DWORD yuv_to_argb(long y, long u, long v)
{
	long c = y - 16, d = u - 128, e = v - 128;

	return argb(255, clamp_byte((298 * c + 409 * e + 128) >> 8), clamp_byte((298 * c - 100 * d - 208 * e + 128) >> 8),
		clamp_byte((298 * c + 516 * d + 128) >> 8));
}

/* texels keep the Xbox's little-endian order (trap 27): read byte by byte, as xbox_textures.c */
static DWORD convert_texel(unsigned char kind, const unsigned char *source, const DWORD *palette,
	unsigned long x, const unsigned char *row)
{
	DWORD v16 = source[0] | ((DWORD)source[1] << 8);
	DWORD v32 = v16 | ((DWORD)source[2] << 16) | ((DWORD)source[3] << 24);

	switch (kind)
	{
	case _texel_a8r8g8b8: return v32;
	case _texel_x8r8g8b8: return v32 | 0xff000000UL;
	case _texel_r5g6b5: return argb(255, expand5(v16 >> 11), expand6((v16 >> 5) & 0x3f), expand5(v16 & 0x1f));
	case _texel_a1r5g5b5: return argb((v16 & 0x8000) ? 255 : 0, expand5((v16 >> 10) & 0x1f), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case _texel_x1r5g5b5: return argb(255, expand5((v16 >> 10) & 0x1f), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case _texel_a4r4g4b4: return argb(expand4(v16 >> 12), expand4((v16 >> 8) & 0xf), expand4((v16 >> 4) & 0xf), expand4(v16 & 0xf));
	case _texel_l8: return argb(255, source[0], source[0], source[0]);
	case _texel_al8: return argb(source[0], source[0], source[0], source[0]);
	case _texel_a8: return argb(source[0], 255, 255, 255);
	case _texel_a8l8: return argb(source[1], source[0], source[0], source[0]);
	case _texel_p8: return palette ? palette[source[0]] : argb(255, source[0], source[0], source[0]);
	/* V8U8 shares this format: U (the low byte) reads as red, V as green */
	case _texel_g8b8: return argb(255, source[0], source[1], 0);
	case _texel_r8b8: return argb(255, source[1], 0, source[0]);
	case _texel_r6g5b5: return argb(255, expand6(v16 >> 10), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case _texel_l16: return argb(255, source[1], source[1], source[1]);
	case _texel_v16u16: return argb(255, source[1], source[3], 0);
	case _texel_a8b8g8r8: return argb(source[3], source[0], source[1], source[2]);
	case _texel_b8g8r8a8: return argb(source[0], source[1], source[2], source[3]);
	case _texel_r8g8b8a8: return argb(source[0], source[3], source[2], source[1]);
	case _texel_r5g5b5a1: return argb((v16 & 1) ? 255 : 0, expand5(v16 >> 11), expand5((v16 >> 6) & 0x1f), expand5((v16 >> 1) & 0x1f));
	case _texel_r4g4b4a4: return argb(expand4(v16 & 0xf), expand4(v16 >> 12), expand4((v16 >> 8) & 0xf), expand4((v16 >> 4) & 0xf));
	case _texel_yuy2:
	{
		const unsigned char *pair = row + (x & ~1UL) * 2;

		return yuv_to_argb(pair[(x & 1) ? 2 : 0], pair[1], pair[3]);
	}
	case _texel_uyvy:
	{
		const unsigned char *pair = row + (x & ~1UL) * 2;

		return yuv_to_argb(pair[(x & 1) ? 3 : 1], pair[0], pair[2]);
	}
	case _texel_d24s8: return argb(255, source[3], source[3], source[3]);
	case _texel_d16: return argb(255, source[1], source[1], source[1]);
	default: return v32;
	}
}

/* 8- and 16-bit texels at their size where the RSX samples their format, with the remap that gives convert_texel's
channels: half or a quarter of A8R8G8B8's memory and texture bandwidth. 0 converts every one to A8R8G8B8. */
#define RSX_NATIVE_TEXELS 1

struct rsx_native
{
	/* GCM's format, 0 for A8R8G8B8 words from convert_texel */
	unsigned char format;
	unsigned char bytes;
	/* GCM_TEXTURE_REMAP_MODE's word; a single 8-bit texel is the B input, a G8B8's bytes are G then B */
	unsigned short remap;
	/* 16-bit texels: the Xbox's little-endian words to the RSX's big-endian ones */
	unsigned char swap;
};

static struct rsx_native rsx_native(unsigned char kind)
{
	struct rsx_native native = { 0, 4, 0xaae4, 0 };

#if RSX_NATIVE_TEXELS
	switch (kind)
	{
	case _texel_r5g6b5: native.format = 4; native.bytes = 2; native.swap = 1; break;
	case _texel_a1r5g5b5: native.format = 2; native.bytes = 2; native.swap = 1; break;
	/* (A1R5G5B5 with alpha one) */
	case _texel_x1r5g5b5: native.format = 2; native.bytes = 2; native.swap = 1; native.remap = 0xa9e4; break;
	case _texel_a4r4g4b4: native.format = 3; native.bytes = 2; native.swap = 1; break;
	/* B8: l8 is (1, L, L, L), al8 (L, L, L, L), a8 (A, 1, 1, 1) */
	case _texel_l8: native.format = 1; native.bytes = 1; native.remap = 0xa9ff; break;
	case _texel_al8: native.format = 1; native.bytes = 1; native.remap = 0xaaff; break;
	case _texel_a8: native.format = 1; native.bytes = 1; native.remap = 0x5603; break;
	/* G8B8 from the bytes as they lie: L (G), then A (B) */
	case _texel_a8l8: native.format = 11; native.bytes = 2; native.remap = 0xaaab; break;
	default: break;
	}
#endif
	return native;
}

/* the RSX's format for a texture (GCM's, without LIN and UNRM) and its remap */
unsigned char xgpu_texture_rsx_format(const struct xgpu_texture_description *description, unsigned long *remap)
{
	struct format_information information = format_information(description->format);
	struct rsx_native native = rsx_native(information.kind);

	*remap = native.remap;
	if (description->compressed)
		return information.kind == _texel_dxt1 ? 6 : information.kind == _texel_dxt3 ? 7 : 8;
	return native.format ? native.format : 5;
}

/* linear levels: rows 64-byte aligned, as the RSX's linear surfaces */
unsigned long xgpu_texture_rsx_pitch(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);
	unsigned long width = level_dimension(description->width, level);

	if (description->compressed)
		return ((width + 3) / 4) * information.bytes;
	return (width * rsx_native(information.kind).bytes + 63) & ~63UL;
}

unsigned long xgpu_texture_rsx_level_bytes(const struct xgpu_texture_description *description, unsigned long level)
{
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);

	if (description->compressed)
		return level_bytes(description, level);
	if (description->linear)
		return xgpu_texture_rsx_pitch(description, level) * height;
	return width * height * depth * rsx_native(format_information(description->format).kind).bytes;
}

/* a native level's texels as they are, 16-bit ones swapped to the RSX's order */
static void rsx_native_copy(const struct rsx_native *native, const unsigned char *source, unsigned char *destination,
	unsigned long count)
{
	unsigned long index;

	if (!native->swap)
	{
		memcpy(destination, source, count * native->bytes);
		return;
	}
	for (index = 0; index < count; index++)
	{
		destination[index * 2] = source[index * 2 + 1];
		destination[index * 2 + 1] = source[index * 2];
	}
}

/* destination is RSX memory, written once in order; the RSX's swizzle is the NV2A's, so swizzled
texels keep their places */
void xgpu_texture_rsx_convert_level(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, const DWORD *palette, void *destination)
{
	struct format_information information = format_information(description->format);
	struct rsx_native native = rsx_native(information.kind);
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);
	DWORD *texels = destination;
	unsigned long x, y, index, count;

	if (description->compressed)
	{
		memcpy(destination, source, level_bytes(description, level));
		return;
	}
	if (native.format)
	{
		unsigned char *bytes = destination;

		if (!description->linear)
		{
			rsx_native_copy(&native, source, bytes, width * height * depth);
			return;
		}
		/* each row into the RSX's pitch, the rest of it zero */
		for (y = 0; y < height; y++)
		{
			unsigned long pitch = xgpu_texture_rsx_pitch(description, level);

			rsx_native_copy(&native, source + y * description->pitch, bytes + y * pitch, width);
			memset(bytes + y * pitch + width * native.bytes, 0, pitch - width * native.bytes);
		}
		return;
	}
	if (description->linear)
	{
		unsigned long pitch = xgpu_texture_rsx_pitch(description, level) / 4;

		for (y = 0; y < height; y++)
		{
			const unsigned char *row = source + y * description->pitch;

			for (x = 0; x < pitch; x++)
				texels[y * pitch + x] = x < width ? convert_texel(information.kind, row + x * information.bytes, palette, x, row) : 0;
		}
		return;
	}
	count = width * height * depth;
	for (index = 0; index < count; index++)
	{
		const unsigned char *texel = source + index * information.bytes;

		texels[index] = convert_texel(information.kind, texel, palette, 0, texel);
	}
}
