#ifndef DMD_H
#define DMD_H

#include <stdint.h>

extern volatile uint8_t swapBufferRequest;
extern volatile uint8_t *readBuffer;
extern volatile uint8_t *writeBuffer;

extern void DMDInit();
extern void SwapDMDBuffers();
extern void EncodeFrameToDMDBuffer(uint8_t *frame, uint8_t *codedPalette);
extern void DMDMatrixFrame();
extern void SetDMDLuminosity(uint8_t lum);

#endif
