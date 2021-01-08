#ifndef GIF_H
#define GIF_H

#include <stdint.h>
#include <integer.h>
#include <ff.h>

typedef int(*GIFStreamReadFunc)(void*, UINT, UINT*);
typedef FSIZE_t(*GIFStreamTell)();
typedef void(*GIFStreamSeek)(FSIZE_t);

typedef struct __attribute__((packed))
{
    uint32_t  signatureHi;             //  "GIF8"
    uint16_t  signatureLo;             //  GIF version: "7a" or "9a"
    uint16_t  width;                   //  logical screen width in pixels
    uint16_t  height;                  //  logical screen height in pixels
    uint8_t   flags;                   //  Global Color Table specification
    uint8_t   backgroundColorIndex;              //  background color
    uint8_t   ratio;                   //  default pixel aspect ratio
} GifHeader;

typedef struct __attribute__((packed))
{
	uint8_t label;
	uint8_t blockSize;
} GifExtensionHeader;

typedef struct __attribute__((packed))
{
	uint8_t flags;
	uint16_t delayTime;
	uint8_t transparentColorIndex;
	uint8_t terminator; // always 0
} GifGraphicsControlExtension;

typedef struct __attribute__((packed))
{
	uint16_t left;         /* X position of image on the display */
	uint16_t top;          /* Y position of image on the display */
	uint16_t width;        /* Width of the image in pixels */
	uint16_t height;       /* Height of the image in pixels */
	uint8_t flags;       /* Image and Color Table Data Information */
} GifImageDescriptor;

struct GIFInfo {
	FSIZE_t gifStart;

	int globalPaletteColorCount;
	uint8_t globalPalette[256 * 3];

	int useLocalPalette;
	int localPaletteColorCount;
	uint8_t localPalette[256 * 3];

	uint8_t codedGlobalPalette[256 * 8];

	uint16_t delayTime;

	// stream callback
	GIFStreamReadFunc streamReadCallback;
	GIFStreamTell streamTellCallback;
	GIFStreamSeek streamSeekCallback;
};

extern uint8_t frame[128 * 32];

extern struct GIFInfo GIFInfo;

// Convert sRGB color to RGB
extern uint8_t sRGB2RGB(uint8_t v);

// encode palette to GIFInfo.codedGlobalPalette
extern void CodePalette(uint8_t *palette, int colorCount);

extern void ReadGifPalette(uint8_t *palette, int colorCount);

// read a gif image from current stream
extern void ReadGifImage();

// start GIF reading
extern void LoadGIF();

#endif
