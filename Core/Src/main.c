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
extern unsigned char nocardGIF[5988UL + 1];
extern unsigned char startupGIF[8182UL + 1];
extern unsigned char clockFont[624UL + 1];
uint8_t clockFontInfo[] = {
		0, 0, 16, 15, // 0
		16, 0, 16, 15, // 1
		32, 0, 16, 15, // 2
		48, 0, 16, 15, // 3
		64, 0, 16, 15, // 4
		80, 0, 16, 15, // 5
		96, 0, 16, 15, // 6
		112, 0, 16, 15, // 7
		0, 16, 16, 15, // 8
		16, 16, 16, 15, // 9
		32, 16, 16, 15, // :
};
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

void ScanDirectory(char *path) {
	DIR dir;
	FILINFO finfo;

	if(f_opendir(&dir, path) != FR_OK) {
		printf2("Failed to open dir\r\n");
		return;
	}

	finfo.fname[0] = 0;
	while(1) {
	  if(f_readdir(&dir, &finfo) != FR_OK) {
		  printf2("Failed to read dir\r\n");
		  break;
	  }

	  if(finfo.fname[0] == 0)
		  break;

	  if(finfo.fattrib & AM_DIR)
		  printf2("%s/%s <dir>\r\n", path, finfo.fname);
	  else
		  printf2("%s/%s %ld\r\n", path, finfo.fname, finfo.fsize);
	}
	f_closedir(&dir);
}

DIR rootDir;
DIR subDir;
FATFS fs;
FIL GIFFile;
int FileCount;
char subDirName[255];

// return next gif file or 0
int NextGIFFilenameInSubDir(char *gifFilename) {
	FILINFO finfo;

	while(1) {
		if(f_readdir(&subDir, &finfo) != FR_OK) {
			printf2("Failed to read sub dir!\r\n");
			return 0;
		}

		if(finfo.fname[0] == 0) // no more file
			return 0;

		if(!(finfo.fattrib & AM_DIR)) {
			// is it a gif file?
			int l = strlen(finfo.fname);
			if(!strcasecmp(&finfo.fname[l - 4], ".gif")) {
				sprintf(gifFilename, "%s/%s", subDirName, finfo.fname);
				return 1;
			}
		}
	}

	return 0;
}

int NextGIFFilename(char *gifFilename) {
	FILINFO finfo;

	while(NextGIFFilenameInSubDir(gifFilename) == 0) {
		f_closedir(&subDir);

		// go to next subdir
		if(f_readdir(&rootDir, &finfo) != FR_OK) {
			printf2("Failed to read dir\r\n");
			return 1;
		}

		if(finfo.fname[0] == 0) // no more file
			f_rewinddir(&rootDir); // rewind

		else if(finfo.fattrib & AM_DIR && finfo.fname[0] != '.') { // don't load hidden directory
			f_opendir(&subDir, finfo.fname);
			strcpy(subDirName, finfo.fname);
		}
	}

	return 0;
}

int GetFileCount() {
	int count = 0;

	// enumerate all subdir
	DIR rd;
	f_opendir(&rd, "/");

	FILINFO finfo;
	f_readdir(&rd, &finfo);
	while(finfo.fname[0] != '\0') {
		if(finfo.fattrib & AM_DIR && finfo.fname[0] != '.') {
			// we are in a subDir, count file
			DIR sd;
			f_opendir(&sd, finfo.fname);

			FILINFO finfo2;
			f_readdir(&sd, &finfo2);
			while(finfo2.fname[0] != '\0') {
				if(!(finfo2.fattrib & AM_DIR))
					count++;

				f_readdir(&sd, &finfo2);
			}
			f_closedir(&sd);
		}

		f_readdir(&rd, &finfo);
	}
	f_closedir(&rd);

	return count;
}

int GetFilenameForFileIndex(uint32_t idx, char *filename) {
	int count = 0;

	filename[0] = '\0';

	// enumerate all subdir
	DIR rd;
	f_opendir(&rd, "/");

	FILINFO finfo;
	f_readdir(&rd, &finfo);
	while(finfo.fname[0] != '\0' && filename[0] == '\0') {
		if(finfo.fattrib & AM_DIR && finfo.fname[0] != '.') {
			// we are in a subDir, count file
			DIR sd;
			f_opendir(&sd, finfo.fname);

			FILINFO finfo2;
			f_readdir(&sd, &finfo2);
			while(finfo2.fname[0] != '\0') {
				if(!(finfo2.fattrib & AM_DIR)) {
					if(idx == count) {
						sprintf(filename, "%s/%s", finfo.fname, finfo2.fname);
						break;
					}
					count++;
				}


				f_readdir(&sd, &finfo2);
			}
			f_closedir(&sd);
		}

		f_readdir(&rd, &finfo);
	}
	f_closedir(&rd);

	return 0;
}


// mount file system and open root dir
int InitSDCard() {
	  if(f_mount(&fs, "", 1) != FR_OK)
		  return 1; // XXX define error

	  FileCount = GetFileCount();

	  // open rootdir
	  if(f_opendir(&rootDir, "/") != FR_OK)
		  return 1; // XXX define error

	  return 0;
}

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

GIFError ReadNextFrame() {
	// try to read another image
	GIFError err = ReadGifImage();
	if(err != GIF_NO_ERROR)
		return err;

	// wait for swap
	while(swapBufferRequest) { }

	// encode new frame
	EncodeFrameToDMDBuffer(GIFInfo.frame, GIFInfo.codedGlobalPalette);

	return GIF_NO_ERROR;
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
	GetFilenameForFileIndex(rand() % FileCount, GIFFilename); // random
	LoadGIFFile(GIFFilename);
}

uint8_t *memoryStreamPointer = NULL;
uint8_t *memoryReadStreamPointer = NULL;
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

void LoadGIFMemory(uint8_t *data) {
	memoryStreamPointer = data;
	memoryReadStreamPointer = data;

	GIFInfo.streamReadCallback = MemoryStreamRead;
	GIFInfo.streamTellCallback = MemoryStreamTell;
	GIFInfo.streamSeekCallback = MemoryStreamSeek;

	LoadGIF();
	ReadNextFrame();
}

typedef void(*stateFunction)();

void StartState();
void CardErrorState();
void GIFFileState();
void ClockState();

stateFunction currentState = StartState;
uint32_t prevFrameTick = 0;
uint32_t frameTick = 0;
uint32_t clockStartTick = 0;

void SetState(stateFunction func) {
	if(func == StartState) {
		LoadGIFMemory(startupGIF);
	}

	else if(func == CardErrorState) {
		f_closedir(&subDir);
		memset(&subDir, 0, sizeof(DIR));
		f_closedir(&rootDir);
		memset(&rootDir, 0, sizeof(DIR));
		f_mount(NULL, "", 0);
		memset(&fs, 0, sizeof(FATFS));
		MX_FATFS_DeInit();

		LoadGIFMemory(nocardGIF);
	}

	else if(func == GIFFileState) {
		LoadNextGifFile();
	}

	else if(func == ClockState) {
		LoadGIFMemory(clockFont);
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
			SetState(GIFFileState);
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
}

uint8_t menuFrame[128 * 32];
void CopySubImage(uint8_t *src, uint8_t sx, uint8_t sy, uint8_t w, uint8_t h, uint8_t *dst, uint8_t dx, uint8_t dy) {
	uint8_t _w = dx + w > 127 ? 128 - dx : w;
	uint8_t _h = dy + h > 31 ? 32 - dy : h;

	for(uint8_t y = 0; y < _h; y++)
		for(uint8_t x = 0; x < _w; x++)
			if(!GIFInfo.hasTransparentColor || src[sx + x + (sy + y) * 128] != GIFInfo.transparentColor)
				dst[dx + x + (dy + y) * 128] = src[sx + x + (sy + y) * 128];
}

void PrintChar(int c, uint8_t x, uint8_t y) {
	CopySubImage(GIFInfo.frame, clockFontInfo[c * 4], clockFontInfo[c * 4 + 1], clockFontInfo[c * 4 + 2], clockFontInfo[c * 4 + 3], menuFrame, x, y);
}

void DrawTime(uint8_t x, uint8_t y) {
	RTC_TimeTypeDef time;
	HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
	RTC_DateTypeDef date;
	HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);

	PrintChar(time.Hours / 10, x + 0, y);
	PrintChar(time.Hours % 10, x + 16, y);
	if(time.SubSeconds & ((time.SecondFraction + 1) >> 1)) // this is half second
		PrintChar(10, x + 28, 8);
	PrintChar(time.Minutes / 10, x + 40, y);
	PrintChar(time.Minutes % 10, x + 56, y);
}

void ClockState() {
	memset(menuFrame, 0/*GIFInfo.transparentColor*/, 128 * 32);

	DrawTime(30, 8);

	swapBufferRequest = 1;
	while(swapBufferRequest) { }
	EncodeFrameToDMDBuffer(menuFrame, GIFInfo.codedGlobalPalette);

	if(HAL_GetTick() - clockStartTick > 5000)
		SetState(GIFFileState);
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
