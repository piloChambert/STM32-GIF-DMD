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
#include "spi.h"
#include "tim.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_cdc_if.h"
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
FATFS fs;
FIL file;
FSIZE_t gifStart;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void SendUART(char *txt) {
#if 0
	while(CDC_Transmit_FS(txt, strlen(txt)) == USBD_BUSY) {
	}
#else
	CDC_Transmit_FS(txt, strlen(txt));
#endif
}

static char strBuffer[512];

void ScanDirectory(char *path) {
	DIR dir;
	FILINFO finfo;

	if(f_opendir(&dir, path) != FR_OK) {
		SendUART("Failed to open dir\r\n");
		return;
	}

	finfo.fname[0] = 0;
	while(1) {
	  if(f_readdir(&dir, &finfo) != FR_OK) {
		  SendUART("Failed to read dir\r\n");
		  break;
	  }

	  if(finfo.fname[0] == 0)
		  break;

	  if(finfo.fattrib & AM_DIR)
		  sprintf(strBuffer, "%s/%s <dir>\r\n", path, finfo.fname);
	  else
		  sprintf(strBuffer, "%s/%s %ld\r\n", path, finfo.fname, finfo.fsize);

	  SendUART(strBuffer);
	}
}

typedef struct __attribute__((packed))
{
    uint32_t  signatureHi;             //  "GIF8"
    uint16_t  signatureLo;             //  GIF version: "7a" or "9a"
    uint16_t  width;                   //  logical screen width in pixels
    uint16_t  height;                  //  logical screen height in pixels
    uint8_t   flags;                   //  Global Color Table specification
    uint8_t   backgroundColorIndex;              //  background color
    uint8_t   ratio;                   //  default pixel aspect ratio
} GifHeader;

typedef struct __attribute__((packed))
{
	uint8_t label;
	uint8_t blockSize;
} GifExtensionHeader;

typedef struct __attribute__((packed))
{
	uint8_t flags;
	uint16_t delayTime;
	uint8_t transparentColorIndex;
	uint8_t terminator; // always 0
} GifGraphicsControlExtension;

typedef struct __attribute__((packed))
{
	uint16_t left;         /* X position of image on the display */
	uint16_t top;          /* Y position of image on the display */
	uint16_t width;        /* Width of the image in pixels */
	uint16_t height;       /* Height of the image in pixels */
	uint8_t flags;       /* Image and Color Table Data Information */
} GifImageDescriptor;


int globalPaletteColorCount;
uint8_t globalPalette[256 * 3];

int useLocalPalette;
int localPaletteColorCount;
uint8_t localPalette[256 * 3];

uint16_t delayTime;

uint8_t extBuffer[64];

int frameIdx = 0;
uint8_t frame[128 * 32];

uint8_t dictBuffer[128 * 32 * 4];
struct {
	uint16_t idx;
	uint16_t l;
} dict[4096];
int dictSize = 0;

void ClearDict(int mcs) {
	for(int i = 0; i < 4096; i++) {
		if(i < (1<< mcs)) {
			dict[i].idx = i;
			dict[i].l = 1;
			dictBuffer[dict[i].idx] = i; // ith color at ith entry
		}
		else {
			dict[i].idx = 1 << mcs;
			dict[i].l = 0;
		}
	}


	dictSize = (1<< mcs) + 2;
}

uint8_t imageSubData[256];
uint8_t imageSubDataSize = 0;
int imageSubDataIdx = 0;
int imageSubDataBitsLeft = 8;

int LoadImageSubData() {
	imageSubDataIdx = 0;
	imageSubDataBitsLeft = 8;

	UINT l;
	FRESULT res = f_read(&file, &imageSubDataSize, 1, &l);
	if(res != FR_OK)
		SendUART("Error while reading data!");

	if(imageSubDataSize > 0) {
		sprintf(strBuffer, "data size = %d\r\n", imageSubDataSize);
		SendUART(strBuffer);
		f_read(&file, imageSubData, imageSubDataSize, &l);
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
			if(imageSubDataIdx >= imageSubDataSize)
				if(!LoadImageSubData())
					break;

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
		// get current
		current = GetNextCode(compressedSize);

		if(current == clearCode)
			ClearDict(mcs);

		else if(current == eoi)
			return; // we're done decoding

		else if(dict[current].l > 0) {
			// output
			for(int i = 0; i < dict[current].l; i++)
				frame[frameIdx++] = dictBuffer[dict[current].idx + i];

			if(last != clearCode) {
				uint8_t k = dictBuffer[dict[current].idx];

				// add new code
				dict[dictSize].idx = dict[dictSize - 1].idx + dict[dictSize - 1].l;
				dict[dictSize].l = dict[last].l + 1;
				for(int i = 0; i < dict[dictSize].l; i++)
					dictBuffer[dict[dictSize].idx + i] = i == dict[dictSize].l - 1 ? k : dictBuffer[dict[last].idx + i];

				dictSize++;

				if(dictSize >= (1 << compressedSize))
					compressedSize++;
			}
		}
		else {
			uint8_t k = dictBuffer[dict[last].idx];

			// output
			for(int i = 0; i < dict[last].l; i++)
				frame[frameIdx++] = dictBuffer[dict[last].idx + i];
			frame[frameIdx++] = k;

			// add new code
			dict[dictSize].idx = dict[dictSize - 1].idx + dict[dictSize - 1].l;
			dict[dictSize].l = dict[last].l + 1;
			for(int i = 0; i < dict[dictSize].l; i++)
				dictBuffer[dict[dictSize].idx + i] = i == dict[dictSize].l - 1 ? k : dictBuffer[dict[last].idx + i];

			dictSize++;

			if(dictSize >= (1 << compressedSize))
				compressedSize++;
		}

		last = current;
	}
}

uint8_t sRGB2RGB(uint8_t v) {
	return powf(v / 255.0f, 2.2f) * 255.0f;
}

void ReadGifPalette(uint8_t *palette, int colorCount) {
	UINT l;
	if(f_read(&file, palette, sizeof(uint8_t) * 3 * colorCount, &l) != FR_OK) {
		SendUART("Can't read gif colors!);");
		return;
	}

	// gamma correction ???
	for(int i = 0; i < colorCount * 3; i++) {
		palette[i] = sRGB2RGB(palette[i]);
	}
}

void ReadGifImage() {
	UINT l;

	while(1) {
		uint8_t sep;
		FRESULT res = f_read(&file, &sep, sizeof(uint8_t), &l);

		if(sep == 0x3b) // rewind
			f_lseek(&file, gifStart);

		if(sep == 0x21) {
			GifExtensionHeader extHeader;
			f_read(&file, &extHeader, sizeof(GifExtensionHeader), &l);

			if(extHeader.label == 0xf9) {
				//SendUART("---- Graphics Control Extension ----\r\n");
				GifGraphicsControlExtension desc;
				f_read(&file, &desc, sizeof(GifGraphicsControlExtension), &l);

				delayTime = desc.delayTime;
				sprintf(strBuffer, "delay time: %d\r\n", delayTime);
				//SendUART(strBuffer);

				//SendUART("\r\n");
			}
			else {
				sprintf(strBuffer, "---- Unknown Extension %#X ----\r\n", extHeader.label);
				//SendUART(strBuffer);
				// read remaining bytes
				f_read(&file, &extBuffer, extHeader.blockSize, &l);
				//SendUART("\r\n");
			}
		}

		if(sep == 0x2c) {
			// image data
			//SendUART("---- Image Descriptor ----\r\n");

			GifImageDescriptor desc;
			res = f_read(&file, &desc, sizeof(GifImageDescriptor), &l);

			sprintf(strBuffer, "pos: %dx%d\r\nsize: %dx%d\r\n", desc.left, desc.top, desc.width, desc.height);
			//SendUART(strBuffer);

			localPaletteColorCount = 2 << (desc.flags & 0x07);
			useLocalPalette = desc.flags & 0x80 ? 1 : 0;
			if(useLocalPalette) {
				ReadGifPalette(localPalette, localPaletteColorCount);
			}

			// decode GIF

			// read min code size
			uint8_t mcs;
			res = f_read(&file, &mcs, 1, &l);
			sprintf(strBuffer, "MCS = %d\r\n", mcs);
			SendUART(strBuffer);

			Decode(mcs);

			// dump image to console
			/*
			for(int y = 0; y < 32; y++) {
				for(int x = 0; x < 128; x++) {
					char c[2] = {0, 0};
					c[0] = 32 + frame[x + y * 128];
					SendUART(c);
				}
				SendUART("\r\n");
			}

			SendUART("\r\n");
			*/
			break;
		}
	}
}

void ReadGif(char *path) {
	if(f_open(&file, path, FA_READ) != FR_OK) {
		SendUART("Can't open file!);");
		return;
	}


	GifHeader header;
	UINT l;
	if(f_read(&file, &header, sizeof(GifHeader), &l) != FR_OK) {
		SendUART("Can't read gif header!);");
		return;
	}

	sprintf(strBuffer, "GIF resolution: %dx%d\r\n", header.width, header.height);
	SendUART(strBuffer);

	sprintf(strBuffer, "background color index: %d\r\n", header.backgroundColorIndex);
	SendUART(strBuffer);

	// read global palette
	int hasColorTable = header.flags & 0x80 ? 1 : 0;
	int colorResolution = 2 << ((header.flags & 0x70) >> 4);
	globalPaletteColorCount = 2 << (header.flags & 0x07);

	sprintf(strBuffer,"Color resolution: %d\r\n", colorResolution);
	SendUART(strBuffer);

	sprintf(strBuffer, "Has global color palette: %d\r\n", hasColorTable);
	SendUART(strBuffer);

	sprintf(strBuffer, "Color count: %d\r\n", globalPaletteColorCount);
	SendUART(strBuffer);


	if(hasColorTable) {
		ReadGifPalette(globalPalette, globalPaletteColorCount);
	}

	// look for image data now
	gifStart = f_tell(&file);
	ReadGifImage();
}

uint8_t DMDBuffer[2][128 * 2 * 16 * 8];


uint8_t *readBuffer = DMDBuffer[0];
uint8_t *writeBuffer = DMDBuffer[1];

void SwapBuffer() {
	readBuffer = readBuffer == DMDBuffer[0] ? DMDBuffer[1] : DMDBuffer[0];
	writeBuffer = writeBuffer == DMDBuffer[0] ? DMDBuffer[1] : DMDBuffer[0];
}

void InitDMDBuffer() {
	for(int p = 0; p < 8; p++) {
		uint8_t m = 1 << p;
		for(int y = 0; y < 16; y++) {
			for(int x = 0; x < 128; x++) {
				uint8_t c = x; //powf(x / 255.0f * 2.0f, 2.2f) * 255.0f;
				uint8_t col0 = (c & m ? 1 : 0) + (c & m ? 2 : 0) + (c & m ? 4 : 0);
				uint8_t col1 = (c & m ? 1 : 0) + (c & m ? 2 : 0) + (c & m ? 4 : 0);
				writeBuffer[(x * 2 + 0 + (y * 256)) + p * 256 * 16] = col0 + (col1 << 3);
				writeBuffer[(x * 2 + 1 + (y * 256)) + p * 256 * 16] = col0 + (col1 << 3) + 64;
			}
		}
	}
}

void FillDMDBuffer() {
	int idx = 0;
	for(int p = 0; p < 8; p++) {
		uint8_t m = 1 << p;
		for(int y = 0; y < 16; y++) {
			for(int x = 0; x < 128; x++) {
				uint8_t col0 = frame[x + y * 128];
				uint8_t px0 = (globalPalette[col0 * 3 + 0] & m ? 1 : 0) + (globalPalette[col0 * 3 + 1] & m ? 2 : 0) + (globalPalette[col0 * 3 + 2] & m ? 4 : 0);

				uint8_t col1 = frame[x + (y + 16) * 128];
				uint8_t px1 = (globalPalette[col1 * 3 + 0] & m ? 1 : 0) + (globalPalette[col1 * 3 + 1] & m ? 2 : 0) + (globalPalette[col1 * 3 + 2] & m ? 4 : 0);


				writeBuffer[idx++] = px0 + (px1 << 3);
				writeBuffer[idx++] = px0 + (px1 << 3) + 64;
			}
		}
	}
}

volatile uint8_t pass = 0;
volatile uint8_t y = 0;

void SendFrame() {
	TIM4->CR1 &= ~TIM_CR1_CEN;

	GPIOA->ODR = (GPIOA->ODR & ~(0x01E)) | (y<<1); // set row
	GPIOB->BSRR = GPIO_PIN_8; // strobe up


	int prevPass = pass;
	int prevY = y;

	pass++;
	if(pass == 8) {
		y++;
		if(y == 16)
			y = 0;
		pass = 0;
	}

	TIM4->ARR = 0x1FFF;
	uint16_t litTime = (0x1FFF - 0x100) >> (7 - prevPass);
	TIM4->CCR2 = 0x1FFF - litTime;
	TIM4->PSC = 0;//(1 << prevPass) - 1;

	GPIOB->BSRR = GPIO_PIN_8 << 16; // strobe down
	TIM4->CNT = 0; // reset counter



	TIM4->CR1 |= TIM_CR1_CEN;

	// send data
	GPIOA->ODR |= (1 << 8); // profile
	uint16_t idx = y * 256 + prevPass * 256 * 16;
	uint16_t v = GPIOB->ODR & ~(0xFF);
	for(int x = 0; x < 256; x++) {
		//GPIOB->ODR =  (y == 0 || y == 1) ? (x % 2 ? 9 : 73) : ((GPIOB->ODR & ~(0xFF)) | DMDBuffer[x + (y * 256) + pass * 256*16]);
		GPIOB->ODR = (v | readBuffer[idx++]);
	}
	GPIOA->ODR &= ~(1 << 8); // profile
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
  MX_SPI1_Init();
  MX_FATFS_Init();
  MX_TIM4_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */
  unsigned short pinState = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  //HAL_Delay(1000);
  SendUART("Start:\r\n");
  if(f_mount(&fs, "", 0) != FR_OK)
	  SendUART("Error (sdcard): can't mount sdcard\r\n");
  else
	  SendUART("Success (sdcard): SD CARD mounted successfully\r\n");

  //ScanDirectory("Arcade");
  //ReadGif("Computers/AMIGA_MonkeyIsland01.gif");
  ReadGif("Arcade/ARCADE_NEOGEO_MetalSlugFire05_Shabazz.gif");
  //ReadGif("Arcade/ARCADE_MortalKombat05SubZero.gif");



  SendUART("Ended SD card\r\n");

  InitDMDBuffer();
  FillDMDBuffer();
  SwapBuffer();

  HAL_TIM_Base_Start_IT(&htim4);
  HAL_TIM_PWM_Init(&htim4);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

	ReadGifImage();
	FillDMDBuffer();
	SwapBuffer();

	//SendUART("Oh!\r\n");
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
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
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	if(htim == &htim4)
		SendFrame();
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
