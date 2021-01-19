/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "fatfs.h"
#include "rtc.h"
#include "sdio.h"
#include "tim.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>
#include "GIF.h"
#include "DMD.h"
#include "FileManager.h"
#include "profiling.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
#include "images.c"
#include "gifs.c"

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void SendUART(char *txt) {
#if 0
	while(CDC_Transmit_FS(txt, strlen(txt)) == USBD_BUSY) {
	}

	//CDC_Transmit_FS(txt, strlen(txt));
#else
	//CDC_Transmit_FS(txt, strlen(txt));
#endif
}

static char strBuffer[512];

#include <stdio.h>
#include <stdarg.h>
void printf2(char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(strBuffer, 255, format, args);
    SendUART(strBuffer);
    va_end(args);
}

// Key stuff
#define KEY_DEBOUNCE_THRESHOLD 4
#define KEY_FIRST_REPEAT_TIME 500 // ms
#define KEY_REPEAT_TIME 150 // ms

enum keys {
	KEY_A = 0,
	KEY_B,
	KEY_C,
	KEY_D
};
struct {
	GPIO_TypeDef *port;
	uint16_t pin;

	uint32_t repeatTime; // time when switch state for repeat

	uint8_t value;
	uint8_t state;
} keys[] = {
		{GPIOA, GPIO_PIN_0, 0, 0},
		{GPIOA, GPIO_PIN_5, 0, 0},
		{GPIOA, GPIO_PIN_7, 0, 0},
		{GPIOA, GPIO_PIN_9, 0, 0},
};

#define KEY_UP_EVENT 0x02
#define KEY_DOWN_EVENT 0x04
#define KEY_REPEAT_EVENT 0x08

// switches are on pull-up GPIO, so when press pin state is GPIO_PIN_RESET
void UpdateKeyState() {
	uint32_t currentTime = HAL_GetTick();

	for(int i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		GPIO_PinState v = HAL_GPIO_ReadPin(keys[i].port, keys[i].pin);

		if(v == GPIO_PIN_RESET && keys[i].value < KEY_DEBOUNCE_THRESHOLD)
			keys[i].value++;
		else if(v == GPIO_PIN_SET && keys[i].value > 0)
			keys[i].value--;

		if(keys[i].state == 0 && keys[i].value == KEY_DEBOUNCE_THRESHOLD) {
			keys[i].state = 1 + KEY_UP_EVENT; // set to up state and set up event
			keys[i].repeatTime = currentTime + KEY_FIRST_REPEAT_TIME;
		}
		else if(keys[i].state == 1 && keys[i].value == 0)
			keys[i].state = KEY_DOWN_EVENT; // set to down state and set down event
		else {
			// key repeat
			if((keys[i].state & 0x01) && (currentTime > keys[i].repeatTime)) {
				keys[i].state |= KEY_UP_EVENT + KEY_REPEAT_EVENT; // generate event
				keys[i].repeatTime = currentTime + KEY_REPEAT_TIME; // reset time
			}
			else
				keys[i].state &= 0x1; // keep state bit, reset events
		}
	}
}

int queuedFrame = 0;
GIFError QueueNextGifStreamFrame() {
	// try to read another image in gif stream
	GIFError err = ReadGifImage();
	if(err != GIF_NO_ERROR)
		return err;

	// wait for swap
	while(swapBufferRequest) { }

	// queue the new frame
	EncodeFrameToDMDBuffer(GIFInfo.frame, GIFInfo.codedGlobalPalette);
	queuedFrame = 1;

	return GIF_NO_ERROR;
}

// File Stream
FIL GIFFile;
int FileStreamRead(void* buff, UINT btr,	UINT *l) {
	if(f_read(&GIFFile, buff, btr, l) != FR_OK)
		return GIF_STREAM_ERROR;

	// else
	return GIF_NO_ERROR;
}

FSIZE_t FileStreamTell() {
	return f_tell(&GIFFile);
}


void FileStreamSeek(FSIZE_t pos) {
	f_lseek(&GIFFile, pos);
}

void LoadGIFFile(const char *path) {
	f_close(&GIFFile);

	if(f_open(&GIFFile, path, FA_READ) != FR_OK) {
		SendUART("Can't open file!);");
		return;
	}

	GIFInfo.streamReadCallback = FileStreamRead;
	GIFInfo.streamTellCallback = FileStreamTell;
	GIFInfo.streamSeekCallback = FileStreamSeek;

	LoadGIF();
	QueueNextGifStreamFrame();
}

char GIFFilename[512];
void LoadNextGifFile() {
	//NextGIFFilename(GIFFilename);
	GetFilenameAtIndex(rand() % FileManager.fileCount, GIFFilename); // random
	LoadGIFFile(GIFFilename);
}

// Memory Stream
const uint8_t *memoryStreamPointer = NULL;
const uint8_t *memoryReadStreamPointer = NULL;
int MemoryStreamRead(void* buff, UINT btr,	UINT *l) {
	memcpy(buff, memoryReadStreamPointer, btr);
	*l = btr;

	memoryReadStreamPointer += btr;

	return GIF_NO_ERROR;
}

FSIZE_t MemoryStreamTell() {
	return (memoryReadStreamPointer - memoryStreamPointer);
}


void MemoryStreamSeek(FSIZE_t pos) {
	memoryReadStreamPointer = memoryStreamPointer + pos;
}

void LoadGIFMemory(const uint8_t *data) {
	memoryStreamPointer = data;
	memoryReadStreamPointer = data;

	GIFInfo.streamReadCallback = MemoryStreamRead;
	GIFInfo.streamTellCallback = MemoryStreamTell;
	GIFInfo.streamSeekCallback = MemoryStreamSeek;

	LoadGIF();
	QueueNextGifStreamFrame();
}

// FSM
typedef void(*stateFunction)();

void StartState();
void CardErrorState();
void GIFFileState();
void ClockState();
void SetClockState();
void MenuState();


stateFunction currentState = StartState;
uint32_t prevFrameTick = 0;
uint32_t frameTick = 0;
uint32_t clockStartTick = 0;

// Menu drawing
uint8_t menuFrame[128 * 32];
uint8_t menuPalette[256 * 8];

void SetState(stateFunction func) {
	if(func == StartState) {
		LoadGIFMemory(startupGIF);
	}

	else if(func == CardErrorState) {
		ResetSDCard();
		MX_FATFS_DeInit();

		LoadGIFMemory(nocardGIF);
	}

	else if(func == GIFFileState) {
		LoadNextGifFile();
	}

	else if(func == ClockState) {
		CodePalette((uint8_t *)clockFont.palette, menuPalette, 256);
		clockStartTick = HAL_GetTick();
	}

	else if(func == SetClockState) {
		CodePalette((uint8_t *)clockFont.palette, menuPalette, 256);
		clockStartTick = HAL_GetTick();
	}

	else if(func == MenuState) {
		CodePalette((uint8_t *)menuFont.palette, menuPalette, 256);
		clockStartTick = HAL_GetTick();
	}

	currentState = func;
}

// swap buffer if needed, decode next frame
// return if there's no more frame in current gif
GIFError UpdateGIF() {
	uint32_t currentTick = HAL_GetTick();
	GIFError err = GIF_NO_ERROR;
	if(currentTick - prevFrameTick > frameTick) {
		if(queuedFrame) {
			// request swap
			swapBufferRequest = 1;
			queuedFrame = 0;

			// reset frame timer
			frameTick = GIFInfo.delayTime;
			prevFrameTick = currentTick;

			// try to read another image
			err = QueueNextGifStreamFrame();
		}
		else {
			// no more gif frame to display
			err = GIF_NO_MORE_QUEUED_FRAME;
		}
	}

	return err;
}

#define LUMINOSITY_GAMMA 2.2f
uint32_t luminosityTimer = 0;
int luminosityVisible = 0;
void UpdateLuminosity() {
	float lum = powf(luminosityAttenuation, 1.0f / LUMINOSITY_GAMMA);
	if(keys[KEY_A].state & KEY_UP_EVENT) {
		lum = MIN(lum + 0.05f, 1.0f);
		luminosityAttenuation = powf(lum, LUMINOSITY_GAMMA);

		luminosityTimer = HAL_GetTick();
		luminosityVisible = 1;
	}
	if(keys[KEY_B].state & KEY_UP_EVENT) {
		lum = MAX(lum - 0.05f, 0.0f);
		luminosityAttenuation = powf(lum, LUMINOSITY_GAMMA);

		luminosityTimer = HAL_GetTick();
		luminosityVisible = 1;
	}

	if(luminosityVisible) {
		if(HAL_GetTick() - luminosityTimer > 2000) {
			luminosityVisible = 0;
		}
		else {
			// draw luminosity progress bar over current frame
			uint8_t v = lum * 100.0f;

			for(uint32_t p = 0; p < 8; p++) {
				for(uint32_t y = 12; y < 16; y++) {
					for(uint32_t x = 13; x < 115; x++) {
						if(y == 12 || y == 15) { // black line
							writeBuffer[x + y * 128 + p * 128 * 16] &= ~(0XE0); // black
						}
						else {
							if(x == 13 || x > 14 + v)
								writeBuffer[x + y * 128 + p * 128 * 16] &= ~(0xE0); // black
							else
								writeBuffer[x + y * 128 + p * 128 * 16] |= 0xE0; // white
						}
					}
				}
			}
		}
	}
}

void StartState() {
	GIFError err = UpdateGIF();

	if(err == GIF_NO_MORE_QUEUED_FRAME) {
		// if there's a car
		if(BSP_PlatformIsDetected() == SD_PRESENT && !InitSDCard()) {
			// switch to file state
			SetState(ClockState);
		}
		else {
			// switch to error state
			SetState(CardErrorState);
		}
	}

	UpdateLuminosity();
}

void CardErrorState() {
	GIFError err = UpdateGIF();

	// loop animation if needed
	if(err == GIF_STREAM_FINISHED) {
		GIFInfo.streamSeekCallback(GIFInfo.gifStart);
		QueueNextGifStreamFrame();
	}
	else {
		if(BSP_PlatformIsDetected() == SD_PRESENT) {
			// try to mount the card
			MX_FATFS_Init();

			if(!InitSDCard()) {
				// we are good!
				SetState(GIFFileState);
			}
			else {
				// we are not good!
				MX_FATFS_DeInit();
			}
		}
	}

	UpdateLuminosity();
}


void GIFFileState() {
	GIFError err = UpdateGIF();
	if(err == GIF_STREAM_FINISHED) {
		// load a new gif
		//LoadNextGifFile();
	}

	else if(err == GIF_NO_MORE_QUEUED_FRAME) {
		SetState(ClockState);
	}

	else if(err != GIF_NO_ERROR) {
		// switch to error state
		SetState(CardErrorState);
	}

	else if((keys[KEY_C].state & 0x04)) {
		SetState(MenuState);
	}

	UpdateLuminosity();
}


void CopySubImage(const Image *src, uint8_t sx, uint8_t sy, uint8_t w, uint8_t h, uint8_t *dst, uint8_t dx, uint8_t dy) {
	uint8_t _w = dx + w > 127 ? 128 - dx : w;
	uint8_t _h = dy + h > 31 ? 32 - dy : h;

	for(uint8_t y = 0; y < _h; y++)
		for(uint8_t x = 0; x < _w; x++)
			if(src->pixels[sx + x + (sy + y) * src->width] != 0) // transparent color is always 0!
				dst[dx + x + (dy + y) * 128] = src->pixels[sx + x + (sy + y) * src->width];
}

void PrintChar(int c, uint8_t x, uint8_t y) {
	CopySubImage(&clockFont, c * 16, 0, 16, 16, menuFrame, x, y);
}

void DrawTime(uint8_t x, uint8_t y, uint8_t blink) {
	RTC_TimeTypeDef time;
	HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BCD);
	RTC_DateTypeDef date;
	HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BCD);

	uint8_t h2 = time.SubSeconds & ((time.SecondFraction + 1) >> 1); // this is 1/2 second
	uint8_t h4 = time.SubSeconds & ((time.SecondFraction + 1) >> 2); // this is 1/4 second

	if(!(blink & 0x01) || h4)
		PrintChar(time.Hours >> 4, x + 0, y);

	if(!(blink & 0x01) || h4)
		PrintChar(time.Hours & 0x0F, x + 16, y);

	if(h2)
		PrintChar(10, x + 28, 8);

	if(!(blink & 0x02) || h4)
		PrintChar(time.Minutes >> 4, x + 40, y);

	if(!(blink & 0x02) || h4)
		PrintChar(time.Minutes & 0x0F, x + 56, y);
}

void ClockState() {
	// erase bg
	memset(menuFrame, 10, 128 * 32);

	DrawTime(30, 8, 0);

	swapBufferRequest = 1;
	while(swapBufferRequest) { }
	EncodeFrameToDMDBuffer(menuFrame, menuPalette);
	UpdateLuminosity();

	if(HAL_GetTick() - clockStartTick > 5000)
		SetState(GIFFileState);

	else if((keys[KEY_C].state & KEY_UP_EVENT)) {
		SetState(MenuState);
	}

}

int digit = 0;
void SetClockState() {
	// erase bg
	memset(menuFrame, 10, 128 * 32);

	DrawTime(30, 8, 1 << digit);

	if((keys[KEY_C].state & KEY_UP_EVENT)) {
		digit++;
		if(digit == 2)
			digit = 0;
	}

	RTC_TimeTypeDef time;
	HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
	RTC_DateTypeDef date;
	HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);

	if((keys[KEY_A].state & KEY_UP_EVENT)) {
		switch (digit){
			case 0:
				time.Hours = (time.Hours + 1) % 24;
				break;
			case 1:
				time.Minutes = (time.Minutes + 1) % 60;
				break;
		}

		HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN);
		HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN);
	}

	if((keys[KEY_B].state & KEY_UP_EVENT)) {
		switch (digit){
			case 0:
				time.Hours = (time.Hours + 24 - 1) % 24;
				break;
			case 1:
				time.Minutes = (time.Minutes + 60 - 1) % 60;
				break;
		}

		HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN);
		HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN);
	}

	swapBufferRequest = 1;
	while(swapBufferRequest) { }
	EncodeFrameToDMDBuffer(menuFrame, menuPalette);

}

int32_t CopySubImageCharacter(const Image *src, int32_t sx, int32_t sy, int32_t w, int32_t h, uint8_t *dst, int32_t dx, int32_t dy) {
	int32_t maxX = 0;

	for(int32_t y = 0; y < h; y++) {
		for(int32_t x = 0; x < w; x++) {
			if(src->pixels[sx + x + (sy + y) * src->width] != 0) { // transparent color is always 0!
				if(dx + x >= 0 && dx + x < 128 && dy + y >= 0 && dy + y < 32)
					dst[dx + x + (dy + y) * 128] = src->pixels[sx + x + (sy + y) * src->width];
				maxX = x > maxX ? x : maxX;
			}
		}
	}

	return maxX + 1;
}

int32_t PrintMenuChar(int c, int32_t x, int32_t y) {
	return CopySubImageCharacter(&menuFont, c * 9, 0, 9, 10, menuFrame, x, y);
}

void PrintMenuStr(const char *str, int32_t x, int32_t y) {
	const char *p = str;
	while(*p) {
		if(*p == 32)
			x += 9; // space
		if(*p > 32 && *p < 127)
			x += PrintMenuChar(*p - 30, x, y) - 1; // -1 to merge character outline together
		p++;
	}
}

int y = 0;
int selectedDirectory = 0;
void MenuState() {
	// erase bg
	memset(menuFrame, 0x03, 128 * 32);

	char dirName[255];

	int firstIdx = MAX(y - 11, 0) / 10;

	for(int i = firstIdx; i < MIN(firstIdx + 4, FileManager.directoryCount); i++) {
		GetDirectoryAtIndex(i, dirName);
		if(i == selectedDirectory)
			PrintMenuChar(0, 1, i * 10 - y + 11);

		PrintMenuChar(FileManager.directories[i].enable ? 2 : 1, 10, i * 10 - y + 11);
		PrintMenuStr(dirName, 19, i *  10 - y + 11);
	}

	if((keys[KEY_A].state & KEY_UP_EVENT) && selectedDirectory < FileManager.directoryCount - 1) {
		selectedDirectory++;
	}
	if((keys[KEY_B].state & KEY_UP_EVENT) && selectedDirectory > 0) {
		selectedDirectory--;
	}

	if(keys[KEY_C].state & KEY_UP_EVENT) {
		SetState(ClockState);
	}

	if(keys[KEY_D].state & KEY_UP_EVENT) {
		FileManager.directories[selectedDirectory].enable = !FileManager.directories[selectedDirectory].enable;
		UpdateFileCount();
	}

	else {
		// scroll
		if(y < selectedDirectory * 10)
			y += 2;
		else if(y > selectedDirectory * 10)
			y -= 2;

		swapBufferRequest = 1;
		while(swapBufferRequest) { }
		EncodeFrameToDMDBuffer(menuFrame, menuPalette);
	}
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USB_DEVICE_Init();
  MX_FATFS_Init();
  MX_TIM4_Init();
  MX_TIM1_Init();
  MX_SDIO_SD_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  //HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x02);
  uint32_t v = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);
  if(v != 0x88) {
	  printf("OUch");
  }


  //LoadGIFFile("Computers/AMIGA_MonkeyIsland01.gif");
  //LoadNextGif();

  srand(hrtc.Instance->TR);
  SetState(StartState);

  prevFrameTick = HAL_GetTick();
  frameTick = 0;

  DMDInit();

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  // update keys
	  UpdateKeyState();

	  // run current state
	  currentState();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */

  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
