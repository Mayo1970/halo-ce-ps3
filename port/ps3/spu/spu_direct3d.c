/* SPU_DIRECT3D.C: the Direct3D thread's work (d3d8_rsx.c's records, ps3_gpu.c's RSX state, librsx's commands) on an SPU,
function for function: the commands go into chunks the PPE calls; what needs the PPE (memory, translations) is a request. */

#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_event.h>
#include <sys/spu_thread.h>

#include <stddef.h>
#include <string.h>

#include "spu_direct3d.h"
#include "spu_vertices.h"
#include "../src/ps3_rsx_program.h"

#define TAG_RECORDS 1
#define TAG_STAGING 2
/* and + 1: the conversions' input and output buffers */
#define TAG_INPUT 4
#define TAG_OUTPUT 6
#define TAG_TABLE 8
#define TAG_ECHO 9
#define TAG_MISC 10
#define OUTPUT_TAGS ((3 << TAG_STAGING) | (3 << TAG_OUTPUT) | (1 << TAG_TABLE) | (1 << TAG_MISC))
/* 1 ms of the timebase: a wake if a reservation event is lost */
#define DECREMENTER_TICKS 79800

typedef int BOOL;
#define TRUE 1
#define FALSE 0

/* ---------- GCM's values and nv40.h's methods (the PPU's headers cannot be included here) */

#define METHOD(method, count) (((uint32_t)(count) << 18) | (method))
#define METHOD_NI(method, count) (METHOD(method, count) | 0x40000000u)
#define METHOD_RETURN 0x00020000u
#define MAX_METHOD_COUNT 0x7ff

#define NV40TCL_DMA_COLOR0 0x00000194
#define NV40TCL_DMA_COLOR1 0x0000018c
#define NV40TCL_DMA_COLOR2 0x000001b4
#define NV40TCL_DMA_ZETA 0x00000198
#define NV40TCL_RT_FORMAT 0x00000208
#define NV40TCL_ZETA_PITCH 0x0000022c
#define NV40TCL_COLOR2_PITCH 0x00000280
#define NV40TCL_RT_ENABLE 0x00000220
#define NV40TCL_WINDOW_OFFSET 0x000002b8
#define NV40TCL_RT_HORIZ 0x00000200
#define NV40TCL_SHADER_WINDOW 0x00001d88
#define NV40TCL_COLOR_MASK 0x00000324
#define NV40TCL_VIEWPORT_HORIZ 0x00000a00
#define NV40TCL_DEPTH_RANGE 0x00000394
#define NV40TCL_VIEWPORT_OFFSET 0x00000a20
#define NV40TCL_VIEWPORT_CLIP_HORIZ(x) (0x000002c0 + (x) * 8)
#define NV40TCL_DEPTH_TEST_ENABLE 0x00000a74
#define NV40TCL_DEPTH_FUNC 0x00000a6c
#define NV40TCL_DEPTH_WRITE_ENABLE 0x00000a70
#define NV40TCL_DEPTH_BOUNDS_TEST_ENABLE 0x00000380
#define NV40TCL_DEPTH_BOUNDS_MIN 0x00000384
#define NV40TCL_CULL_FACE_ENABLE 0x0000183c
#define NV40TCL_CULL_FACE 0x00001830
#define NV40TCL_FRONT_FACE 0x00001834
#define NV40TCL_POLYGON_MODE_FRONT 0x00001828
#define NV40TCL_POLYGON_MODE_BACK 0x0000182c
#define NV40TCL_POLYGON_OFFSET_FILL_ENABLE 0x00000a68
#define NV40TCL_POLYGON_OFFSET_FACTOR 0x00000a78
#define NV40TCL_POLYGON_OFFSET_LINE_ENABLE 0x00000a64
#define NV40TCL_CLEAR_BUFFERS 0x00001d94
#define NV40TCL_NOP 0x00000100
#define NV40TCL_CLEAR_VALUE_COLOR 0x00001d90
#define NV40TCL_CLEAR_VALUE_DEPTH 0x00001d8c
#define NV40TCL_STENCIL_BACK_FUNC_FUNC 0x00000330
#define NV40TCL_STENCIL_BACK_MASK 0x0000032c
#define NV40TCL_STENCIL_BACK_OP_FAIL 0x0000033c
#define NV40TCL_STENCIL_BACK_ENABLE 0x00000328
#define NV40TCL_QUERY_GET 0x00001800
#define NV40TCL_CLEAR_ZCULL_SURFACE 0x00001438
#define NV40TCL_ZCULL_ENABLE 0x00001d84
#define NV40TCL_INVALIDATE_ZCULL 0x00000234
#define NV40TCL_VP_UPLOAD_FROM_ID 0x00001e9c
#define NV40TCL_VP_UPLOAD_INST(x) (0x00000b80 + (x) * 16)
#define NV40TCL_VP_ATTRIB_EN 0x00001ff0
#define NV40TCL_VP_RESULT_EN 0x00001ff4
#define NV40TCL_TRANSFORM_TIMEOUT 0x00001ef8
#define NV40TCL_FP_ADDRESS 0x000008e4
#define NV40TCL_TEX_COORD_CONTROL(x) (0x00000b40 + (x) * 4)
#define NV40TCL_FP_CONTROL 0x00001d60
#define NV40TCL_VP_UPLOAD_CONST_ID 0x00001efc
#define NV40TCL_VTX_CACHE_INVALIDATE 0x00001714
#define NV40TCL_VTX_CACHE_INVALIDATE2 0x00001710
#define NV40TCL_TEX_CACHE_CTL 0x00001fd8
#define NV40TCL_TEX_OFFSET(x) (0x00001a00 + (x) * 32)
#define NV40TCL_TEX_WRAP(x) (0x00001a08 + (x) * 32)
#define NV40TCL_TEX_ENABLE(x) (0x00001a0c + (x) * 32)
#define NV40TCL_TEX_SWIZZLE(x) (0x00001a10 + (x) * 32)
#define NV40TCL_TEX_FILTER(x) (0x00001a14 + (x) * 32)
#define NV40TCL_TEX_SIZE0(x) (0x00001a18 + (x) * 32)
#define NV40TCL_TEX_BORDER_COLOR(x) (0x00001a1c + (x) * 32)
#define NV40TCL_TEX_SIZE1(x) (0x00001840 + (x) * 4)
#define NV40TCL_VTXFMT(x) (0x00001740 + (x) * 4)
#define NV40TCL_VTXBUF_ADDRESS(x) (0x00001680 + (x) * 4)
#define NV40TCL_BEGIN_END 0x00001808
#define NV40TCL_VB_VERTEX_BATCH 0x00001814
#define NV40TCL_VB_INDEX_BATCH_OFFSET 0x0000181c
#define NV40TCL_VB_INDEX_BATCH_DRAW 0x00001824
#define NV40TCL_SCISSOR_HORIZ 0x000008c0
#define NV40TCL_ALPHA_TEST_FUNC 0x00000308
#define NV40TCL_ALPHA_TEST_ENABLE 0x00000304
#define NV40TCL_BLEND_FUNC_SRC 0x00000314
#define NV40TCL_BLEND_EQUATION 0x00000320
#define NV40TCL_BLEND_COLOR 0x0000031c
#define NV40TCL_BLEND_COLOR2 0x0000037c
#define NV40TCL_BLEND_ENABLE 0x00000310
#define NV40TCL_SEMAPHORE_OFFSET 0x00001d6c
#define NV40TCL_SEMAPHORE_BACKENDWRITE_RELEASE 0x00001d70

#define GCM_DMA_MEMORY_FRAME_BUFFER 0xFEED0000u
#define GCM_LOCATION_RSX 0
#define GCM_ATTRIB_OUTPUT_MASK_POINTSIZE (1 << 5)
#define GCM_INDEX_TYPE_16B 1
#define GCM_VERTEX_DATA_TYPE_F32 2
#define GCM_INVALIDATE_TEXTURE 1
#define GCM_CLEAR_Z 0x01
#define GCM_CLEAR_S 0x02
#define GCM_CLEAR_R 0x10
#define GCM_CLEAR_G 0x20
#define GCM_CLEAR_B 0x40
#define GCM_CLEAR_A 0x80
#define GCM_COLOR_MASK_B 0x00000001
#define GCM_COLOR_MASK_G 0x00000100
#define GCM_COLOR_MASK_R 0x00010000
#define GCM_COLOR_MASK_A 0x01000000
#define GCM_SURFACE_A8R8G8B8 8
#define GCM_SURFACE_TARGET_0 1
#define GCM_SURFACE_ZETA_Z24S8 2
#define GCM_SURFACE_TYPE_LINEAR 1
#define GCM_SURFACE_CENTER_1 0
#define GCM_TEXTURE_CONVOLUTION_QUINCUNX 1
#define GCM_LEQUAL 0x0203
#define GCM_NEVER 0x0200
#define GCM_GREATER 0x0204
#define GCM_ALWAYS 0x0207
#define GCM_ONE 1
#define GCM_SRC_ALPHA 0x0302
#define GCM_TYPE_QUADS 8
#define GCM_TYPE_TRIANGLE_FAN 7
#define GCM_POLYGON_MODE_FILL 0x1b02
#define GCM_CULL_FRONT 0x0404
#define GCM_CULL_BACK 0x0405
#define GCM_FRONTFACE_CW 0x0900
#define GCM_FRONTFACE_CCW 0x0901
#define GCM_FUNC_ADD 0x8006
#define GCM_FUNC_MIN 0x8007
#define GCM_FUNC_MAX 0x8008
#define GCM_FUNC_SUBTRACT 0x800a
#define GCM_FUNC_REVERSE_SUBTRACT 0x800b
#define GCM_TEXTURE_FORMAT_LIN 0x20
#define GCM_TEXTURE_FORMAT_UNRM 0x40
#define GCM_TEXTURE_FORMAT_A8R8G8B8 5
#define GCM_TEXTURE_DIMS_2D 2
#define GCM_TEXTURE_DIMS_3D 3
#define GCM_TEXTURE_NEAREST 1
#define GCM_TEXTURE_LINEAR 2
#define GCM_TEXTURE_NEAREST_MIPMAP_NEAREST 3
#define GCM_TEXTURE_LINEAR_MIPMAP_NEAREST 4
#define GCM_TEXTURE_NEAREST_MIPMAP_LINEAR 5
#define GCM_TEXTURE_LINEAR_MIPMAP_LINEAR 6
#define GCM_TEXTURE_REPEAT 1
#define GCM_TEXTURE_MIRRORED_REPEAT 2
#define GCM_TEXTURE_CLAMP_TO_EDGE 3
#define GCM_TEXTURE_BORDER 4
#define REMAP_IDENTITY 0xaae4

/* ---------- the XDK's values (xdk_pdb.h, xdk_d3d8.h; d3d8_rsx.c checks them) */

#define D3DRS_PSALPHAINPUTS0 0
#define D3DRS_PSFINALCOMBINERINPUTSABCD 8
#define D3DRS_PSFINALCOMBINERINPUTSEFG 9
#define D3DRS_PSCONSTANT0_0 10
#define D3DRS_PSCONSTANT1_0 18
#define D3DRS_PSALPHAOUTPUTS0 26
#define D3DRS_PSRGBINPUTS0 34
#define D3DRS_PSCOMPAREMODE 42
#define D3DRS_PSFINALCOMBINERCONSTANT0 43
#define D3DRS_PSFINALCOMBINERCONSTANT1 44
#define D3DRS_PSRGBOUTPUTS0 45
#define D3DRS_PSCOMBINERCOUNT 53
#define D3DRS_PSDOTMAPPING 55
#define D3DRS_PSINPUTTEXTURE 56
#define D3DRS_ZFUNC 57
#define D3DRS_ALPHAFUNC 58
#define D3DRS_ALPHABLENDENABLE 59
#define D3DRS_ALPHATESTENABLE 60
#define D3DRS_ALPHAREF 61
#define D3DRS_SRCBLEND 62
#define D3DRS_DESTBLEND 63
#define D3DRS_ZWRITEENABLE 64
#define D3DRS_COLORWRITEENABLE 67
#define D3DRS_STENCILZFAIL 68
#define D3DRS_STENCILPASS 69
#define D3DRS_STENCILFUNC 70
#define D3DRS_STENCILREF 71
#define D3DRS_STENCILMASK 72
#define D3DRS_STENCILWRITEMASK 73
#define D3DRS_BLENDOP 74
#define D3DRS_BLENDCOLOR 75
#define D3DRS_POLYGONOFFSETZSLOPESCALE 77
#define D3DRS_POLYGONOFFSETZOFFSET 78
#define D3DRS_SOLIDOFFSETENABLE 81
#define D3DRS_FOGENABLE 82
#define D3DRS_FOGTABLEMODE 83
#define D3DRS_FOGSTART 84
#define D3DRS_FOGEND 85
#define D3DRS_FOGDENSITY 86
#define D3DRS_PSTEXTUREMODES 116
#define D3DRS_FOGCOLOR 118
#define D3DRS_FILLMODE 119
#define D3DRS_ZENABLE 123
#define D3DRS_STENCILENABLE 124
#define D3DRS_STENCILFAIL 125
#define D3DRS_FRONTFACE 126
#define D3DRS_CULLMODE 127
#define D3DRS_MAX 144
#define D3DTSS_ADDRESSU 10
#define D3DTSS_ADDRESSV 11
#define D3DTSS_ADDRESSW 12
#define D3DTSS_MAGFILTER 13
#define D3DTSS_MINFILTER 14
#define D3DTSS_MIPFILTER 15
#define D3DTSS_MIPMAPLODBIAS 16
#define D3DTSS_MAXMIPLEVEL 17
#define D3DTSS_MAXANISOTROPY 18
#define D3DTSS_COLORSIGN 20
#define D3DTSS_ALPHAKILL 21
#define D3DTSS_BUMPENVMAT00 22
#define D3DTSS_BUMPENVMAT01 23
#define D3DTSS_BUMPENVMAT11 24
#define D3DTSS_BUMPENVMAT10 25
#define D3DTSS_BUMPENVLSCALE 26
#define D3DTSS_BUMPENVLOFFSET 27
#define D3DTSS_BORDERCOLOR 29
#define D3DTSS_MAX 32
#define D3DTSS_MAXSTAGES 4
#define D3DTADDRESS_MIRROR 2
#define D3DTADDRESS_CLAMP 3
#define D3DTADDRESS_BORDER 4
#define D3DTADDRESS_CLAMPTOEDGE 5
#define D3DTEXF_NONE 0
#define D3DTEXF_POINT 1
#define D3DTEXF_ANISOTROPIC 3
#define D3DTALPHAKILL_ENABLE 4
#define D3DFRONT_CCW 2305
#define D3DCULL_NONE 0
#define D3DBLENDOP_SUBTRACT 32778
#define D3DBLENDOP_REVSUBTRACT 32779
#define D3DBLENDOP_MIN 32775
#define D3DBLENDOP_MAX 32776
#define D3DBLENDOP_REVSUBTRACTSIGNED 61445
#define D3DBLEND_ZERO 0
#define D3DBLEND_ONE 1
#define D3DBLEND_SRCALPHA 770
#define D3DBLEND_INVSRCALPHA 771
#define D3DBLEND_DESTCOLOR 774
#define D3DSTENCILOP_KEEP 7680
#define D3DSCM_NORESERVEDCONSTANTS 0x10
#define D3DCLEAR_ZBUFFER 0x01
#define D3DCLEAR_STENCIL 0x02
#define D3DCLEAR_TARGET_R 0x10
#define D3DCLEAR_TARGET_G 0x20
#define D3DCLEAR_TARGET_B 0x40
#define D3DCLEAR_TARGET_A 0x80
#define D3DCOLORWRITEENABLE_BLUE (1u << 0)
#define D3DCOLORWRITEENABLE_GREEN (1u << 8)
#define D3DCOLORWRITEENABLE_RED (1u << 16)
#define D3DCOLORWRITEENABLE_ALPHA (1u << 24)
#define D3DPT_POLYGON 10
#define D3DVSDT_NONE 0x02
#define D3DFMT_D24S8 42
#define D3DFMT_F24S8 43
#define D3DFMT_D16 44
#define D3DFMT_F16 45
#define D3DFMT_LIN_D24S8 46
#define D3DFMT_LIN_F24S8 47
#define D3DFMT_LIN_D16 48
#define D3DFMT_LIN_F16 49
#define D3DFORMAT_CUBEMAP 0x00000004
#define D3DFORMAT_DIMENSION_MASK 0x000000F0
#define D3DFORMAT_DIMENSION_SHIFT 4
#define D3DFORMAT_FORMAT_MASK 0x0000FF00
#define D3DFORMAT_FORMAT_SHIFT 8
#define D3DFORMAT_MIPMAP_MASK 0x000F0000
#define D3DFORMAT_MIPMAP_SHIFT 16
#define D3DFORMAT_USIZE_MASK 0x00F00000
#define D3DFORMAT_USIZE_SHIFT 20
#define D3DFORMAT_VSIZE_MASK 0x0F000000
#define D3DFORMAT_VSIZE_SHIFT 24
#define D3DFORMAT_PSIZE_MASK 0xF0000000
#define D3DFORMAT_PSIZE_SHIFT 28
#define D3DSIZE_WIDTH_MASK 0x00000FFF
#define D3DSIZE_HEIGHT_MASK 0x00FFF000
#define D3DSIZE_HEIGHT_SHIFT 12
#define D3DSIZE_PITCH_MASK 0xFF000000
#define D3DSIZE_PITCH_SHIFT 24
#define D3DTEXTURE_CUBEFACE_ALIGNMENT 128
#define D3DTEXTURE_PITCH_ALIGNMENT 64

/* ---------- d3d8_rsx.c's and ps3_gpu.c's sizes */

#define SCREEN_HEIGHT 480
#define SECONDARY_TARGET_WIDTH 320
#define SECONDARY_TARGET_HEIGHT 240
#define VERTEX_PROGRAM_SLOTS 136
#define VERTEX_ATTRIBUTE_COUNT 16
#define VERTEX_CONSTANT_BIAS 96
#define TEXTURE_STAGES 4
#define TEXTURE_UNITS 4
#define ATTRIBUTE_COUNT 16
#define STATISTICS_FRAMES 600
#define TEXTURE_IDLE_FRAMES 1800
#define SCRATCH_HALF_SIZE (16 * 1024 * 1024)
#define PROGRAM_SLOTS 512
#define RESIDENT_PROGRAM_COUNT 32
#define TIMESTAMP_REPORT_BASE 256
#define TIMESTAMP_PARTS SPU_D3D_TIMESTAMP_PARTS
#define DRAW_STAMP_REPORT_BASE 512
#define DRAW_STAMPS SPU_D3D_DRAW_STAMPS
#define DRAW_STAMP_FRAMES 8
#define DRAW_STAMP_START 0xffff
#define REPORT_TIMESTAMP 1
#define FRAME_LABEL 64
#define CHANGED_SAMPLER(stage) (1u << (stage))
#define CHANGED_TEXTURE_MODES 0x10u
#define CHANGED_FRAGMENT_KEY 0x20u
#define CHANGED_TEXTURE(stage) (0x100u << (stage))
#define CHANGED_TEXTURES (0xfu | CHANGED_TEXTURE_MODES | 0xf00u)

#define _timer_thread_busy _spu_d3d_timer_thread_busy
#define _timer_record_wrap _spu_d3d_timer_record_wrap
#define _timer_draw_state _spu_d3d_timer_draw_state
#define _timer_draw_textures _spu_d3d_timer_draw_textures
#define _timer_texture_hash _spu_d3d_timer_texture_hash
#define _timer_texture_upload _spu_d3d_timer_texture_upload
#define _timer_draw_colours _spu_d3d_timer_draw_colours
#define _timer_draw_vertices _spu_d3d_timer_draw_vertices
#define _timer_draw_indices _spu_d3d_timer_draw_indices
#define _timer_draw_program _spu_d3d_timer_draw_program
#define _timer_draw_targets _spu_d3d_timer_draw_targets
#define _timer_draw_raster _spu_d3d_timer_draw_raster
#define _timer_draw_screen _spu_d3d_timer_draw_screen
#define _timer_draw_key _spu_d3d_timer_draw_key
#define _timer_draw_fragment _spu_d3d_timer_draw_fragment
#define _timer_draw_uniforms _spu_d3d_timer_draw_uniforms
#define _timer_draw_vertex_program _spu_d3d_timer_draw_vertex_program
#define _timer_draw_constants _spu_d3d_timer_draw_constants
#define SPECULAR_LIGHTS SPU_D3D_TIMER_SPECULAR_LIGHTS
#define SPECULAR_FOG SPU_D3D_TIMER_SPECULAR_FOG
#define TIMER_PRESENT SPU_D3D_TIMER_PRESENT
#define STAMP_SPECULAR_FOG SPU_D3D_STAMP_SPECULAR_FOG
#define STAMP_PART_SHIFT SPU_D3D_STAMP_PART_SHIFT

typedef char timers_check[_spu_d3d_timer_count <= SPU_D3D_TIMERS ? 1 : -1];

/* ---------- LS */

static spu_d3d_bell bell;
static spu_d3d_echo echo;
static spu_d3d_setup setup;
static spu_d3d_request request_block;
static spu_d3d_response response_block;
static spu_d3d_statistics statistics;
static uint64_t bell_ea, echo_ea;
static uint32_t request_number;

/* the decrementer's ticks (counting down) and what bell_wait's new starts took off it: a span's start, and the
timers since the start */
static uint32_t timer_shift;

static inline uint32_t timer_now(void)
{
	return spu_read_decrementer() + timer_shift;
}

static inline uint32_t timer_add(int timer, uint32_t start)
{
	uint32_t now = timer_now();

	statistics.timers[timer] += start - now;
	return now;
}

/* ---------- DMA */

static void dma_wait(uint32_t tags)
{
	mfc_write_tag_mask(tags);
	mfc_read_tag_status_all();
}

/* 16-aligned LS and EA, a 16-multiple size, in pieces of 16 KB */
static void dma_get(void *ls, uint32_t ea, uint32_t size, uint32_t tag)
{
	unsigned char *to = ls;

	while (size)
	{
		uint32_t n = size < 16384 ? size : 16384;

		mfc_get(to, ea, n, tag, 0, 0);
		to += n;
		ea += n;
		size -= n;
	}
}

static void dma_put(const void *ls, uint32_t ea, uint32_t size, uint32_t tag)
{
	const unsigned char *from = ls;

	while (size)
	{
		uint32_t n = size < 16384 ? size : 16384;

		mfc_put((void *)from, ea, n, tag, 0, 0);
		from += n;
		ea += n;
		size -= n;
	}
}

/* size bytes at ea into ls (room for size + 32), with the 16-byte lines that hold them; where ea's byte is */
static void *fetch(void *ls, uint32_t ea, uint32_t size, uint32_t tag)
{
	uint32_t first = ea & ~15u, end = (ea + size + 15) & ~15u;

	if (!size)
		return ls;
	dma_get(ls, first, end - first, tag);
	dma_wait(1 << tag);
	return (unsigned char *)ls + (ea & 15);
}

/* size bytes from ls to ea, whose low 4 bits are equal, without a byte outside them: the ends in 1, 2, 4 and 8 */
static void put_exact(const void *ls, uint32_t ea, uint32_t size, uint32_t tag)
{
	uint32_t from = (uint32_t)ls;

	while (size && (ea & 15))
	{
		uint32_t n = ea & -ea;

		while (n > size)
			n >>= 1;
		mfc_put((void *)from, ea, n, tag, 0, 0);
		from += n;
		ea += n;
		size -= n;
	}
	while (size >= 16)
	{
		uint32_t n = size & ~15u;

		if (n > 16384)
			n = 16384;
		mfc_put((void *)from, ea, n, tag, 0, 0);
		from += n;
		ea += n;
		size -= n;
	}
	while (size)
	{
		uint32_t n = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;

		mfc_put((void *)from, ea, n, tag, 0, 0);
		from += n;
		ea += n;
		size -= n;
	}
}

/* the echo out, after every output before it */
static void put_echo(void)
{
	dma_wait(OUTPUT_TAGS);
	mfc_put(&echo, echo_ea, sizeof(echo), TAG_ECHO, 0, 0);
	dma_wait(1 << TAG_ECHO);
}

/* the bell as the PPE last wrote it */
static void bell_read(void)
{
	mfc_getllar((void *)&bell, bell_ea, 0, 0);
	mfc_read_atomic_status();
}

/* until the bell's line changes, or 1 ms */
static void bell_wait(void)
{
	uint32_t events = spu_read_event_status();

	spu_write_event_ack(events);
	if (events & MFC_LLR_LOST_EVENT)
		echo.wakes++;
	if (events & MFC_DECREMENTER_EVENT)
	{
		echo.timeouts++;
		/* (the timers' clock goes on from where the decrementer was) */
		timer_shift += spu_read_decrementer() - DECREMENTER_TICKS;
		spu_write_decrementer(DECREMENTER_TICKS);
	}
}

static void event_send(uint32_t port, uint32_t data)
{
	if (spu_thread_send_event(port, data & 0xffffff, 0) != 0)
		echo.events_refused++;
}

/* ---------- floats as the PPE rounds them: each operation in double, rounded once to float (53 >= 2 * 24 + 2) */

static inline float f_mul(float a, float b)
{
	return (float)((double)a * (double)b);
}

static inline float f_add(float a, float b)
{
	return (float)((double)a + (double)b);
}

static inline float f_sub(float a, float b)
{
	return (float)((double)a - (double)b);
}

static inline float f_div(float a, float b)
{
	return (float)((double)a / (double)b);
}

static inline float f_from_u32(uint32_t value)
{
	return (float)(double)value;
}

static inline float f_from_s32(int32_t value)
{
	return (float)(double)value;
}

static inline float word_float(uint32_t word)
{
	union { uint32_t u; float f; } v;

	v.u = word;
	return v.f;
}

static inline uint32_t float_word(float value)
{
	union { uint32_t u; float f; } v;

	v.f = value;
	return v.u;
}

/* the PPE's fctiwz on the bits: toward zero, saturated, NaN to the most negative */
static inline int32_t f_to_s32(float value)
{
	uint32_t x = float_word(value), exponent = (x >> 23) & 0xff, mantissa = (x & 0x7fffff) | 0x800000;
	int32_t magnitude;

	if (exponent == 0xff && (x & 0x7fffff))
		return (int32_t)0x80000000;
	if (exponent < 127)
		return 0;
	if (exponent >= 127 + 31)
		return (x & 0x80000000) ? (int32_t)0x80000000 : 0x7fffffff;
	magnitude = exponent >= 150 ? (int32_t)(mantissa << (exponent - 150)) : (int32_t)(mantissa >> (150 - exponent));
	return (x & 0x80000000) ? -magnitude : magnitude;
}

/* d3d8_rsx.c's (DWORD) of a float under 2^31 */
static inline uint32_t f_to_u32(float value)
{
	return (uint32_t)f_to_s32(value);
}

/* IEEE comparisons on the bits (the SPU's own flush denormals and have no NaN): a NaN compares false */
static inline int f_nan(uint32_t x)
{
	return (x & 0x7fffffff) > 0x7f800000;
}

static inline int32_t f_order(uint32_t x)
{
	return (x & 0x80000000) ? -(int32_t)(x & 0x7fffffff) : (int32_t)x;
}

static inline int f_eq(float a, float b)
{
	uint32_t x = float_word(a), y = float_word(b);

	return !f_nan(x) && !f_nan(y) && f_order(x) == f_order(y);
}

static inline int f_lt(float a, float b)
{
	uint32_t x = float_word(a), y = float_word(b);

	return !f_nan(x) && !f_nan(y) && f_order(x) < f_order(y);
}

static inline int f_gt(float a, float b)
{
	return f_lt(b, a);
}

/* the PPE's 1.0e-6f (spu-gcc rounds a literal toward zero) */
#define DEPTH_RANGE_EPSILON 0x358637bdu

/* ---------- the conversions' buffers, which the rest borrows between conversions (inputs whole: 8 KB) */

#define FETCH_BYTES (4096 - 32)
#define OUTPUT_QUADS 256
#define INPUT_SLOP 96

static vec_float4 outputs[2][OUTPUT_QUADS] __attribute__((aligned(128)));
static unsigned char inputs[2][4096 + INPUT_SLOP] __attribute__((aligned(128)));
static uint32_t output_next;

/* ---------- the record ring's window */

#define WINDOW_BYTES 4096
static unsigned char window[WINDOW_BYTES] __attribute__((aligned(128)));
static uint64_t window_start;
static uint32_t window_valid;

/* need bytes of the record at position (16-aligned), in LS; records never cross the ring's end */
static const void *ring_bytes(uint64_t position, uint32_t need)
{
	uint32_t offset, limit, bytes;

	if (position >= window_start && position + need <= window_start + window_valid)
		return window + (uint32_t)(position - window_start);
	offset = ((uint32_t)position & (setup.ring_bytes - 1));
	limit = setup.ring_bytes - offset;
	bytes = need > WINDOW_BYTES ? need : WINDOW_BYTES;
	if ((uint64_t)bytes > bell.published - position)
		bytes = (uint32_t)(bell.published - position);
	if (bytes < need)
		bytes = need;
	if (bytes > limit)
		bytes = limit;
	if (bytes > sizeof(window))
		bytes = sizeof(window);
	dma_get(window, setup.ring + offset, (bytes + 15) & ~15u, TAG_RECORDS);
	dma_wait(1 << TAG_RECORDS);
	window_start = position;
	window_valid = bytes;
	return window;
}

/* ---------- commands: two 4 KB LS blocks of the chunks' ring; a chunk ends in a RETURN and is published */

#define STAGING_WORDS 512
static uint32_t staging[2][STAGING_WORDS] __attribute__((aligned(128)));
static uint32_t staging_block;
/* the write pointer, the current block's ring offset (4 KB aligned) and the words of it put */
static uint32_t *cmd;
static uint32_t block_offset;
static uint32_t block_flushed;
/* the chunk's start (ring offset); the chunks published; the ring's room the RSX can still read starts at busy_start
(busy_all: all of it); each frame's first chunk, by its frame_index */
static uint32_t chunk_start;
static uint32_t chunks_published;
static uint32_t busy_start;
static BOOL busy_all;
static uint32_t frame_starts[SPU_D3D_FRAMES];
static BOOL quitting;

static inline uint32_t write_offset(void)
{
	return block_offset + (uint32_t)(cmd - staging[staging_block]) * 4;
}

/* the block's words from block_flushed to its end (or to cmd) out; flushed stays 16-aligned */
static void block_put(uint32_t words)
{
	uint32_t from = block_flushed;

	if (words > from)
	{
		dma_put(&staging[staging_block][from], setup.chunks + block_offset + from * 4, (words - from) * 4,
			TAG_STAGING + staging_block);
		block_flushed = words;
	}
}

/* the next block (its last put done), at the next 4 KB of the ring */
static void block_next(void)
{
	block_put(STAGING_WORDS);
	staging_block ^= 1;
	dma_wait(1 << (TAG_STAGING + staging_block));
	block_offset += STAGING_WORDS * 4;
	block_flushed = 0;
	cmd = staging[staging_block];
}

/* n words (at most 64) together in the block: the rest of a block too short is NOPs */
static inline uint32_t *commands(uint32_t n)
{
	uint32_t *end = staging[staging_block] + STAGING_WORDS;

	if (__builtin_expect(cmd + n > end, 0))
	{
		while (cmd < end)
			*cmd++ = 0;
		block_next();
	}
	return cmd;
}

/* a method's data, any length, across the blocks */
static void commands_stream(const uint32_t *words, uint32_t n)
{
	while (n)
	{
		uint32_t room = (uint32_t)(staging[staging_block] + STAGING_WORDS - cmd), take = n < room ? n : room;

		memcpy(cmd, words, take * 4);
		cmd += take;
		words += take;
		n -= take;
		if (cmd == staging[staging_block] + STAGING_WORDS)
			block_next();
	}
}

static inline void command_word(uint32_t word)
{
	commands_stream(&word, 1);
}

/* the chunk ends in a RETURN and goes to the service thread; the next starts 16-aligned after it */
static void chunk_publish(void)
{
	static spu_d3d_chunk entry[2] __attribute__((aligned(16)));
	uint32_t start = chunk_start, end, index;

	if (write_offset() == start)
		return;
	*commands(1) = METHOD_RETURN;
	cmd++;
	while ((uint32_t)(cmd - staging[staging_block]) & 3)
		*cmd++ = 0;
	end = write_offset();
	block_put((uint32_t)(cmd - staging[staging_block]));
	if (cmd == staging[staging_block] + STAGING_WORDS)
		block_next();
	/* (the list's slot free once the service thread called the chunk in it) */
	while (chunks_published - bell.called >= SPU_D3D_CHUNK_LIST)
	{
		bell_read();
		if (chunks_published - bell.called >= SPU_D3D_CHUNK_LIST)
			bell_wait();
	}
	index = chunks_published & (SPU_D3D_CHUNK_LIST - 1);
	entry[index & 1].offset = setup.chunks_offset + start;
	entry[index & 1].bytes = end - start;
	dma_wait(3 << TAG_STAGING);
	put_exact(&entry[index & 1], setup.chunk_list + index * 8, 8, TAG_MISC);
	chunks_published++;
	statistics.chunks++;
	statistics.chunk_bytes += end - start;
	echo.chunks = chunks_published;
	put_echo();
	event_send(SPU_D3D_SERVICE_PORT, chunks_published);
	chunk_start = end;
}

/* the PPE's answer to a request; the chunk before it published */
static spu_d3d_response *request(uint32_t kind, uint32_t words)
{
	uint32_t start = timer_now();

	chunk_publish();
	request_block.number = ++request_number;
	request_block.kind = kind;
	(void)words;
	dma_wait(OUTPUT_TAGS);
	mfc_put(&request_block, setup.request, sizeof(request_block), TAG_MISC, 0, 0);
	echo.request = request_number;
	put_echo();
	event_send(SPU_D3D_SERVICE_PORT, 0x800000 | (request_number & 0x7fffff));
	for (;;)
	{
		bell_read();
		if (bell.response == request_number)
			break;
		if (bell.quit)
		{
			/* (no answer comes: the records stop) */
			quitting = TRUE;
			memset(&response_block, 0, sizeof(response_block));
			return &response_block;
		}
		bell_wait();
	}
	dma_get(&response_block, setup.response, sizeof(response_block), TAG_MISC);
	dma_wait(1 << TAG_MISC);
	statistics.requests++;
	statistics.request_ticks += start - timer_now();
	return &response_block;
}

/* room for a record's commands before the ring's end and before the room the RSX can still read; else a chunk from
the ring's start, or the RSX idle first */
static void commands_room(void)
{
	uint32_t offset = write_offset();

	if (offset + SPU_D3D_RECORD_COMMANDS > setup.chunks_bytes)
	{
		chunk_publish();
		/* (the rest of the ring is never called) */
		dma_wait(3 << TAG_STAGING);
		staging_block = 0;
		block_offset = 0;
		block_flushed = 0;
		cmd = staging[0];
		chunk_start = 0;
		offset = 0;
	}
	if (busy_all || (offset < busy_start && offset + SPU_D3D_RECORD_COMMANDS > busy_start))
	{
		request(_spu_d3d_request_wait_idle, 0);
		busy_all = FALSE;
		busy_start = write_offset();
	}
}

/* ---------- librsx's commands (commands_impl.h), word for word */

static void rsx_clear_color(uint32_t color)
{
	uint32_t *c = commands(2);

	c[0] = METHOD(NV40TCL_CLEAR_VALUE_COLOR, 1);
	c[1] = color;
	cmd += 2;
}

static void rsx_clear_depth_stencil(uint32_t value)
{
	uint32_t *c = commands(2);

	c[0] = METHOD(NV40TCL_CLEAR_VALUE_DEPTH, 1);
	c[1] = value;
	cmd += 2;
}

static void rsx_write_backend_label(uint32_t index, uint32_t value)
{
	uint32_t *c = commands(4);

	c[0] = METHOD(NV40TCL_SEMAPHORE_OFFSET, 1);
	c[1] = 0x10 * index;
	c[2] = METHOD(NV40TCL_SEMAPHORE_BACKENDWRITE_RELEASE, 1);
	c[3] = (value & 0xff00ff00) | ((value >> 16) & 0xff) | ((value & 0xff) << 16);
	cmd += 4;
}

typedef struct
{
	uint32_t color_offset;
	uint32_t color_pitch;
	uint32_t depth_offset;
	uint32_t depth_pitch;
	uint16_t width;
	uint16_t height;
	/* ps3_gpu.h's: the screen's view drawn smaller (a depth clear of all of it is ZCull's whole) */
	uint32_t smaller;
} gpu_surface;

/* rsxSetSurface of ps3_gpu.c's description: A8R8G8B8 into target 0 (the MRT slots 1 to 3 at the same offset, pitch 64),
Z24S8, linear, from (0, 0) */
static void rsx_surface(uint32_t color_offset, uint32_t color_pitch, uint32_t depth_offset, uint32_t depth_pitch,
	uint32_t width, uint32_t height)
{
	uint32_t log2_width = 31 - spu_extract(spu_cntlz(spu_promote(width, 0)), 0);
	uint32_t log2_height = 31 - spu_extract(spu_cntlz(spu_promote(height, 0)), 0);
	uint32_t *c = commands(32);

	c[0] = METHOD(NV40TCL_DMA_COLOR0, 1);
	c[1] = GCM_DMA_MEMORY_FRAME_BUFFER + GCM_LOCATION_RSX;
	c[2] = METHOD(NV40TCL_DMA_COLOR1, 1);
	c[3] = GCM_DMA_MEMORY_FRAME_BUFFER + GCM_LOCATION_RSX;
	c[4] = METHOD(NV40TCL_DMA_COLOR2, 2);
	c[5] = GCM_DMA_MEMORY_FRAME_BUFFER + GCM_LOCATION_RSX;
	c[6] = GCM_DMA_MEMORY_FRAME_BUFFER + GCM_LOCATION_RSX;
	c[7] = METHOD(NV40TCL_DMA_ZETA, 1);
	c[8] = GCM_DMA_MEMORY_FRAME_BUFFER + GCM_LOCATION_RSX;
	c[9] = METHOD(NV40TCL_RT_FORMAT, 6);
	c[10] = (log2_height << 24) | (log2_width << 16) | (GCM_SURFACE_CENTER_1 << 12) | (GCM_SURFACE_TYPE_LINEAR << 8) |
		(GCM_SURFACE_ZETA_Z24S8 << 5) | GCM_SURFACE_A8R8G8B8;
	c[11] = color_pitch;
	c[12] = color_offset;
	c[13] = depth_offset;
	c[14] = color_offset;
	c[15] = 64;
	c[16] = METHOD(NV40TCL_ZETA_PITCH, 1);
	c[17] = depth_pitch;
	c[18] = METHOD(NV40TCL_COLOR2_PITCH, 4);
	c[19] = 64;
	c[20] = 64;
	c[21] = color_offset;
	c[22] = color_offset;
	c[23] = METHOD(NV40TCL_RT_ENABLE, 1);
	c[24] = GCM_SURFACE_TARGET_0;
	c[25] = METHOD(NV40TCL_WINDOW_OFFSET, 1);
	c[26] = 0;
	c[27] = METHOD(NV40TCL_RT_HORIZ, 2);
	c[28] = width << 16;
	c[29] = height << 16;
	c[30] = METHOD(NV40TCL_SHADER_WINDOW, 1);
	c[31] = (1 << 12) | height;
	cmd += 32;
}

static void rsx_method1(uint32_t method, uint32_t value)
{
	uint32_t *c = commands(2);

	c[0] = METHOD(method, 1);
	c[1] = value;
	cmd += 2;
}

static void rsx_method2(uint32_t method, uint32_t a, uint32_t b)
{
	uint32_t *c = commands(3);

	c[0] = METHOD(method, 2);
	c[1] = a;
	c[2] = b;
	cmd += 3;
}

static void rsx_method3(uint32_t method, uint32_t a, uint32_t b, uint32_t d)
{
	uint32_t *c = commands(4);

	c[0] = METHOD(method, 3);
	c[1] = a;
	c[2] = b;
	c[3] = d;
	cmd += 4;
}

#define rsx_color_mask(mask) rsx_method1(NV40TCL_COLOR_MASK, (mask))
#define rsx_depth_test_enable(e) rsx_method1(NV40TCL_DEPTH_TEST_ENABLE, (e))
#define rsx_depth_func(f) rsx_method1(NV40TCL_DEPTH_FUNC, (f))
#define rsx_depth_write_enable(e) rsx_method1(NV40TCL_DEPTH_WRITE_ENABLE, (e))
#define rsx_depth_bounds_test_enable(e) rsx_method1(NV40TCL_DEPTH_BOUNDS_TEST_ENABLE, (e))
#define rsx_depth_bounds(minimum, maximum) \
	rsx_method2(NV40TCL_DEPTH_BOUNDS_MIN, float_word(minimum), float_word(maximum))
#define rsx_cull_face_enable(e) rsx_method1(NV40TCL_CULL_FACE_ENABLE, (e))
#define rsx_cull_face(c) rsx_method1(NV40TCL_CULL_FACE, (c))
#define rsx_front_face(d) rsx_method1(NV40TCL_FRONT_FACE, (d))
#define rsx_front_polygon_mode(m) rsx_method1(NV40TCL_POLYGON_MODE_FRONT, (m))
#define rsx_back_polygon_mode(m) rsx_method1(NV40TCL_POLYGON_MODE_BACK, (m))
#define rsx_polygon_offset_fill_enable(e) rsx_method1(NV40TCL_POLYGON_OFFSET_FILL_ENABLE, (e))
#define rsx_polygon_offset_line_enable(e) rsx_method1(NV40TCL_POLYGON_OFFSET_LINE_ENABLE, (e))
#define rsx_stencil_mask(m) rsx_method1(NV40TCL_STENCIL_BACK_MASK, (m))
#define rsx_stencil_test_enable(e) rsx_method1(NV40TCL_STENCIL_BACK_ENABLE, (e))
#define rsx_alpha_test_enable(e) rsx_method1(NV40TCL_ALPHA_TEST_ENABLE, (e))
#define rsx_blend_enable(e) rsx_method1(NV40TCL_BLEND_ENABLE, (e))
#define rsx_zcull_enable(depth, stencil) rsx_method1(NV40TCL_ZCULL_ENABLE, ((stencil) << 1) | (depth))
#define rsx_clear_zcull_surface(depth, stencil) rsx_method1(NV40TCL_CLEAR_ZCULL_SURFACE, ((stencil) << 1) | (depth))
#define rsx_zcull_invalidate() rsx_method1(NV40TCL_INVALIDATE_ZCULL, 0)
#define rsx_texture_cache_invalidate(type) rsx_method1(NV40TCL_TEX_CACHE_CTL, (type))
#define rsx_report(type, index) rsx_method1(NV40TCL_QUERY_GET, ((type) << 24) | (0x10 * (index)))
#define rsx_texture_border_color(unit, color) rsx_method1(NV40TCL_TEX_BORDER_COLOR(unit), (color))

static void rsx_stencil_func(uint32_t func, uint32_t ref, uint32_t mask)
{
	rsx_method3(NV40TCL_STENCIL_BACK_FUNC_FUNC, func, ref, mask);
}

static void rsx_stencil_op(uint32_t fail, uint32_t depth_fail, uint32_t depth_pass)
{
	rsx_method3(NV40TCL_STENCIL_BACK_OP_FAIL, fail, depth_fail, depth_pass);
}

static void rsx_alpha_func(uint32_t func, uint32_t ref)
{
	rsx_method2(NV40TCL_ALPHA_TEST_FUNC, func, ref);
}

static void rsx_blend_func(uint32_t sfcolor, uint32_t dfcolor, uint32_t sfalpha, uint32_t dfalpha)
{
	rsx_method2(NV40TCL_BLEND_FUNC_SRC, ((sfalpha & 0xffff) << 16) | (sfcolor & 0xffff),
		((dfalpha & 0xffff) << 16) | (dfcolor & 0xffff));
}

static void rsx_blend_equation(uint32_t color, uint32_t alpha)
{
	rsx_method1(NV40TCL_BLEND_EQUATION, ((alpha & 0xffff) << 16) | (color & 0xffff));
}

static void rsx_blend_color(uint32_t color0, uint32_t color1)
{
	uint32_t *c = commands(4);

	c[0] = METHOD(NV40TCL_BLEND_COLOR, 1);
	c[1] = color0;
	c[2] = METHOD(NV40TCL_BLEND_COLOR2, 1);
	c[3] = color1;
	cmd += 4;
}

static void rsx_polygon_offset(float factor, float units)
{
	rsx_method2(NV40TCL_POLYGON_OFFSET_FACTOR, float_word(factor), float_word(units));
}

static void rsx_scissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	rsx_method2(NV40TCL_SCISSOR_HORIZ, ((w & 0xffff) << 16) | (x & 0xffff), ((h & 0xffff) << 16) | (y & 0xffff));
}

static void rsx_clear_surface(uint32_t mask)
{
	uint32_t *c = commands(4);

	c[0] = METHOD(NV40TCL_CLEAR_BUFFERS, 1);
	c[1] = mask;
	c[2] = METHOD(NV40TCL_NOP, 1);
	c[3] = 0;
	cmd += 4;
}

static void rsx_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float minimum, float maximum,
	const float scale[4], const float offset[4])
{
	uint32_t *c = commands(24);
	int pass;

	c[0] = METHOD(NV40TCL_VIEWPORT_HORIZ, 2);
	c[1] = ((width & 0xffff) << 16) | (x & 0xffff);
	c[2] = ((height & 0xffff) << 16) | (y & 0xffff);
	c[3] = METHOD(NV40TCL_DEPTH_RANGE, 2);
	c[4] = float_word(minimum);
	c[5] = float_word(maximum);
	for (pass = 0; pass < 2; pass++)
	{
		uint32_t *d = c + 6 + pass * 9;

		d[0] = METHOD(NV40TCL_VIEWPORT_OFFSET, 8);
		d[1] = float_word(offset[0]);
		d[2] = float_word(offset[1]);
		d[3] = float_word(offset[2]);
		d[4] = float_word(offset[3]);
		d[5] = float_word(scale[0]);
		d[6] = float_word(scale[1]);
		d[7] = float_word(scale[2]);
		d[8] = float_word(scale[3]);
	}
	cmd += 24;
}

static void rsx_viewport_clip(uint32_t width, uint32_t height)
{
	rsx_method2(NV40TCL_VIEWPORT_CLIP_HORIZ(0), ((width - 1) & 0xffff) << 16, ((height - 1) & 0xffff) << 16);
}

/* the microcode's piece from done bytes on (whole 8-instruction blocks, 4 KB at most) in the inputs' buffer */
static const uint32_t *microcode_piece(uint32_t microcode, uint32_t bytes, uint32_t *done)
{
	uint32_t piece = bytes - *done < 4096 ? bytes - *done : 4096;
	const uint32_t *data = (const uint32_t *)fetch(inputs[0], microcode + *done, piece, TAG_MISC);

	*done += piece;
	return data;
}

/* rsxLoadVertexProgramBlock: the microcode (words at an EA) inline after its header, then the masks and the timeout */
static void rsx_vertex_program_block(uint32_t start, uint32_t count, uint32_t microcode, uint32_t input_mask,
	uint32_t output_mask, uint32_t register_count)
{
	uint32_t loop = count / 8, rest = (count % 8) * 4, index, done = 0, *c;
	const uint32_t *data = NULL;

	c = commands(3);
	c[0] = METHOD(NV40TCL_VP_UPLOAD_FROM_ID, 2);
	c[1] = start;
	c[2] = start;
	cmd += 3;
	for (index = 0; index < loop; index++)
	{
		if (!(index & 31))
			data = microcode_piece(microcode, count * 16, &done);
		command_word(METHOD(NV40TCL_VP_UPLOAD_INST(0), 32));
		commands_stream(data, 32);
		data += 32;
	}
	if (rest)
	{
		if (!(loop & 31))
			data = microcode_piece(microcode, count * 16, &done);
		command_word(METHOD(NV40TCL_VP_UPLOAD_INST(0), rest));
		commands_stream(data, rest);
	}
	c = commands(6);
	c[0] = METHOD(NV40TCL_VP_ATTRIB_EN, 1);
	c[1] = input_mask;
	c[2] = METHOD(NV40TCL_VP_RESULT_EN, 1);
	c[3] = output_mask | GCM_ATTRIB_OUTPUT_MASK_POINTSIZE;
	c[4] = METHOD(NV40TCL_TRANSFORM_TIMEOUT, 1);
	c[5] = register_count <= 32 ? 0x0020FFFF : 0x0030FFFF;
	cmd += 6;
}

/* rsxLoadVertexProgramParameterBlock */
static void rsx_vertex_constants(uint32_t base, uint32_t count, const float *values)
{
	uint32_t block_count = (count * 4) >> 5, rest = (count * 4) & 0x1f, index;

	for (index = 0; index < block_count; index++)
	{
		uint32_t *c = commands(2);

		c[0] = METHOD(NV40TCL_VP_UPLOAD_CONST_ID, 33);
		c[1] = base + index * 8;
		cmd += 2;
		commands_stream((const uint32_t *)values, 32);
		values += 32;
	}
	if (rest)
	{
		uint32_t *c = commands(2);

		c[0] = METHOD(NV40TCL_VP_UPLOAD_CONST_ID, rest + 1);
		c[1] = base + block_count * 8;
		cmd += 2;
		commands_stream((const uint32_t *)values, rest);
	}
}

/* rsxLoadFragmentProgramLocation (RSX memory) */
static void rsx_fragment_program_location(uint32_t offset, uint32_t control, uint32_t register_count, uint32_t texcoords,
	uint32_t texcoords_2d)
{
	uint32_t texcoords_3d = texcoords & ~texcoords_2d, index, registers;

	rsx_method1(NV40TCL_FP_ADDRESS, (GCM_LOCATION_RSX + 1) | (offset & 0x1fffffff));
	for (index = 0; texcoords; index++)
	{
		if (texcoords & 1)
			rsx_method1(NV40TCL_TEX_COORD_CONTROL(index), ((texcoords_3d & 1) << 4) | (texcoords_2d & 1));
		texcoords >>= 1;
		texcoords_2d >>= 1;
		texcoords_3d >>= 1;
	}
	registers = register_count > 2 ? register_count : 2;
	rsx_method1(NV40TCL_FP_CONTROL, control | (registers << 24) | (1 << 10));
}

/* rsxLoadTexture of ps3_gpu.c's gcmTexture: RSX memory, at least one level and a depth of 1, the remap's identity for
0 (the texture's fields: ps3_gpu.h's struct ps3_gpu_texture) */
static void rsx_load_texture(uint32_t unit, uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height,
	uint32_t depth, uint32_t format, uint32_t levels, uint32_t dimension, uint32_t cube, uint32_t remap)
{
	uint32_t *c = commands(9);

	c[0] = METHOD(NV40TCL_TEX_OFFSET(unit), 2);
	c[1] = offset;
	c[2] = (GCM_LOCATION_RSX + 1) | ((cube ? 1u : 0u) << 2) | (dimension << 4) | (format << 8) |
		((levels ? levels : 1) << 16) | (1 << 3) | 0x8000;
	c[3] = METHOD(NV40TCL_TEX_SWIZZLE(unit), 1);
	c[4] = remap ? remap : REMAP_IDENTITY;
	c[5] = METHOD(NV40TCL_TEX_SIZE0(unit), 1);
	c[6] = (width << 16) | height;
	c[7] = METHOD(NV40TCL_TEX_SIZE1(unit), 1);
	c[8] = ((depth ? depth : 1) << 20) | pitch;
	cmd += 9;
}

static void rsx_texture_control(uint32_t unit, uint32_t enable, uint32_t minimum_level, uint32_t maximum_level,
	uint32_t anisotropy)
{
	rsx_method1(NV40TCL_TEX_ENABLE(unit), (enable << 31) | ((minimum_level & 0xffff) << 19) |
		((maximum_level & 0xffff) << 7) | ((anisotropy & 0xff) << 4));
}

static void rsx_texture_filter(uint32_t unit, uint32_t bias, uint32_t minify, uint32_t magnify, uint32_t convolution)
{
	rsx_method1(NV40TCL_TEX_FILTER(unit), ((magnify & 0xff) << 24) | ((minify & 0xff) << 16) |
		((convolution & 0xff) << 13) | (bias & 0x1fff));
}

static void rsx_texture_wrap(uint32_t unit, uint32_t s, uint32_t t, uint32_t r)
{
	rsx_method1(NV40TCL_TEX_WRAP(unit), ((s & 0xff) << 0) | ((t & 0xff) << 8) | ((r & 0xff) << 16));
}

static void rsx_vertex_attribute(uint32_t index, uint32_t offset, uint32_t stride, uint32_t elements)
{
	uint32_t *c = commands(4);

	c[0] = METHOD(NV40TCL_VTXFMT(index), 1);
	c[1] = ((stride & 0xff) << 8) | ((elements & 0xff) << 4) | GCM_VERTEX_DATA_TYPE_F32;
	c[2] = METHOD(NV40TCL_VTXBUF_ADDRESS(index), 1);
	c[3] = ((uint32_t)GCM_LOCATION_RSX << 31) | offset;
	cmd += 4;
}

static void rsx_vertex_cache_invalidate(void)
{
	uint32_t *c = commands(8);

	c[0] = METHOD(NV40TCL_VTX_CACHE_INVALIDATE2, 1);
	c[1] = 0;
	c[2] = METHOD(NV40TCL_VTX_CACHE_INVALIDATE, 1);
	c[3] = 0;
	c[4] = METHOD(NV40TCL_VTX_CACHE_INVALIDATE, 1);
	c[5] = 0;
	c[6] = METHOD(NV40TCL_VTX_CACHE_INVALIDATE, 1);
	c[7] = 0;
	cmd += 8;
}

/* rsxDrawVertexArray */
static void rsx_draw_vertex_array(uint32_t type, uint32_t start, uint32_t count)
{
	uint32_t lcount, loop, rest, index, *c;

	--count;
	lcount = count & 0xff;
	count >>= 8;
	loop = count / MAX_METHOD_COUNT;
	rest = count % MAX_METHOD_COUNT;
	c = commands(8);
	c[0] = METHOD_NI(NV40TCL_VTX_CACHE_INVALIDATE, 3);
	c[1] = 0;
	c[2] = 0;
	c[3] = 0;
	c[4] = METHOD(NV40TCL_BEGIN_END, 1);
	c[5] = type;
	c[6] = METHOD(NV40TCL_VB_VERTEX_BATCH, 1);
	c[7] = (lcount << 24) | start;
	cmd += 8;
	start += lcount + 1;
	for (index = 0; index < loop; index++)
	{
		uint32_t j;

		command_word(METHOD_NI(NV40TCL_VB_VERTEX_BATCH, MAX_METHOD_COUNT));
		for (j = 0; j < MAX_METHOD_COUNT; j++)
		{
			command_word(0xff000000u | start);
			start += 0x100;
		}
	}
	if (rest)
	{
		uint32_t j;

		command_word(METHOD_NI(NV40TCL_VB_VERTEX_BATCH, rest));
		for (j = 0; j < rest; j++)
		{
			command_word(0xff000000u | start);
			start += 0x100;
		}
	}
	c = commands(2);
	c[0] = METHOD(NV40TCL_BEGIN_END, 1);
	c[1] = 0;
	cmd += 2;
}

/* rsxDrawIndexArray of 16-bit indices in RSX memory */
static void rsx_draw_index_array(uint32_t type, uint32_t offset, uint32_t count)
{
	uint32_t start = 0, misaligned = (((offset + 127) & ~127u) - offset) >> 1, odd, mcount, *c;

	odd = misaligned && misaligned < count ? 1 : 0;
	c = commands(9);
	c[0] = METHOD(NV40TCL_VTX_CACHE_INVALIDATE, 1);
	c[1] = 0;
	c[2] = METHOD(NV40TCL_VB_INDEX_BATCH_OFFSET, 2);
	c[3] = offset;
	c[4] = (GCM_INDEX_TYPE_16B << 4) | GCM_LOCATION_RSX;
	c[5] = METHOD(NV40TCL_BEGIN_END, 1);
	c[6] = type & 0xff;
	if (odd)
	{
		c[7] = METHOD(NV40TCL_VB_INDEX_BATCH_DRAW, 1);
		c[8] = ((misaligned - 1) << 24) | start;
		start += misaligned;
		count -= misaligned;
	}
	cmd += 7 + odd * 2;
	while (count > 0x7ff00)
	{
		uint32_t j;

		command_word(METHOD_NI(NV40TCL_VB_INDEX_BATCH_DRAW, MAX_METHOD_COUNT));
		for (j = 0; j < MAX_METHOD_COUNT; j++)
		{
			command_word(0xff000000u | start);
			start += 0x100;
		}
		count -= 0x7ff00;
	}
	mcount = (count + 0xff) >> 8;
	command_word(METHOD_NI(NV40TCL_VB_INDEX_BATCH_DRAW, mcount));
	while (count > 0x100)
	{
		command_word(0xff000000u | start);
		start += 0x100;
		count -= 0x100;
	}
	if (count)
	{
		--count;
		command_word((count << 24) | start);
	}
	c = commands(2);
	c[0] = METHOD(NV40TCL_BEGIN_END, 1);
	c[1] = 0;
	cmd += 2;
}

/* ---------- ps3_gpu.c's RSX state: what the RSX holds, the scratch, the draws */

typedef struct
{
	uint32_t color_mask;
	uint32_t depth_function;
	uint32_t depth_write;
	uint32_t stencil_function;
	uint32_t stencil_reference;
	uint32_t stencil_mask;
	uint32_t stencil_write_mask;
	uint32_t stencil_fail;
	uint32_t stencil_depth_fail;
	uint32_t stencil_pass;
	uint32_t blend;
	uint32_t blend_source;
	uint32_t blend_destination;
	uint32_t blend_source_alpha;
	uint32_t blend_destination_alpha;
	uint32_t blend_equation;
	uint32_t blend_color;
	uint32_t alpha_function;
	uint32_t alpha_reference;
	uint32_t cull_face;
	uint32_t front_face;
	uint32_t polygon_mode;
	uint32_t polygon_offset;
	float polygon_offset_factor;
	float polygon_offset_units;
	uint16_t scissor_x;
	uint16_t scissor_y;
	uint16_t scissor_width;
	uint16_t scissor_height;
	float depth_minimum;
	float depth_maximum;
	uint32_t depth_bounds;
	float depth_bounds_minimum;
	float depth_bounds_maximum;
} gpu_state;

/* a texture as ps3_gpu.h's struct ps3_gpu_texture: its words as the PPE lays them out */
typedef struct
{
	uint32_t offset;
	uint32_t pitch;
	uint32_t border;
	uint16_t width;
	uint16_t height;
	uint16_t depth;
	uint8_t format;
	uint8_t levels;
	uint8_t dimension;
	uint8_t cube;
	uint8_t minify;
	uint8_t magnify;
	uint8_t wrap_s;
	uint8_t wrap_t;
	uint8_t wrap_r;
	uint8_t anisotropy;
	uint16_t bias;
	uint16_t minimum_level;
	uint16_t maximum_level;
	uint32_t remap;
} gpu_texture;

typedef char gpu_texture_check[sizeof(gpu_texture) == 40 ? 1 : -1];

static uint32_t caches_epoch;
static gpu_state state;
static int state_valid;
static gpu_surface surface;
static int surface_valid;
static uint32_t viewport_size[2];
static float viewport_depth[2];
static int viewport_valid;
/* the program loaded: its rsx_vertex_program's EA (the blit's: 1) */
static uint32_t vertex_program;
static struct
{
	/* the fragment table's index + 1 of the program loaded */
	uint32_t program;
	float values[32][4];
} fragment;
static gpu_texture textures[TEXTURE_UNITS];
static uint8_t texture_states[TEXTURE_UNITS];
static int texture_cache_dirty;
static int vertex_cache_dirty;
static uint8_t attribute_enabled[ATTRIBUTE_COUNT];
/* ZCull's surface (the PPE binds it with a target's tiles) */
static uint32_t zcull_offset, zcull_width, zcull_height;
static int zcull_bound, zcull_valid;
static struct
{
	uint32_t program;
	uint32_t start;
	uint32_t count;
	uint32_t used;
} resident_programs[RESIDENT_PROGRAM_COUNT];
static uint32_t program_clock;
static struct
{
	uint32_t head;
	uint32_t peak;
	uint32_t drops;
} scratch;
static uint32_t frame_index = 1;
/* the timestamps of this frame's set (the PPE reads them two frames on), the draws since the last */
static struct
{
	uint16_t parts[TIMESTAMP_PARTS];
	uint32_t draws[TIMESTAMP_PARTS];
	uint32_t count;
	uint32_t frame;
	uint32_t eighths;
	uint32_t pad;
} stamps __attribute__((aligned(16)));
static uint32_t part_draws;
static struct
{
	uint16_t keys[DRAW_STAMPS];
	uint32_t count;
	uint32_t frame;
	int on;
	uint32_t pad;
} draw_stamps __attribute__((aligned(16)));

typedef char stamps_size_check[sizeof(stamps) == sizeof(spu_d3d_stamps) &&
	sizeof(draw_stamps) == sizeof(spu_d3d_draw_stamps) ? 1 : -1];

static void caches_invalidate(void)
{
	caches_epoch++;
	state_valid = 0;
	surface_valid = 0;
	viewport_valid = 0;
	vertex_program = 0;
	fragment.program = 0;
	memset(texture_states, 0, sizeof(texture_states));
	memset(attribute_enabled, 1, sizeof(attribute_enabled));
	texture_cache_dirty = 1;
}

/* the scratch's EA for this frame's size bytes, NULL when the half is full; offset its RSX offset */
static uint32_t gpu_scratch(uint32_t size, uint32_t alignment, uint32_t *offset)
{
	uint32_t half = (frame_index & 1) * SCRATCH_HALF_SIZE;
	uint32_t head = (scratch.head + alignment - 1) & ~(alignment - 1);

	if (head + size > SCRATCH_HALF_SIZE)
	{
		scratch.drops++;
		return 0;
	}
	scratch.head = head + size;
	if (scratch.head > scratch.peak)
		scratch.peak = scratch.head;
	*offset = setup.scratch_offset + half + head;
	return setup.scratch + half + head;
}

static int zcull_surface(void)
{
	return zcull_bound && surface_valid && surface.depth_offset == zcull_offset;
}

static void zcull_invalidate(void)
{
	if (zcull_valid)
	{
		rsx_zcull_invalidate();
		zcull_valid = 0;
	}
}

static void gpu_set_surface(const gpu_surface *target)
{
	if (surface_valid && target->color_offset == surface.color_offset && target->color_pitch == surface.color_pitch &&
		target->depth_offset == surface.depth_offset && target->depth_pitch == surface.depth_pitch &&
		target->width == surface.width && target->height == surface.height && target->smaller == surface.smaller)
		return;
	surface = *target;
	surface_valid = 1;
	rsx_surface(target->color_offset, target->color_pitch, target->depth_offset, target->depth_pitch, target->width,
		target->height);
	if (zcull_bound)
		rsx_zcull_enable(target->depth_offset == zcull_offset ? 1 : 0, 0);
	viewport_valid = 0;
	texture_cache_dirty = 1;
}

static void viewport_set(uint32_t width, uint32_t height, float minimum, float maximum)
{
	float scale[4], offset[4];

	if (viewport_valid && viewport_size[0] == width && viewport_size[1] == height &&
		f_eq(viewport_depth[0], minimum) && f_eq(viewport_depth[1], maximum))
		return;
	viewport_valid = 1;
	viewport_size[0] = width;
	viewport_size[1] = height;
	viewport_depth[0] = minimum;
	viewport_depth[1] = maximum;
	scale[0] = f_mul(f_from_u32(width), 0.5f);
	scale[1] = f_mul(f_from_u32(height), -0.5f);
	scale[2] = f_mul(f_sub(maximum, minimum), 0.5f);
	scale[3] = 0.0f;
	offset[0] = f_mul(f_from_u32(width), 0.5f);
	offset[1] = f_mul(f_from_u32(height), 0.5f);
	offset[2] = f_mul(f_add(maximum, minimum), 0.5f);
	offset[3] = 0.0f;
	rsx_viewport(0, 0, width & 0xffff, height & 0xffff, minimum, maximum, scale, offset);
	rsx_viewport_clip(width & 0xffff, height & 0xffff);
}

static void gpu_clear(uint32_t flags, uint32_t argb, uint32_t depth_stencil, uint32_t x, uint32_t y, uint32_t width,
	uint32_t height)
{
	uint32_t mask = 0, color_mask = 0;

	if (!width || !height)
		return;
	if (flags & 0x10) { mask |= GCM_CLEAR_R; color_mask |= GCM_COLOR_MASK_R; }
	if (flags & 0x20) { mask |= GCM_CLEAR_G; color_mask |= GCM_COLOR_MASK_G; }
	if (flags & 0x40) { mask |= GCM_CLEAR_B; color_mask |= GCM_COLOR_MASK_B; }
	if (flags & 0x80) { mask |= GCM_CLEAR_A; color_mask |= GCM_COLOR_MASK_A; }
	if (flags & 0x01)
		mask |= GCM_CLEAR_Z;
	if (flags & 0x02)
		mask |= GCM_CLEAR_S;
	if (!mask)
		return;
	rsx_scissor(x & 0xffff, y & 0xffff, width & 0xffff, height & 0xffff);
	rsx_color_mask(color_mask);
	rsx_clear_color(argb);
	if (mask & GCM_CLEAR_Z)
		rsx_depth_write_enable(1);
	if (mask & GCM_CLEAR_S)
		rsx_stencil_mask(0xff);
	if (state.depth_bounds)
	{
		rsx_depth_bounds_test_enable(0);
		state.depth_bounds = 0;
	}
	rsx_clear_depth_stencil(depth_stencil);
	rsx_clear_surface(mask);
	if ((mask & GCM_CLEAR_Z) && zcull_surface())
	{
		if (!x && !y && ((width >= zcull_width && height >= zcull_height) ||
			(surface.smaller && width >= surface.width && height >= surface.height)))
		{
			rsx_clear_zcull_surface(1, 0);
			zcull_valid = 1;
		}
		else
		{
			zcull_invalidate();
		}
	}
	state_valid = 0;
}

static void gpu_set_state(const gpu_state *next)
{
	const gpu_state *old = &state;
	int all = !state_valid;

	if (all || next->color_mask != old->color_mask)
		rsx_color_mask(next->color_mask);
	if (all || next->depth_function != old->depth_function)
	{
		rsx_depth_test_enable(next->depth_function != 0);
		if (next->depth_function)
			rsx_depth_func(next->depth_function);
	}
	if (all || next->depth_write != old->depth_write)
		rsx_depth_write_enable(next->depth_write != 0);
	if (next->depth_write && next->depth_function > GCM_LEQUAL && zcull_surface())
		zcull_invalidate();
	if (all || next->stencil_function != old->stencil_function || next->stencil_reference != old->stencil_reference ||
		next->stencil_mask != old->stencil_mask)
	{
		rsx_stencil_test_enable(next->stencil_function != 0);
		if (next->stencil_function)
			rsx_stencil_func(next->stencil_function, next->stencil_reference, next->stencil_mask);
	}
	if (all || next->stencil_write_mask != old->stencil_write_mask)
		rsx_stencil_mask(next->stencil_write_mask);
	if (all || next->stencil_fail != old->stencil_fail || next->stencil_depth_fail != old->stencil_depth_fail ||
		next->stencil_pass != old->stencil_pass)
		rsx_stencil_op(next->stencil_fail, next->stencil_depth_fail, next->stencil_pass);
	if (all || next->blend != old->blend)
		rsx_blend_enable(next->blend != 0);
	if (all || next->blend_source != old->blend_source || next->blend_destination != old->blend_destination ||
		next->blend_source_alpha != old->blend_source_alpha ||
		next->blend_destination_alpha != old->blend_destination_alpha)
		rsx_blend_func(next->blend_source, next->blend_destination, next->blend_source_alpha,
			next->blend_destination_alpha);
	if (all || next->blend_equation != old->blend_equation)
		rsx_blend_equation(next->blend_equation, next->blend_equation);
	if (all || next->blend_color != old->blend_color)
		rsx_blend_color(next->blend_color, 0);
	if (all || next->alpha_function != old->alpha_function || next->alpha_reference != old->alpha_reference)
	{
		rsx_alpha_test_enable(next->alpha_function != 0);
		if (next->alpha_function)
			rsx_alpha_func(next->alpha_function, next->alpha_reference);
	}
	if (all || next->cull_face != old->cull_face)
	{
		rsx_cull_face_enable(next->cull_face != 0);
		if (next->cull_face)
			rsx_cull_face(next->cull_face);
	}
	if (all || next->front_face != old->front_face)
		rsx_front_face(next->front_face);
	if (all || next->polygon_mode != old->polygon_mode)
	{
		rsx_front_polygon_mode(next->polygon_mode);
		rsx_back_polygon_mode(next->polygon_mode);
	}
	if (all || next->polygon_offset != old->polygon_offset)
	{
		rsx_polygon_offset_fill_enable(next->polygon_offset != 0);
		rsx_polygon_offset_line_enable(next->polygon_offset != 0);
	}
	if (all || !f_eq(next->polygon_offset_factor, old->polygon_offset_factor) ||
		!f_eq(next->polygon_offset_units, old->polygon_offset_units))
		rsx_polygon_offset(next->polygon_offset_factor, next->polygon_offset_units);
	if (all || next->scissor_x != old->scissor_x || next->scissor_y != old->scissor_y ||
		next->scissor_width != old->scissor_width || next->scissor_height != old->scissor_height)
		rsx_scissor(next->scissor_x, next->scissor_y, next->scissor_width, next->scissor_height);
	if (all || next->depth_bounds != old->depth_bounds)
		rsx_depth_bounds_test_enable(next->depth_bounds != 0);
	if (next->depth_bounds && (all || !old->depth_bounds || !f_eq(next->depth_bounds_minimum, old->depth_bounds_minimum) ||
		!f_eq(next->depth_bounds_maximum, old->depth_bounds_maximum)))
		rsx_depth_bounds(next->depth_bounds_minimum, next->depth_bounds_maximum);
	state = *next;
	state_valid = 1;
	viewport_set(surface.width, surface.height, next->depth_minimum, next->depth_maximum);
}

/* a resident program's slots, or -1; with none free, the programs that ran longest ago go */
static int program_slots_find(uint32_t program, uint32_t count, int *uploaded)
{
	int index, slot = -1;

	*uploaded = 0;
	for (index = 0; index < RESIDENT_PROGRAM_COUNT; index++)
	{
		if (resident_programs[index].program == program)
		{
			resident_programs[index].used = ++program_clock;
			return (int)resident_programs[index].start;
		}
	}
	if (!count || count > PROGRAM_SLOTS)
		return -1;
	for (;;)
	{
		uint32_t start = 0;
		int free_entry = -1, oldest = -1, blocked = 0;

		do
		{
			blocked = 0;
			for (index = 0; index < RESIDENT_PROGRAM_COUNT; index++)
			{
				if (resident_programs[index].program && start < resident_programs[index].start +
					resident_programs[index].count && resident_programs[index].start < start + count)
				{
					start = resident_programs[index].start + resident_programs[index].count;
					blocked = 1;
				}
			}
		} while (blocked && start + count <= PROGRAM_SLOTS);
		for (index = 0; index < RESIDENT_PROGRAM_COUNT; index++)
		{
			if (!resident_programs[index].program)
			{
				if (free_entry < 0)
					free_entry = index;
			}
			else if (oldest < 0 || resident_programs[index].used < resident_programs[oldest].used)
			{
				oldest = index;
			}
		}
		if (start + count <= PROGRAM_SLOTS && free_entry >= 0)
		{
			resident_programs[free_entry].program = program;
			resident_programs[free_entry].start = start;
			resident_programs[free_entry].count = count;
			resident_programs[free_entry].used = ++program_clock;
			slot = (int)start;
			*uploaded = 1;
			break;
		}
		if (oldest < 0)
			break;
		resident_programs[oldest].program = 0;
	}
	return slot;
}

/* a vertex program: its identity, microcode, instruction count, masks and registers (rsx_vertex_program) */
typedef struct
{
	uint32_t identity;
	uint32_t microcode;
	uint32_t instruction_count;
	uint32_t input_mask;
	uint32_t output_mask;
	uint32_t register_count;
} vertex_program_info;

static void gpu_set_vertex_program(const vertex_program_info *program)
{
	int uploaded = 1, slot;

	if (program->identity == vertex_program)
		return;
	slot = program_slots_find(program->identity, program->instruction_count, &uploaded);
	if (slot < 0)
	{
		memset(resident_programs, 0, sizeof(resident_programs));
		slot = 0;
		uploaded = 1;
	}
	rsx_vertex_program_block((uint32_t)slot, uploaded ? program->instruction_count : 0, program->microcode,
		program->input_mask, program->output_mask, program->register_count);
	vertex_program = program->identity;
	if (uploaded)
		statistics.vertex_uploads++;
	else
		statistics.vertex_switches++;
}

static void gpu_set_vertex_constants(uint32_t first, uint32_t count, const float *values)
{
	if (count)
		rsx_vertex_constants(first, count, values);
}

/* the program's microcode with the uniforms' values (each word's halves swapped), into the scratch at memory */
#define FRAGMENT_PIECE 4096

typedef struct
{
	uint32_t index;
	uint32_t microcode;
	uint32_t word_count;
	uint32_t control;
	uint32_t register_count;
	uint32_t texcoord_mask;
	uint32_t uniform_count;
	uint32_t uniforms[32];
	uint32_t stamp_key;
	uint32_t ppe_entry;
} fragment_program_info;

static int gpu_set_fragment_program(const fragment_program_info *program, const float uniforms[][4])
{
	uint32_t index, offset, bytes = program->word_count * 4, memory, done;

	if (fragment.program == program->index + 1)
	{
		for (index = 0; index < program->uniform_count; index++)
		{
			const uint32_t *a = (const uint32_t *)fragment.values[index];
			const uint32_t *b = (const uint32_t *)uniforms[program->uniforms[index] & 0xff];

			/* (the PPE's memcmp of the words) */
			if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3])
				break;
		}
		if (index == program->uniform_count)
			return 0;
	}
	memory = gpu_scratch(bytes, 64, &offset);
	if (!memory)
		return -1;
	for (index = 0; index < program->uniform_count; index++)
		memcpy(fragment.values[index], uniforms[program->uniforms[index] & 0xff], sizeof(fragment.values[0]));
	/* in pieces of whole quadwords, each uniform's words in order (rsx_fragment_program_set_uniform's) */
	for (done = 0; done < bytes; )
	{
		uint32_t piece = bytes - done < FRAGMENT_PIECE ? bytes - done : FRAGMENT_PIECE;
		uint32_t *words = (uint32_t *)fetch(inputs[0], program->microcode + done, piece, TAG_MISC);

		if ((uint32_t)words & 15)
		{
			memmove(inputs[0], words, piece);
			words = (uint32_t *)inputs[0];
		}
		for (index = 0; index < program->uniform_count; index++)
		{
			const uint32_t *value = (const uint32_t *)fragment.values[index];
			uint32_t word = program->uniforms[index] >> 8, component;

			for (component = 0; component < 4; component++)
			{
				uint32_t at = (word + component) * 4;

				if (at >= done && at < done + piece)
					words[(at - done) / 4] = (value[component] << 16) | (value[component] >> 16);
			}
		}
		dma_put(words, memory + done, (piece + 15) & ~15u, TAG_MISC);
		dma_wait(1 << TAG_MISC);
		done += piece;
	}
	rsx_fragment_program_location(offset, program->control, program->register_count, program->texcoord_mask, 0);
	fragment.program = program->index + 1;
	statistics.fragment_copies++;
	return 0;
}

static int texture_same(const gpu_texture *a, const gpu_texture *b)
{
	const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
	uint32_t index, differ = 0;

	for (index = 0; index < sizeof(*a) / 4; index++)
		differ |= x[index] ^ y[index];
	return !differ;
}

static void texture_unit_off(uint32_t unit)
{
	if (texture_states[unit] != 1)
	{
		rsx_texture_control(unit, 0, 0, 0, 0);
		texture_states[unit] = 1;
	}
}

static void gpu_set_texture(uint32_t unit, const gpu_texture *texture)
{
	if (unit >= TEXTURE_UNITS)
		return;
	if (!texture)
	{
		texture_unit_off(unit);
		return;
	}
	if (texture_states[unit] == 2 && texture_same(texture, &textures[unit]))
		return;
	textures[unit] = *texture;
	texture_states[unit] = 2;
	rsx_load_texture(unit, texture->offset, texture->pitch, texture->width, texture->height, texture->depth,
		texture->format, texture->levels, texture->dimension, texture->cube, texture->remap);
	rsx_texture_control(unit, 1, texture->minimum_level, texture->maximum_level, texture->anisotropy);
	rsx_texture_filter(unit, texture->bias, texture->minify, texture->magnify, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
	rsx_texture_wrap(unit, texture->wrap_s, texture->wrap_t, texture->wrap_r);
	rsx_texture_border_color(unit, texture->border);
	statistics.texture_loads++;
}

static void gpu_set_attribute(uint32_t index, uint32_t offset, uint32_t stride, uint32_t elements)
{
	if (index >= ATTRIBUTE_COUNT)
		return;
	if (!elements)
	{
		if (attribute_enabled[index])
		{
			rsx_vertex_attribute(index, 0, 0, 0);
			attribute_enabled[index] = 0;
		}
		return;
	}
	rsx_vertex_attribute(index, offset, stride, elements);
	attribute_enabled[index] = 1;
}

static void draw_begin(void)
{
	if (texture_cache_dirty)
	{
		rsx_texture_cache_invalidate(GCM_INVALIDATE_TEXTURE);
		texture_cache_dirty = 0;
	}
	if (vertex_cache_dirty)
	{
		rsx_vertex_cache_invalidate();
		vertex_cache_dirty = 0;
	}
	statistics.gpu_draws++;
	part_draws++;
}

static void gpu_draw(uint32_t primitive, uint32_t first, uint32_t count)
{
	if (!count)
		return;
	draw_begin();
	rsx_draw_vertex_array(primitive, first, count);
}

static void gpu_draw_indexed(uint32_t primitive, uint32_t offset, uint32_t count)
{
	if (!count)
		return;
	draw_begin();
	rsx_draw_index_array(primitive, offset, count);
}

static void gpu_timestamp(int part)
{
	if (stamps.count < TIMESTAMP_PARTS)
	{
		rsx_report(REPORT_TIMESTAMP, TIMESTAMP_REPORT_BASE + (frame_index % 3) * TIMESTAMP_PARTS + stamps.count);
		stamps.draws[stamps.count] = part_draws;
		stamps.parts[stamps.count++] = (uint16_t)part;
		part_draws = 0;
	}
}

static void gpu_draw_stamps(int on)
{
	draw_stamps.on = 0;
	if (!on || frame_index % DRAW_STAMP_FRAMES || (draw_stamps.count && draw_stamps.frame != frame_index) ||
		draw_stamps.count >= DRAW_STAMPS)
		return;
	draw_stamps.frame = frame_index;
	rsx_report(REPORT_TIMESTAMP, DRAW_STAMP_REPORT_BASE + draw_stamps.count);
	draw_stamps.keys[draw_stamps.count++] = DRAW_STAMP_START;
	draw_stamps.on = 1;
}

static uint32_t draw_stamps_dropped;

static void gpu_draw_stamp(uint32_t key)
{
	if (!draw_stamps.on)
		return;
	if (draw_stamps.count >= DRAW_STAMPS)
	{
		draw_stamps_dropped++;
		return;
	}
	rsx_report(REPORT_TIMESTAMP, DRAW_STAMP_REPORT_BASE + draw_stamps.count);
	draw_stamps.keys[draw_stamps.count++] = (uint16_t)key;
}

/* ---------- the device's state (d3d8_rsx.c's device, render_state, texture_state) */

static struct
{
	spu_d3d_surface_words targets[2];
	spu_d3d_viewport viewport;
	spu_d3d_stage_texture stages[TEXTURE_STAGES];
	uint32_t shader_constant_mode;
	/* the vertex shader objects' EAs */
	uint32_t vertex_shader;
	uint32_t program_slots[VERTEX_PROGRAM_SLOTS];
	uint32_t program_address;
	float constants[_rsx_vp_constant_count][4] __attribute__((aligned(16)));
	uint32_t constants_dirty_first;
	uint32_t constants_dirty_last;
	float viewport_scale[4];
	float viewport_offset[4];
	struct
	{
		uint32_t base;
		uint32_t stride;
	} streams[16];
	float attributes[VERTEX_ATTRIBUTE_COUNT][4] __attribute__((aligned(16)));
	uint32_t target_width;
	uint32_t target_height;
	float target_scale[2];
	uint32_t target_pixel_width;
	uint32_t target_pixel_height;
	float texture_scale[TEXTURE_STAGES][2];
	uint32_t frame;
} device;

static uint32_t render_state[D3DRS_MAX] __attribute__((aligned(16)));
static uint32_t texture_state[D3DTSS_MAXSTAGES][D3DTSS_MAX] __attribute__((aligned(16)));
static uint32_t draw_state_changes = ~0u;
static int32_t ui_offset;
static uint32_t screen_width;
static float screen_scale[2];
static uint32_t static_begin, static_end;
static float byte_floats[256] __attribute__((aligned(16)));
static int button_style;
static uint32_t texture_frame = 1;
static uint32_t texture_epoch;
static uint32_t render_target_generation;
static int draw_stamps_part = -1;

static void color_to_vec4(uint32_t color, float *out)
{
	out[0] = byte_floats[(color >> 16) & 0xff];
	out[1] = byte_floats[(color >> 8) & 0xff];
	out[2] = byte_floats[color & 0xff];
	out[3] = byte_floats[(color >> 24) & 0xff];
}

/* ---------- vertex constants */

static void constants_mark(uint32_t first, uint32_t last)
{
	if (device.constants_dirty_first > first)
		device.constants_dirty_first = first;
	if (device.constants_dirty_last < last)
		device.constants_dirty_last = last;
}

static void constants_store(uint32_t first, const void *data, uint32_t count)
{
	const uint32_t *values = data;
	uint32_t index;

	for (index = 0; index < count; index++, values += 4)
	{
		uint32_t *to = (uint32_t *)device.constants[first + index];

		/* (the PPE's memcmp: the words) */
		if (to[0] != values[0] || to[1] != values[1] || to[2] != values[2] || to[3] != values[3])
		{
			to[0] = values[0];
			to[1] = values[1];
			to[2] = values[2];
			to[3] = values[3];
			constants_mark(first + index, first + index);
		}
	}
}

static void constants_upload(void)
{
	uint32_t first = device.constants_dirty_first, last = device.constants_dirty_last;

	if (first > last)
		return;
	gpu_set_vertex_constants(first, last - first + 1, &device.constants[first][0]);
	device.constants_dirty_first = _rsx_vp_constant_count;
	device.constants_dirty_last = 0;
}

/* ---------- xgpu_texture_describe (xbox_texture_layout.c): the parts the SPU reads */

typedef struct
{
	uint32_t format;
	uint32_t width, height, depth, levels;
	int cube_map, linear, compressed;
	uint32_t pitch;
	uint32_t bytes;
} texture_description;

/* format_information's table: the texel's bytes (a DXT block's for DXT), linear, compressed; 0 bytes: unknown */
static const uint8_t format_table[0x42][3] =
{
	[0x00] = { 1, 0, 0 }, [0x01] = { 1, 0, 0 }, [0x02] = { 2, 0, 0 }, [0x03] = { 2, 0, 0 }, [0x04] = { 2, 0, 0 },
	[0x05] = { 2, 0, 0 }, [0x06] = { 4, 0, 0 }, [0x07] = { 4, 0, 0 }, [0x0b] = { 1, 0, 0 }, [0x0c] = { 8, 0, 1 },
	[0x0e] = { 16, 0, 1 }, [0x0f] = { 16, 0, 1 }, [0x10] = { 2, 1, 0 }, [0x11] = { 2, 1, 0 }, [0x12] = { 4, 1, 0 },
	[0x13] = { 1, 1, 0 }, [0x16] = { 2, 1, 0 }, [0x17] = { 2, 1, 0 }, [0x19] = { 1, 0, 0 }, [0x1a] = { 2, 0, 0 },
	[0x1b] = { 1, 1, 0 }, [0x1c] = { 2, 1, 0 }, [0x1d] = { 2, 1, 0 }, [0x1e] = { 4, 1, 0 }, [0x1f] = { 1, 1, 0 },
	[0x20] = { 2, 1, 0 }, [0x24] = { 2, 1, 0 }, [0x25] = { 2, 1, 0 }, [0x27] = { 2, 0, 0 }, [0x28] = { 2, 0, 0 },
	[0x29] = { 2, 0, 0 }, [0x2a] = { 4, 0, 0 }, [0x2b] = { 4, 0, 0 }, [0x2c] = { 2, 0, 0 }, [0x2d] = { 2, 0, 0 },
	[0x2e] = { 4, 1, 0 }, [0x2f] = { 4, 1, 0 }, [0x30] = { 2, 1, 0 }, [0x31] = { 2, 1, 0 }, [0x32] = { 2, 0, 0 },
	[0x33] = { 4, 0, 0 }, [0x35] = { 2, 1, 0 }, [0x36] = { 4, 1, 0 }, [0x37] = { 2, 1, 0 }, [0x38] = { 2, 0, 0 },
	[0x39] = { 2, 0, 0 }, [0x3a] = { 4, 0, 0 }, [0x3b] = { 4, 0, 0 }, [0x3c] = { 4, 0, 0 }, [0x3d] = { 2, 1, 0 },
	[0x3e] = { 2, 1, 0 }, [0x3f] = { 4, 1, 0 }, [0x40] = { 4, 1, 0 }, [0x41] = { 4, 1, 0 },
};

static void texture_describe(uint32_t format_word, uint32_t size_word, texture_description *d)
{
	uint32_t format = (format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT;
	uint32_t bytes = 4, linear = 0, compressed = 0;

	if (format < 0x42 && format_table[format][0])
	{
		bytes = format_table[format][0];
		linear = format_table[format][1];
		compressed = format_table[format][2];
	}
	memset(d, 0, sizeof(*d));
	d->format = format;
	d->bytes = bytes;
	d->cube_map = (format_word & D3DFORMAT_CUBEMAP) != 0;
	d->compressed = (int)compressed;
	if (size_word)
	{
		d->width = (size_word & D3DSIZE_WIDTH_MASK) + 1;
		d->height = ((size_word & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1;
		d->depth = 1;
		d->levels = 1;
		d->pitch = (((size_word & D3DSIZE_PITCH_MASK) >> D3DSIZE_PITCH_SHIFT) + 1) * D3DTEXTURE_PITCH_ALIGNMENT;
		d->linear = 1;
	}
	else
	{
		d->width = 1u << ((format_word & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT);
		d->height = 1u << ((format_word & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT);
		d->depth = 1u << ((format_word & D3DFORMAT_PSIZE_MASK) >> D3DFORMAT_PSIZE_SHIFT);
		d->levels = (format_word & D3DFORMAT_MIPMAP_MASK) >> D3DFORMAT_MIPMAP_SHIFT;
		if (!d->levels)
			d->levels = 1;
		d->linear = (int)linear;
		d->pitch = d->width * bytes;
	}
	if ((format_word & D3DFORMAT_DIMENSION_MASK) >> D3DFORMAT_DIMENSION_SHIFT != 3)
		d->depth = 1;
}

static uint32_t level_dimension(uint32_t base, uint32_t level)
{
	uint32_t value = base >> level;

	return value ? value : 1;
}

static uint32_t level_bytes(const texture_description *d, uint32_t level)
{
	uint32_t width = level_dimension(d->width, level), height = level_dimension(d->height, level);
	uint32_t depth = level_dimension(d->depth, level);

	if (d->compressed)
		return ((width + 3) / 4) * ((height + 3) / 4) * d->bytes * depth;
	if (d->linear)
		return d->pitch * height;
	return width * height * depth * d->bytes;
}

static uint32_t texture_face_size(const texture_description *d)
{
	uint32_t size = 0, level;

	for (level = 0; level < d->levels; level++)
		size += level_bytes(d, level);
	if (d->cube_map)
		size = (size + D3DTEXTURE_CUBEFACE_ALIGNMENT - 1) & ~(uint32_t)(D3DTEXTURE_CUBEFACE_ALIGNMENT - 1);
	return size;
}

/* ---------- the depth's range and the screen's constants */

static float depth_scale(void)
{
	if (device.targets[1].valid)
	{
		texture_description d;

		texture_describe(device.targets[1].format, device.targets[1].size, &d);
		if (d.format == D3DFMT_D16 || d.format == 0x30 /* LIN_D16 */ || d.format == D3DFMT_F16 ||
			d.format == D3DFMT_LIN_F16)
			return 65535.0f;
	}
	return 16777215.0f;
}

static void viewport_update_constants(void)
{
	float zscale = depth_scale();
	float width = f_from_u32(device.viewport.width), height = f_from_u32(device.viewport.height);

	device.viewport_scale[0] = f_mul(width, 0.5f);
	device.viewport_scale[1] = f_mul(-height, 0.5f);
	device.viewport_scale[2] = f_mul(zscale, f_sub(device.viewport.maximum_z, device.viewport.minimum_z));
	device.viewport_scale[3] = 0.0f;
	device.viewport_offset[0] = f_add(f_from_u32(device.viewport.x), f_mul(width, 0.5f));
	device.viewport_offset[1] = f_add(f_from_u32(device.viewport.y), f_mul(height, 0.5f));
	device.viewport_offset[2] = f_mul(zscale, device.viewport.minimum_z);
	device.viewport_offset[3] = 0.0f;
	if (!(device.shader_constant_mode & D3DSCM_NORESERVEDCONSTANTS))
	{
		constants_store(VERTEX_CONSTANT_BIAS - 38, device.viewport_scale, 1);
		constants_store(VERTEX_CONSTANT_BIAS - 37, device.viewport_offset, 1);
	}
}

static void screen_constants_update(void)
{
	float zscale = depth_scale();
	float range = f_sub(device.viewport.maximum_z, device.viewport.minimum_z);
	float width = f_from_u32(device.target_width), height = f_from_u32(device.target_height);
	float values[2][4] __attribute__((aligned(16)));

	int ranged = f_gt(range, word_float(DEPTH_RANGE_EPSILON));

	values[0][0] = f_div(2.0f, width);
	values[0][1] = f_div(-2.0f, height);
	values[0][2] = ranged ? f_div(2.0f, f_mul(zscale, range)) : 0.0f;
	values[0][3] = 0.0f;
	values[1][0] = f_sub(f_div(f_add(f_mul(2.0f, f_from_s32(ui_offset)), 1.0f), width), 1.0f);
	values[1][1] = f_sub(1.0f, f_div(1.0f, height));
	values[1][2] = ranged ? f_sub(f_div(f_mul(-2.0f, device.viewport.minimum_z), range), 1.0f) : 0.0f;
	values[1][3] = 0.0f;
	constants_store(_rsx_vp_constant_screen_scale, values, 2);
}

/* ---------- render targets: the PPE's RSX surfaces in place of the memory their data names */

typedef struct
{
	uint32_t data;
	uint32_t width;
	uint32_t height;
	uint32_t units_width;
	uint32_t units_height;
	float scale[2];
	uint32_t depth;
	uint32_t offset;
	uint32_t pitch;
	uint32_t last_rendered;
	uint32_t memory;
} render_target;

static render_target targets[SPU_D3D_TARGETS];
static uint32_t target_count;

static int format_is_depth(uint32_t format)
{
	return format == D3DFMT_D24S8 || format == D3DFMT_F24S8 || format == D3DFMT_D16 || format == D3DFMT_F16 ||
		format == D3DFMT_LIN_D24S8 || format == D3DFMT_LIN_F24S8 || format == D3DFMT_LIN_D16 || format == D3DFMT_LIN_F16;
}

/* d3d8_rsx.c's scaled_pixel: the float sum, toward zero, a negative with a fraction one less */
static int32_t scaled_pixel(float coordinate, float scale)
{
	float value = f_add(f_mul(coordinate, scale), 0.5f);
	int32_t pixel = f_to_s32(value);

	if (f_gt(f_from_s32(pixel), value))
		pixel--;
	return pixel;
}

/* the PPE's targets are found in its order (the newest first) */
static render_target *render_target_lookup(uint32_t data, uint32_t units_width, uint32_t units_height, uint32_t depth)
{
	int screen = units_width == screen_width && units_height == SCREEN_HEIGHT;
	float scale[2] = { 1.0f, 1.0f };
	uint32_t width, height, index;
	render_target *target;
	spu_d3d_response *answer;

	if (screen || (!depth && units_width == SECONDARY_TARGET_WIDTH && units_height == SECONDARY_TARGET_HEIGHT))
	{
		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
	}
	width = (uint32_t)scaled_pixel(f_from_u32(units_width), scale[0]);
	height = (uint32_t)scaled_pixel(f_from_u32(units_height), scale[1]);
	for (index = target_count; index-- > 0; )
	{
		target = &targets[index];
		if (target->data == data && target->width == width && target->height == height && target->depth == depth &&
			((target->data >> 12) ^ (target->data >> 20)) % 256 == ((data >> 12) ^ (data >> 20)) % 256)
			return target;
	}
	if (target_count == SPU_D3D_TARGETS)
		return NULL;
	request_block.words[0] = data;
	request_block.words[1] = width;
	request_block.words[2] = height;
	request_block.words[3] = depth;
	request_block.words[4] = screen && data;
	request_block.words[5] = screen && depth && data;
	answer = request(_spu_d3d_request_target, 6);
	if (!answer->status)
		return NULL;
	target = &targets[target_count++];
	memset(target, 0, sizeof(*target));
	target->data = data;
	target->width = width;
	target->height = height;
	target->units_width = units_width;
	target->units_height = units_height;
	target->scale[0] = scale[0];
	target->scale[1] = scale[1];
	target->depth = depth;
	target->offset = answer->words[0];
	target->pitch = answer->words[1];
	target->memory = answer->words[2];
	/* the PPE's ZCull, which this target's allocation may have bound */
	zcull_bound = (int)answer->words[3];
	zcull_offset = answer->words[4];
	zcull_width = answer->words[5];
	zcull_height = answer->words[6];
	if (answer->words[7])
		zcull_valid = 0;
	texture_epoch++;
	return target;
}

static render_target *render_target_get(const spu_d3d_surface_words *words, uint32_t depth)
{
	texture_description d;

	if (!words->valid || !words->data)
		return NULL;
	texture_describe(words->format, words->size, &d);
	if ((uint32_t)format_is_depth(d.format) != depth || !d.width || !d.height)
		return NULL;
	return render_target_lookup(words->data, d.width, d.height, depth);
}

/* the colour target last drawn with this data, for a texture over it */
static render_target *render_target_find(uint32_t data)
{
	render_target *best = NULL;
	uint32_t index;

	for (index = target_count; index-- > 0; )
	{
		render_target *target = &targets[index];

		if (target->data == data && data && !target->depth && (!best || target->last_rendered > best->last_rendered))
			best = target;
	}
	return best;
}

/* d3d8_rsx.c's particles (the Direct3D thread's part) */
static struct
{
	BOOL on, bound;
	spu_d3d_record_particles half;
	render_target *main_color, *main_depth;
	uint32_t width, height;
} particles;

/* d3d8_rsx.c's scaled (the Direct3D thread's part) */
static struct
{
	BOOL on;
	spu_d3d_record_scale view;
	float factor;
	render_target *color, *depth;
	uint32_t width, height;
	float scale[2];
} scaled;

static struct
{
	render_target *color, *depth;
} targets_bound;

/* d3d8_rsx.c's target_surface */
static BOOL target_surface(render_target *color, render_target *depth, gpu_surface *next)
{
	BOOL smaller = scaled.on && color->data == scaled.view.data && !color->depth;

	next->depth_offset = depth->offset;
	next->depth_pitch = depth->pitch;
	next->smaller = (uint32_t)smaller;
	if (!smaller)
	{
		next->color_offset = color->offset;
		next->color_pitch = color->pitch;
		next->width = (uint16_t)color->width;
		next->height = (uint16_t)color->height;
		return FALSE;
	}
	if (!scaled.width)
	{
		scaled.scale[0] = f_mul(color->scale[0], scaled.factor);
		scaled.scale[1] = f_mul(color->scale[1], scaled.factor);
		scaled.width = (uint32_t)scaled_pixel(f_from_u32(color->units_width), scaled.scale[0]);
		scaled.height = (uint32_t)scaled_pixel(f_from_u32(color->units_height), scaled.scale[1]);
		scaled.color = color;
		scaled.depth = depth;
	}
	next->color_offset = scaled.view.color_offset;
	next->color_pitch = scaled.view.color_pitch;
	next->width = (uint16_t)scaled.width;
	next->height = (uint16_t)scaled.height;
	return TRUE;
}

static BOOL bind_targets(BOOL *has_depth)
{
	static struct
	{
		spu_d3d_surface_words targets[2];
		uint32_t generation;
		render_target *color, *depth;
		BOOL has_depth, valid;
	} last;
	render_target *color, *depth;
	gpu_surface next;
	float scale[2];

	if (last.valid && last.generation == render_target_generation &&
		!memcmp(last.targets, device.targets, sizeof(last.targets)))
	{
		color = last.color;
		depth = last.depth;
		*has_depth = last.has_depth;
		goto bind;
	}
	last.valid = FALSE;
	color = render_target_get(&device.targets[0], 0);
	depth = render_target_get(&device.targets[1], 1);
	if (!color && !depth)
		return FALSE;
	if (!color)
		color = render_target_lookup(0, depth->units_width, depth->units_height, 0);
	*has_depth = depth && color && depth->width >= color->width && depth->height >= color->height;
	if (!*has_depth && color)
		depth = render_target_lookup(0, color->units_width, color->units_height, 1);
	if (!color || !depth)
		return FALSE;
	memcpy(last.targets, device.targets, sizeof(last.targets));
	last.generation = render_target_generation;
	last.color = color;
	last.depth = depth;
	last.has_depth = *has_depth;
	last.valid = TRUE;

bind:
	if (color->last_rendered != device.frame + 1)
	{
		color->last_rendered = device.frame + 1;
		texture_epoch++;
	}
	targets_bound.color = color;
	targets_bound.depth = depth;
	particles.bound = particles.on && color == particles.main_color && depth == particles.main_depth;
	if (target_surface(color, depth, &next))
	{
		scale[0] = scaled.scale[0];
		scale[1] = scaled.scale[1];
	}
	else
	{
		scale[0] = color->scale[0];
		scale[1] = color->scale[1];
	}
	if (particles.bound)
	{
		next.color_offset = particles.half.color_offset;
		next.color_pitch = particles.half.color_pitch;
		next.depth_offset = particles.half.depth_offset;
		next.depth_pitch = particles.half.depth_pitch;
		next.width = (uint16_t)particles.width;
		next.height = (uint16_t)particles.height;
		next.smaller = 0;
		gpu_set_surface(&next);
		device.target_width = color->units_width;
		device.target_height = color->units_height;
		device.target_scale[0] = f_mul(scale[0], 0.5f);
		device.target_scale[1] = f_mul(scale[1], 0.5f);
		device.target_pixel_width = particles.width;
		device.target_pixel_height = particles.height;
		return TRUE;
	}
	gpu_set_surface(&next);
	device.target_width = color->units_width;
	device.target_height = color->units_height;
	device.target_scale[0] = scale[0];
	device.target_scale[1] = scale[1];
	device.target_pixel_width = next.width;
	device.target_pixel_height = next.height;
	return TRUE;
}

/* ---------- textures: the table (main memory), its LS index and a few entries in LS */

#define TEXTURE_BUCKETS 1024
#define TEXTURE_CACHED 32
#define NO_ENTRY 0xffff

static uint16_t texture_buckets[TEXTURE_BUCKETS];
static uint16_t texture_next[SPU_D3D_TEXTURES];
static uint16_t texture_tags[SPU_D3D_TEXTURES];
static uint16_t texture_free;
static uint32_t textures_in_use;
static spu_d3d_texture texture_cache[TEXTURE_CACHED] __attribute__((aligned(128)));
static uint16_t texture_cache_index[TEXTURE_CACHED];
static uint8_t texture_cache_dirty_flags[TEXTURE_CACHED];

static uint32_t texture_key_hash(uint32_t data, uint32_t format_word, uint32_t size_word, uint32_t palette_hash)
{
	uint32_t hash = (data >> 7) ^ (format_word * 2654435761u) ^ size_word ^ (palette_hash * 0x9e3779b9u);

	return hash ^ (hash >> 16);
}

static void texture_entry_write(uint32_t slot)
{
	dma_put(&texture_cache[slot], setup.textures + texture_cache_index[slot] * sizeof(spu_d3d_texture),
		sizeof(spu_d3d_texture), TAG_TABLE);
	texture_cache_dirty_flags[slot] = 0;
}

/* entry index in LS (written back when it leaves) */
static spu_d3d_texture *texture_entry(uint32_t index)
{
	uint32_t slot = index % TEXTURE_CACHED;

	if (texture_cache_index[slot] == index)
		return &texture_cache[slot];
	if (texture_cache_index[slot] != NO_ENTRY && texture_cache_dirty_flags[slot])
	{
		texture_entry_write(slot);
		dma_wait(1 << TAG_TABLE);
	}
	dma_get(&texture_cache[slot], setup.textures + index * sizeof(spu_d3d_texture), sizeof(spu_d3d_texture), TAG_TABLE);
	dma_wait(1 << TAG_TABLE);
	texture_cache_index[slot] = (uint16_t)index;
	texture_cache_dirty_flags[slot] = 0;
	return &texture_cache[slot];
}

static void texture_entry_dirty(uint32_t index)
{
	texture_cache_dirty_flags[index % TEXTURE_CACHED] = 1;
}

/* every changed entry back in main memory (before the PPE reads the table) */
static void textures_write_back(void)
{
	uint32_t slot;

	for (slot = 0; slot < TEXTURE_CACHED; slot++)
	{
		if (texture_cache_index[slot] != NO_ENTRY && texture_cache_dirty_flags[slot])
			texture_entry_write(slot);
	}
	dma_wait(1 << TAG_TABLE);
}

static void textures_reset(void)
{
	uint32_t index;

	memset(texture_buckets, 0xff, sizeof(texture_buckets));
	for (index = 0; index < SPU_D3D_TEXTURES; index++)
		texture_next[index] = (uint16_t)(index + 1 < SPU_D3D_TEXTURES ? index + 1 : NO_ENTRY);
	texture_free = 0;
	memset(texture_cache_index, 0xff, sizeof(texture_cache_index));
	textures_in_use = 0;
}

static uint32_t texture_hashed_bytes;

/* x * the FNV prime, each lane: x << 24 plus x * 0x193 from its halves' products (mod 2^32) */
static inline vec_uint4 times_prime(vec_uint4 x)
{
	static const vec_ushort8 low = { 0, 0x193, 0, 0x193, 0, 0x193, 0, 0x193 };

	return spu_add(spu_sl(x, 24), spu_add(spu_mulo((vec_ushort8)x, low), (vec_uint4)spu_mulh((vec_short8)x,
		(vec_short8)low)));
}

/* a piece of texture_hash's stream (4 KB, the last shorter) after its input buffer's first 16 bytes, with the line
before it (back 16: the shuffle's first line); the last with the lines up to the end (the last bytes) */
static inline void texture_hash_fetch(uint32_t stream, uint32_t end, uint32_t back, uint32_t piece, uint32_t pieces)
{
	uint32_t first = stream + piece * 4096, bytes = piece + 1 == pieces ? end - first : 4096;

	mfc_get(inputs[piece & 1] + 16 - back, first - back, bytes + back, TAG_INPUT + (piece & 1), 0, 0);
}

/* xgpu_texture_hash (spu_vertices.c's hash_task): 16 lanes of FNV-1a on the words in order, the last bytes into lane
0; 64 bytes at a time at any alignment, 4 KB fetches (the first line and last bytes in them) while one is hashed */
static uint64_t texture_hash(uint32_t source, uint32_t size)
{
	static const vec_uchar16 in_order = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	uint32_t words[TEXTURE_HASH_LANES], index, done = 64 * (size / 64), word_index = done / 4, piece;
	uint32_t pieces = (done + 4095) / 4096, rest = size - done, offset = source & 15, back = offset ? 16 : 0;
	uint32_t stream = offset ? (source & ~15u) + 16 : source, end = (source + size + 15) & ~15u;
	/* (bytes 0..15 plus the offset: no carry between them) */
	vec_uchar16 pattern = (vec_uchar16)spu_add((vec_ushort8)in_order, spu_splats((unsigned short)(offset * 0x0101)));
	vec_uint4 vector_lanes[4];
	const unsigned char *tail = inputs[0] + 16 + offset;

	for (index = 0; index < 4; index++)
	{
		vec_uint4 lane = { 4 * index, 4 * index + 1, 4 * index + 2, 4 * index + 3 };

		vector_lanes[index] = spu_xor(spu_splats(TEXTURE_HASH_BASIS), lane);
	}
	/* (under 64 bytes: their lines alone) */
	if (!pieces && rest)
		mfc_get(inputs[0] + 16, source & ~15u, end - (source & ~15u), TAG_INPUT, 0, 0);
	for (piece = 0; piece < 2 && piece < pieces; piece++)
		texture_hash_fetch(stream, end, back, piece, pieces);
	/* (a loop for each alignment: no branch between the pieces of one) */
	for (piece = 0; !offset && piece < pieces; piece++)
	{
		uint32_t groups = (done - piece * 4096 < 4096 ? done - piece * 4096 : 4096) / 64, group;
		const vec_uint4 *quads = (const vec_uint4 *)(inputs[piece & 1] + 16);

		dma_wait(1 << (TAG_INPUT + (piece & 1)));
		for (group = 0; group < groups; group++, quads += 4)
		{
			vector_lanes[0] = times_prime(spu_xor(vector_lanes[0], quads[0]));
			vector_lanes[1] = times_prime(spu_xor(vector_lanes[1], quads[1]));
			vector_lanes[2] = times_prime(spu_xor(vector_lanes[2], quads[2]));
			vector_lanes[3] = times_prime(spu_xor(vector_lanes[3], quads[3]));
		}
		if (__builtin_expect(piece + 2 < pieces, 1))
			texture_hash_fetch(stream, end, back, piece + 2, pieces);
	}
	for (piece = 0; offset && piece < pieces; piece++)
	{
		uint32_t groups = (done - piece * 4096 < 4096 ? done - piece * 4096 : 4096) / 64, group;
		const vec_uint4 *quads = (const vec_uint4 *)(inputs[piece & 1] + 16);
		vec_uint4 carry;

		dma_wait(1 << (TAG_INPUT + (piece & 1)));
		carry = quads[-1];
		for (group = 0; group < groups; group++, quads += 4)
		{
			vec_uint4 next0 = quads[0], next1 = quads[1], next2 = quads[2], next3 = quads[3];

			vector_lanes[0] = times_prime(spu_xor(vector_lanes[0], spu_shuffle(carry, next0, pattern)));
			vector_lanes[1] = times_prime(spu_xor(vector_lanes[1], spu_shuffle(next0, next1, pattern)));
			vector_lanes[2] = times_prime(spu_xor(vector_lanes[2], spu_shuffle(next1, next2, pattern)));
			vector_lanes[3] = times_prime(spu_xor(vector_lanes[3], spu_shuffle(next2, next3, pattern)));
			carry = next3;
		}
		if (__builtin_expect(piece + 2 < pieces, 1))
			texture_hash_fetch(stream, end, back, piece + 2, pieces);
	}
	for (index = 0; index < 4; index++)
	{
		words[4 * index] = spu_extract(vector_lanes[index], 0);
		words[4 * index + 1] = spu_extract(vector_lanes[index], 1);
		words[4 * index + 2] = spu_extract(vector_lanes[index], 2);
		words[4 * index + 3] = spu_extract(vector_lanes[index], 3);
	}
	/* the last bytes: in the last piece's buffer, after its stream's (or alone) */
	if (pieces)
		tail = inputs[(pieces - 1) & 1] + 16 + (source + done - (stream + (pieces - 1) * 4096));
	else if (rest)
		dma_wait(1 << TAG_INPUT);
	for (index = 0; index + 4 <= rest; index += 4, word_index++)
	{
		uint32_t word = (uint32_t)tail[index] << 24 | (uint32_t)tail[index + 1] << 16 | (uint32_t)tail[index + 2] << 8 |
			tail[index + 3];

		words[word_index & 15] = (words[word_index & 15] ^ word) * TEXTURE_HASH_PRIME;
	}
	for (; index < rest; index++)
		words[0] = (words[0] ^ tail[index]) * TEXTURE_HASH_PRIME;
	texture_hashed_bytes += size;
	return texture_hash_finish(words, size);
}

/* the button icons' placeholders (hud_hires_rsx.c), read at each frame; each style's texture of an asset, as made */
static struct
{
	uint32_t data;
	int32_t asset;
	uint32_t pad[2];
} placeholders[8] __attribute__((aligned(16)));
#define PLACEHOLDER_ASSETS 16
static struct
{
	uint8_t state[2];
	uint32_t gpu[2][10];
} placeholder_textures[PLACEHOLDER_ASSETS];

static int placeholder_texture(int32_t asset, int style, uint32_t *gpu)
{
	spu_d3d_response *answer;
	int index;

	if (asset < 0)
		return 0;
	if (asset < PLACEHOLDER_ASSETS && placeholder_textures[asset].state[style - 1])
	{
		if (placeholder_textures[asset].state[style - 1] == 1)
			memcpy(gpu, placeholder_textures[asset].gpu[style - 1], 40);
		return placeholder_textures[asset].state[style - 1] == 1;
	}
	request_block.words[0] = (uint32_t)asset;
	request_block.words[1] = (uint32_t)style;
	answer = request(_spu_d3d_request_placeholder, 2);
	if (answer->words[10])
		texture_cache_dirty = 1;
	if (asset < PLACEHOLDER_ASSETS)
	{
		placeholder_textures[asset].state[style - 1] = answer->status ? 1 : 2;
		for (index = 0; index < 10; index++)
			placeholder_textures[asset].gpu[style - 1][index] = answer->words[index];
	}
	if (answer->status)
		memcpy(gpu, answer->words, 40);
	return answer->status != 0;
}

/* hud_hires_rsx_placeholder: the first placeholder with this data */
static int placeholder_check(uint32_t data, uint32_t *gpu)
{
	int style = button_style, index;

	for (index = 0; data && style > 0 && index < 8; index++)
	{
		if (placeholders[index].data == data)
			return placeholder_texture(placeholders[index].asset, style, gpu);
	}
	return 0;
}

/* texture_check, the PPE's part a request: once a frame, or fresh */
static spu_d3d_texture *texture_check(uint32_t index, spu_d3d_texture *entry, BOOL fresh, uint32_t palette,
	uint32_t palette_hash)
{
	int style = button_style;
	BOOL cached, changed;
	uint32_t span;

	entry->checked_frame = texture_frame;
	entry->placeholder = (uint8_t)placeholder_check(entry->data, entry->hires_gpu);
	if (entry->placeholder)
		return entry;
	if (!entry->size)
		return entry->memory ? entry : NULL;
	cached = entry->cached;
	if (cached && entry->memory && entry->checked && !fresh && entry->hires_style == style)
		return entry;
	span = timer_now();
	if (cached)
	{
		changed = !entry->memory || !entry->checked || fresh;
	}
	else
	{
		uint64_t hash = texture_hash(entry->source, entry->size) ^ palette_hash;

		changed = !entry->memory || hash != entry->content_hash;
		entry->content_hash = hash;
	}
	span = timer_add(_timer_texture_hash, span);
	if (changed || (entry->memory && entry->hires_style != style))
	{
		spu_d3d_response *answer;

		/* the PPE uploads (texture_upload) and finds the hires bitmap, in the table's entry */
		texture_entry_dirty(index);
		textures_write_back();
		request_block.words[0] = index;
		request_block.words[1] = changed;
		request_block.words[2] = palette;
		request_block.words[3] = (uint32_t)style;
		answer = request(_spu_d3d_request_texture, 4);
		dma_get(entry, setup.textures + index * sizeof(spu_d3d_texture), sizeof(spu_d3d_texture), TAG_TABLE);
		dma_wait(1 << TAG_TABLE);
		/* (an upload moves texture_epoch; texels written: ps3_gpu_textures_written) */
		if (answer->words[0])
			texture_epoch++;
		if (answer->words[1])
			texture_cache_dirty = 1;
		if (changed)
			timer_add(_timer_texture_upload, span);
	}
	entry->checked = cached && entry->memory;
	return entry->memory ? entry : NULL;
}

static spu_d3d_texture *texture_get(spu_d3d_stage_texture *texture, uint32_t palette, uint32_t palette_hash)
{
	uint32_t data = texture->data, format_word = texture->format, size_word = texture->size;
	uint32_t hash = texture_key_hash(data, format_word, size_word, palette_hash), index, placeholder, hires;
	uint32_t hires_gpu[10];
	BOOL fresh = texture->fresh != 0;
	spu_d3d_texture *entry = NULL, *result;

	for (index = texture_buckets[hash % TEXTURE_BUCKETS]; index != NO_ENTRY; index = texture_next[index])
	{
		if (texture_tags[index] != (uint16_t)(hash >> 10))
			continue;
		entry = texture_entry(index);
		if (entry->data == data && entry->format_word == format_word && entry->size_word == size_word &&
			entry->palette_hash == palette_hash)
			break;
		entry = NULL;
	}
	if (index == NO_ENTRY)
	{
		texture_description d;
		uint32_t source = data | 0x80000000u, size;

		if (texture_free == NO_ENTRY)
			return NULL;
		index = texture_free;
		texture_free = texture_next[index];
		texture_next[index] = texture_buckets[hash % TEXTURE_BUCKETS];
		texture_buckets[hash % TEXTURE_BUCKETS] = (uint16_t)index;
		texture_tags[index] = (uint16_t)(hash >> 10);
		textures_in_use++;
		entry = texture_entry(index);
		memset(entry, 0, sizeof(*entry));
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		entry->palette_hash = palette_hash;
		entry->valid = 1;
		texture_describe(format_word, size_word, &d);
		size = texture_face_size(&d) * (d.cube_map ? 6 : 1);
		/* (the texels the check reads: in the Xbox window, else none) */
		if (size && source >= 0x80000000u && source - 0x80000000u < 0x08000000u &&
			source + size - 1 >= 0x80000000u && source + size - 1 - 0x80000000u < 0x08000000u)
		{
			entry->source = source;
			entry->size = size;
		}
		entry->cached = setup.texture_cache && source >= setup.texture_cache &&
			source + size <= setup.texture_cache + setup.texture_cache_bytes;
		entry->hires_candidate = !d.linear && !d.cube_map && d.depth == 1;
	}
	entry->last_used_frame = texture_frame;
	texture_entry_dirty(index);
	texture->fresh = 0;
	if (entry->checked_frame == texture_frame && !fresh)
		return entry->memory || entry->placeholder ? entry : NULL;
	placeholder = entry->placeholder;
	hires = entry->hires;
	memcpy(hires_gpu, entry->hires_gpu, sizeof(hires_gpu));
	result = texture_check(index, entry, fresh, palette, palette_hash);
	texture_entry_dirty(index);
	if (entry->placeholder != placeholder || entry->hires != hires || memcmp(entry->hires_gpu, hires_gpu, 40))
		texture_epoch++;
	return result;
}

/* the textures not used for a while go: their RSX memory freed by the PPE */
static void textures_begin_frame(void)
{
	uint32_t bucket, frees = 0;

	texture_frame++;
	texture_epoch++;
	if (texture_frame % 600)
		return;
	textures_write_back();
	for (bucket = 0; bucket < TEXTURE_BUCKETS; bucket++)
	{
		uint16_t *link = &texture_buckets[bucket];

		while (*link != NO_ENTRY)
		{
			uint32_t index = *link;
			spu_d3d_texture *entry = texture_entry(index);

			if (texture_frame - entry->last_used_frame > TEXTURE_IDLE_FRAMES)
			{
				*link = texture_next[index];
				if (entry->memory)
				{
					request_block.words[2 * frees] = entry->memory;
					request_block.words[2 * frees + 1] = entry->bytes;
					if (++frees == SPU_D3D_REQUEST_WORDS / 2)
					{
						request_block.words[SPU_D3D_REQUEST_WORDS - 1] = frees;
						request(_spu_d3d_request_free, SPU_D3D_REQUEST_WORDS);
						frees = 0;
					}
				}
				memset(entry, 0, sizeof(*entry));
				texture_cache_dirty_flags[index % TEXTURE_CACHED] = 0;
				texture_cache_index[index % TEXTURE_CACHED] = NO_ENTRY;
				texture_next[index] = texture_free;
				texture_free = (uint16_t)index;
				textures_in_use--;
			}
			else
			{
				link = &texture_next[index];
			}
		}
	}
	if (frees)
	{
		request_block.words[SPU_D3D_REQUEST_WORDS - 1] = frees;
		request(_spu_d3d_request_free, SPU_D3D_REQUEST_WORDS);
	}
}

/* the RSX copies, and the part that no draw used in the last STATISTICS_FRAMES frames */
static void textures_measure(void)
{
	uint32_t bucket, count = 0, bytes = 0, unused = 0;

	textures_write_back();
	for (bucket = 0; bucket < TEXTURE_BUCKETS; bucket++)
	{
		uint32_t index;

		for (index = texture_buckets[bucket]; index != NO_ENTRY; index = texture_next[index])
		{
			spu_d3d_texture *entry = texture_entry(index);

			if (!entry->memory)
				continue;
			count++;
			bytes += entry->bytes;
			if (texture_frame - entry->last_used_frame > STATISTICS_FRAMES)
				unused += entry->bytes;
		}
	}
	statistics.textures = count;
	statistics.texture_bytes = bytes;
	statistics.texture_unused_bytes = unused;
}

/* ---------- texture stages */

static uint32_t stage_texture_mode(int stage)
{
	return (render_state[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f;
}

static uint32_t address_mode(uint32_t mode)
{
	switch (mode)
	{
	case D3DTADDRESS_MIRROR: return GCM_TEXTURE_MIRRORED_REPEAT;
	case D3DTADDRESS_CLAMP: return GCM_TEXTURE_CLAMP_TO_EDGE;
	case D3DTADDRESS_BORDER: return GCM_TEXTURE_BORDER;
	case D3DTADDRESS_CLAMPTOEDGE: return GCM_TEXTURE_CLAMP_TO_EDGE;
	default: return GCM_TEXTURE_REPEAT;
	}
}

static uint32_t anisotropy_level(uint32_t maximum)
{
	return maximum >= 16 ? 7 : maximum >= 12 ? 6 : maximum >= 10 ? 5 : maximum >= 8 ? 4 : maximum >= 6 ? 3 :
		maximum >= 4 ? 2 : maximum >= 2 ? 1 : 0;
}

static void sampler_fill(int stage, gpu_texture *texture)
{
	uint32_t *s = texture_state[stage];
	BOOL mipmapped = texture->levels > 1 && !(texture->format & GCM_TEXTURE_FORMAT_UNRM);
	uint32_t mip_filter = mipmapped ? s[D3DTSS_MIPFILTER] : D3DTEXF_NONE;
	BOOL point = s[D3DTSS_MINFILTER] == D3DTEXF_POINT;
	int32_t bias = f_to_s32(f_mul(word_float(s[D3DTSS_MIPMAPLODBIAS]), 256.0f));
	uint32_t top = texture->levels ? texture->levels - 1u : 0;
	uint32_t lowest = s[D3DTSS_MAXMIPLEVEL] < top ? s[D3DTSS_MAXMIPLEVEL] : top;

	if (mip_filter == D3DTEXF_NONE)
		texture->minify = point ? GCM_TEXTURE_NEAREST : GCM_TEXTURE_LINEAR;
	else if (mip_filter == D3DTEXF_POINT)
		texture->minify = point ? GCM_TEXTURE_NEAREST_MIPMAP_NEAREST : GCM_TEXTURE_LINEAR_MIPMAP_NEAREST;
	else
		texture->minify = point ? GCM_TEXTURE_NEAREST_MIPMAP_LINEAR : GCM_TEXTURE_LINEAR_MIPMAP_LINEAR;
	texture->magnify = s[D3DTSS_MAGFILTER] == D3DTEXF_POINT ? GCM_TEXTURE_NEAREST : GCM_TEXTURE_LINEAR;
	texture->wrap_s = (uint8_t)address_mode(s[D3DTSS_ADDRESSU]);
	texture->wrap_t = (uint8_t)address_mode(s[D3DTSS_ADDRESSV]);
	texture->wrap_r = (uint8_t)address_mode(s[D3DTSS_ADDRESSW]);
	texture->anisotropy = s[D3DTSS_MINFILTER] == D3DTEXF_ANISOTROPIC ?
		(uint8_t)anisotropy_level(s[D3DTSS_MAXANISOTROPY]) : 0;
	texture->bias = (uint16_t)((bias < -4096 ? -4096 : bias > 4095 ? 4095 : bias) & 0x1fff);
	texture->minimum_level = (uint16_t)(mipmapped ? lowest << 8 : 0);
	texture->maximum_level = (uint16_t)(mipmapped ? top << 8 : 0);
	texture->border = s[D3DTSS_BORDERCOLOR];
}

/* a stage's palette as last read: its data, its words, their hash (palette_load's) */
static struct
{
	uint32_t data;
	uint32_t hash;
	uint32_t words[256] __attribute__((aligned(16)));
} palettes[TEXTURE_STAGES];

static BOOL palette_same(int stage)
{
	uint32_t data = device.stages[stage].palette, index, differ = 0;
	const uint32_t *words;

	if (!data || palettes[stage].data != data || !palettes[stage].hash)
		return FALSE;
	words = (const uint32_t *)fetch(inputs[1], data | 0x80000000u, 1024, TAG_MISC);
	for (index = 0; index < 256; index++)
		differ |= words[index] ^ palettes[stage].words[index];
	return !differ;
}

/* the palette's hash (0 for none), its entries' FNV over the swapped words */
static uint32_t palette_load(int stage)
{
	uint32_t data = device.stages[stage].palette, hash = 2166136261u, index;

	if (!data)
		return 0;
	if (!palette_same(stage))
	{
		const uint32_t *words = (const uint32_t *)fetch(inputs[1], data | 0x80000000u, 1024, TAG_MISC);

		memcpy(palettes[stage].words, words, sizeof(palettes[stage].words));
		for (index = 0; index < 256; index++)
		{
			uint32_t word = palettes[stage].words[index];
			uint32_t entry = (word >> 24) | ((word >> 8) & 0xff00) | ((word << 8) & 0xff0000) | (word << 24);

			hash = (hash ^ entry) * 16777619u;
		}
		palettes[stage].data = data;
		palettes[stage].hash = hash ? hash : 1;
	}
	return palettes[stage].hash;
}

typedef struct
{
	uint8_t types[TEXTURE_STAGES];
	uint8_t scaled;
} texture_samplers;

static gpu_texture bound_textures[TEXTURE_STAGES];
static uint8_t bound_texture_mask;

static struct
{
	gpu_texture bound;
	BOOL kept, on, palette;
	uint32_t epoch;
	uint8_t sampler_type, scaled;
} stage_binds[TEXTURE_STAGES];
static uint32_t binds_gpu_epoch;
static int binds_style;

static void stage_bind(int stage)
{
	spu_d3d_stage_texture *texture = &device.stages[stage];
	uint32_t mode = stage_texture_mode(stage);
	gpu_texture *bound = &stage_binds[stage].bound;
	render_target *target;
	BOOL hires = FALSE;

	stage_binds[stage].on = FALSE;
	stage_binds[stage].palette = FALSE;
	stage_binds[stage].scaled = 0;
	stage_binds[stage].sampler_type = mode == 0x11 ? _rsx_sampler_2d : _rsx_sampler_none;
	device.texture_scale[stage][0] = device.texture_scale[stage][1] = 1.0f;
	if (!texture->bound || !texture->data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
	{
		gpu_set_texture((uint32_t)stage, NULL);
		return;
	}
	memset(bound, 0, sizeof(*bound));
	target = render_target_find(texture->data);
	if (target)
	{
		texture_description d;
		BOOL smaller = scaled.on && scaled.width && target == scaled.color;
		const float *scale = smaller ? scaled.scale : target->scale;

		texture_describe(texture->format, texture->size, &d);
		bound->offset = smaller ? scaled.view.color_offset : target->offset;
		bound->pitch = smaller ? scaled.view.color_pitch : target->pitch;
		bound->width = (uint16_t)(smaller ? scaled.width : target->width);
		bound->height = (uint16_t)(smaller ? scaled.height : target->height);
		bound->depth = 1;
		bound->format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN | (d.linear ? GCM_TEXTURE_FORMAT_UNRM : 0);
		bound->levels = 1;
		bound->dimension = GCM_TEXTURE_DIMS_2D;
		if (d.linear && (!f_eq(scale[0], 1.0f) || !f_eq(scale[1], 1.0f)))
		{
			stage_binds[stage].scaled = (uint8_t)(1 << stage);
			device.texture_scale[stage][0] = scale[0];
			device.texture_scale[stage][1] = scale[1];
		}
	}
	else
	{
		uint32_t palette = 0, palette_hash = 0;
		spu_d3d_texture *entry;

		if (((texture->format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == 0x0b)
		{
			palette_hash = palette_load(stage);
			stage_binds[stage].palette = palette_hash != 0;
			if (palette_hash)
				palette = texture->palette;
		}
		entry = texture_get(texture, palette, palette_hash);
		if (!entry)
		{
			gpu_set_texture((uint32_t)stage, NULL);
			return;
		}
		hires = entry->hires || entry->placeholder;
		memcpy(bound, hires ? entry->hires_gpu : entry->gpu, sizeof(*bound));
	}
	sampler_fill(stage, bound);
	if (hires)
	{
		bound->minify = GCM_TEXTURE_LINEAR_MIPMAP_LINEAR;
		bound->magnify = GCM_TEXTURE_LINEAR;
		bound->minimum_level = 0;
		bound->maximum_level = (uint16_t)((bound->levels - 1) << 8);
	}
	gpu_set_texture((uint32_t)stage, bound);
	stage_binds[stage].sampler_type = bound->cube ? _rsx_sampler_cube :
		bound->dimension == GCM_TEXTURE_DIMS_3D ? _rsx_sampler_3d : _rsx_sampler_2d;
	stage_binds[stage].on = TRUE;
}

static void bind_textures(texture_samplers *samplers)
{
	uint32_t changes = draw_state_changes, gpu_epoch = caches_epoch;
	int style = button_style, stage;

	if (gpu_epoch != binds_gpu_epoch || style != binds_style)
	{
		binds_gpu_epoch = gpu_epoch;
		binds_style = style;
		texture_epoch++;
	}
	samplers->scaled = 0;
	bound_texture_mask = 0;
	for (stage = 0; stage < TEXTURE_STAGES; stage++)
	{
		if (!stage_binds[stage].kept || stage_binds[stage].epoch != texture_epoch ||
			(changes & (CHANGED_TEXTURE_MODES | CHANGED_SAMPLER(stage) | CHANGED_TEXTURE(stage))) ||
			(stage_binds[stage].palette && !palette_same(stage)))
		{
			stage_bind(stage);
			stage_binds[stage].epoch = texture_epoch;
			stage_binds[stage].kept = TRUE;
			statistics.counts[_timer_draw_textures]++;
		}
		samplers->types[stage] = stage_binds[stage].sampler_type;
		samplers->scaled |= stage_binds[stage].scaled;
		if (draw_stamps.on && draw_stamps_part >= 0 && stage_binds[stage].on)
		{
			bound_textures[stage] = stage_binds[stage].bound;
			bound_texture_mask |= (uint8_t)(1 << stage);
		}
	}
	draw_state_changes &= ~CHANGED_TEXTURES;
}

/* ---------- fragment programs: the PPE's table, a few in LS */

/* ps3_rsx_fp.c's texture modes and the fog input (rsx_fragment_key_clean's) */
enum
{
	_mode_none = 0x00,
	_mode_passthru = 0x04,
	_mode_clipplane = 0x05,
	_mode_dependent_ar_unused,
	_mode_brdf = 0x08,
	_mode_dot_st = 0x09,
	_mode_dot_zw = 0x0a,
	_mode_dot_reflect_diffuse = 0x0b,
	_mode_dot_str_cube = 0x0e,
	_mode_dependent_ar = 0x0f,
	_mode_dependent_gb = 0x10,
	_mode_dot_product = 0x11,
	_mode_dot_reflect_specular_constant = 0x12,
};
#define INPUT_FOG 3

static int stage_mode(const struct rsx_fragment_key *key, int stage)
{
	return (int)((key->texture_modes >> (5 * stage)) & 0x1f);
}

static int stage_active(const struct rsx_fragment_key *key, int stage)
{
	int mode = stage_mode(key, stage);

	if (mode == _mode_passthru || mode == _mode_clipplane || mode == _mode_dot_product || mode == _mode_dot_zw)
		return 1;
	return mode != _mode_none && mode != _mode_brdf && mode <= _mode_dot_reflect_specular_constant &&
		key->sampler_type[stage] != _rsx_sampler_none;
}

static int stage_dot_mode(int mode)
{
	return mode == _mode_dot_product || (mode >= _mode_dot_st && mode <= _mode_dot_str_cube) ||
		mode == _mode_dot_reflect_specular_constant;
}

static int reads_fog(const struct rsx_fragment_key *key, uint32_t combiner_count)
{
	uint32_t stage, shift;

	for (stage = 0; stage < combiner_count; stage++)
	{
		for (shift = 0; shift < 32; shift += 8)
		{
			if (((key->rgb_inputs[stage] >> shift) & 0x0f) == INPUT_FOG ||
				((key->alpha_inputs[stage] >> shift) & 0x0f) == INPUT_FOG)
				return 1;
		}
	}
	for (shift = 0; shift < 32; shift += 8)
	{
		if (((key->final_inputs_abcd >> shift) & 0x0f) == INPUT_FOG ||
			(shift && ((key->final_inputs_efg >> shift) & 0x0f) == INPUT_FOG))
			return 1;
	}
	return 0;
}

/* rsx_fragment_key_clean (ps3_rsx_fp.c) */
static void fragment_key_clean(struct rsx_fragment_key *key)
{
	uint32_t combiner_count = key->combiner_count & 0xff, texture_modes = 0, dot_mapping = 0, input_texture = 0;
	uint32_t compare_mode = 0;
	int stage, mode, active, reads_dots, reads_input, sampled, diffuse_reflection;

	if (combiner_count > 8)
		combiner_count = 8;
	for (stage = (int)combiner_count; stage < 8; stage++)
	{
		key->rgb_inputs[stage] = key->rgb_outputs[stage] = 0;
		key->alpha_inputs[stage] = key->alpha_outputs[stage] = 0;
	}
	diffuse_reflection = stage_active(key, 2) && stage_mode(key, 2) == _mode_dot_reflect_diffuse;
	for (stage = 0; stage < 4; stage++)
	{
		active = stage_active(key, stage);
		mode = active ? stage_mode(key, stage) : _mode_none;
		reads_dots = (active && stage_dot_mode(mode)) || (stage == 3 && diffuse_reflection);
		reads_input = reads_dots || mode == _mode_dependent_ar || mode == _mode_dependent_gb;
		sampled = active && mode != _mode_passthru && mode != _mode_clipplane && mode != _mode_dot_product &&
			mode != _mode_dot_zw;
		texture_modes |= (uint32_t)mode << (5 * stage);
		if (mode == _mode_clipplane)
			compare_mode |= key->compare_mode & (0xfu << (4 * stage));
		if (reads_dots)
			dot_mapping |= stage ? key->dot_mapping & (7u << ((stage - 1) * 4)) : key->dot_mapping;
		if (reads_input && stage == 2)
			input_texture |= key->input_texture & (1u << 16);
		if (reads_input && stage == 3)
			input_texture |= key->input_texture & (3u << 20);
		if (!sampled)
		{
			key->sampler_type[stage] = _rsx_sampler_none;
			key->texture_scaled &= (uint8_t)~(1 << stage);
		}
		if (!active)
			key->alpha_kill[stage] = key->color_sign[stage] = 0;
	}
	key->texture_modes = texture_modes;
	key->dot_mapping = dot_mapping;
	key->input_texture = input_texture;
	key->compare_mode = compare_mode;
	if (!reads_fog(key, combiner_count))
		key->fog_enable = 0;
	if (!key->fog_enable || key->fog_table_mode > 3)
		key->fog_table_mode = 0;
	if (((key->kill_mode & 3) != RSX_KILL_ZERO && (key->kill_mode & 3) != RSX_KILL_ONE) || !(key->kill_mode >> 4))
		key->kill_mode = 0;
	key->kill_mode &= 0xf3;
}

typedef char fragment_key_words_check[sizeof(struct rsx_fragment_key) == SPU_D3D_KEY_WORDS * 4 ? 1 : -1];

static uint32_t hash_words(const uint32_t *words, uint32_t count)
{
	uint32_t hash = 0;

	for (; count; count--)
		hash = ((hash << 5) | (hash >> 27)) ^ *words++;
	hash ^= hash >> 16;
	hash *= 0x85ebca6bu;
	return hash ^ (hash >> 13);
}

#define FRAGMENTS_CACHED 16

static int32_t fragment_buckets[SPU_D3D_FRAGMENT_BUCKETS];
static spu_d3d_fragment fragment_cache[FRAGMENTS_CACHED] __attribute__((aligned(128)));
static int32_t fragment_cache_index[FRAGMENTS_CACHED];
static uint32_t fragment_cache_next;
static fragment_program_info fragment_last;
static BOOL fragment_last_valid;
static uint32_t fragment_last_key[SPU_D3D_KEY_WORDS];

static void fragment_info(int32_t index, const spu_d3d_fragment *entry, fragment_program_info *info)
{
	info->index = (uint32_t)index;
	info->microcode = entry->microcode;
	info->word_count = entry->word_count;
	info->control = entry->control;
	info->register_count = entry->register_count;
	info->texcoord_mask = entry->texcoord_mask;
	info->uniform_count = entry->uniform_count <= 32 ? entry->uniform_count : 32;
	memcpy(info->uniforms, entry->uniforms, sizeof(info->uniforms));
	info->stamp_key = entry->stamp_key;
	info->ppe_entry = entry->ppe_entry;
}

/* the table's entry index in LS */
static const spu_d3d_fragment *fragment_entry(int32_t index)
{
	uint32_t slot;

	for (slot = 0; slot < FRAGMENTS_CACHED; slot++)
	{
		if (fragment_cache_index[slot] == index)
			return &fragment_cache[slot];
	}
	slot = fragment_cache_next++ % FRAGMENTS_CACHED;
	dma_get(&fragment_cache[slot], setup.fragments + (uint32_t)index * sizeof(spu_d3d_fragment),
		sizeof(spu_d3d_fragment), TAG_TABLE);
	dma_wait(1 << TAG_TABLE);
	fragment_cache_index[slot] = index;
	return &fragment_cache[slot];
}

/* fragment_program_get: the program of the key, translated by the PPE when the table lacks it; NULL: none */
static const fragment_program_info *fragment_program_get(const struct rsx_fragment_key *key)
{
	const uint32_t *words = (const uint32_t *)key;
	uint32_t hash, index;
	int32_t entry_index;

	if (fragment_last_valid)
	{
		for (index = 0; index < SPU_D3D_KEY_WORDS && words[index] == fragment_last_key[index]; index++)
			;
		if (index == SPU_D3D_KEY_WORDS)
			return fragment_last.index != 0xffffffffu ? &fragment_last : NULL;
	}
	hash = hash_words(words, SPU_D3D_KEY_WORDS);
	for (entry_index = fragment_buckets[hash % SPU_D3D_FRAGMENT_BUCKETS]; entry_index >= 0; )
	{
		const spu_d3d_fragment *entry = fragment_entry(entry_index);

		if (entry->hash == hash)
		{
			for (index = 0; index < SPU_D3D_KEY_WORDS && entry->key[index] == words[index]; index++)
				;
			if (index == SPU_D3D_KEY_WORDS)
				break;
		}
		entry_index = entry->next;
	}
	if (entry_index < 0)
	{
		spu_d3d_response *answer;

		memcpy(request_block.words, words, SPU_D3D_KEY_WORDS * 4);
		request_block.words[SPU_D3D_KEY_WORDS] = hash;
		answer = request(_spu_d3d_request_fragment_program, SPU_D3D_KEY_WORDS + 1);
		entry_index = (int32_t)answer->words[0];
		if (entry_index < 0)
			return NULL;
		/* (the PPE put it first in its bucket) */
		fragment_buckets[hash % SPU_D3D_FRAGMENT_BUCKETS] = entry_index;
	}
	{
		const spu_d3d_fragment *entry = fragment_entry(entry_index);

		memcpy(fragment_last_key, words, sizeof(fragment_last_key));
		fragment_last_valid = TRUE;
		if (entry->translated)
			fragment_info(entry_index, entry, &fragment_last);
		else
			fragment_last.index = 0xffffffffu;
	}
	return fragment_last.index != 0xffffffffu ? &fragment_last : NULL;
}

/* (the timer build) a stamped frame's draw: its RSX time by its pixel shader and part; its part (in the specular or
effects list), texture mask and textures into the PPE's fragment_entry (shaders_log reads them) */
static void draw_stamp(void)
{
	static unsigned char bytes[32] __attribute__((aligned(16)));
	static unsigned char stamped[sizeof(bound_textures) + 32] __attribute__((aligned(16)));
	const fragment_program_info *entry = &fragment_last;

	if (draw_stamps_part < 0 || !draw_stamps.on || !fragment_last_valid || entry->index == 0xffffffffu)
		return;
	if (entry->ppe_entry)
	{
		uint32_t part_ea = entry->ppe_entry + setup.stamp_part_offset + (draw_stamps_part > STAMP_SPECULAR_FOG);
		uint32_t mask_ea = entry->ppe_entry + setup.stamp_part_offset + 2;
		uint32_t textures_ea = entry->ppe_entry + setup.stamp_textures_offset;

		dma_wait(1 << TAG_MISC);
		bytes[part_ea & 15] = (uint8_t)draw_stamps_part;
		bytes[16 + (mask_ea & 15)] = bound_texture_mask;
		put_exact(bytes + (part_ea & 15), part_ea, 1, TAG_MISC);
		put_exact(bytes + 16 + (mask_ea & 15), mask_ea, 1, TAG_MISC);
		memcpy(stamped + (textures_ea & 15), bound_textures, sizeof(bound_textures));
		put_exact(stamped + (textures_ea & 15), textures_ea, sizeof(bound_textures), TAG_MISC);
	}
	gpu_draw_stamp(entry->stamp_key | (uint32_t)draw_stamps_part << STAMP_PART_SHIFT);
}

static void draw_stamps_split(int part)
{
	if (draw_stamps_part >= 0 && part >= SPECULAR_LIGHTS && part < SPECULAR_FOG)
		draw_stamps_part = part - SPECULAR_LIGHTS + 1;
}

/* the part the next draws are in (-1 none): a span starts where they were in none */
static void draw_stamps_set(int part)
{
	int was = draw_stamps_part;

	draw_stamps_part = part;
	if (part < 0 || was < 0)
		gpu_draw_stamps(part >= 0);
}

static uint32_t blend_equation(uint32_t operation);

/* d3d8_rsx.c's unchanged_fragments */
static uint8_t unchanged_fragments(void)
{
	uint32_t *rs = render_state;
	uint32_t write = rs[D3DRS_COLORWRITEENABLE], source = rs[D3DRS_SRCBLEND], destination = rs[D3DRS_DESTBLEND];
	uint32_t equation = blend_equation(rs[D3DRS_BLENDOP]);
	unsigned channels = ((write & D3DCOLORWRITEENABLE_RED) ? 1u : 0u) |
		((write & D3DCOLORWRITEENABLE_GREEN) ? 2u : 0u) | ((write & D3DCOLORWRITEENABLE_BLUE) ? 4u : 0u) |
		((write & D3DCOLORWRITEENABLE_ALPHA) ? 8u : 0u);

	if (!rs[D3DRS_ALPHABLENDENABLE] || !channels || (rs[D3DRS_ZENABLE] && rs[D3DRS_ZWRITEENABLE]))
		return 0;
	if (rs[D3DRS_STENCILENABLE] && (rs[D3DRS_STENCILWRITEMASK] & 0xff) && (rs[D3DRS_STENCILFAIL] != D3DSTENCILOP_KEEP ||
		rs[D3DRS_STENCILZFAIL] != D3DSTENCILOP_KEEP || rs[D3DRS_STENCILPASS] != D3DSTENCILOP_KEEP))
		return 0;
	if (equation == GCM_FUNC_ADD && source == D3DBLEND_SRCALPHA &&
		(destination == D3DBLEND_INVSRCALPHA || destination == D3DBLEND_ONE))
		return RSX_KILL_ZERO | 8u << 4;
	if (equation == GCM_FUNC_ADD && source == D3DBLEND_ONE && destination == D3DBLEND_INVSRCALPHA)
		return (uint8_t)(RSX_KILL_ZERO | (channels | 8u) << 4);
	if (source == D3DBLEND_ONE && destination == D3DBLEND_ONE)
	{
		if (equation == GCM_FUNC_ADD || equation == GCM_FUNC_REVERSE_SUBTRACT || equation == GCM_FUNC_MAX)
			return (uint8_t)(RSX_KILL_ZERO | channels << 4);
		if (equation == GCM_FUNC_MIN)
			return (uint8_t)(RSX_KILL_ONE | channels << 4);
	}
	if (equation == GCM_FUNC_ADD && source == D3DBLEND_DESTCOLOR && destination == D3DBLEND_ZERO)
		return (uint8_t)(RSX_KILL_ONE | channels << 4);
	return 0;
}

/* (d3d8_rsx.c's) */
#define KILL_ALPHA_ZERO (RSX_KILL_ZERO | 8u << 4)

/* d3d8_rsx.c's alpha_test_removes */
static int alpha_test_removes(unsigned alpha)
{
	uint32_t *rs = render_state;
	uint32_t function = (rs[D3DRS_ALPHAFUNC] ? rs[D3DRS_ALPHAFUNC] : GCM_NEVER) - GCM_NEVER;
	unsigned reference = rs[D3DRS_ALPHAREF] & 0xff;

	if (!rs[D3DRS_ALPHATESTENABLE] || function > 7)
		return 0;
	return !(function & (alpha < reference ? 1u : alpha == reference ? 2u : 4u));
}

static uint8_t fragment_kill_mode(void)
{
	uint8_t mode = unchanged_fragments();

	if (mode == KILL_ALPHA_ZERO && !render_state[D3DRS_ALPHATESTENABLE])
		return 0;
	if ((mode & (8u << 4)) && alpha_test_removes((mode & 3) == RSX_KILL_ONE ? 255 : 0))
		return 0;
	return mode;
}

static void fragment_key_build(struct rsx_fragment_key *key)
{
	uint32_t *rs = render_state;
	int index;

	for (index = 0; index < 8; index++)
	{
		key->rgb_inputs[index] = rs[D3DRS_PSRGBINPUTS0 + index];
		key->rgb_outputs[index] = rs[D3DRS_PSRGBOUTPUTS0 + index];
		key->alpha_inputs[index] = rs[D3DRS_PSALPHAINPUTS0 + index];
		key->alpha_outputs[index] = rs[D3DRS_PSALPHAOUTPUTS0 + index];
	}
	key->final_inputs_abcd = rs[D3DRS_PSFINALCOMBINERINPUTSABCD];
	key->final_inputs_efg = rs[D3DRS_PSFINALCOMBINERINPUTSEFG];
	key->combiner_count = rs[D3DRS_PSCOMBINERCOUNT];
	key->texture_modes = rs[D3DRS_PSTEXTUREMODES];
	key->dot_mapping = rs[D3DRS_PSDOTMAPPING];
	key->input_texture = rs[D3DRS_PSINPUTTEXTURE];
	key->compare_mode = rs[D3DRS_PSCOMPAREMODE];
	for (index = 0; index < TEXTURE_STAGES; index++)
	{
		key->alpha_kill[index] = texture_state[index][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key->color_sign[index] = (uint8_t)((texture_state[index][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	key->fog_enable = rs[D3DRS_FOGENABLE] != 0;
	key->fog_table_mode = (uint8_t)rs[D3DRS_FOGTABLEMODE];
	key->kill_mode = fragment_kill_mode();
}

#define TEXTURE_STATE(stage, type) (D3DRS_MAX + (stage) * D3DTSS_MAX + (type))
#define STAGE_STATE_PARTS(stage) \
	[TEXTURE_STATE(stage, D3DTSS_ADDRESSU) ... TEXTURE_STATE(stage, D3DTSS_MAXANISOTROPY)] = CHANGED_SAMPLER(stage), \
	[TEXTURE_STATE(stage, D3DTSS_BORDERCOLOR)] = CHANGED_SAMPLER(stage), \
	[TEXTURE_STATE(stage, D3DTSS_COLORSIGN)] = CHANGED_FRAGMENT_KEY, \
	[TEXTURE_STATE(stage, D3DTSS_ALPHAKILL)] = CHANGED_FRAGMENT_KEY

static const uint8_t state_parts[D3DRS_MAX + D3DTSS_MAXSTAGES * D3DTSS_MAX] =
{
	[D3DRS_PSALPHAINPUTS0 ... D3DRS_PSFINALCOMBINERINPUTSEFG] = CHANGED_FRAGMENT_KEY,
	[D3DRS_PSALPHAOUTPUTS0 ... D3DRS_PSCOMPAREMODE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_PSRGBOUTPUTS0 ... D3DRS_PSCOMBINERCOUNT] = CHANGED_FRAGMENT_KEY,
	[D3DRS_PSDOTMAPPING] = CHANGED_FRAGMENT_KEY,
	[D3DRS_PSINPUTTEXTURE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_FOGENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_FOGTABLEMODE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_PSTEXTUREMODES] = CHANGED_TEXTURE_MODES | CHANGED_FRAGMENT_KEY,
	[D3DRS_ALPHATESTENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_ALPHABLENDENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_SRCBLEND] = CHANGED_FRAGMENT_KEY,
	[D3DRS_DESTBLEND] = CHANGED_FRAGMENT_KEY,
	[D3DRS_BLENDOP] = CHANGED_FRAGMENT_KEY,
	[D3DRS_COLORWRITEENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_ZENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_ZWRITEENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_STENCILENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_STENCILWRITEMASK] = CHANGED_FRAGMENT_KEY,
	[D3DRS_STENCILFAIL] = CHANGED_FRAGMENT_KEY,
	[D3DRS_STENCILZFAIL] = CHANGED_FRAGMENT_KEY,
	[D3DRS_STENCILPASS] = CHANGED_FRAGMENT_KEY,
	STAGE_STATE_PARTS(0), STAGE_STATE_PARTS(1), STAGE_STATE_PARTS(2), STAGE_STATE_PARTS(3),
};

static struct
{
	BOOL valid;
	texture_samplers samplers;
	const fragment_program_info *program;
} fragment_kept;

static const fragment_program_info *fragment_program_find(const texture_samplers *samplers, uint32_t *span)
{
	const texture_samplers *kept = &fragment_kept.samplers;
	const fragment_program_info *program;
	struct rsx_fragment_key key __attribute__((aligned(16)));

	if (fragment_kept.valid && !(draw_state_changes & CHANGED_FRAGMENT_KEY) && samplers->scaled == kept->scaled &&
		samplers->types[0] == kept->types[0] && samplers->types[1] == kept->types[1] &&
		samplers->types[2] == kept->types[2] && samplers->types[3] == kept->types[3])
		return fragment_kept.program;
	fragment_key_build(&key);
	memcpy(key.sampler_type, samplers->types, sizeof(key.sampler_type));
	key.texture_scaled = samplers->scaled;
	*span = timer_add(_timer_draw_key, *span);
	fragment_key_clean(&key);
	program = fragment_program_get(&key);
	fragment_kept.valid = program != NULL;
	fragment_kept.samplers = *samplers;
	fragment_kept.program = program;
	draw_state_changes &= ~CHANGED_FRAGMENT_KEY;
	statistics.counts[_timer_draw_fragment]++;
	return program;
}

static void colour_update(uint32_t now, uint32_t *source, BOOL valid, float *out)
{
	if (valid && now == *source)
		return;
	*source = now;
	color_to_vec4(now, out);
}

static void words_set(float *out, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
	uint32_t *words = (uint32_t *)out;

	words[0] = x;
	words[1] = y;
	words[2] = z;
	words[3] = w;
}

static void fragment_uniforms_build(float uniforms[_rsx_uniform_count][4])
{
	static uint32_t colours[19];
	static float converted[_rsx_uniform_texture_scale][4] __attribute__((aligned(16)));
	static BOOL valid;
	uint32_t *rs = render_state;
	int stage;

	for (stage = 0; stage < 8; stage++)
	{
		colour_update(rs[D3DRS_PSCONSTANT0_0 + stage], &colours[stage * 2], valid, converted[_rsx_uniform_c0 + stage]);
		colour_update(rs[D3DRS_PSCONSTANT1_0 + stage], &colours[stage * 2 + 1], valid,
			converted[_rsx_uniform_c1 + stage]);
	}
	colour_update(rs[D3DRS_PSFINALCOMBINERCONSTANT0], &colours[16], valid, converted[_rsx_uniform_final_c0]);
	colour_update(rs[D3DRS_PSFINALCOMBINERCONSTANT1], &colours[17], valid, converted[_rsx_uniform_final_c1]);
	colour_update(rs[D3DRS_FOGCOLOR], &colours[18], valid, converted[_rsx_uniform_fog_color]);
	valid = TRUE;
	words_set(converted[_rsx_uniform_fog], rs[D3DRS_FOGSTART], rs[D3DRS_FOGEND], rs[D3DRS_FOGDENSITY], 0);
	for (stage = 0; stage < TEXTURE_STAGES; stage++)
	{
		uint32_t *s = texture_state[stage];

		words_set(converted[_rsx_uniform_bump_matrix + stage], s[D3DTSS_BUMPENVMAT00], s[D3DTSS_BUMPENVMAT01],
			s[D3DTSS_BUMPENVMAT10], s[D3DTSS_BUMPENVMAT11]);
		words_set(converted[_rsx_uniform_bump_luminance + stage], s[D3DTSS_BUMPENVLSCALE], s[D3DTSS_BUMPENVLOFFSET], 0,
			0);
	}
	memcpy(uniforms, converted, sizeof(converted));
	for (stage = 0; stage < TEXTURE_STAGES; stage++)
	{
		uniforms[_rsx_uniform_texture_scale + stage][0] = device.texture_scale[stage][0];
		uniforms[_rsx_uniform_texture_scale + stage][1] = device.texture_scale[stage][1];
		uniforms[_rsx_uniform_texture_scale + stage][2] = 1.0f;
		uniforms[_rsx_uniform_texture_scale + stage][3] = 1.0f;
	}
}

/* ---------- raster state */

static uint32_t blend_equation(uint32_t operation)
{
	switch (operation)
	{
	case D3DBLENDOP_SUBTRACT: return GCM_FUNC_SUBTRACT;
	case D3DBLENDOP_REVSUBTRACT:
	case D3DBLENDOP_REVSUBTRACTSIGNED: return GCM_FUNC_REVERSE_SUBTRACT;
	case D3DBLENDOP_MIN: return GCM_FUNC_MIN;
	case D3DBLENDOP_MAX: return GCM_FUNC_MAX;
	default: return GCM_FUNC_ADD;
	}
}

static void viewport_scissor(int32_t *left, int32_t *top, int32_t *right, int32_t *bottom)
{
	static struct
	{
		uint32_t x, y, width, height;
		float scale[2];
		uint32_t pixel_width, pixel_height;
		int32_t left, top, right, bottom;
		BOOL valid;
	} kept;

	if (!kept.valid || kept.x != device.viewport.x || kept.y != device.viewport.y ||
		kept.width != device.viewport.width || kept.height != device.viewport.height ||
		!f_eq(kept.scale[0], device.target_scale[0]) || !f_eq(kept.scale[1], device.target_scale[1]) ||
		kept.pixel_width != device.target_pixel_width || kept.pixel_height != device.target_pixel_height)
	{
		kept.x = device.viewport.x;
		kept.y = device.viewport.y;
		kept.width = device.viewport.width;
		kept.height = device.viewport.height;
		kept.scale[0] = device.target_scale[0];
		kept.scale[1] = device.target_scale[1];
		kept.pixel_width = device.target_pixel_width;
		kept.pixel_height = device.target_pixel_height;
		kept.left = scaled_pixel(f_from_u32(device.viewport.x), device.target_scale[0]);
		kept.top = scaled_pixel(f_from_u32(device.viewport.y), device.target_scale[1]);
		kept.right = scaled_pixel(f_from_u32(device.viewport.x + device.viewport.width), device.target_scale[0]);
		kept.bottom = scaled_pixel(f_from_u32(device.viewport.y + device.viewport.height), device.target_scale[1]);
		if (kept.left < 0)
			kept.left = 0;
		if (kept.top < 0)
			kept.top = 0;
		if (kept.right > (int32_t)device.target_pixel_width)
			kept.right = (int32_t)device.target_pixel_width;
		if (kept.bottom > (int32_t)device.target_pixel_height)
			kept.bottom = (int32_t)device.target_pixel_height;
		kept.valid = TRUE;
	}
	*left = kept.left;
	*top = kept.top;
	*right = kept.right;
	*bottom = kept.bottom;
}

/* d3d8_rsx.c's light_bounds (the Direct3D thread's part) */
static struct
{
	BOOL on;
	spu_d3d_record_light_bounds bounds;
} light_bounds;

/* d3d8_rsx.c's light_bounds_pixel */
static int32_t light_bounds_pixel(float value)
{
	int32_t pixel = f_to_s32(value);

	if (f_gt(f_from_s32(pixel), value))
		pixel--;
	return pixel;
}

/* d3d8_rsx.c's light_bounds_scissor */
static void light_bounds_scissor(int32_t *left, int32_t *top, int32_t *right, int32_t *bottom)
{
	const spu_d3d_record_light_bounds *b = &light_bounds.bounds;
	float offset_x = f_add(f_add(device.viewport_offset[0], f_from_s32(ui_offset)), 0.5f);
	float offset_y = f_add(device.viewport_offset[1], 0.5f);
	int32_t x0 = light_bounds_pixel(f_mul(f_add(f_mul(b->x0, device.viewport_scale[0]), offset_x),
		device.target_scale[0])) - 2;
	int32_t x1 = light_bounds_pixel(f_mul(f_add(f_mul(b->x1, device.viewport_scale[0]), offset_x),
		device.target_scale[0])) + 3;
	int32_t y0 = light_bounds_pixel(f_mul(f_add(f_mul(b->y1, device.viewport_scale[1]), offset_y),
		device.target_scale[1])) - 2;
	int32_t y1 = light_bounds_pixel(f_mul(f_add(f_mul(b->y0, device.viewport_scale[1]), offset_y),
		device.target_scale[1])) + 3;

	if (x0 > *left)
		*left = x0;
	if (x1 < *right)
		*right = x1;
	if (y0 > *top)
		*top = y0;
	if (y1 < *bottom)
		*bottom = y1;
}

/* d3d8_rsx.c's particles_state */
static void particles_state(gpu_state *state)
{
	state->color_mask |= GCM_COLOR_MASK_A;
	state->stencil_function = 0;
	state->stencil_write_mask = 0;
	if (!state->blend)
	{
		state->blend = 1;
		state->blend_source = D3DBLEND_ONE;
		state->blend_destination = D3DBLEND_ZERO;
		state->blend_equation = GCM_FUNC_ADD;
	}
	state->blend_source_alpha = D3DBLEND_ZERO;
	state->blend_destination_alpha = state->blend_destination;
}

static BOOL apply_raster_state(BOOL has_depth)
{
	uint32_t *rs = render_state;
	uint32_t write = rs[D3DRS_COLORWRITEENABLE];
	BOOL depth_test = has_depth && rs[D3DRS_ZENABLE];
	gpu_state next;
	int32_t left, top, right, bottom;

	viewport_scissor(&left, &top, &right, &bottom);
	if (light_bounds.on)
		light_bounds_scissor(&left, &top, &right, &bottom);
	if (left >= right || top >= bottom)
		return FALSE;
	memset(&next, 0, sizeof(next));
	next.color_mask = ((write & D3DCOLORWRITEENABLE_RED) ? GCM_COLOR_MASK_R : 0) |
		((write & D3DCOLORWRITEENABLE_GREEN) ? GCM_COLOR_MASK_G : 0) |
		((write & D3DCOLORWRITEENABLE_BLUE) ? GCM_COLOR_MASK_B : 0) |
		((write & D3DCOLORWRITEENABLE_ALPHA) ? GCM_COLOR_MASK_A : 0);
	if (depth_test)
		next.depth_function = rs[D3DRS_ZFUNC] ? rs[D3DRS_ZFUNC] : GCM_NEVER;
	next.depth_write = depth_test && rs[D3DRS_ZWRITEENABLE];
	if (has_depth && rs[D3DRS_STENCILENABLE])
		next.stencil_function = rs[D3DRS_STENCILFUNC] ? rs[D3DRS_STENCILFUNC] : GCM_NEVER;
	next.stencil_reference = rs[D3DRS_STENCILREF] & 0xff;
	next.stencil_mask = rs[D3DRS_STENCILMASK] & 0xff;
	next.stencil_write_mask = rs[D3DRS_STENCILWRITEMASK] & 0xff;
	next.stencil_fail = rs[D3DRS_STENCILFAIL];
	next.stencil_depth_fail = rs[D3DRS_STENCILZFAIL];
	next.stencil_pass = rs[D3DRS_STENCILPASS];
	next.blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
	next.blend_source = next.blend_source_alpha = rs[D3DRS_SRCBLEND];
	next.blend_destination = next.blend_destination_alpha = rs[D3DRS_DESTBLEND];
	next.blend_equation = blend_equation(rs[D3DRS_BLENDOP]);
	next.blend_color = rs[D3DRS_BLENDCOLOR];
	if (rs[D3DRS_ALPHATESTENABLE])
		next.alpha_function = rs[D3DRS_ALPHAFUNC] ? rs[D3DRS_ALPHAFUNC] : GCM_NEVER;
	next.alpha_reference = rs[D3DRS_ALPHAREF] & 0xff;
	if (!rs[D3DRS_ALPHATESTENABLE] && unchanged_fragments() == KILL_ALPHA_ZERO)
	{
		next.alpha_function = GCM_GREATER;
		next.alpha_reference = 0;
	}
	next.front_face = rs[D3DRS_FRONTFACE] == D3DFRONT_CCW ? GCM_FRONTFACE_CCW : GCM_FRONTFACE_CW;
	if (rs[D3DRS_CULLMODE] != D3DCULL_NONE)
		next.cull_face = rs[D3DRS_CULLMODE] == rs[D3DRS_FRONTFACE] ? GCM_CULL_FRONT : GCM_CULL_BACK;
	next.polygon_mode = rs[D3DRS_FILLMODE] ? rs[D3DRS_FILLMODE] : GCM_POLYGON_MODE_FILL;
	next.polygon_offset = rs[D3DRS_SOLIDOFFSETENABLE] != 0;
	next.polygon_offset_factor = word_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
	next.polygon_offset_units = word_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
	next.scissor_x = (uint16_t)left;
	next.scissor_y = (uint16_t)top;
	next.scissor_width = (uint16_t)(right - left);
	next.scissor_height = (uint16_t)(bottom - top);
	next.depth_minimum = device.viewport.minimum_z;
	next.depth_maximum = device.viewport.maximum_z;
	if (light_bounds.on && depth_test)
	{
		float range = f_sub(device.viewport.maximum_z, device.viewport.minimum_z);

		next.depth_bounds = 1;
		next.depth_bounds_minimum = f_add(device.viewport.minimum_z, f_mul(light_bounds.bounds.z0, range));
		next.depth_bounds_maximum = f_add(device.viewport.minimum_z, f_mul(light_bounds.bounds.z1, range));
	}
	if (particles.bound)
		particles_state(&next);
	gpu_set_state(&next);
	return TRUE;
}

/* ---------- vertex shaders: the PPE's objects, a few in LS */

#define SHADERS_CACHED 32

typedef struct
{
	uint32_t ea;
	uint32_t element_count;
	/* reg, stream, type, bytes, offset (16 bits) */
	uint8_t elements[16][6];
	uint32_t translated;
	uint32_t untranslatable;
	vertex_program_info program;
} shader_info;

static shader_info shader_cache[SHADERS_CACHED];
static uint32_t shader_cache_next;

static const shader_info *shader_get(uint32_t ea, BOOL refresh)
{
	static unsigned char buffer[SPU_D3D_SHADER_BYTES + 32] __attribute__((aligned(16)));
	shader_info *info = NULL;
	const unsigned char *object;
	uint32_t slot;

	if (!ea)
		return NULL;
	for (slot = 0; slot < SHADERS_CACHED; slot++)
	{
		if (shader_cache[slot].ea == ea)
		{
			info = &shader_cache[slot];
			break;
		}
	}
	if (info && !refresh)
		return info;
	if (!info)
		info = &shader_cache[shader_cache_next++ % SHADERS_CACHED];
	object = (const unsigned char *)fetch(buffer, ea, SPU_D3D_SHADER_BYTES, TAG_MISC);
	info->ea = ea;
	memcpy(&info->element_count, object + SPU_D3D_SHADER_ELEMENT_COUNT, 4);
	if (info->element_count > 16)
		info->element_count = 16;
	memcpy(info->elements, object + SPU_D3D_SHADER_ELEMENTS, sizeof(info->elements));
	memcpy(&info->translated, object + SPU_D3D_SHADER_TRANSLATED, 4);
	memcpy(&info->untranslatable, object + SPU_D3D_SHADER_UNTRANSLATABLE, 4);
	/* rsx_vertex_program: an 8-byte pointer, then four words */
	info->program.identity = ea + SPU_D3D_SHADER_PROGRAM;
	memcpy(&info->program.microcode, object + SPU_D3D_SHADER_PROGRAM + 4, 4);
	memcpy(&info->program.instruction_count, object + SPU_D3D_SHADER_PROGRAM + 8, 4);
	memcpy(&info->program.input_mask, object + SPU_D3D_SHADER_PROGRAM + 12, 4);
	memcpy(&info->program.output_mask, object + SPU_D3D_SHADER_PROGRAM + 16, 4);
	memcpy(&info->program.register_count, object + SPU_D3D_SHADER_PROGRAM + 20, 4);
	return info;
}

/* vertex_program_get: translated by the PPE at the first draw */
static const vertex_program_info *vertex_program_get(uint32_t ea)
{
	const shader_info *info = shader_get(ea, FALSE);

	if (!info)
		return NULL;
	if (!info->translated && !info->untranslatable)
	{
		request_block.words[0] = ea;
		request(_spu_d3d_request_vertex_program, 1);
		info = shader_get(ea, TRUE);
	}
	return info->translated ? &info->program : NULL;
}

static uint32_t current_program(void)
{
	uint32_t program = device.program_slots[device.program_address];

	return program ? program : device.vertex_shader;
}

enum
{
	_shader_set,
	_shader_load,
	_shader_select
};

static void vertex_shader_apply(uint32_t kind, uint32_t object, uint32_t address)
{
	switch (kind)
	{
	case _shader_set:
		device.vertex_shader = object;
		device.program_address = 0;
		device.program_slots[0] = object;
		break;
	case _shader_load:
		device.program_slots[address] = object;
		break;
	default:
		if (object)
			device.vertex_shader = object;
		if (address < VERTEX_PROGRAM_SLOTS)
			device.program_address = address;
		break;
	}
}

static uint32_t vertex_type_bytes(uint32_t type)
{
	switch (type)
	{
	case SPU_VERTICES_FLOAT1: return 4;
	case SPU_VERTICES_FLOAT2: return 8;
	case SPU_VERTICES_FLOAT3: return 12;
	case SPU_VERTICES_FLOAT4: return 16;
	case SPU_VERTICES_D3DCOLOR: return 4;
	case SPU_VERTICES_SHORT1: return 2;
	case SPU_VERTICES_SHORT2: return 4;
	case SPU_VERTICES_SHORT3: return 6;
	case SPU_VERTICES_SHORT4: return 8;
	case SPU_VERTICES_NORMSHORT1: return 2;
	case SPU_VERTICES_NORMSHORT2: return 4;
	case SPU_VERTICES_NORMSHORT3: return 6;
	case SPU_VERTICES_NORMSHORT4: return 8;
	case SPU_VERTICES_NORMPACKED3: return 4;
	case SPU_VERTICES_PBYTE1: return 1;
	case SPU_VERTICES_PBYTE2: return 2;
	case SPU_VERTICES_PBYTE3: return 3;
	case SPU_VERTICES_PBYTE4: return 4;
	case SPU_VERTICES_FLOAT2H: return 12;
	case SPU_VERTICES_TEXCOORD_WORD: return 4;
	default: return 0;
	}
}

/* ---------- the conversions (spu_vertices.c's, bit for bit), into RSX memory */

#define INDEX_BATCH 2048
#define GROUP_SPAN 2048

typedef struct
{
	uint32_t base;
	uint32_t stride;
	uint32_t span;
	uint32_t slots;
} slot_group;

typedef struct
{
	uint32_t source;
	uint32_t stride;
	uint32_t type;
	uint32_t bytes;
} convert_slot;


static const vec_uchar16 byte_order = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const vec_float4 defaults = { 0.0f, 0.0f, 0.0f, 1.0f };
static const vec_uint4 lanes[5] =
{
	{ 0, 0, 0, 0 },
	{ ~0u, 0, 0, 0 },
	{ ~0u, ~0u, 0, 0 },
	{ ~0u, ~0u, ~0u, 0 },
	{ ~0u, ~0u, ~0u, ~0u },
};

static const unsigned char *fetch_start(uint32_t k, uint32_t ea, uint32_t size)
{
	uint32_t first = ea & ~15u, end = (ea + size + 15) & ~15u;

	mfc_get(inputs[k], first, end - first, TAG_INPUT + k, 0, 0);
	return inputs[k] + (ea & 15);
}

static vec_float4 *output_take(void)
{
	dma_wait(1 << (TAG_OUTPUT + output_next));
	return outputs[output_next];
}

static void output_put(uint32_t ea, uint32_t bytes)
{
	mfc_put(outputs[output_next], ea, bytes, TAG_OUTPUT + output_next, 0, 0);
	output_next ^= 1;
}

static inline vec_uchar16 load_pattern(const unsigned char *p, vec_uchar16 pattern)
{
	const vec_uchar16 *q = (const vec_uchar16 *)((uintptr_t)p & ~15u);
	vec_ushort8 shift = spu_splats((unsigned short)(((uintptr_t)p & 15) * 0x0101));

	return spu_shuffle(q[0], q[1], (vec_uchar16)spu_add((vec_ushort8)pattern, shift));
}

static inline vec_float4 window_float(vec_uint4 value, vec_uint4 exponents)
{
	vec_uint4 zeros = spu_cntlz(value);
	vec_uint4 top = spu_sl(value, zeros);
	vec_uint4 rounded = spu_add(spu_rlmask(top, -8), spu_and(spu_rlmask(top, -7), 1));

	return spu_mul(spu_convtf(rounded, 24), (vec_float4)spu_sl(spu_sub(exponents, zeros), 23));
}

static inline vec_float4 divide_repeating(vec_int4 value, vec_uint4 shifts1, vec_uint4 shifts2, vec_uint4 exponents,
	vec_uint4 largest)
{
	vec_int4 sign = spu_rlmaska(value, -31);
	vec_uint4 magnitude = (vec_uint4)spu_sub(spu_xor(value, sign), sign);
	vec_uint4 bits = spu_add(spu_add(magnitude, spu_sl(magnitude, shifts1)), spu_sl(magnitude, shifts2));
	vec_float4 quotient = spu_sel(window_float(bits, exponents), spu_splats(1.0f), spu_cmpgt(magnitude, largest));

	return (vec_float4)spu_or((vec_uint4)quotient, spu_and((vec_uint4)value, spu_splats(0x80000000u)));
}

static inline vec_int4 element_shorts(const unsigned char *p)
{
	static const vec_uchar16 spread = { 0, 1, 0x80, 0x80, 2, 3, 0x80, 0x80, 4, 5, 0x80, 0x80, 6, 7, 0x80, 0x80 };

	return spu_rlmaska((vec_int4)load_pattern(p, spread), -16);
}

static inline vec_float4 element_normshorts(const unsigned char *p, vec_uint4 mask)
{
	vec_float4 value = divide_repeating(element_shorts(p), spu_splats(15u), spu_splats(32u), spu_splats(129u),
		spu_splats(32767u));

	return spu_sel(defaults, value, mask);
}

static inline vec_float4 element_texcoord_word(const unsigned char *p)
{
	static const vec_uchar16 halves =
	{
		2, 3, 0x80, 0x80, 0, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	};
	vec_float4 value = divide_repeating(spu_rlmaska((vec_int4)load_pattern(p, halves), -16), spu_splats(15u),
		spu_splats(32u), spu_splats(129u), spu_splats(~0u));

	return spu_sel(defaults, value, lanes[2]);
}

static inline vec_float4 element_normpacked3(const unsigned char *p)
{
	static const vec_uchar16 splat = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 };
	static const vec_uint4 left = { 21, 10, 0, 0 };
	static const vec_int4 right = { -21, -21, -22, 0 };
	static const vec_uint4 shifts1 = { 10, 10, 9, 32 }, shifts2 = { 20, 20, 18, 32 }, exponents = { 129, 129, 132, 129 };
	vec_int4 value = spu_rlmaska((vec_int4)spu_sl((vec_uint4)load_pattern(p, splat), left), right);

	return spu_sel(defaults, divide_repeating(value, shifts1, shifts2, exponents, spu_splats(~0u)), lanes[3]);
}

static inline vec_float4 element_bytes(const unsigned char *p, vec_uchar16 replicate)
{
	return window_float((vec_uint4)load_pattern(p, replicate), spu_splats(127u));
}

#define CONVERT_EACH(element) \
	do \
	{ \
		for (vertex = 0; vertex + 4 <= n; vertex += 4, data += 4 * stride, out += 4 * slots) \
		{ \
			vec_float4 v0 = element(data), v1 = element(data + stride); \
			vec_float4 v2 = element(data + 2 * stride), v3 = element(data + 3 * stride); \
			out[0] = v0; \
			out[slots] = v1; \
			out[2 * slots] = v2; \
			out[3 * slots] = v3; \
		} \
		for (; vertex < n; vertex++, data += stride, out += slots) \
			out[0] = element(data); \
	} \
	while (0)

static void slot_convert(uint32_t type, const unsigned char *data, uint32_t stride, uint32_t n, vec_float4 *out,
	uint32_t slots)
{
	static const vec_uchar16 color = { 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 0, 0, 0, 0 };
	static const vec_uchar16 bytes = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 };
	vec_uint4 mask = lanes[type == SPU_VERTICES_FLOAT2H ? 3 : type >> 4 <= 4 ? type >> 4 : 0];
	uint32_t vertex;

#define FLOATS(p) spu_sel(defaults, (vec_float4)load_pattern(p, byte_order), mask)
#define SHORTS(p) spu_sel(defaults, spu_convtf(element_shorts(p), 0), mask)
#define NORMSHORTS(p) element_normshorts(p, mask)
#define COLOR(p) element_bytes(p, color)
#define BYTES(p) spu_sel(defaults, element_bytes(p, bytes), mask)
#define DEFAULTS(p) defaults
	switch (type)
	{
	case SPU_VERTICES_FLOAT1:
	case SPU_VERTICES_FLOAT2:
	case SPU_VERTICES_FLOAT3:
	case SPU_VERTICES_FLOAT4:
	case SPU_VERTICES_FLOAT2H:
		CONVERT_EACH(FLOATS);
		break;
	case SPU_VERTICES_SHORT1:
	case SPU_VERTICES_SHORT2:
	case SPU_VERTICES_SHORT3:
	case SPU_VERTICES_SHORT4:
		CONVERT_EACH(SHORTS);
		break;
	case SPU_VERTICES_NORMSHORT1:
	case SPU_VERTICES_NORMSHORT2:
	case SPU_VERTICES_NORMSHORT3:
	case SPU_VERTICES_NORMSHORT4:
		CONVERT_EACH(NORMSHORTS);
		break;
	case SPU_VERTICES_NORMPACKED3:
		CONVERT_EACH(element_normpacked3);
		break;
	case SPU_VERTICES_TEXCOORD_WORD:
		CONVERT_EACH(element_texcoord_word);
		break;
	case SPU_VERTICES_D3DCOLOR:
		CONVERT_EACH(COLOR);
		break;
	case SPU_VERTICES_PBYTE1:
	case SPU_VERTICES_PBYTE2:
	case SPU_VERTICES_PBYTE3:
	case SPU_VERTICES_PBYTE4:
		CONVERT_EACH(BYTES);
		break;
	default:
		CONVERT_EACH(DEFAULTS);
		break;
	}
#undef FLOATS
#undef SHORTS
#undef NORMSHORTS
#undef COLOR
#undef BYTES
#undef DEFAULTS
}

static uint32_t groups_make(const convert_slot *slot_list, uint32_t slot_count, slot_group *groups)
{
	uint32_t count = 0, slot, group, start = 0, end = 0;

	for (slot = 0; slot < slot_count; slot++)
	{
		const convert_slot *element = &slot_list[slot];

		if (!element->bytes)
			continue;
		for (group = 0; group < count; group++)
		{
			start = element->source < groups[group].base ? element->source : groups[group].base;
			end = element->source + element->bytes > groups[group].base + groups[group].span ?
				element->source + element->bytes : groups[group].base + groups[group].span;
			if (element->stride && groups[group].stride == element->stride && end - start <= GROUP_SPAN)
				break;
		}
		if (group == count)
		{
			groups[group].base = element->source;
			groups[group].stride = element->stride;
			groups[group].span = element->bytes;
			groups[group].slots = 0;
			count++;
		}
		else
		{
			groups[group].base = start;
			groups[group].span = end - start;
		}
		groups[group].slots |= 1u << slot;
	}
	return count;
}

static const unsigned char *group_fetch(const slot_group *group, uint32_t first, uint32_t n, uint32_t k)
{
	return fetch_start(k, group->base + first * group->stride, (n - 1) * group->stride + group->span);
}

/* count vertices of the slots as float4s, a vertex's together, into output (RSX memory) */
static void convert_vertices(const convert_slot *slot_list, uint32_t slots, uint32_t count, uint32_t output)
{
	slot_group groups[SPU_VERTICES_SLOTS];
	uint32_t first = 0, limit, slot, group, group_count, k = 0;
	const unsigned char *data = NULL;

	/* (d3d8_rsx.c's vertices_convert counts them by type) */
	for (slot = 0; slot < slots; slot++)
		statistics.type_elements[slot_list[slot].type & 127] += count;
	if (!slots || slots > SPU_VERTICES_SLOTS || !count)
		return;
	group_count = groups_make(slot_list, slots, groups);
	limit = OUTPUT_QUADS / slots;
	for (group = 0; group < group_count; group++)
	{
		uint32_t stride = groups[group].stride, most = FETCH_BYTES - 16 - groups[group].span;

		if (stride && most / stride + 1 < limit)
			limit = most / stride + 1;
	}
	if (group_count)
		data = group_fetch(&groups[0], 0, count < limit ? count : limit, k);
	while (first < count)
	{
		uint32_t n = count - first < limit ? count - first : limit;
		vec_float4 *out = output_take();

		for (slot = 0; slot < slots; slot++)
		{
			if (!slot_list[slot].bytes)
				slot_convert(0, NULL, 0, n, out + slot, slots);
		}
		for (group = 0; group < group_count; group++)
		{
			uint32_t next_group = group + 1 < group_count ? group + 1 : 0;
			uint32_t next_first = group + 1 < group_count ? first : first + n;
			const unsigned char *next = NULL;

			if (next_first < count)
				next = group_fetch(&groups[next_group], next_first,
					count - next_first < limit ? count - next_first : limit, k ^ 1);
			dma_wait(1 << (TAG_INPUT + k));
			for (slot = 0; slot < slots; slot++)
			{
				const convert_slot *element = &slot_list[slot];

				if (groups[group].slots & (1u << slot))
					slot_convert(element->type, data + (element->source - groups[group].base), element->stride, n,
						out + slot, slots);
			}
			data = next;
			k ^= 1;
		}
		output_put(output + first * slots * 16, n * slots * 16);
		first += n;
	}
}

/* count 16-bit indices less subtract into output (16-aligned, its room rounded up to 16 bytes) */
static void convert_indices(uint32_t source, uint32_t count, uint32_t subtract, uint32_t output)
{
	vec_ushort8 less = spu_splats((unsigned short)subtract);
	uint32_t first = 0, k = 0;
	const unsigned char *data;

	if (!count)
		return;
	data = fetch_start(k, source, (count < INDEX_BATCH ? count : INDEX_BATCH) * 2);
	while (first < count)
	{
		uint32_t n = count - first < INDEX_BATCH ? count - first : INDEX_BATCH, quads = (n + 7) / 8, quad;
		uint32_t next_first = first + n;
		const unsigned char *next = NULL;
		vec_ushort8 *out = (vec_ushort8 *)output_take();

		if (next_first < count)
			next = fetch_start(k ^ 1, source + next_first * 2,
				(count - next_first < INDEX_BATCH ? count - next_first : INDEX_BATCH) * 2);
		dma_wait(1 << (TAG_INPUT + k));
		for (quad = 0; quad < quads; quad++)
			out[quad] = spu_sub((vec_ushort8)load_pattern(data + quad * 16, byte_order), less);
		output_put(output + first * 2, quads * 16);
		data = next;
		k ^= 1;
		first = next_first;
	}
}

/* the largest of count indices at source and the OR of all, by DMA */
static void indices_scan(uint32_t source, uint32_t count, uint32_t *largest, uint32_t *bits)
{
	uint32_t done = 0, top = 0, any = 0;

	while (done < count)
	{
		uint32_t n = count - done < INDEX_BATCH ? count - done : INDEX_BATCH, index;
		const unsigned char *data = fetch_start(0, source + done * 2, n * 2);

		dma_wait(1 << TAG_INPUT);
		for (index = 0; index < n; index++)
		{
			uint32_t value = (uint32_t)data[index * 2] << 8 | data[index * 2 + 1];

			any |= value;
			if (value > top)
				top = value;
		}
		done += n;
	}
	*largest = top;
	*bits = any;
}

/* the least and the greatest index */
static void indices_range(uint32_t source, uint32_t count, uint32_t *minimum, uint32_t *maximum)
{
	uint32_t done = 0, low = 0xffff, high = 0;

	while (done < count)
	{
		uint32_t n = count - done < INDEX_BATCH ? count - done : INDEX_BATCH, index;
		const unsigned char *data = fetch_start(0, source + done * 2, n * 2);

		dma_wait(1 << TAG_INPUT);
		for (index = 0; index < n; index++)
		{
			uint32_t value = (uint32_t)data[index * 2] << 8 | data[index * 2 + 1];

			if (value < low)
				low = value;
			if (value > high)
				high = value;
		}
		done += n;
	}
	*minimum = low;
	*maximum = high;
}

/* 16 bytes of LS into RSX memory at ea (16-aligned) */
static void put_quad(const void *ls, uint32_t ea)
{
	vec_float4 *out = output_take();

	out[0] = *(const vec_float4 *)ls;
	output_put(ea, 16);
}

/* ---------- the vertex cache: the map's vertex buffers converted once into the arena */

#define ARRAY_BUCKETS 2048
#define ARRAYS_CACHED 128
#define INDEX_BUCKETS 1024
#define INDEX_ARRAYS_CACHED 64

static uint16_t array_buckets[ARRAY_BUCKETS];
static uint16_t array_next[SPU_D3D_ARRAYS];
static uint8_t array_tags[SPU_D3D_ARRAYS];
static uint32_t array_count;
static spu_d3d_array array_cache[ARRAYS_CACHED] __attribute__((aligned(128)));
static uint16_t array_cache_index[ARRAYS_CACHED];
static uint16_t index_buckets[INDEX_BUCKETS];
static uint16_t index_next[SPU_D3D_INDEX_ARRAYS];
static uint8_t index_tags[SPU_D3D_INDEX_ARRAYS];
static uint32_t index_count;
static spu_d3d_index_array index_cache[INDEX_ARRAYS_CACHED] __attribute__((aligned(128)));
static uint16_t index_cache_index[INDEX_ARRAYS_CACHED];
static struct
{
	uint32_t used;
	BOOL failed;
} arena;

static void arrays_reset(void)
{
	memset(array_buckets, 0xff, sizeof(array_buckets));
	memset(array_cache_index, 0xff, sizeof(array_cache_index));
	array_count = 0;
	memset(index_buckets, 0xff, sizeof(index_buckets));
	memset(index_cache_index, 0xff, sizeof(index_cache_index));
	index_count = 0;
}

static spu_d3d_array *array_entry(uint32_t index)
{
	uint32_t slot = index % ARRAYS_CACHED;

	if (array_cache_index[slot] != index)
	{
		/* (after the slot's write-through: one tag's commands can run in any order) */
		dma_wait(1 << TAG_TABLE);
		dma_get(&array_cache[slot], setup.arrays + index * sizeof(spu_d3d_array), sizeof(spu_d3d_array), TAG_TABLE);
		dma_wait(1 << TAG_TABLE);
		array_cache_index[slot] = (uint16_t)index;
	}
	return &array_cache[slot];
}

static void array_write(uint32_t index)
{
	dma_put(&array_cache[index % ARRAYS_CACHED], setup.arrays + index * sizeof(spu_d3d_array), sizeof(spu_d3d_array),
		TAG_TABLE);
}

static spu_d3d_index_array *index_entry(uint32_t index)
{
	uint32_t slot = index % INDEX_ARRAYS_CACHED;

	if (index_cache_index[slot] != index)
	{
		dma_wait(1 << TAG_TABLE);
		dma_get(&index_cache[slot], setup.index_arrays + index * sizeof(spu_d3d_index_array),
			sizeof(spu_d3d_index_array), TAG_TABLE);
		dma_wait(1 << TAG_TABLE);
		index_cache_index[slot] = (uint16_t)index;
	}
	return &index_cache[slot];
}

/* bytes of the arena, in order; it has no frees: a load empties it */
static uint32_t arena_allocate(uint32_t bytes, uint32_t *offset)
{
	/* (the PPE's RSX block at the first use, as d3d8_rsx.c's vertex_arena_allocate) */
	if (!setup.arena && !arena.failed)
	{
		spu_d3d_response *answer = request(_spu_d3d_request_arena, 0);

		setup.arena = answer->words[0];
		setup.arena_offset = answer->words[1];
		setup.arena_bytes = answer->words[2];
		arena.failed = !setup.arena;
	}
	bytes = (bytes + 127) & ~127u;
	if (!setup.arena || bytes > setup.arena_bytes - arena.used)
		return 0;
	*offset = setup.arena_offset + arena.used;
	arena.used += bytes;
	return setup.arena + arena.used - bytes;
}

static uint32_t array_hash(uint32_t source, uint32_t stride, uint32_t type)
{
	return (source >> 4) ^ (source >> 16) ^ (stride * 2654435761u) ^ type;
}

/* the RSX copy of one element of a stream with vertices [0, first + count) converted, room for [0, extent) */
static const spu_d3d_array *vertex_cache_get(uint32_t source, uint32_t stride, uint32_t type, uint32_t bytes,
	uint32_t first, uint32_t count, uint32_t extent)
{
	uint32_t needed = first + count, hash = array_hash(source, stride, type), index, tag = (hash >> 11) & 0xff;
	spu_d3d_array *entry = NULL;
	convert_slot slot;

	if (!static_end || source < static_begin || source + (needed - 1) * stride + bytes > static_end)
		return NULL;
	for (index = array_buckets[hash % ARRAY_BUCKETS]; index != NO_ENTRY; index = array_next[index])
	{
		if (array_tags[index] != tag)
			continue;
		entry = array_entry(index);
		if (entry->source == source && entry->stride == stride && entry->type == type)
			break;
		entry = NULL;
	}
	if (entry && entry->count >= needed)
		return entry;
	if (!entry || needed > entry->capacity)
	{
		uint32_t capacity = extent >= needed ? extent :
			entry && entry->capacity * 2 > needed ? entry->capacity * 2 : needed;
		uint32_t limit = stride ? (static_end - bytes - source) / stride + 1 : needed;
		uint32_t offset, memory;

		if (capacity > limit)
			capacity = limit;
		if (!entry && array_count == SPU_D3D_ARRAYS)
			return NULL;
		memory = arena_allocate(capacity * 16, &offset);
		if (!memory)
		{
			statistics.cache_refused++;
			return NULL;
		}
		if (!entry)
		{
			index = array_count++;
			array_next[index] = array_buckets[hash % ARRAY_BUCKETS];
			array_buckets[hash % ARRAY_BUCKETS] = (uint16_t)index;
			array_tags[index] = (uint8_t)tag;
			dma_wait(1 << TAG_TABLE);
			entry = &array_cache[index % ARRAYS_CACHED];
			array_cache_index[index % ARRAYS_CACHED] = (uint16_t)index;
			entry->source = source;
			entry->stride = stride;
			entry->type = type;
		}
		entry->memory = memory;
		entry->offset = offset;
		entry->capacity = capacity;
		entry->count = 0;
	}
	slot.source = source + entry->count * stride;
	slot.stride = stride;
	slot.type = type;
	slot.bytes = bytes;
	convert_vertices(&slot, 1, needed - entry->count, entry->memory + entry->count * 16);
	statistics.cached_elements += needed - entry->count;
	entry->count = needed;
	array_write(index);
	vertex_cache_dirty = 1;
	return entry;
}

/* a constant for an input the declaration leaves out: one value read by every vertex */
static BOOL attribute_constant(uint32_t reg, const float *value)
{
	uint32_t offset, memory = gpu_scratch(16, 16, &offset);

	if (!memory)
		return FALSE;
	put_quad(value, memory);
	gpu_set_attribute(reg, offset, 0, 4);
	return TRUE;
}

static BOOL setup_vertices(const vertex_program_info *program, uint32_t first, uint32_t count, uint32_t extent)
{
	const shader_info *declaration = shader_get(device.vertex_shader, FALSE);
	convert_slot slot_list[VERTEX_ATTRIBUTE_COUNT];
	uint32_t regs[VERTEX_ATTRIBUTE_COUNT];
	uint32_t slot_count = 0, index, slot, stride, placed = 0, offset = 0, out = 0;

	for (index = 0; index < declaration->element_count && slot_count < 15; index++)
	{
		const uint8_t *element = declaration->elements[index];
		uint32_t reg = element[0], stream = element[1], type = element[2], bytes = element[3];
		uint32_t element_offset = (uint32_t)element[4] << 8 | element[5];
		uint32_t base = device.streams[stream & 15].base;
		const spu_d3d_array *cached;
		uint32_t source;

		if (reg >= VERTEX_ATTRIBUTE_COUNT || !(program->input_mask & (1u << reg)) || (placed & (1u << reg)) || !base ||
			type == D3DVSDT_NONE)
			continue;
		placed |= 1u << reg;
		source = base + element_offset;
		cached = vertex_cache_get(source, device.streams[stream & 15].stride, type, bytes, first, count, extent);
		if (cached)
		{
			gpu_set_attribute(reg, cached->offset + first * 16, 16, 4);
			continue;
		}
		slot_list[slot_count].stride = device.streams[stream & 15].stride;
		slot_list[slot_count].source = source + first * slot_list[slot_count].stride;
		slot_list[slot_count].type = type;
		slot_list[slot_count].bytes = bytes;
		regs[slot_count] = reg;
		slot_count++;
	}
	stride = slot_count * 16;
	if (slot_count)
	{
		out = gpu_scratch(count * stride, 16, &offset);
		if (!out)
			return FALSE;
		convert_vertices(slot_list, slot_count, count, out);
		statistics.converted_elements += count * slot_count;
	}
	for (slot = 0; slot < slot_count; slot++)
		gpu_set_attribute(regs[slot], offset + slot * 16, stride, 4);
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (placed & (1u << index))
			continue;
		if (!(program->input_mask & (1u << index)))
			gpu_set_attribute(index, 0, 0, 0);
		else if (!attribute_constant(index, device.attributes[index]))
			return FALSE;
	}
	return TRUE;
}

static BOOL setup_cached_vertices(const vertex_program_info *program, uint32_t base, uint32_t extent)
{
	const shader_info *declaration = shader_get(device.vertex_shader, FALSE);
	uint32_t cached_offsets[VERTEX_ATTRIBUTE_COUNT];
	uint32_t placed = 0, index;

	for (index = 0; index < declaration->element_count; index++)
	{
		const uint8_t *element = declaration->elements[index];
		uint32_t reg = element[0], stream = element[1], type = element[2], bytes = element[3];
		uint32_t element_offset = (uint32_t)element[4] << 8 | element[5];
		uint32_t stream_base = device.streams[stream & 15].base;
		const spu_d3d_array *cached;

		if (reg >= VERTEX_ATTRIBUTE_COUNT || !(program->input_mask & (1u << reg)) || (placed & (1u << reg)) ||
			!stream_base || type == D3DVSDT_NONE)
			continue;
		cached = vertex_cache_get(stream_base + element_offset, device.streams[stream & 15].stride, type, bytes, 0,
			extent, extent);
		if (!cached)
			return FALSE;
		cached_offsets[reg] = cached->offset;
		placed |= 1u << reg;
	}
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (placed & (1u << index))
			gpu_set_attribute(index, cached_offsets[index] + base * 16, 16, 4);
		else if (!(program->input_mask & (1u << index)))
			gpu_set_attribute(index, 0, 0, 0);
		else if (!attribute_constant(index, device.attributes[index]))
			return FALSE;
	}
	return TRUE;
}

/* a model's indices in the tag cache: the arena's copy, made at their first draw if each reads one of the buffer's
vertices */
static BOOL index_cache_offset(uint32_t source, uint32_t count, uint32_t vertices, uint32_t *offset)
{
	uint32_t hash = ((source >> 1) ^ (source >> 13) ^ count), index, tag = (hash >> 10) & 0xff, largest, bits;
	uint32_t copy;
	spu_d3d_index_array *entry;

	if (!static_end || source < static_begin || source + count * 2 > static_end)
		return FALSE;
	for (index = index_buckets[hash % INDEX_BUCKETS]; index != NO_ENTRY; index = index_next[index])
	{
		if (index_tags[index] != tag)
			continue;
		entry = index_entry(index);
		if (entry->source == source && entry->count == count && entry->vertices == vertices)
		{
			*offset = entry->offset;
			return TRUE;
		}
	}
	indices_scan(source, count, &largest, &bits);
	if (largest >= vertices || index_count == SPU_D3D_INDEX_ARRAYS)
		return FALSE;
	copy = arena_allocate(count * 2, offset);
	if (!copy)
		return FALSE;
	convert_indices(source, count, 0, copy);
	index = index_count++;
	index_next[index] = index_buckets[hash % INDEX_BUCKETS];
	index_buckets[hash % INDEX_BUCKETS] = (uint16_t)index;
	index_tags[index] = (uint8_t)tag;
	dma_wait(1 << TAG_TABLE);
	entry = &index_cache[index % INDEX_ARRAYS_CACHED];
	index_cache_index[index % INDEX_ARRAYS_CACHED] = (uint16_t)index;
	entry->source = source;
	entry->count = count;
	entry->vertices = vertices;
	entry->offset = *offset;
	dma_put(entry, setup.index_arrays + index * sizeof(spu_d3d_index_array), sizeof(spu_d3d_index_array), TAG_TABLE);
	return TRUE;
}

static BOOL index_copy(uint32_t source, uint32_t count, uint32_t vertices, uint32_t *offset)
{
	uint32_t indices = gpu_scratch((count * 2 + 15) & ~15u, 128, offset), largest, bits;

	if (!indices)
		return FALSE;
	indices_scan(source, count, &largest, &bits);
	if (bits >= vertices && largest >= vertices)
		return FALSE;
	convert_indices(source, count, 0, indices);
	return TRUE;
}

static uint32_t primitive_type(uint32_t type)
{
	return type == D3DPT_POLYGON ? GCM_TYPE_TRIANGLE_FAN : type;
}

/* ---------- draws */

static const vertex_program_info *prepare_draw_state(void)
{
	uint32_t span = timer_now(), colours;
	uint32_t object = current_program();
	const vertex_program_info *vertex;
	const fragment_program_info *fragment_program;
	texture_samplers samplers;
	float uniforms[_rsx_uniform_count][4] __attribute__((aligned(16)));
	BOOL has_depth = FALSE;

	if (!object || !device.vertex_shader || !(vertex = vertex_program_get(object)))
	{
		statistics.skipped_program++;
		return NULL;
	}
	span = timer_add(_timer_draw_program, span);
	if (!bind_targets(&has_depth))
	{
		statistics.skipped_target++;
		return NULL;
	}
	span = timer_add(_timer_draw_targets, span);
	if (!apply_raster_state(has_depth))
	{
		statistics.skipped_scissor++;
		return NULL;
	}
	span = timer_add(_timer_draw_raster, span);
	screen_constants_update();
	span = timer_add(_timer_draw_screen, span);
	bind_textures(&samplers);
	span = timer_add(_timer_draw_textures, span);
	fragment_program = fragment_program_find(&samplers, &span);
	if (!fragment_program)
	{
		statistics.skipped_program++;
		return NULL;
	}
	colours = span = timer_add(_timer_draw_fragment, span);
	fragment_uniforms_build(uniforms);
	span = timer_add(_timer_draw_uniforms, span);
	if (gpu_set_fragment_program(fragment_program, (const float (*)[4])uniforms) != 0)
	{
		statistics.skipped_scratch++;
		return NULL;
	}
	span = timer_add(_timer_draw_colours, colours);
	gpu_set_vertex_program(vertex);
	span = timer_add(_timer_draw_vertex_program, span);
	constants_upload();
	timer_add(_timer_draw_constants, span);
	return vertex;
}

static const vertex_program_info *prepare_draw(void)
{
	uint32_t start = timer_now();
	const vertex_program_info *program = prepare_draw_state();

	timer_add(_timer_draw_state, start);
	return program;
}

static void streams_load(const spu_d3d_record_draw *draw)
{
	const spu_d3d_record_stream *stream = (const spu_d3d_record_stream *)(draw + 1);
	uint32_t index;

	for (index = 0; index < 16; index++)
	{
		if (draw->stream_mask & (1u << index))
		{
			device.streams[index].base = (uint32_t)stream->base;
			device.streams[index].stride = stream->stride;
			stream++;
		}
	}
}

static void draw_vertices_run(const spu_d3d_record_draw *draw)
{
	const vertex_program_info *program;
	uint32_t span;
	BOOL ready;

	streams_load(draw);
	if (!draw->count || !(program = prepare_draw()))
		return;
	span = timer_now();
	ready = setup_vertices(program, draw->start, draw->count, 0);
	timer_add(_timer_draw_vertices, span);
	if (!ready)
	{
		statistics.skipped_scratch++;
		return;
	}
	gpu_draw(primitive_type(draw->primitive), 0, draw->count);
	statistics.draws++;
	draw_stamp();
}

static void draw_indexed_run(const spu_d3d_record_draw *draw)
{
	const vertex_program_info *program;
	uint32_t index_data = (uint32_t)draw->indices, vertex_count = draw->count, extent = draw->extent;
	uint32_t base_vertex_index = draw->start, minimum = 0xffff, maximum = 0, offset, indices, span;
	BOOL ready;

	streams_load(draw);
	if (!vertex_count || !index_data || !(program = prepare_draw()))
		return;
	span = timer_now();
	if (extent && setup_cached_vertices(program, base_vertex_index, extent))
	{
		uint32_t vertices = extent - base_vertex_index;

		span = timer_add(_timer_draw_vertices, span);
		ready = index_cache_offset(index_data, vertex_count, vertices, &offset) ||
			index_copy(index_data, vertex_count, vertices, &offset);
		timer_add(_timer_draw_indices, span);
		if (ready)
		{
			gpu_draw_indexed(primitive_type(draw->primitive), offset, vertex_count);
			statistics.draws++;
			draw_stamp();
			return;
		}
		span = timer_now();
	}
	if (draw->ranged)
	{
		minimum = draw->minimum;
		maximum = draw->maximum;
	}
	else
	{
		indices_range(index_data, vertex_count, &minimum, &maximum);
	}
	span = timer_add(_timer_draw_indices, span);
	ready = setup_vertices(program, base_vertex_index + minimum, maximum - minimum + 1, extent);
	span = timer_add(_timer_draw_vertices, span);
	if (!ready || !(indices = gpu_scratch((vertex_count * 2 + 15) & ~15u, 128, &offset)))
	{
		statistics.skipped_scratch++;
		return;
	}
	convert_indices(index_data, vertex_count, minimum, indices);
	timer_add(_timer_draw_indices, span);
	gpu_draw_indexed(primitive_type(draw->primitive), offset, vertex_count);
	statistics.draws++;
	draw_stamp();
}

/* the record's vertices (EA data) into the scratch: each program input from its written register, else its value */
static void immediate_run(const spu_d3d_record_immediate *immediate, uint32_t data)
{
	const vertex_program_info *program;
	uint32_t count = immediate->count, regs[VERTEX_ATTRIBUTE_COUNT], slots[VERTEX_ATTRIBUTE_COUNT];
	uint32_t reg_count = 0, written_count = 0, index, vertex, offset = 0, out = 0, batch;

	if (!count || !(program = prepare_draw()))
		return;
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
		slots[index] = immediate->written & (1u << index) ? written_count++ : VERTEX_ATTRIBUTE_COUNT;
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT && reg_count < 15; index++)
	{
		if (program->input_mask & (1u << index))
			regs[reg_count++] = index;
	}
	out = reg_count ? gpu_scratch(count * reg_count * 16, 16, &offset) : 0;
	if (reg_count && !out)
	{
		statistics.skipped_scratch++;
		return;
	}
	/* the vertices a batch: both buffers' 4 KB */
	batch = 4096 / ((written_count > reg_count ? written_count : reg_count) * 16 + 1);
	for (vertex = 0; reg_count && vertex < count; vertex += batch)
	{
		uint32_t n = count - vertex < batch ? count - vertex : batch, at;
		float *vertex_out = (float *)output_take();
		const float *source = NULL;

		if (written_count)
			source = (const float *)fetch(inputs[0], data + vertex * written_count * 16, n * written_count * 16,
				TAG_INPUT);
		for (at = 0; at < n; at++)
		{
			for (index = 0; index < reg_count; index++)
			{
				uint32_t slot = slots[regs[index]];

				memcpy(vertex_out + (at * reg_count + index) * 4, slot < VERTEX_ATTRIBUTE_COUNT ?
					source + (at * written_count + slot) * 4 : device.attributes[regs[index]], 16);
			}
		}
		output_put(out + vertex * reg_count * 16, n * reg_count * 16);
	}
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
		gpu_set_attribute(index, 0, 0, 0);
	for (index = 0; index < reg_count; index++)
		gpu_set_attribute(regs[index], offset + index * 16, reg_count * 16, 4);
	gpu_draw(primitive_type(immediate->primitive), 0, count);
	statistics.draws++;
	draw_stamp();
}

/* the elements the program reads converted into the scratch, the others its registers' values */
static void elements_run(const spu_d3d_record_elements *record, uint32_t data)
{
	const vertex_program_info *program;
	convert_slot slot_list[SPU_D3D_RECORD_ELEMENTS];
	uint32_t regs[SPU_D3D_RECORD_ELEMENTS];
	uint32_t count = record->count, slot_count = 0, index, slot, stride, placed = 0, offset = 0, out = 0, span;

	if (!count || !(program = prepare_draw()))
		return;
	span = timer_now();
	for (index = 0; index < record->element_count && index < SPU_D3D_RECORD_ELEMENTS; index++)
	{
		uint32_t reg = record->elements[index].reg;

		if (reg >= VERTEX_ATTRIBUTE_COUNT || !(program->input_mask & (1u << reg)) || (placed & (1u << reg)))
			continue;
		placed |= 1u << reg;
		slot_list[slot_count].source = data + record->elements[index].offset;
		slot_list[slot_count].stride = record->elements[index].stride;
		slot_list[slot_count].type = record->elements[index].type;
		slot_list[slot_count].bytes = vertex_type_bytes(record->elements[index].type);
		regs[slot_count] = reg;
		slot_count++;
	}
	stride = slot_count * 16;
	if (slot_count)
	{
		out = gpu_scratch(count * stride, 16, &offset);
		if (!out)
		{
			statistics.skipped_scratch++;
			return;
		}
		convert_vertices(slot_list, slot_count, count, out);
		statistics.converted_elements += count * slot_count;
	}
	for (slot = 0; slot < slot_count; slot++)
		gpu_set_attribute(regs[slot], offset + slot * 16, stride, 4);
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (placed & (1u << index))
			continue;
		if (!(program->input_mask & (1u << index)))
			gpu_set_attribute(index, 0, 0, 0);
		else if (!attribute_constant(index, device.attributes[index]))
		{
			statistics.skipped_scratch++;
			return;
		}
	}
	timer_add(_timer_draw_vertices, span);
	gpu_draw(primitive_type(record->primitive), 0, count);
	statistics.draws++;
	draw_stamp();
}

static void clear_run(const spu_d3d_record *record, uint64_t position)
{
	const spu_d3d_record_clear *clear = (const spu_d3d_record_clear *)(record + 1);
	uint32_t count = record->a, flags = record->b, color = clear->color, stencil = clear->stencil;
	uint32_t gpu_flags, depth, index;
	BOOL has_depth = FALSE;
	float z = word_float(clear->z);

	if (!bind_targets(&has_depth))
		return;
	gpu_flags = flags & (D3DCLEAR_TARGET_R | D3DCLEAR_TARGET_G | D3DCLEAR_TARGET_B | D3DCLEAR_TARGET_A);
	if (has_depth)
		gpu_flags |= flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
	if (!gpu_flags)
		return;
	if (f_lt(z, 0.0f))
		z = 0.0f;
	depth = f_lt(z, 1.0f) ? f_to_u32(f_add(f_mul(z, 16777215.0f), 0.5f)) : 0xffffffu;
	if (depth > 0xffffffu)
		depth = 0xffffffu;
	depth = (depth << 8) | (stencil & 0xff);
	for (index = 0; index < (count ? count : 1); index++)
	{
		int32_t left, top, right, bottom, offset = 0;

		left = (int32_t)device.viewport.x;
		top = (int32_t)device.viewport.y;
		right = left + (int32_t)device.viewport.width;
		bottom = top + (int32_t)device.viewport.height;
		if (count)
		{
			/* (the rectangles after the record's clear; read again: a request can move the window) */
			const int32_t *rectangle = (const int32_t *)ring_bytes(position + sizeof(spu_d3d_record) +
				sizeof(spu_d3d_record_clear) + index * 16, 16);

			if (rectangle[0] > left) left = rectangle[0];
			if (rectangle[1] > top) top = rectangle[1];
			if (rectangle[2] < right) right = rectangle[2];
			if (rectangle[3] < bottom) bottom = rectangle[3];
			offset = ui_offset;
		}
		left = scaled_pixel(f_from_s32(left + offset), device.target_scale[0]);
		top = scaled_pixel(f_from_s32(top), device.target_scale[1]);
		right = scaled_pixel(f_from_s32(right + offset), device.target_scale[0]);
		bottom = scaled_pixel(f_from_s32(bottom), device.target_scale[1]);
		if (left < 0) left = 0;
		if (top < 0) top = 0;
		if (right > (int32_t)device.target_pixel_width) right = (int32_t)device.target_pixel_width;
		if (bottom > (int32_t)device.target_pixel_height) bottom = (int32_t)device.target_pixel_height;
		if (left < right && top < bottom)
			gpu_clear(gpu_flags, color, depth, (uint32_t)left, (uint32_t)top, (uint32_t)(right - left),
				(uint32_t)(bottom - top));
	}
}

/* ---------- the records */

static void states_apply(const uint32_t *pairs, uint32_t count)
{
	uint32_t changes = 0;

	for (; count; count--, pairs += 2)
	{
		if (pairs[0] >= D3DRS_MAX + D3DTSS_MAXSTAGES * D3DTSS_MAX)
			continue;
		changes |= state_parts[pairs[0]];
		if (pairs[0] < D3DRS_MAX)
			render_state[pairs[0]] = pairs[1];
		else
			(&texture_state[0][0])[pairs[0] - D3DRS_MAX] = pairs[1];
	}
	draw_state_changes |= changes;
}

static void attributes_apply(uint32_t mask, const float *values)
{
	uint32_t reg;

	for (reg = 0; reg < VERTEX_ATTRIBUTE_COUNT; reg++)
	{
		if (mask & (1u << reg))
		{
			memcpy(device.attributes[reg], values, sizeof(device.attributes[0]));
			values += 4;
		}
	}
}

static void render_target_apply(const spu_d3d_surface_words *words, BOOL reset)
{
	memcpy(device.targets, words, sizeof(device.targets));
	if (!reset)
		return;
	if (device.targets[0].valid)
	{
		texture_description d;

		texture_describe(device.targets[0].format, device.targets[0].size, &d);
		device.viewport.x = 0;
		device.viewport.y = 0;
		device.viewport.width = d.width;
		device.viewport.height = d.height;
		device.viewport.minimum_z = 0.0f;
		device.viewport.maximum_z = 1.0f;
	}
	viewport_update_constants();
}

/* ---------- presentation */

static void blit_quad(uint32_t display_width, uint32_t display_height, float left, float top, float right, float bottom)
{
	static float quad[4][8] __attribute__((aligned(16)));
	uint32_t vertices_offset, unit, vertices = gpu_scratch(4 * 8 * 4, 16, &vertices_offset);
	float width = f_from_u32(display_width), height = f_from_u32(display_height);
	float x0 = f_sub(f_mul(f_div(left, width), 2.0f), 1.0f), x1 = f_sub(f_mul(f_div(right, width), 2.0f), 1.0f);
	float y0 = f_sub(1.0f, f_mul(f_div(top, height), 2.0f)), y1 = f_sub(1.0f, f_mul(f_div(bottom, height), 2.0f));
	uint32_t corner;

	if (!vertices)
		return;
	for (corner = 0; corner < 4; corner++)
	{
		quad[corner][0] = corner == 0 || corner == 3 ? x0 : x1;
		quad[corner][1] = corner < 2 ? y0 : y1;
		quad[corner][2] = 0.0f;
		quad[corner][3] = 1.0f;
		quad[corner][4] = corner == 0 || corner == 3 ? 0.0f : 1.0f;
		quad[corner][5] = corner < 2 ? 0.0f : 1.0f;
		quad[corner][6] = 0.0f;
		quad[corner][7] = 1.0f;
	}
	dma_put(quad, vertices, sizeof(quad), TAG_MISC);
	dma_wait(1 << TAG_MISC);
	for (unit = 0; unit < ATTRIBUTE_COUNT; unit++)
		gpu_set_attribute(unit, vertices_offset + (unit ? 16 : 0), 32, unit < 2 ? 4 : 0);
	gpu_draw(GCM_TYPE_QUADS, 0, 4);
}

/* ps3_gpu.c's blit_draw after ps3_video_begin_frame's surface (display buffer at color_offset) */
static void blit_draw(uint32_t display_width, uint32_t display_height, uint32_t offset, uint32_t pitch, uint32_t width,
	uint32_t height)
{
	uint32_t picture_width = width, picture_height = height, unit;
	float scale[4], viewport_offset[4], left, top;
	gpu_texture texture;
	vertex_program_info blit;

	if (picture_width > display_width || picture_height > display_height)
	{
		picture_width = display_height * width <= display_width * height ? display_height * width / height : display_width;
		picture_height = display_height * width <= display_width * height ? display_height : display_width * height / width;
	}
	left = f_from_u32((display_width - picture_width) / 2);
	top = f_from_u32((display_height - picture_height) / 2);
	rsx_color_mask(GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
	rsx_depth_test_enable(0);
	rsx_depth_write_enable(0);
	rsx_stencil_test_enable(0);
	rsx_blend_enable(0);
	rsx_alpha_test_enable(0);
	rsx_cull_face_enable(0);
	rsx_polygon_offset_fill_enable(0);
	rsx_front_polygon_mode(GCM_POLYGON_MODE_FILL);
	rsx_back_polygon_mode(GCM_POLYGON_MODE_FILL);
	rsx_scissor(0, 0, display_width & 0xffff, display_height & 0xffff);
	scale[0] = f_mul(f_from_u32(display_width), 0.5f);
	scale[1] = f_mul(f_from_u32(display_height), -0.5f);
	scale[2] = 0.5f;
	scale[3] = 0.0f;
	viewport_offset[0] = f_mul(f_from_u32(display_width), 0.5f);
	viewport_offset[1] = f_mul(f_from_u32(display_height), 0.5f);
	viewport_offset[2] = 0.5f;
	viewport_offset[3] = 0.0f;
	rsx_viewport(0, 0, display_width & 0xffff, display_height & 0xffff, 0.0f, 1.0f, scale, viewport_offset);
	rsx_viewport_clip(display_width & 0xffff, display_height & 0xffff);
	if (picture_width < display_width || picture_height < display_height)
	{
		rsx_clear_color(0xff000000u);
		rsx_clear_surface(GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
	}
	blit.identity = 1;
	blit.microcode = setup.blit_vertex;
	blit.instruction_count = 2;
	blit.input_mask = 0x3;
	blit.output_mask = 0x4000;
	blit.register_count = 1;
	gpu_set_vertex_program(&blit);
	rsx_fragment_program_location(setup.blit_fragment_offset, 0x40, 2, 1, 1);
	fragment.program = 0;
	memset(&texture, 0, sizeof(texture));
	texture.offset = offset;
	texture.pitch = pitch;
	texture.width = (uint16_t)width;
	texture.height = (uint16_t)height;
	texture.depth = 1;
	texture.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
	texture.levels = 1;
	texture.dimension = GCM_TEXTURE_DIMS_2D;
	texture.minify = GCM_TEXTURE_LINEAR;
	texture.magnify = GCM_TEXTURE_LINEAR;
	texture.wrap_s = GCM_TEXTURE_CLAMP_TO_EDGE;
	texture.wrap_t = GCM_TEXTURE_CLAMP_TO_EDGE;
	texture.wrap_r = GCM_TEXTURE_CLAMP_TO_EDGE;
	gpu_set_texture(0, &texture);
	for (unit = 1; unit < TEXTURE_UNITS; unit++)
		texture_unit_off(unit);
	texture_cache_dirty = 1;
	blit_quad(display_width, display_height, left, top, f_add(left, f_from_u32(picture_width)),
		f_add(top, f_from_u32(picture_height)));
}


/* ---------- the half resolution particles (d3d8_rsx.c's, with ps3_gpu.c's passes) */

static void surface_quad(float u0, float v0, float u1, float v1)
{
	static float quad[4][8] __attribute__((aligned(16)));
	uint32_t vertices_offset, unit, corner, vertices = gpu_scratch(4 * 8 * 4, 16, &vertices_offset);

	if (!vertices)
		return;
	for (corner = 0; corner < 4; corner++)
	{
		quad[corner][0] = corner == 0 || corner == 3 ? -1.0f : 1.0f;
		quad[corner][1] = corner < 2 ? 1.0f : -1.0f;
		quad[corner][2] = 0.0f;
		quad[corner][3] = 1.0f;
		quad[corner][4] = corner == 0 || corner == 3 ? u0 : u1;
		quad[corner][5] = corner < 2 ? v0 : v1;
		quad[corner][6] = 0.0f;
		quad[corner][7] = 1.0f;
	}
	dma_put(quad, vertices, sizeof(quad), TAG_MISC);
	dma_wait(1 << TAG_MISC);
	for (unit = 0; unit < ATTRIBUTE_COUNT; unit++)
		gpu_set_attribute(unit, vertices_offset + (unit ? 16 : 0), 32, unit < 2 ? 4 : 0);
	gpu_draw(GCM_TYPE_QUADS, 0, 4);
}

static void surface_texture(uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height, uint8_t filter)
{
	gpu_texture texture;
	uint32_t unit;

	memset(&texture, 0, sizeof(texture));
	texture.offset = offset;
	texture.pitch = pitch;
	texture.width = (uint16_t)width;
	texture.height = (uint16_t)height;
	texture.depth = 1;
	texture.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
	texture.levels = 1;
	texture.dimension = GCM_TEXTURE_DIMS_2D;
	texture.minify = filter;
	texture.magnify = filter;
	texture.wrap_s = GCM_TEXTURE_CLAMP_TO_EDGE;
	texture.wrap_t = GCM_TEXTURE_CLAMP_TO_EDGE;
	texture.wrap_r = GCM_TEXTURE_CLAMP_TO_EDGE;
	gpu_set_texture(0, &texture);
	for (unit = 1; unit < TEXTURE_UNITS; unit++)
		gpu_set_texture(unit, NULL);
}

static void surface_state(gpu_state *next)
{
	memset(next, 0, sizeof(*next));
	next->front_face = GCM_FRONTFACE_CCW;
	next->polygon_mode = GCM_POLYGON_MODE_FILL;
	next->scissor_width = surface.width;
	next->scissor_height = surface.height;
	next->depth_maximum = 1.0f;
}

static void blit_program(vertex_program_info *blit)
{
	blit->identity = 1;
	blit->microcode = setup.blit_vertex;
	blit->instruction_count = 2;
	blit->input_mask = 0x3;
	blit->output_mask = 0x4000;
	blit->register_count = 1;
}

/* ps3_gpu_particles_depth */
static void particles_depth(uint32_t depth_offset, uint32_t depth_pitch, uint32_t width, uint32_t height)
{
	vertex_program_info blit;
	gpu_state next;
	float u = f_div(0.5f, f_from_u32(width)), v = f_div(0.5f, f_from_u32(height));

	if (!surface_valid || !width || !height)
		return;
	surface_state(&next);
	next.depth_function = GCM_ALWAYS;
	next.depth_write = 1;
	gpu_set_state(&next);
	blit_program(&blit);
	gpu_set_vertex_program(&blit);
	rsx_fragment_program_location(setup.particles_depth_offset, 0x40 | 0x0e, 4, 1, 1);
	surface_texture(depth_offset, depth_pitch, width, height, GCM_TEXTURE_NEAREST);
	surface_quad(-u, -v, f_sub(1.0f, u), f_sub(1.0f, v));
	caches_invalidate();
}

/* ps3_gpu_particles_composite */
static void particles_composite(uint32_t color_offset, uint32_t color_pitch, uint32_t width, uint32_t height)
{
	vertex_program_info blit;
	gpu_state next;

	if (!surface_valid || !width || !height)
		return;
	surface_state(&next);
	next.color_mask = GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B;
	next.blend = 1;
	next.blend_source = next.blend_source_alpha = GCM_ONE;
	next.blend_destination = next.blend_destination_alpha = GCM_SRC_ALPHA;
	next.blend_equation = GCM_FUNC_ADD;
	gpu_set_state(&next);
	blit_program(&blit);
	gpu_set_vertex_program(&blit);
	rsx_fragment_program_location(setup.blit_fragment_offset, 0x40, 2, 1, 1);
	surface_texture(color_offset, color_pitch, width, height, GCM_TEXTURE_LINEAR);
	surface_quad(0.0f, 0.0f, 1.0f, 1.0f);
	caches_invalidate();
}

/* d3d8_rsx.c's particles_begin */
static void particles_begin(const spu_d3d_record_particles *half)
{
	gpu_surface next, frame;
	BOOL has_depth;

	if (particles.on || !bind_targets(&has_depth) || !has_depth ||
		(targets_bound.color->width + 1) / 2 != half->width || (targets_bound.color->height + 1) / 2 != half->height)
		return;
	particles.on = TRUE;
	particles.half = *half;
	particles.main_color = targets_bound.color;
	particles.main_depth = targets_bound.depth;
	target_surface(particles.main_color, particles.main_depth, &frame);
	particles.width = (frame.width + 1u) / 2;
	particles.height = (frame.height + 1u) / 2;
	next.color_offset = half->color_offset;
	next.color_pitch = half->color_pitch;
	next.depth_offset = half->depth_offset;
	next.depth_pitch = half->depth_pitch;
	next.width = (uint16_t)particles.width;
	next.height = (uint16_t)particles.height;
	next.smaller = 0;
	gpu_set_surface(&next);
	gpu_clear(0xf2, 0xff000000u, 0, 0, 0, particles.width, particles.height);
	particles_depth(particles.main_depth->offset, particles.main_depth->pitch, frame.width, frame.height);
}

static void particles_end(void)
{
	gpu_surface next;

	if (!particles.on)
		return;
	particles.on = FALSE;
	particles.bound = FALSE;
	target_surface(particles.main_color, particles.main_depth, &next);
	gpu_set_surface(&next);
	particles_composite(particles.half.color_offset, particles.half.color_pitch, particles.width, particles.height);
}

/* d3d8_rsx.c's scale_begin */
static void scale_begin(const spu_d3d_record_scale *view)
{
	if (view->eighths >= 1 && view->eighths <= 8)
		stamps.eighths = view->eighths;
	if (scaled.on || view->eighths < 1 || view->eighths > 7)
		return;
	memset(&scaled, 0, sizeof(scaled));
	scaled.on = TRUE;
	scaled.view = *view;
	scaled.factor = f_div(f_from_u32(view->eighths), 8.0f);
	texture_epoch++;
}

/* d3d8_rsx.c's scale_end with ps3_gpu_scale_up */
static void scale_end(void)
{
	vertex_program_info blit;
	gpu_state next;
	gpu_surface whole;

	if (!scaled.on)
		return;
	scaled.on = FALSE;
	texture_epoch++;
	if (!scaled.width)
		return;
	whole.color_offset = scaled.color->offset;
	whole.color_pitch = scaled.color->pitch;
	whole.depth_offset = scaled.depth->offset;
	whole.depth_pitch = scaled.depth->pitch;
	whole.width = (uint16_t)scaled.color->width;
	whole.height = (uint16_t)scaled.color->height;
	whole.smaller = 0;
	gpu_set_surface(&whole);
	surface_state(&next);
	next.color_mask = GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A;
	gpu_set_state(&next);
	blit_program(&blit);
	gpu_set_vertex_program(&blit);
	rsx_fragment_program_location(setup.blit_fragment_offset, 0x40, 2, 1, 1);
	surface_texture(scaled.view.color_offset, scaled.view.color_pitch, scaled.width, scaled.height, GCM_TEXTURE_LINEAR);
	surface_quad(0.0f, 0.0f, 1.0f, 1.0f);
	caches_invalidate();
}

/* the counters and timers, at each present (the PPE logs them every STATISTICS_FRAMES frames and adds the timers to
ps3_timers.h's at each of their logs) */
static void statistics_put(void)
{
	statistics.texture_hashed = texture_hashed_bytes;
	statistics.arrays = array_count;
	statistics.arena_used = arena.used;
	statistics.scratch_peak = scratch.peak;
	statistics.scratch_drops = scratch.drops;
	statistics.frame = device.frame;
	dma_put(&statistics, setup.statistics, sizeof(statistics), TAG_MISC);
	dma_wait(1 << TAG_MISC);
}

/* d3d8_rsx.c's present_run with ps3_gpu_present: the PPE's flip slot, the display buffer's surface and the blit, the
frame's last stamp and label; the PPE flips and starts the next frame (its number, the RSX's progress) */
static void present_run(const spu_d3d_record_present *present)
{
	render_target *back_buffer = render_target_get(&present->back_buffer, 0);
	spu_d3d_response *answer;
	uint32_t index, finished, log_gpu;

	/* (a view the game left smaller is scaled up for the frame) */
	scale_end();
	answer = request(_spu_d3d_request_present_begin, 0);
	if (quitting)
		return;
	rsx_surface(answer->words[2], answer->words[3], answer->words[4], answer->words[5], answer->words[0],
		answer->words[1]);
	if (back_buffer)
		blit_draw(answer->words[0], answer->words[1], back_buffer->offset, back_buffer->pitch, back_buffer->width,
			back_buffer->height);
	gpu_timestamp(TIMER_PRESENT);
	rsx_write_backend_label(FRAME_LABEL, frame_index);
	stamps.frame = frame_index;
	dma_put(&stamps, setup.stamps + (frame_index % 3) * sizeof(stamps), sizeof(stamps), TAG_MISC);
	dma_put(&draw_stamps, setup.draw_stamps, sizeof(draw_stamps), TAG_MISC);
	log_gpu = frame_index % STATISTICS_FRAMES == 0;
	if (log_gpu)
		statistics_put();
	request_block.words[0] = frame_index;
	request_block.words[1] = log_gpu;
	request_block.words[2] = draw_stamps_dropped;
	answer = request(_spu_d3d_request_present_end, 3);
	if (quitting)
		return;
	if (log_gpu)
	{
		statistics.gpu_draws = statistics.vertex_uploads = statistics.vertex_switches = 0;
		statistics.fragment_copies = statistics.texture_loads = 0;
	}
	draw_stamps_dropped = 0;
	/* ps3_gpu.c's frame_begin: the next frame's half of the scratch, the RSX's state unknown */
	frame_index = answer->words[0];
	finished = answer->words[1];
	scratch.head = 0;
	if (answer->words[2])
		memset(resident_programs, 0, sizeof(resident_programs));
	if (answer->words[3])
		draw_stamps.count = 0;
	button_style = (int)answer->words[4];
	for (index = 0; index < 8; index++)
	{
		placeholders[index].data = answer->words[5 + 2 * index];
		placeholders[index].asset = (int32_t)answer->words[6 + 2 * index];
	}
	stamps.count = 0;
	stamps.eighths = 0;
	caches_invalidate();
	/* the chunks' room the RSX can still read: from the first frame it has not finished */
	frame_starts[frame_index % SPU_D3D_FRAMES] = chunk_start;
	if (frame_index - finished <= SPU_D3D_FRAMES - 1)
	{
		busy_start = frame_starts[(finished + 1) % SPU_D3D_FRAMES];
		busy_all = FALSE;
	}
	else
	{
		busy_all = TRUE;
	}
	/* present_run after ps3_gpu_present: a new frame's programs and constants are sent again */
	constants_mark(0, _rsx_vp_constant_count - 1);
	textures_begin_frame();
	device.frame++;
	if (device.frame == 1 || device.frame % STATISTICS_FRAMES == 0)
	{
		textures_measure();
		statistics_put();
		request(_spu_d3d_request_log, 0);
		statistics.draws = statistics.skipped_program = statistics.skipped_target = statistics.skipped_scratch = 0;
		statistics.skipped_scissor = 0;
		statistics.cached_elements = statistics.converted_elements = statistics.cache_refused = 0;
		statistics.chunks = statistics.chunk_bytes = statistics.requests = statistics.request_ticks = 0;
		memset(statistics.type_elements, 0, sizeof(statistics.type_elements));
		texture_hashed_bytes = 0;
	}
	else
	{
		/* (the timers' log reads them at any frame) */
		statistics_put();
	}
}

/* ---------- the records' loop */

static void record_execute(const spu_d3d_record *record, uint64_t position)
{
	const void *payload = record + 1;

	switch (record->type)
	{
	case _spu_d3d_record_states:
		states_apply(payload, record->a);
		break;
	case _spu_d3d_record_attributes:
		attributes_apply(record->a, payload);
		break;
	case _spu_d3d_record_texture:
		if (record->a < TEXTURE_STAGES)
		{
			memcpy(&device.stages[record->a], payload, sizeof(spu_d3d_stage_texture));
			draw_state_changes |= CHANGED_TEXTURE(record->a);
		}
		break;
	case _spu_d3d_record_vertex_shader:
		vertex_shader_apply(record->a, (uint32_t)*(const uint64_t *)payload, record->b);
		break;
	case _spu_d3d_record_constants:
		if (record->a < 192 && record->a + record->b <= 192)
			constants_store(record->a, payload, record->b);
		break;
	case _spu_d3d_record_constant_mode:
		device.shader_constant_mode = record->a;
		viewport_update_constants();
		break;
	case _spu_d3d_record_viewport:
		memcpy(&device.viewport, payload, sizeof(device.viewport));
		viewport_update_constants();
		break;
	case _spu_d3d_record_render_target:
		render_target_apply(payload, record->b != 0);
		break;
	case _spu_d3d_record_ui_offset:
		ui_offset = (int32_t)record->a;
		break;
	case _spu_d3d_record_clear:
		clear_run(record, position);
		break;
	case _spu_d3d_record_draw:
		draw_vertices_run(payload);
		break;
	case _spu_d3d_record_draw_indexed:
		draw_indexed_run(payload);
		break;
	case _spu_d3d_record_immediate:
		immediate_run(payload, setup.ring + ((uint32_t)position & (setup.ring_bytes - 1)) + sizeof(spu_d3d_record) +
			sizeof(spu_d3d_record_immediate));
		break;
	case _spu_d3d_record_elements:
		elements_run(payload, setup.ring + ((uint32_t)position & (setup.ring_bytes - 1)) + sizeof(spu_d3d_record) +
			sizeof(spu_d3d_record_elements));
		break;
	case _spu_d3d_record_timestamp:
		gpu_timestamp((int)record->a);
		draw_stamps_split((int)record->a);
		break;
	case _spu_d3d_record_draw_stamps:
		draw_stamps_set((int)record->a - 1);
		break;
	case _spu_d3d_record_present:
		present_run(payload);
		break;
	case _spu_d3d_record_screen:
	{
		const spu_d3d_record_screen *screen = payload;

		/* the PPE freed the targets of the size before (halo_screen_commit, render_targets_evict) */
		screen_width = screen->width;
		screen_scale[0] = screen->scale[0];
		screen_scale[1] = screen->scale[1];
		target_count = 0;
		render_target_generation++;
		texture_epoch++;
		zcull_bound = 0;
		zcull_offset = 0;
		surface_valid = 0;
		scaled.on = FALSE;
		break;
	}
	case _spu_d3d_record_tag_cache:
	{
		const spu_d3d_record_tag_cache *span = payload;

		/* (d3d8_tag_cache_loaded) every copy goes, as they share one block */
		arrays_reset();
		if (arena.used)
		{
			/* the frames in flight can still read the copies */
			request(_spu_d3d_request_wait_idle, 0);
			arena.used = 0;
		}
		if (!static_end || span->begin < static_begin)
			static_begin = span->begin;
		if (span->end > static_end)
			static_end = span->end;
		break;
	}
	case _spu_d3d_record_label:
		rsx_write_backend_label(record->a, record->b);
		break;
	case _spu_d3d_record_particles:
		if (record->a)
			particles_begin(payload);
		else
			particles_end();
		break;
	case _spu_d3d_record_light_bounds:
		light_bounds.on = record->a != 0;
		if (light_bounds.on)
			memcpy(&light_bounds.bounds, payload, sizeof(light_bounds.bounds));
		break;
	case _spu_d3d_record_scale:
		if (record->a)
			scale_begin(payload);
		else
			scale_end();
		break;
	default:
		break;
	}
}

/* the LS bytes a record needs (the rest by its EA) */
static uint32_t record_need(const spu_d3d_record *record)
{
	switch (record->type)
	{
	case _spu_d3d_record_states: return 16 + record->a * 8;
	case _spu_d3d_record_attributes: return 16 + 16 * 16;
	case _spu_d3d_record_constants: return 16 + record->b * 16;
	case _spu_d3d_record_clear: return 16 + sizeof(spu_d3d_record_clear);
	case _spu_d3d_record_draw:
	case _spu_d3d_record_draw_indexed: return 16 + sizeof(spu_d3d_record_draw) + 16 * sizeof(spu_d3d_record_stream);
	case _spu_d3d_record_elements: return 16 + sizeof(spu_d3d_record_elements);
	default: return 64;
	}
}

static uint32_t record_draw_number(const spu_d3d_record *record)
{
	if (record->type == _spu_d3d_record_draw || record->type == _spu_d3d_record_draw_indexed)
		return ((const spu_d3d_record_draw *)(record + 1))->draw;
	if (record->type == _spu_d3d_record_immediate)
		return ((const spu_d3d_record_immediate *)(record + 1))->draw;
	if (record->type == _spu_d3d_record_elements)
		return ((const spu_d3d_record_elements *)(record + 1))->draw;
	return 0;
}

/* the records published so far; their room back every 16 records and at the last, the game thread woken there */
static void records_run(void)
{
	uint64_t position = echo.consumed, published = bell.published;
	uint32_t busy = timer_now(), done = 0;

	while (position != published && !quitting)
	{
		const spu_d3d_record *header = (const spu_d3d_record *)ring_bytes(position, 16);
		uint32_t bytes = header->bytes, type = header->type, need, draw, start;
		const spu_d3d_record *record;

		if (bytes < 16 || (bytes & 15))
			break;
		echo.record_type = type;
		if (type == _spu_d3d_record_wrap)
		{
			position += bytes;
			continue;
		}
		need = record_need(header);
		if (need > bytes)
			need = bytes;
		start = timer_now();
		commands_room();
		/* (a request can move the window: the record is read from it after) */
		record = (const spu_d3d_record *)ring_bytes(position, need);
		draw = record_draw_number(record);
		record_execute(record, position);
		if (type <= _spu_d3d_record_present)
		{
			timer_add(_timer_record_wrap + (int)type, start);
			statistics.counts[_timer_record_wrap + type]++;
		}
		position += bytes;
		if (draw)
			echo.completed_draws = draw;
		if (!(++done & 15) || position == published)
		{
			echo.consumed = position;
			put_echo();
			if (bell.notify && (int64_t)(position - bell.notify) >= 0)
				event_send(SPU_D3D_GAME_PORT, 1);
		}
	}
	echo.consumed = position;
	timer_add(_timer_thread_busy, busy);
}

typedef char initial_size_check[sizeof(spu_d3d_initial) <= sizeof(inputs) ? 1 : -1];

static void initial_state(void)
{
	spu_d3d_initial *initial = (spu_d3d_initial *)inputs;
	uint32_t index;

	dma_get(initial, setup.initial, sizeof(*initial), TAG_MISC);
	dma_get(byte_floats, setup.byte_floats, sizeof(byte_floats), TAG_MISC);
	dma_get(fragment_buckets, setup.fragment_buckets, sizeof(fragment_buckets), TAG_MISC);
	dma_wait(1 << TAG_MISC);
	memcpy(render_state, initial->render_state, sizeof(render_state));
	memcpy(texture_state, initial->texture_state, sizeof(texture_state));
	memcpy(device.targets, initial->targets, sizeof(device.targets));
	device.viewport = initial->viewport;
	device.shader_constant_mode = initial->shader_constant_mode;
	memcpy(device.constants, initial->constants, sizeof(device.constants));
	memcpy(device.attributes, initial->attributes, sizeof(device.attributes));
	device.constants_dirty_first = _rsx_vp_constant_count;
	device.constants_dirty_last = 0;
	constants_mark(0, _rsx_vp_constant_count - 1);
	screen_width = initial->screen_width;
	screen_scale[0] = initial->screen_scale[0];
	screen_scale[1] = initial->screen_scale[1];
	static_begin = initial->static_begin;
	static_end = initial->static_end;
	button_style = initial->button_style;
	for (index = 0; index < 8; index++)
	{
		placeholders[index].data = initial->placeholders[index][0];
		placeholders[index].asset = (int32_t)initial->placeholders[index][1];
	}
	frame_index = setup.frame_index;
	frame_starts[frame_index % SPU_D3D_FRAMES] = 0;
	caches_invalidate();
	textures_reset();
	arrays_reset();
	memset(fragment_cache_index, 0xff, sizeof(fragment_cache_index));
	memset(texture_cache_dirty_flags, 0, sizeof(texture_cache_dirty_flags));
	busy_start = 0;
	busy_all = FALSE;
}

int main(uint64_t bell_address, uint64_t echo_address, uint64_t setup_address)
{
	bell_ea = bell_address;
	echo_ea = echo_address;
	mfc_get(&setup, setup_address, sizeof(setup), TAG_MISC, 0, 0);
	dma_wait(1 << TAG_MISC);
	initial_state();
	staging_block = 0;
	block_offset = 0;
	block_flushed = 0;
	cmd = staging[0];
	chunk_start = 0;
	echo.state = SPU_D3D_RUNNING;
	echo.ls_address = (uint32_t)&echo;
	put_echo();
	spu_write_event_mask(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_event_ack(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_decrementer(DECREMENTER_TICKS);
	for (;;)
	{
		/* the check after each reservation closes the race with a store that lands before it */
		bell_read();
		if (bell.quit || quitting)
			break;
		if (bell.published != echo.consumed)
		{
			records_run();
			/* the commands so far to the RSX while the game thread records */
			chunk_publish();
			put_echo();
			if (bell.notify && (int64_t)(echo.consumed - bell.notify) >= 0)
				event_send(SPU_D3D_GAME_PORT, 1);
			continue;
		}
		bell_wait();
	}
	chunk_publish();
	echo.state = SPU_D3D_EXITED;
	put_echo();
	spu_thread_exit(0);
	return 0;
}
