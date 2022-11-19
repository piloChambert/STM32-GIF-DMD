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
#include "adc.h"
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
#define LERP(A, B, X) (A * (1.0f - X) + B * X)

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
	KEY_UP = 0,
	KEY_DOWN,
	KEY_MENU,
	KEY_OK
};
struct {
	GPIO_TypeDef *port;
	uint16_t pin;

	uint32_t repeatTime; // time when switch state for repeat

	uint8_t value;
	uint8_t state;
} keys[] = {
		{GPIOB, GPIO_PIN_10, 0, 0},
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
			keys[i].state = 1 + KEY_DOWN_EVENT; // set to up state and set down event
			keys[i].repeatTime = currentTime + KEY_FIRST_REPEAT_TIME;
		}
		else if(keys[i].state == 1 && keys[i].value == 0)
			keys[i].state = KEY_UP_EVENT; // set to down state and set down event
		else {
			// key repeat
			if((keys[i].state & 0x01) && (currentTime > keys[i].repeatTime)) {
				keys[i].state |= KEY_DOWN_EVENT + KEY_REPEAT_EVENT; // generate event
				keys[i].repeatTime = currentTime + KEY_REPEAT_TIME; // reset time
			}
			else
				keys[i].state &= 0x1; // keep state bit, reset events
		}
	}
}

// this is saved in RTC register
struct {
	unsigned int randomPlay : 1;
	unsigned int clockDisplayInterval : 3; // 10s increment, from 0 (never) to 7 which is always (no gif)
	unsigned int clockDisplayTime : 3; // 10s increment, if clockDisplayInterval is not 0
	unsigned int luminosityMax : 7; // % of luminosity
	unsigned int luminosityMin : 7; // % of luminosity
} Configuration;

uint32_t currentGIFFileIndex = 0;
float ambientlLuminosity = 1.0f;

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
	// load only .gif file
	int stop = 0;
	while(!stop) {
		if(Configuration.randomPlay)
			currentGIFFileIndex = rand() % FileManager.fileCount; // use GDC 2017 noise based RNG

		GetFilenameAtIndex(currentGIFFileIndex, GIFFilename);

		if(!Configuration.randomPlay)
			currentGIFFileIndex = (currentGIFFileIndex + 1) % FileManager.fileCount;

		int l = strlen(GIFFilename);
		if(l > 5 && strcasecmp(GIFFilename + l - 4, ".gif") == 0)
			stop = 1;
	}

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

void MainMenuState();
void DirectoryMenuState();
void SetClockState();
void SettingsMenuState();

stateFunction currentState = StartState;
uint32_t prevFrameTick = 0;
uint32_t frameTick = 0;
uint32_t clockStartTick = 0;
uint32_t gifStartTick = 0;
uint32_t luminosityStartTick = 0;
uint8_t luminosityVisible = 0;

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
		gifStartTick = HAL_GetTick();
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

	else if(func == DirectoryMenuState) {
		CodePalette((uint8_t *)menuFont.palette, menuPalette, 256);
	}

	else if(func == SettingsMenuState) {
		CodePalette((uint8_t *)menuFont.palette, menuPalette, 256);
	}

	else if(func == MainMenuState) {
		CodePalette((uint8_t *)menuFont.palette, menuPalette, 256);
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


void UpdateLuminosity() {
	if(keys[KEY_UP].state & KEY_DOWN_EVENT) {
		Configuration.luminosityMin = MIN(Configuration.luminosityMin + 5 * (1.0f - ambientlLuminosity), 100);
		Configuration.luminosityMax = MIN(Configuration.luminosityMax + 5 * ambientlLuminosity, 100);

		luminosityStartTick = HAL_GetTick();
		luminosityVisible = 1;
	}
	if(keys[KEY_DOWN].state & KEY_DOWN_EVENT) {
		Configuration.luminosityMin = MIN(Configuration.luminosityMin - 5 * (1.0f - ambientlLuminosity), 100);
		Configuration.luminosityMax = MIN(Configuration.luminosityMax - 5 * ambientlLuminosity, 100);

		luminosityStartTick = HAL_GetTick();
		luminosityVisible = 1;
	}

	int v = LERP(Configuration.luminosityMin, Configuration.luminosityMax, ambientlLuminosity);
	if(luminosityVisible) {
		if(HAL_GetTick() - luminosityStartTick > 20000) {
			luminosityVisible = 0;
		}
		else {
			// draw luminosity progress bar over current frame
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
			if(Configuration.clockDisplayInterval != 0)
				SetState(ClockState);
			else
				SetState(GIFFileState);
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
		// how long?
		if(Configuration.clockDisplayInterval == 0 || HAL_GetTick() - gifStartTick < Configuration.clockDisplayInterval * 10000) {
			LoadNextGifFile();
		}
	}

	else if(err == GIF_NO_MORE_QUEUED_FRAME) {
		// we get there only if there's no more gif frame, ie, it's time to show the clock!
		SetState(ClockState);
	}

	else if(err != GIF_NO_ERROR) {
		// switch to error state
		SetState(CardErrorState);
	}

	else if((keys[KEY_MENU].state & KEY_DOWN_EVENT)) {
		SetState(MainMenuState);
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

	if(Configuration.clockDisplayInterval != 0x07 && HAL_GetTick() - clockStartTick > Configuration.clockDisplayTime * 10000)
		SetState(GIFFileState);

	else if((keys[KEY_MENU].state & KEY_DOWN_EVENT)) {
		SetState(MainMenuState);
	}

}

int digit = 0;
void SetClockState() {
	// erase bg
	memset(menuFrame, 10, 128 * 32);

	DrawTime(30, 8, 1 << digit);

	if((keys[KEY_OK].state & KEY_DOWN_EVENT)) {
		digit++;
		if(digit == 2)
			digit = 0;
	}

	RTC_TimeTypeDef time;
	HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
	RTC_DateTypeDef date;
	HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);

	if((keys[KEY_UP].state & KEY_DOWN_EVENT)) {
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

	if((keys[KEY_DOWN].state & KEY_DOWN_EVENT)) {
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

	if(keys[KEY_MENU].state & KEY_DOWN_EVENT) {
		SetState(MainMenuState);
	}
	else {
		swapBufferRequest = 1;
		while(swapBufferRequest) { }
		EncodeFrameToDMDBuffer(menuFrame, menuPalette);
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
			x += PrintMenuChar(*p - 30, x, y) - 1; // -1 to merge character outline together
		p++;
	}
}

int directoryMenuOffset = 0;
int directoryMenuSelectedItem = 0;
void DirectoryMenuState() {
	// erase bg
	memset(menuFrame, 0x03, 128 * 32);

	PrintMenuStr("Exit", 9, 0 + directoryMenuOffset);

	char dirName[255];
	int firstIdx = MAX(-directoryMenuOffset - 10, 0) / 10;
	for(int i = firstIdx; i < MIN(firstIdx + 4, FileManager.directoryCount); i++) {
		GetDirectoryAtIndex(i, dirName);
		PrintMenuChar(FileManager.directories[i].enable ? 2 : 1, 10, (i + 1) * 10 + directoryMenuOffset);
		PrintMenuStr(dirName, 19, (i + 1) *  10 + directoryMenuOffset);
	}

	// cursor
	PrintMenuChar(0, 1, directoryMenuSelectedItem * 10 + directoryMenuOffset);

	if((keys[KEY_DOWN].state & KEY_DOWN_EVENT) && directoryMenuSelectedItem < FileManager.directoryCount) {
		directoryMenuSelectedItem++;
	}
	if((keys[KEY_UP].state & KEY_DOWN_EVENT) && directoryMenuSelectedItem > 0) {
		directoryMenuSelectedItem--;
	}

	if(keys[KEY_OK].state & KEY_DOWN_EVENT) {
		if(directoryMenuSelectedItem == 0)
			SetState(MainMenuState);
		else {
			FileManager.directories[directoryMenuSelectedItem - 1].enable = !FileManager.directories[directoryMenuSelectedItem - 1].enable;
			UpdateFileCount();
			if(Configuration.randomPlay)
				currentGIFFileIndex = rand() % FileManager.fileCount;
			else
				currentGIFFileIndex = 0;
		}
	}

	else {
		// scroll
		if(directoryMenuSelectedItem * 10 + directoryMenuOffset < 0)
			directoryMenuOffset += 2;
		else if(directoryMenuSelectedItem * 10 + 9 + directoryMenuOffset > 32)
			directoryMenuOffset -= 2;

		swapBufferRequest = 1;
		while(swapBufferRequest) { }
		EncodeFrameToDMDBuffer(menuFrame, menuPalette);
	}
}

int settingsMenuSelectedItem = 0;
int settingsMenuOffset = 0;
void SettingsMenuState() {
	memset(menuFrame, 0x03, 128 * 32);

	// print menu item
	PrintMenuStr("Exit", 9, 0 + settingsMenuOffset);
	if(Configuration.randomPlay) {
		PrintMenuStr("Random  [On]", 9, 10 + settingsMenuOffset);
		currentGIFFileIndex = rand() % FileManager.fileCount;
	}
	else {
		PrintMenuStr("Random  [Off]", 9, 10 + settingsMenuOffset);
		currentGIFFileIndex = 0;
	}

	// clock display interval
	char buff[16];
	PrintMenuStr("Clock", 9, 20 + settingsMenuOffset);
	if(!Configuration.clockDisplayInterval)
		PrintMenuStr("Disabled", 54, 20 + settingsMenuOffset);
	else if(Configuration.clockDisplayInterval == 0x07)
		PrintMenuStr("Always", 54, 20 + settingsMenuOffset);
	else {
		sprintf(buff, "Every %ds", Configuration.clockDisplayInterval * 10);
		PrintMenuStr(buff, 54, 20 + settingsMenuOffset);
	}

	// clock display time
	sprintf(buff, "for %ds", Configuration.clockDisplayTime * 10);
	PrintMenuStr(buff, 9, 30 + settingsMenuOffset);

	// cursor
	PrintMenuChar(0, 1, settingsMenuSelectedItem * 10 + settingsMenuOffset);

	if((keys[KEY_DOWN].state & KEY_DOWN_EVENT) && settingsMenuSelectedItem < 3) {
		settingsMenuSelectedItem++;
	}
	if((keys[KEY_UP].state & KEY_DOWN_EVENT) && settingsMenuSelectedItem > 0) {
		settingsMenuSelectedItem--;
	}

	if(keys[KEY_MENU].state & KEY_DOWN_EVENT) {
		if(Configuration.clockDisplayInterval != 0)
			SetState(ClockState);
		else
			SetState(GIFFileState);
	}

	if(keys[KEY_OK].state & KEY_DOWN_EVENT) {
		switch(settingsMenuSelectedItem) {
		case 0:
			SetState(MainMenuState);
			break;
		case 1:
			Configuration.randomPlay = !Configuration.randomPlay;
			break;
		case 2:
			Configuration.clockDisplayInterval++;
			break;
		case 3:
			Configuration.clockDisplayTime++;
			if(!Configuration.clockDisplayTime)
				Configuration.clockDisplayTime = 1; // 10s min
			break;
		}
	}

	// scroll
	if(settingsMenuSelectedItem * 10 + settingsMenuOffset < 0)
		settingsMenuOffset += 2;

	if(settingsMenuSelectedItem * 10 + 9 + settingsMenuOffset > 32)
		settingsMenuOffset -= 2;



	swapBufferRequest = 1;
	while(swapBufferRequest) { }
	EncodeFrameToDMDBuffer(menuFrame, menuPalette);
}

int mainMenuSelectedItem = 0;
int mainMenuOffset = 0;
void MainMenuState() {
	memset(menuFrame, 0x03, 128 * 32);

	// print menu item
	PrintMenuStr("Exit", 9, 0 + mainMenuOffset);
	PrintMenuStr("Settings", 9, 10 + mainMenuOffset);
	PrintMenuStr("Directories", 9, 20 + mainMenuOffset);
	PrintMenuStr("Clock", 9, 30 + mainMenuOffset);

	// cursor
	PrintMenuChar(0, 1, mainMenuSelectedItem * 10 + mainMenuOffset);

	if((keys[KEY_DOWN].state & KEY_DOWN_EVENT) && mainMenuSelectedItem < 3) {
		mainMenuSelectedItem++;
	}
	if((keys[KEY_UP].state & KEY_DOWN_EVENT) && mainMenuSelectedItem > 0) {
		mainMenuSelectedItem--;
	}

	if(keys[KEY_MENU].state & KEY_DOWN_EVENT) {
		if(Configuration.clockDisplayInterval != 0)
			SetState(ClockState);
		else
			SetState(GIFFileState);
	}

	if(keys[KEY_OK].state & KEY_DOWN_EVENT) {
		switch(mainMenuSelectedItem) {
		case 0:
			if(Configuration.clockDisplayInterval != 0)
				SetState(ClockState);
			else
				SetState(GIFFileState);
			break;
		case 1:
			SetState(SettingsMenuState);
			break;
		case 2:
			SetState(DirectoryMenuState);
			break;
		case 3:
			SetState(SetClockState);
			break;
		}
	}

	// scroll
	if(mainMenuSelectedItem * 10 + mainMenuOffset < 0)
		mainMenuOffset += 2;

	if(mainMenuSelectedItem * 10 + 9 + mainMenuOffset > 32)
		mainMenuOffset -= 2;



	swapBufferRequest = 1;
	while(swapBufferRequest) { }
	EncodeFrameToDMDBuffer(menuFrame, menuPalette);
}

// H : 0..360
// S : 0..1
// V : 0..1
// rgb: uint8_t[3]
void HSVtoRGB(float h, float s, float v, uint8_t *rgb) {
    float c = s*v;
    float x = c * (1.0f - fabs(fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r, g, b;
    if(h >= 300.0f) {
        r = c;
        g = 0.0f;
        b = x;
    }

    else if(h >= 240.0f) {
        r = x;
        g = 0.0f;
        b = c;
    }

    else if(h >= 180.0f) {
        r = 0.0f;
        g = x;
        b = c;
    }

    else if(h >= 120.0f) {
        r = 0.0f;
        g = c;
        b = x;
    }

    else if(h > 60.0f) {
        r = x;
        g = c;
        b = 0.0f;
    }

    else {
        r = c;
        g = x;
        b = 0.0f;
    }

    rgb[0] = (r + m) * 255.0f;
    rgb[1] = (g + m) * 255.0f;
    rgb[2] = (b + m) * 255.0f;
}

#if 0
uint8_t testPalette[256 * 3];
uint8_t codedTestPalette[256 * 8];
uint8_t testFrame[4096];

double time = 0.0;
uint32_t prevTick = 0;

void ColorTestState() {
	float h = 360.0f * (sin(M_PI * 2.0f * time * 0.1f) * 0.5f + 0.5f);
	float s = (sin(M_PI * 2.0f * time * 1.0f) * 0.5f + 0.5f);

	// create palette for this frame
	for(int i = 0; i < 256; i++) {
		HSVtoRGB(h, s, 1.0f - i / 255.0f, &testPalette[i * 3]);
	}

	CodePalette(testPalette, codedTestPalette, 256);

	// create image
	for(int y = 0; y < 32; y++) {
		for(int x = 0; x < 128; x++) {
			testFrame[x + y * 128] = x + y * 4;
		}
	}

	swapBufferRequest = 1;
	while(swapBufferRequest) { }
	EncodeFrameToDMDBuffer(testFrame, codedTestPalette);

	uint32_t current = HAL_GetTick();
	double dt = (current - prevTick) * 0.001;
	time += dt;
	prevTick = current;
}
#endif


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
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  // default configuration
  Configuration.randomPlay = 1;
  Configuration.clockDisplayInterval = 0;
  Configuration.clockDisplayTime = 1;
  Configuration.luminosityMin = 10;
  Configuration.luminosityMax = 100;

  //HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x02);
  uint32_t v = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);
  if(v != 0x88) {
	  printf("OUch");
  }

  //LoadGIFFile("Computers/AMIGA_MonkeyIsland01.gif");
  //LoadNextGif();

  // init rand
  srand(hrtc.Instance->TR);

  prevFrameTick = HAL_GetTick();
  frameTick = 0;

  DMDInit();

  // start ambient light capture
  HAL_ADC_Start(&hadc1);

  SetState(StartState);

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  // update keys
	  UpdateKeyState();

	  // run current state
	  currentState();

	  // detect light

	  if(HAL_ADC_PollForConversion(&hadc1, 0) == HAL_OK) {
		  float adcLum = HAL_ADC_GetValue(&hadc1);

#define ADC_LUM_MIN 80
#define ADC_LUM_MAX 1400
		  adcLum = MAX(0.0f, MIN(1.0f, (adcLum - ADC_LUM_MIN) / (ADC_LUM_MAX - ADC_LUM_MIN)));

		  // Low pass filter
		  float a = 0.9f;
		  ambientlLuminosity = ambientlLuminosity * a + adcLum * (1.0f - a);

		  // set lum
		  SetDMDLuminosity(LERP(Configuration.luminosityMin, Configuration.luminosityMax, ambientlLuminosity));
		  HAL_ADC_Start(&hadc1);
	  }
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
