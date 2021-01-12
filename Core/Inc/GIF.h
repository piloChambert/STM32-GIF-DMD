#ifndef GIF_H
#define GIF_H

#include <stdint.h>
#include <integer.h>
#include <ff.h>

typedef int(*GIFStreamReadCallback)(void*, UINT, UINT*); // called when GIF needed to read data
typedef FSIZE_t(*GIFStreamTellCallback)(); // called to get the position in current stream
typedef void(*GIFStreamSeekCallback)(FSIZE_t); // called to reposition the stream

typedef enum {
	GIF_NO_ERROR = 0,
	GIF_STREAM_ERROR, // steam read error
	GIF_DECODE_OVERFLOW,
	GIF_STREAM_FINISHED // returned when there is no more frame to read
} GIFError;

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

typedef struct __attribute__((packed))
{
	uint8_t size;
	uint8_t pad;
	uint16_t repeatCount;
	uint8_t terminator;
} GIFNetscapeApplicationExtension;

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
	uint16_t repeatCount;

	// stream callback
	GIFStreamReadCallback streamReadCallback;
	GIFStreamTellCallback streamTellCallback;
	GIFStreamSeekCallback streamSeekCallback;
};

extern uint8_t frame[128 * 32];

extern struct GIFInfo GIFInfo;

// read a gif image from current stream
extern GIFError ReadGifImage();

// start GIF reading
extern GIFError LoadGIF();

#endif
