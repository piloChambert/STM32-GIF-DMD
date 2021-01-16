#ifndef DMD_H
#define DMD_H

#include <stdint.h>

extern volatile uint8_t swapBufferRequest;
extern volatile float luminosityAttenuation;

extern void DMDInit();
extern void SwapDMDBuffers();
extern void EncodeFrameToDMDBuffer(uint8_t *frame, uint8_t *codedPalette);
extern void DMDMatrixFrame();

#endif
