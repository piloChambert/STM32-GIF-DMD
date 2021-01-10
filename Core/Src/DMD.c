#include "DMD.h"

#include "dma.h"
#include "tim.h"

uint8_t DMDBuffer[2][128 * 16 * 8];
volatile uint8_t *readBuffer = DMDBuffer[0];
volatile uint8_t *writeBuffer = DMDBuffer[1];
volatile uint8_t swapBufferRequest = 0;

void SwapDMDBuffers() {
	readBuffer = readBuffer == DMDBuffer[0] ? DMDBuffer[1] : DMDBuffer[0];
	writeBuffer = writeBuffer == DMDBuffer[0] ? DMDBuffer[1] : DMDBuffer[0];
	swapBufferRequest = 0;
}

void InitDMDBuffer() {
	uint16_t idx = 0;
	for(int p = 0; p < 8; p++) {
		uint8_t m = 1 << p;
		for(int y = 0; y < 16; y++) {
			for(int x = 0; x < 128; x++) {
				uint8_t c = x;
				uint8_t col0 = (c & m ? 1 : 0) + (c & m ? 2 : 0) + (c & m ? 4 : 0);
				uint8_t col1 = (c & m ? 1 : 0) + (c & m ? 2 : 0) + (c & m ? 4 : 0);
				writeBuffer[idx++] = col0 + (col1 << 3);
			}
		}
	}
}

// copy a color indexed frame, with a coded palette into dmd write buffer
void EncodeFrameToDMDBuffer(uint8_t *frame, uint8_t *codedPalette) {
	uint16_t idx = 0;
	for(uint8_t p = 0; p < 8; p++) {
		uint16_t idx2 = 0;
		for(uint16_t j = 0;  j < 128 * 16; j++) {
			uint8_t col0 = frame[idx2];
			uint8_t px0 = codedPalette[p * 256 + col0];

			uint8_t col1 = frame[idx2++ + 16 * 128];
			uint8_t px1 = codedPalette[p * 256 + col1];

			uint8_t v = px0 + (px1 << 5);
			writeBuffer[idx++] = v;
		}
	}
}

/* 9 (DMA IRQ callbacks) */
void data_tramsmitted_handler(DMA_HandleTypeDef *hdma)
{
    /* Stop timer */
    __HAL_TIM_DISABLE(&htim1); //TIM1->CR1 &= ~TIM_CR1_CEN;
}

void transmit_error_handler(DMA_HandleTypeDef *hdma)
{
    /* Stop timer */
    __HAL_TIM_DISABLE(&htim1);
    /* Some error handle ? */
}

void DMDInit() {
	  HAL_TIM_Base_Start_IT(&htim4);
	  HAL_TIM_PWM_Init(&htim4);
	  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);

	  HAL_TIM_PWM_Init(&htim1);
	  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);

	  /* Callbacks for DMA IRQs */
	  htim1.hdma[TIM_DMA_ID_CC2]->XferCpltCallback = data_tramsmitted_handler;
	  htim1.hdma[TIM_DMA_ID_CC2]->XferErrorCallback = transmit_error_handler;
}

//volatile uint8_t pass = 0;
volatile uint8_t y = 0;
volatile uint8_t renderIdx = 0;

void DMDMatrixFrame() {
	//TIM4->CR1 &= ~TIM_CR1_CEN;

	GPIOA->ODR = (GPIOA->ODR & ~(0x01E)) | (y<<1); // set row
	GPIOB->BSRR = GPIO_PIN_8; // strobe up

	renderIdx++;
	int prevPass = renderIdx & 0x07;
	y = (renderIdx >> 3) & 0x0F;

	// swap buffer if needed
	if(swapBufferRequest && !(renderIdx & 0x7F)) {
		SwapDMDBuffers();
	}

	// -------------------------------------
	// Setup TIM4 pwm for light intensity
	//TIM4->PSC = 0;
	TIM4->CNT = 0; // reset counter

	uint16_t litTime = 0x8000 >> (7 - prevPass);

	uint16_t period = litTime > 0x1000 ? litTime : 0x1000;
	TIM4->ARR = period-1;
	litTime *= 1.0f; // luminosity
	TIM4->CCR4 = period - (litTime-1);

	GPIOB->BSRR = GPIO_PIN_8 << 16; // strobe down


	TIM4->CR1 |= TIM_CR1_CEN;

	// --------------------------------------
	// send data with DMA
    HAL_DMA_Start_IT(htim1.hdma[TIM_DMA_ID_CC2],(uint32_t)&readBuffer[(y + prevPass * 16) * 128], (uint32_t)&GPIOB->ODR, 128);

    // TIM1->DIER = TIM_DMA_CC1;
    __HAL_TIM_ENABLE_DMA(&htim1, TIM_DMA_CC2);

    //TIM1->PSC = 1;
	//TIM1->ARR = 8;
	//TIM1->CCR1 = 4;

    // TIM1->CR1 = TIM_CR1_CEN;
    __HAL_TIM_ENABLE(&htim1);
}
