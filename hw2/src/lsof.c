#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#define BUFFER_SIZE 1024

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

		printf("%-20s %-20ld %-20ld %s\n", pid, file_stat.st_ino, file_stat.st_size, file_name);
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