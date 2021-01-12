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
char subDirName[255];

// return next gif file or 0
int NextGIFFileInSubDir(char *gifFilename) {
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

int NextGIFFile(char *gifFilename) {
	FILINFO finfo;

	while(NextGIFFileInSubDir(gifFilename) == 0) {
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

// mount file system and open root dir
int InitSDCard() {
	  if(f_mount(&fs, "", 1) != FR_OK)
		  return 1; // XXX define error

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
	NextGIFFile(GIFFilename);
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

stateFunction currentState = StartState;
uint32_t prevFrameTick = 0;
uint32_t frameTick = 0;

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
			LoadNextGifFile();
			currentState = GIFFileState;
		}
		else {
			// switch to error state
			LoadGIFMemory(nocardGIF);
			currentState = CardErrorState;
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
				LoadNextGifFile();
				currentState = GIFFileState;
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
		LoadNextGifFile();
	}

	else if(err != GIF_NO_ERROR) {
		// switch to error state
		LoadGIFMemory(nocardGIF);
		currentState = CardErrorState;

		f_closedir(&subDir);
		memset(&subDir, 0, sizeof(DIR));
		f_closedir(&rootDir);
		memset(&rootDir, 0, sizeof(DIR));
		f_mount(NULL, "", 0);
		memset(&fs, 0, sizeof(FATFS));
		MX_FATFS_DeInit();
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
  //LoadGIFFile("Computers/AMIGA_MonkeyIsland01.gif");
  //LoadNextGif();

  LoadGIFMemory(startupGIF);
  currentState = StartState;

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
