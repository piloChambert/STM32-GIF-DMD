#ifndef GIF_H
#define GIF_H

#include <stdint.h>
#include <integer.h>
#include <ff.h>

typedef int(*GIFStreamReadCallback)(void*, UINT, UINT*); // called when GIF needed to read data
typedef FSIZE_t(*GIFStreamTellCallback)(); // called to get the position in current stream
typedef void(*GIFStreamSeekCallback)(FSIZE_t); // called to reposition the stream
typedef void(*GIFStreamEndCallback)(); // called when GIF animation is over (one loop
typedef void(*GIFStreamError)(); // called when there is a read error in the stream

typedef struct __attribute__((packed))
{
    uint32_t  signatureHi;             //  "GIF8"
    uint16_t  signatureLo;             //  GIF version: "7a" or "9a"
    uint16_t  width;                   //  logical screen width in pixels
    uint16_t  height;                  //  logical screen height in pixels
    uint8_t   flags;                   //  Global Color Table specification
    uint8_t   backgroundColorIndex;              //  background color
    uint8_t   ratio;                   //  default pixel aspect ratio
} GIFHeader;

typedef struct __attribute__((packed))
{
	uint8_t label;
	uint8_t blockSize;
} GIFExtensionHeader;

typedef struct __attribute__((packed))
{
	uint16_t left;         /* X position of image on the display */
	uint16_t top;          /* Y position of image on the display */
	uint16_t width;        /* Width of the image in pixels */
	uint16_t height;       /* Height of the image in pixels */
	uint8_t flags;       /* Image and Color Table Data Information */
} GIFImageDescriptor;

typedef struct __attribute__((packed))
{
	uint8_t flags;
	uint16_t delayTime;
	uint8_t transparentColorIndex;
	uint8_t terminator; // always 0
} GIFGraphicsControlExtension;

struct GIFInfo {
	FSIZE_t gifStart;

	int globalPaletteColorCount;
	uint8_t globalPalette[256 * 3];

	int useLocalPalette;
	int localPaletteColorCount;
	uint8_t localPalette[256 * 3];

	uint8_t codedGlobalPalette[256 * 8];

	uint8_t frame[128 * 32];
	uint16_t frameWriteIndex;

	uint16_t delayTime;

	// stream callback
	GIFStreamReadCallback streamReadCallback;
	GIFStreamTellCallback streamTellCallback;
	GIFStreamSeekCallback streamSeekCallback;
	GIFStreamEndCallback streamEndCallback;
	GIFStreamError streamErrorCallback;
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
