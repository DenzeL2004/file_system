#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>


#define BUFFER_SIZE 1024

#define S_IFMT		__S_IFMT
#define S_IFDIR		__S_IFDIR
#define S_IFCHR		__S_IFCHR
#define S_IFBLK		__S_IFBLK
#define S_IFREG		__S_IFREG
#define S_IFSOCK	__S_IFSOCK
#define S_IFIFO		__S_IFIFO
#define S_IFLNK		__S_IFLNK



const char* GetFileType(uint32_t type) { 
	switch (type & S_IFMT) {  
		case S_IFREG:   return "REG";
		case S_IFDIR:   return "DIR"; 
		case S_IFCHR:   return "CHR";
		case S_IFBLK:   return "BLK";
		case S_IFIFO:   return "FIFO";
		case S_IFLNK:   return "SYM";
		case S_IFSOCK:  return "SOCK";
		default:        return "UNKNOWN";
    }
}

void WriteProcessFiles(const char* pid, const char* dir_path) {
	DIR *dir;
	struct dirent *entry;

	if ((dir = opendir(dir_path)) == NULL) {
		return;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		char sym_link[BUFFER_SIZE]; 
		snprintf(sym_link, sizeof(sym_link), "%s/%s", dir_path, entry->d_name);

		char file_name[BUFFER_SIZE];
		size_t count = readlink(sym_link, file_name, BUFFER_SIZE);

		if (count == -1) {
			continue;
		}

		file_name[count] = '\0';

		struct stat file_stat;
		stat(file_name, &file_stat);

		uint32_t dev_major =  major(file_stat.st_dev);
		uint32_t dev_minor =  minor(file_stat.st_dev);

		const char *str_type = GetFileType(file_stat.st_mode);

		printf("%-20s %-20ld %-20ld %u,%-20u %-20s %s\n", pid, file_stat.st_ino, file_stat.st_size, 
															 dev_major, dev_minor, str_type, file_name);
	}

	closedir(dir);
}

void lsof() {
	const char* dir_path = "/proc";
	
	DIR *dir;
	struct dirent *entry;

	if ((dir = opendir(dir_path)) == NULL) {
		perror("failed open dir");
		return;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		char full_path[BUFFER_SIZE]; 
		snprintf(full_path, sizeof(full_path), "%s/%s/fd", dir_path, entry->d_name);

		WriteProcessFiles(entry->d_name, full_path);
	}

	closedir(dir);
}

int main() {
	lsof();
}