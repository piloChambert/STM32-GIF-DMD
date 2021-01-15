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
#define KEY_THRESHOLD 4
enum keys {
	KEY_A = 0,
	KEY_B,
	KEY_C,
	KEY_D
};
struct {
	GPIO_TypeDef *port;
	uint16_t pin;

	uint8_t value;
	uint8_t state;
} keys[] = {
		{GPIOA, GPIO_PIN_0, 0, 0},
		{GPIOA, GPIO_PIN_5, 0, 0},
		{GPIOA, GPIO_PIN_7, 0, 0},
		{GPIOA, GPIO_PIN_9, 0, 0},
};

GIFError ReadNextFrame() {
	// try to read another image in gif stream
	GIFError err = ReadGifImage();
	if(err != GIF_NO_ERROR)
		return err;

	// wait for swap
	while(swapBufferRequest) { }

	// encode new frame
	EncodeFrameToDMDBuffer(GIFInfo.frame, GIFInfo.codedGlobalPalette);

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
	ReadNextFrame();
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
	ReadNextFrame();
}

// FSM
typedef void(*stateFunction)();

void StartState();
void CardErrorState();
void GIFFileState();
void ClockState();
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
		// request swap
		swapBufferRequest = 1;

		// reset frame timer
		frameTick = GIFInfo.delayTime;
		prevFrameTick = currentTick;

		// try to read another image
		err = ReadNextFrame();
	}

	return err;
}

void StartState() {
	GIFError err = UpdateGIF();

	if(err == GIF_STREAM_FINISHED) {
		// if there's a car
		if(BSP_PlatformIsDetected() == SD_PRESENT && !InitSDCard()) {
			// switch to file state
			SetState(MenuState);
		}
		else {
			// switch to error state
			SetState(CardErrorState);
		}
	}
}

void CardErrorState() {
	GIFError err = UpdateGIF();

	// loop animation if needed
	if(err == GIF_STREAM_FINISHED) {
		GIFInfo.streamSeekCallback(GIFInfo.gifStart);
		ReadNextFrame();
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
}


void GIFFileState() {
	GIFError err = UpdateGIF();
	if(err == GIF_STREAM_FINISHED) {
		//LoadNextGifFile();
		SetState(ClockState);
	}

	else if(err != GIF_NO_ERROR) {
		// switch to error state
		SetState(CardErrorState);
	}

	else if((keys[KEY_C].state & 0x04)) {
		SetState(MenuState);
	}
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

void DrawTime(uint8_t x, uint8_t y) {
	RTC_TimeTypeDef time;
	HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BCD);
	RTC_DateTypeDef date;
	HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BCD);

	PrintChar(time.Hours >> 4, x + 0, y);
	PrintChar(time.Hours & 0x0F, x + 16, y);
	if(time.SubSeconds & ((time.SecondFraction + 1) >> 1)) // this is half second
		PrintChar(10, x + 28, 8);
	PrintChar(time.Minutes >> 4, x + 40, y);
	PrintChar(time.Minutes & 0x0F, x + 56, y);
}

void ClockState() {
	// erase bg
	memset(menuFrame, 2, 128 * 32);

	DrawTime(30, 8);

	swapBufferRequest = 1;
	while(swapBufferRequest) { }
	EncodeFrameToDMDBuffer(menuFrame, menuPalette);

	if(HAL_GetTick() - clockStartTick > 5000)
		SetState(GIFFileState);

	else if((keys[KEY_C].state & 0x04)) {
		SetState(MenuState);
	}
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
			x += PrintMenuChar(*p - 33, x, y) - 1; // -1 to merge character outline together
		p++;
	}
}

int y = 0;
int target = 0;
void MenuState() {
	// erase bg
	memset(menuFrame, 3, 128 * 32);

	char dirName[255];

	int firstIdx = MAX(y - 11, 0) / 16;

	for(int i = firstIdx; i < MIN(firstIdx + 3, FileManager.directoryCount); i++) {
		GetDirectoryAtIndex(i, dirName);
		PrintMenuStr(dirName, 8, i *  16 - y + 11);
	}

	if((keys[KEY_A].state & 0x04) && target < FileManager.directoryCount - 1) {
		target++;
	}
	if((keys[KEY_B].state & 0x04) && target > 0) {
		target--;
	}
	if((keys[KEY_C].state & 0x04)) {
		SetState(ClockState);
	}

	// scroll
	if(y < target * 16)
		y++;
	else if(y > target * 16)
		y--;

	swapBufferRequest = 1;
	while(swapBufferRequest) { }
	EncodeFrameToDMDBuffer(menuFrame, menuPalette);
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
  //LoadGIFFile("Computers/AMIGA_MonkeyIsland01.gif");
  //LoadNextGif();

  SetState(StartState);

  prevFrameTick = HAL_GetTick();
  frameTick = 0;

  DMDInit();

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  // update keys
	  for(int i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		  GPIO_PinState v = HAL_GPIO_ReadPin(keys[i].port, keys[i].pin);

		  if(v == GPIO_PIN_SET && keys[i].value < KEY_THRESHOLD)
			  keys[i].value++;
		  else if(v == GPIO_PIN_RESET && keys[i].value > 0)
			  keys[i].value--;

		  if(keys[i].state == 0 && keys[i].value == KEY_THRESHOLD)
			  keys[i].state = 1 + 2; // set to up state and set up event
		  else if(keys[i].state == 1 && keys[i].value == 0)
			  keys[i].state = 0 + 4; // set to down state and set down event
		  else
			  keys[i].state &= 0x1; // keep state bit
	  }


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
int __io_putchar(int ch) {
    ITM_SendChar(ch);
    return ch;
}
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
