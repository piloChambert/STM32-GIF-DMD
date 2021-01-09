#include "Gif.h"
#include "profiling.h"
#include <math.h>

struct GIFInfo GIFInfo;

uint8_t sRGB2RGB(uint8_t v) {
	return powf(v / 255.0f, 2.2f) * 255.0f;
}

void CodePalette(uint8_t *palette, int colorCount) {
	for(int p = 0; p < 8; p++) {
		uint8_t m = 1 << p;

		for(int i = 0; i < colorCount; i++) {
			GIFInfo.codedGlobalPalette[i + p * 256] = (palette[i * 3 + 0] & m ? 1 : 0) + (palette[i * 3 + 1] & m ? 2 : 0) + (palette[i * 3 + 2] & m ? 4 : 0);
		}
	}
}

int frameIdx = 0;
uint8_t frame[128 * 32];

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

int LoadImageSubData() {
	imageSubDataSize = 0;
	imageSubDataIdx = 0;
	imageSubDataBitsLeft = 8;

	UINT l;
	if(!GIFInfo.streamReadCallback(&imageSubDataSize, 1, &l))
		return 0;

	if(imageSubDataSize > 0) {
		//printf2("data size = %d\r\n", imageSubDataSize);
		GIFInfo.streamReadCallback(imageSubData, imageSubDataSize, &l);
		return 1;
	}

	return 0; // no more data!
}

uint16_t GetNextCode(int codeSize) {
	uint16_t code = 0;

	int bitCount = 0;
	while(bitCount < codeSize) {
		code += (imageSubData[imageSubDataIdx] >> (8 - imageSubDataBitsLeft)) << bitCount;

		if(imageSubDataBitsLeft < codeSize - bitCount) {
			bitCount += imageSubDataBitsLeft;

			imageSubDataIdx++;
			if(imageSubDataIdx >= imageSubDataSize) {
				if(!LoadImageSubData())
					break;
			}

			imageSubDataBitsLeft = 8;
		}
		else {
			imageSubDataBitsLeft -= (codeSize - bitCount);
			bitCount = codeSize;
		}
	}

	return code & ((1 << codeSize) - 1);
}

void Decode(int mcs) {
	int compressedSize = mcs + 1;
	int clearCode = 1 << mcs;
	int eoi = clearCode + 1;

	frameIdx = 0;

	LoadImageSubData(); // load first data chunk

	uint16_t current = 0;
	uint16_t last = 0;

	while(1) { // XXX warning!!!
		// get current code
		current = GetNextCode(compressedSize);

		if(current == clearCode)
			ClearDict(mcs);

		else if(current == eoi)
			return; // we're done decoding

		else if(current < dictSize) {
			// output
			int l = current > colorCount ? dict[current].l : 1;
			uint16_t k = current;
			for(int i = l; i > 1; i--) {
				frame[frameIdx + i-1] = dict[k].k;
				k = dict[k].prev;
			}
			frame[frameIdx] = k;
			frameIdx += l;

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
				frame[frameIdx + i-1] = dict[k].k;
				k = dict[k].prev;
			}
			frame[frameIdx] = k;
			frame[frameIdx + l] = k;
			frameIdx += l + 1;

			// add new code
			dict[dictSize].prev = last;
			dict[dictSize].k = k;
			dict[dictSize].l = l + 1;

			dictSize++;

			if(dictSize >= (1 << compressedSize))
				compressedSize++;
		}

		last = current;

		if(frameIdx > 4096 || dictSize > 4096) {
			printf2("Not Good!!\r\n");
		}

	}
}


void ReadGifPalette(uint8_t *palette, int colorCount) {
	UINT l;
	if(!GIFInfo.streamReadCallback(palette, sizeof(uint8_t) * 3 * colorCount, &l)) {
		return;
	}

	// gamma correction ???
	for(int i = 0; i < colorCount * 3; i++) {
		palette[i] = sRGB2RGB(palette[i]);
	}

	CodePalette(palette, colorCount);
}

uint8_t extBuffer[256];
void ReadGifImage() {
	UINT l;

	while(1) {
		uint8_t sep;
		GIFInfo.streamReadCallback(&sep, sizeof(uint8_t), &l);

		if(sep == 0x3b) // rewind
			GIFInfo.streamSeekCallback(GIFInfo.gifStart);

		else if(sep == 0x21) {
			GifExtensionHeader extHeader;
			GIFInfo.streamReadCallback(&extHeader, sizeof(GifExtensionHeader), &l);

			if(extHeader.label == 0xF9) {
				GifGraphicsControlExtension desc;
				GIFInfo.streamReadCallback(&desc, sizeof(GifGraphicsControlExtension), &l);

				GIFInfo.delayTime = desc.delayTime * 10; // us
				//printf2("delay time: %d\r\n", delayTime);
			}
			else if(extHeader.label == 0xFF) {
				// Application Extension
				GIFInfo.streamReadCallback(&extBuffer, 16, &l);
			}
			else if(extHeader.label == 0xFE) {
				// Application Extension
				GIFInfo.streamReadCallback(&extBuffer, extHeader.blockSize + 1, &l);
			}
			else {
				// read remaining bytes
				GIFInfo.streamReadCallback(&extBuffer, extHeader.blockSize, &l);
			}
		}

		else if(sep == 0x2c) {
			// image data
			GifImageDescriptor desc;
			GIFInfo.streamReadCallback(&desc, sizeof(GifImageDescriptor), &l);

			GIFInfo.localPaletteColorCount = 2 << (desc.flags & 0x07);
			GIFInfo.useLocalPalette = desc.flags & 0x80 ? 1 : 0;
			if(GIFInfo.useLocalPalette) {
				ReadGifPalette(GIFInfo.localPalette, GIFInfo.localPaletteColorCount);
			}

			// decode GIF

			// read min code size
			uint8_t mcs;
			GIFInfo.streamReadCallback(&mcs, 1, &l);
			PROFILING_EVENT("FindImage");

			Decode(mcs);
			PROFILING_EVENT("Decode");
			break;
		}
	}
}

void LoadGIF() {
	GifHeader header;
	UINT l;
	if(!GIFInfo.streamReadCallback(&header, sizeof(GifHeader), &l)) {
		SendUART("Can't read gif header!);");
		return;
	}

	printf2("GIF resolution: %dx%d\r\n", header.width, header.height);
	printf2("background color index: %d\r\n", header.backgroundColorIndex);

	// read global palette
	int hasColorTable = header.flags & 0x80 ? 1 : 0;
	int colorResolution = 2 << ((header.flags & 0x70) >> 4);
	GIFInfo.globalPaletteColorCount = 2 << (header.flags & 0x07);

	printf2("Color resolution: %d\r\n", colorResolution);
	printf2("Has global color palette: %d\r\n", hasColorTable);
	printf2("Color count: %d\r\n", GIFInfo.globalPaletteColorCount);

	if(hasColorTable) {
		ReadGifPalette(GIFInfo.globalPalette, GIFInfo.globalPaletteColorCount);
	}

	// look for image data now
	GIFInfo.gifStart = GIFInfo.streamTellCallback();
}
