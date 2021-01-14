#include "Gif.h"
#include "profiling.h"
#include <math.h>
#include <string.h>

struct GIFInfo GIFInfo;

uint8_t sRGB2RGB(uint8_t v) {
	return powf(v / 255.0f, 2.2f) * 255.0f;
}

// code palette for DMD
// it does the sRGB to RGB conversion
void CodePalette(uint8_t *palette, uint8_t *dst, int colorCount) {
	for(int i = 0; i < colorCount; i++) {
		uint8_t r = sRGB2RGB(palette[i * 3 + 0]);
		uint8_t g = sRGB2RGB(palette[i * 3 + 1]);
		uint8_t b = sRGB2RGB(palette[i * 3 + 2]);
		for(int p = 0; p < 8; p++) {
			uint8_t m = 1 << p;
			dst[i + p * 256] = (r & m ? 1 : 0) + (g & m ? 2 : 0) + (b & m ? 4 : 0);
		}
	}
}

struct {
	uint16_t prev;
	uint8_t k;
	uint8_t l;
} dict[4096];
int dictSize = 0;
int colorCount;

void ClearDict(int mcs) {
	colorCount = 1 << mcs;
	dictSize = colorCount + 2;
}

uint8_t imageSubData[256];
uint16_t imageSubDataSize = 0;
int imageSubDataIdx = 0;
int imageSubDataBitsLeft = 8;

GIFError LoadImageSubData() {
	imageSubDataSize = 0;
	imageSubDataIdx = 0;
	imageSubDataBitsLeft = 8;

	UINT l;
	GIFError err = GIFInfo.streamReadCallback(&imageSubDataSize, 1, &l);
	if(err != GIF_NO_ERROR)
		return err; // stream read error

	if(imageSubDataSize > 0) {
		//printf2("data size = %d\r\n", imageSubDataSize);
		GIFInfo.streamReadCallback(imageSubData, imageSubDataSize, &l);
	}

	return GIF_NO_ERROR;
}

uint16_t EOI = 0;
GIFError GetNextCode(int codeSize, uint16_t *code) {
	*code = 0;
	int bitCount = 0;
	while(bitCount < codeSize) {
		*code += (imageSubData[imageSubDataIdx] >> (8 - imageSubDataBitsLeft)) << bitCount;

		if(imageSubDataBitsLeft < codeSize - bitCount) {
			bitCount += imageSubDataBitsLeft;

			imageSubDataIdx++;
			if(imageSubDataIdx >= imageSubDataSize) {
				GIFError err = LoadImageSubData();
				if(err != GIF_NO_ERROR)
					return err; // propagate error

				if(imageSubDataSize == 0)
					break; // no more data
			}

			imageSubDataBitsLeft = 8;
		}
		else {
			imageSubDataBitsLeft -= (codeSize - bitCount);
			bitCount = codeSize;
		}
	}

	*code = *code & ((1 << codeSize) - 1);
	return GIF_NO_ERROR;
}

GIFError Decode(int mcs) {
	int compressedSize = mcs + 1;
	int clearCode = 1 << mcs;
	EOI = clearCode + 1;

	GIFInfo.frameWriteIndex = 0;

	GIFError err;
	err = LoadImageSubData(); // load first data chunk
	if(err != GIF_NO_ERROR)
		return err;

	uint16_t current = 0;
	uint16_t last = 0;

	while(1) { // XXX warning!!!
		// get current code
		err = GetNextCode(compressedSize, &current);
		if(err != GIF_NO_ERROR)
			return err;

		if(current == clearCode)
			ClearDict(mcs);

		else if(current == EOI)
			return GIF_NO_ERROR; // we're done decoding

		else if(current < dictSize) {
			// output
			int l = current > colorCount ? dict[current].l : 1;
			uint16_t k = current;
			for(int i = l; i > 1; i--) {
				GIFInfo.frame[(GIFInfo.frameWriteIndex + i-1) & 0xFFF] = dict[k].k;
				k = dict[k].prev;
			}
			GIFInfo.frame[GIFInfo.frameWriteIndex & 0xFFF] = k;
			GIFInfo.frameWriteIndex += l;

			// add new code
			if(last != clearCode) {
				// add new code
				dict[dictSize].prev = last;
				dict[dictSize].k = k;
				dict[dictSize].l = (last < colorCount ? 1 : dict[last].l) + 1;
				dictSize++;

				if(dictSize >= (1 << compressedSize))
					compressedSize++;
			}
		}
		else {
			// output
			int l = last > colorCount ? dict[last].l : 1;
			uint16_t k = last;
			for(int i = l; i > 1; i--) {
				GIFInfo.frame[(GIFInfo.frameWriteIndex + i-1) & 0xFFF] = dict[k].k;
				k = dict[k].prev;
			}
			GIFInfo.frame[GIFInfo.frameWriteIndex & 0xFFF] = k;
			GIFInfo.frame[(GIFInfo.frameWriteIndex + l) & 0xFFF] = k;
			GIFInfo.frameWriteIndex += l + 1;

			// add new code
			dict[dictSize].prev = last;
			dict[dictSize].k = k;
			dict[dictSize].l = l + 1;

			dictSize++;

			if(dictSize >= (1 << compressedSize))
				compressedSize++;
		}

		last = current;

		if(GIFInfo.frameWriteIndex > 4096 || dictSize > 4096) {
			return GIF_DECODE_OVERFLOW; // we should never be here
		}
	}

	return GIF_NO_ERROR;
}


GIFError ReadGifPalette(uint8_t *palette, int colorCount) {
	UINT l;
	GIFError err = GIFInfo.streamReadCallback(palette, sizeof(uint8_t) * 3 * colorCount, &l);
	if(err != GIF_NO_ERROR)
		return err;

	CodePalette(palette, GIFInfo.codedGlobalPalette, colorCount);

	return GIF_NO_ERROR;
}

uint8_t extBuffer[256];
uint16_t frameIdx = 0;
GIFError ReadGifImage() {
	UINT l;

	while(1) {
		uint8_t sep;
		GIFError err = GIFInfo.streamReadCallback(&sep, sizeof(uint8_t), &l);
		if(err != GIF_NO_ERROR)
			return err;

		if(sep == 0x3b) {// ended
			return GIF_STREAM_FINISHED;
		}

		else if(sep == 0x21) {
			GIFExtensionHeader extHeader;
			err = GIFInfo.streamReadCallback(&extHeader, sizeof(GIFExtensionHeader), &l);
			if(err != GIF_NO_ERROR)
				return err;

			if(extHeader.label == 0xF9) {
				GIFGraphicsControlExtension desc;
				err = GIFInfo.streamReadCallback(&desc, sizeof(GIFGraphicsControlExtension), &l);
				if(err != GIF_NO_ERROR)
					return err;

				GIFInfo.delayTime = desc.delayTime * 10; // us
				GIFInfo.hasTransparentColor = desc.flags & 0x01;
				GIFInfo.transparentColor = desc.transparentColorIndex;
			}
			else if(extHeader.label == 0xFF) {
				// Application Extension
				// read app name and version
				err = GIFInfo.streamReadCallback(&extBuffer, 11, &l);
				if(err != GIF_NO_ERROR)
					return err;

				if(strncmp((char *)extBuffer, "NETSCAPE2.0", 11) == 0) {
					GIFNetscapeApplicationExtension ext;
					err = GIFInfo.streamReadCallback(&ext, sizeof(GIFNetscapeApplicationExtension), &l);
					if(err != GIF_NO_ERROR)
						return err;

					GIFInfo.repeatCount = ext.repeatCount;
				}
				else {
					// read every data sub block
					uint8_t subBlockSize;
					err = GIFInfo.streamReadCallback(&subBlockSize, 1, &l);
					if(err != GIF_NO_ERROR)
						return err;

					while(subBlockSize > 0) {
						err = GIFInfo.streamReadCallback(&extBuffer, subBlockSize, &l);
						if(err != GIF_NO_ERROR)
							return err;

						err = GIFInfo.streamReadCallback(&subBlockSize, 1, &l);
						if(err != GIF_NO_ERROR)
							return err;
					}
				}
			}
			else if(extHeader.label == 0xFE) {
				// Comment Extension
				err = GIFInfo.streamReadCallback(&extBuffer, extHeader.blockSize + 1, &l);
				if(err != GIF_NO_ERROR)
					return err;
			}
			else {
				// read remaining bytes
				err = GIFInfo.streamReadCallback(&extBuffer, extHeader.blockSize, &l);
				if(err != GIF_NO_ERROR)
					return err;
			}
		}

		else if(sep == 0x2c) {
			// image data
			GIFImageDescriptor desc;
			err = GIFInfo.streamReadCallback(&desc, sizeof(GIFImageDescriptor), &l);
			if(err != GIF_NO_ERROR)
				return err;

			GIFInfo.localPaletteColorCount = 2 << (desc.flags & 0x07);
			GIFInfo.useLocalPalette = desc.flags & 0x80 ? 1 : 0;
			if(GIFInfo.useLocalPalette) {
				err = ReadGifPalette(GIFInfo.localPalette, GIFInfo.localPaletteColorCount);
				if(err != GIF_NO_ERROR)
					return err;
			}

			// read min code size
			uint8_t mcs;
			err = GIFInfo.streamReadCallback(&mcs, 1, &l);
			if(err != GIF_NO_ERROR)
				return err;

			PROFILING_EVENT("FindImage");

			// decode GIF
			err = Decode(mcs);
			if(err != GIF_NO_ERROR)
				return err;

			PROFILING_EVENT("Decode");
			frameIdx++;

			break;
		}
	}

	return GIF_NO_ERROR;
}

GIFError LoadGIF() {
	GIFHeader header;
	UINT l;
	GIFError err = GIFInfo.streamReadCallback(&header, sizeof(GIFHeader), &l);
	if(err != GIF_NO_ERROR)
		return err;

	//printf2("GIF resolution: %dx%d\r\n", header.width, header.height);
	//printf2("background color index: %d\r\n", header.backgroundColorIndex);

	// read global palette
	int hasColorTable = header.flags & 0x80 ? 1 : 0;
	int colorResolution = 2 << ((header.flags & 0x70) >> 4);
	GIFInfo.globalPaletteColorCount = 2 << (header.flags & 0x07);

	//printf2("Color resolution: %d\r\n", colorResolution);
	//printf2("Has global color palette: %d\r\n", hasColorTable);
	//printf2("Color count: %d\r\n", GIFInfo.globalPaletteColorCount);

	if(hasColorTable) {
		err = ReadGifPalette(GIFInfo.globalPalette, GIFInfo.globalPaletteColorCount);
		if(err != GIF_NO_ERROR)
			return err;
	}

	// look for image data now
	GIFInfo.gifStart = GIFInfo.streamTellCallback();

	return GIF_NO_ERROR;
}
