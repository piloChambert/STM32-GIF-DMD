#include "FileManager.h"

#include <string.h>
#include <stdio.h>

uint32_t hash(unsigned char *str)
{
    uint32_t hash = 5381;
    unsigned char c;

    while ((c = *str++) != 0)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

// parse each sub directory
int ParseSDCard() {
	FileManager.fileCount = 0;
	FileManager.directoryCount = 0;

	// enumerate all subdir
	DIR rd;
	if(f_opendir(&rd, "/") != FR_OK)
		return 1; // XXX error handling!

	FILINFO finfo;
	if(f_readdir(&rd, &finfo) != FR_OK)
		return 1; // XXX error handling!

	while(finfo.fname[0] != '\0') {
		if(finfo.fattrib & AM_DIR && finfo.fname[0] != '.') {
			// we are in a subDir, count file
			uint32_t dirIdx = FileManager.directoryCount;
			FileManager.directoryCount++;

			FileManager.directories[dirIdx].hash = hash((unsigned char *)finfo.fname);
			FileManager.directories[dirIdx].enable = 1; // enable directory
			FileManager.directories[dirIdx].fileCount = 0;

			DIR sd;
			if(f_opendir(&sd, finfo.fname) != FR_OK)
				return 1; // XXX error handling!

			FILINFO finfo2;
			if(f_readdir(&sd, &finfo2) != FR_OK)
				return 1; // XXX error handling!

			while(finfo2.fname[0] != '\0') {
				if(!(finfo2.fattrib & AM_DIR))
					FileManager.directories[dirIdx].fileCount++;

				char temp[255];
				strcpy(temp, finfo2.fname);
				if(f_readdir(&sd, &finfo2) != FR_OK)
					return 1; // XXX error handling!
			}

			if(f_closedir(&sd) != FR_OK)
				return 1; // XXX error handling!
		}

		if(f_readdir(&rd, &finfo) != FR_OK)
			return 1; // XXX error handling!
	}

	if(f_closedir(&rd) != FR_OK)
		return 1; // XXX error handling!

	return 0; // XXX error handling
}

// update total file count based on directory state
void UpdateFileCount() {
	FileManager.fileCount = 0;

	for(int i = 0; i < FileManager.directoryCount; i++) {
		if(FileManager.directories[i].enable)
			FileManager.fileCount += FileManager.directories[i].fileCount;
	}
}

// mount file system and open root dir
int InitSDCard() {
	  if(f_mount(&FileManager.fs, "", 1) != FR_OK)
		  return 1; // XXX error handling

	  if(ParseSDCard() != 0)
		  return 1; // XXX error handling!

	  UpdateFileCount();

	  // open rootdir
	  if(f_opendir(&FileManager.rootDir, "/") != FR_OK)
		  return 1; // XXX error handling

	  return 0;
}

int ResetSDCard() {
	f_closedir(&FileManager.subDir);
	memset(&FileManager.subDir, 0, sizeof(DIR));
	f_closedir(&FileManager.rootDir);
	memset(&FileManager.rootDir, 0, sizeof(DIR));
	f_mount(NULL, "", 0);
	memset(&FileManager.fs, 0, sizeof(FATFS));

	return 0; // XXX error handling
}

int GetFilenameAtIndex(uint32_t idx, char *filename) {
	int fileCount = 0;
	int dirCount = 0;

	filename[0] = '\0';

	// enumerate all subdir
	DIR rd;
	if(f_opendir(&rd, "/") != FR_OK)
		return 1; // XXX error handling

	FILINFO finfo;
	if(f_readdir(&rd, &finfo) != FR_OK)
		return 1; // XXX error handling

	while(finfo.fname[0] != '\0' && filename[0] == '\0') {
		if(finfo.fattrib & AM_DIR && finfo.fname[0] != '.') {
			// we are in a subDir, count file
			dirCount++;

			// if it's an enabled directory
			if(FileManager.directories[dirCount].enable) {
				DIR sd;
				if(f_opendir(&sd, finfo.fname) != FR_OK)
					return 1; // XXX error handling

				FILINFO finfo2;
				if(f_readdir(&sd, &finfo2) != FR_OK)
					return 1; // XXX error handling

				while(finfo2.fname[0] != '\0') {
					if(!(finfo2.fattrib & AM_DIR)) {
						if(idx == fileCount) { // we find the file
							sprintf(filename, "%s/%s", finfo.fname, finfo2.fname);
							break;
						}
						fileCount++;
					}

					if(f_readdir(&sd, &finfo2) != FR_OK)
						return 1; // XXX error handling
				}

				if(f_closedir(&sd) != FR_OK)
					return 1; // XXX error handling
			}
		}

		if(f_readdir(&rd, &finfo) != FR_OK)
			return 1; // XXX error handling
	}

	if(f_closedir(&rd) != FR_OK)
		return 1; // XXX error handling

	return 0; // XXX error handling
}

int GetDirectoryAtIndex(uint32_t idx, char *name) {
	int dirCount = 0;

	name[0] = '\0';

	// enumerate all subdir
	DIR rd;
	if(f_opendir(&rd, "/") != FR_OK)
		return 1; // XXX error handling

	FILINFO finfo;
	if(f_readdir(&rd, &finfo) != FR_OK)
		return 1; // XXX error handling

	while(finfo.fname[0] != '\0' && name[0] == '\0') {
		if(finfo.fattrib & AM_DIR && finfo.fname[0] != '.') {
			// we are in a subDir
			if(idx == dirCount) {
				strcpy(name, finfo.fname);
				break;
			}

			dirCount++;
		}

		if(f_readdir(&rd, &finfo) != FR_OK)
			return 1; // XXX error handling
	}

	if(f_closedir(&rd) != FR_OK)
		return 1; // XXX error handling

	return 0; // XXX error handling
}

// return next gif file or 0
int NextGIFFilenameInSubDir(char *gifFilename) {
	FILINFO finfo;

	while(1) {
		if(f_readdir(&FileManager.subDir, &finfo) != FR_OK) {
			return 0; // XXX error handling
		}

		if(finfo.fname[0] == 0) // no more file
			return 0;

		if(!(finfo.fattrib & AM_DIR)) {
			// is it a gif file?
			int l = strlen(finfo.fname);
			if(!strcasecmp(&finfo.fname[l - 4], ".gif")) {
				sprintf(gifFilename, "%s/%s", FileManager.currentSubDir, finfo.fname);
				return 1;
			}
		}
	}

	return 0;
}

int NextGIFFilename(char *gifFilename) {
	FILINFO finfo;

	while(NextGIFFilenameInSubDir(gifFilename) == 0) {
		if(f_closedir(&FileManager.subDir) != FR_OK)
			return 1; // XXX error handling

		// go to next subdir
		if(f_readdir(&FileManager.rootDir, &finfo) != FR_OK) {
			return 1; // XXX error handling
		}

		if(finfo.fname[0] == 0) {
			// no more file
			if(f_rewinddir(&FileManager.rootDir) != FR_OK) // rewind
				return 1; // XXX error handling
		}

		else if(finfo.fattrib & AM_DIR && finfo.fname[0] != '.') { // don't load hidden directory
			if(f_opendir(&FileManager.subDir, finfo.fname) != FR_OK)
				return 1; // XXX error handling

			strcpy(FileManager.currentSubDir, finfo.fname);
		}
	}

	return 0;
}

void ScanDirectory(char *path) {
	DIR dir;
	FILINFO finfo;

	if(f_opendir(&dir, path) != FR_OK) {
		return;
	}

	finfo.fname[0] = 0;
	while(1) {
	  if(f_readdir(&dir, &finfo) != FR_OK) {
		  break;
	  }

	  if(finfo.fname[0] == 0)
		  break;

	  if(finfo.fattrib & AM_DIR)
		  printf("%s/%s <dir>\r\n", path, finfo.fname);
	  else
		  printf("%s/%s %ld\r\n", path, finfo.fname, finfo.fsize);
	}
	f_closedir(&dir);
}
