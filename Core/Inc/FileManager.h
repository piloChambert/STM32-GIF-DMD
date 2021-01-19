#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <stdint.h>
#include "fatfs.h"

struct {
	FATFS fs;

	// File iterator
	DIR rootDir;
	DIR subDir;
	char currentSubDir[255];

	uint32_t fileCount;

	uint32_t directoryCount;
	struct {
		uint32_t hash;
		uint8_t enable;
		uint32_t fileCount;
	} directories[256];
} FileManager;

extern int InitSDCard();
extern int ResetSDCard();

// return the filename in the enable directories
extern int GetFilenameAtIndex(uint32_t idx, char *filename);
extern int GetDirectoryAtIndex(uint32_t idx, char *name);

extern int NextGIFFilename(char *gifFilename);

extern void UpdateFileCount();

#endif
