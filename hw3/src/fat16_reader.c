#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <memory.h>
#include <stdlib.h>

#include <linux/msdos_fs.h>

#define TIME_FORMAT_BUFFER_SIZE 64
#define FAT_NAME_MAX_LEN 256

#define DIR_ENTRY_ENRTY_SIZE 32
#define LFN_ATTR 0x0f

typedef struct {
  uint32_t root_dir_address;
  uint32_t data_area_start;
  uint32_t cluster_size;
  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint32_t fat_address;
} Fat16Layout;

void ReverseString(char* str, size_t len) {
  assert(str != NULL);
  
  if (len == 0) {
      return;
  }
  
  for (size_t i = 0; i < len / 2; i++) {
    char tmp = str[i];
    str[i] = str[len - 1 - i];
    str[len - 1 - i] = tmp;
  }
}

Fat16Layout GetFat16Layout(int fat_img) { 
  uint8_t buffer[SECTOR_SIZE];
  Fat16Layout layout = {0};

  int res = read(fat_img, buffer, SECTOR_SIZE);
  if (res == -1) {
      layout.root_dir_address = -1;
      return layout;
  }

  layout.bytes_per_sector = (buffer[12] << 8) | buffer[11];
  layout.sectors_per_cluster = buffer[13];
  
  uint16_t reserved_sectors = (buffer[15] << 8) | buffer[14];
  uint8_t fat_count = buffer[16];
  uint16_t root_entries = (buffer[18] << 8) | buffer[17];
  uint16_t sectors_per_fat = (buffer[23] << 8) | buffer[22];
  
  layout.fat_address = reserved_sectors * layout.bytes_per_sector;
  layout.root_dir_address = layout.fat_address + 
                            (fat_count * sectors_per_fat * layout.bytes_per_sector);
  layout.data_area_start = layout.root_dir_address + (root_entries * DIR_ENTRY_ENRTY_SIZE);
  layout.cluster_size = layout.bytes_per_sector * layout.sectors_per_cluster;

  return layout;
}

void ConvertDateTimeToString(uint16_t fat_date, uint16_t fat_time, char* buffer) {
  assert(buffer != NULL);

  int year = 1980 + ((fat_date >> 9) & 0x7F);
  int month = (fat_date >> 5) & 0x0F;
  int day = fat_date & 0x1F;
  
  int hour = (fat_time >> 11) & 0x1F;
  int minute = (fat_time >> 5) & 0x3F;
  int second = (fat_time & 0x1F) * 2;
  
  sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d (UTC)", 
          year, month, day, hour, minute, second);
}

size_t GetFileName(int fat_img, uint8_t* dir_entry, uint8_t* full_name) {
  assert(dir_entry != NULL);
  assert(full_name != NULL);
  
  size_t name_len = 0;
  while (dir_entry[11] == LFN_ATTR) {
    for (size_t i = 31; i >= 28; i--) {
      if (dir_entry[i] < 0x80 && dir_entry[i] != 0) {
        full_name[name_len++] = dir_entry[i];
      }
    }

    for (size_t i = 25; i >= 14; i--) {
      if (dir_entry[i] < 0x80 && dir_entry[i] != 0) {
        full_name[name_len++] = dir_entry[i];
      }
    }

    for (size_t i = 10; i >= 1; i--) {
      if (dir_entry[i] < 0x80 && dir_entry[i] != 0) {
        full_name[name_len++] = dir_entry[i];
      }
    }

    int res = read(fat_img, dir_entry, DIR_ENTRY_ENRTY_SIZE);
    if (res == -1) {
      return -1;
    }
  }

  ReverseString(full_name, name_len);

  full_name[name_len] = '\0';

  return name_len;
}

void RootFilesInfo(int fat_img, const Fat16Layout* layout) { 
  lseek(fat_img, layout->root_dir_address, SEEK_SET);

  uint8_t dir_entry[DIR_ENTRY_ENRTY_SIZE];

  while (1) {
    int res = read(fat_img, dir_entry, DIR_ENTRY_ENRTY_SIZE);
    if (res == -1) {
      fprintf(stderr, "Failed reade root entry\n");
      break;
    }

    if (dir_entry[0] == 0) {
      break;
    }

    if (dir_entry[0] == DELETED_FLAG) {
      continue;
    }

    uint8_t file_name[FAT_NAME_MAX_LEN + 1];

    size_t name_len = GetFileName(fat_img, dir_entry, file_name);
    if (name_len == -1) {
      fprintf(stderr, "Failed get file name\n");
      break;
    }

    uint8_t attr = dir_entry[11];

    uint16_t ctime = (dir_entry[15] << 8) | dir_entry[14];
    uint16_t cdate = (dir_entry[17] << 8) | dir_entry[16]; 
    
    char create_date_time[TIME_FORMAT_BUFFER_SIZE];
    ConvertDateTimeToString(cdate, ctime, create_date_time);

    uint16_t time = (dir_entry[23] << 8) | dir_entry[22];  
    uint16_t date = (dir_entry[25] << 8) | dir_entry[24];  

    char date_time[TIME_FORMAT_BUFFER_SIZE];
    ConvertDateTimeToString(date, time, date_time);

    printf("%s 0x%x %s/%s\n", file_name, attr, create_date_time, date_time);
  }
}

uint16_t GetNextCluster(int fat_img, const Fat16Layout* layout, uint16_t cluster_num) {
  uint32_t cluster_offset = layout->fat_address + cluster_num * 2;
  lseek(fat_img, cluster_offset, SEEK_SET);

  uint16_t next_cluster = 0;
  int res = read(fat_img, &next_cluster, sizeof(next_cluster));
  if (res == -1) {
    fprintf(stderr, "Failed read next cluster num\n");
    return EOF_FAT16;
  }

  return next_cluster;
}

void PrintFileContent(int fat_img, const Fat16Layout* layout, const char* file_path) {
  assert(file_path != NULL);

  lseek(fat_img, layout->root_dir_address, SEEK_SET);

  uint8_t dir_entry[DIR_ENTRY_ENRTY_SIZE];

  uint16_t first_cluster = 0;
  uint32_t file_size = 0;
  int file_found = 0;

  while (1) {
    int res = read(fat_img, dir_entry, DIR_ENTRY_ENRTY_SIZE);
    if (res == -1) {
      fprintf(stderr, "Failed reade root entry\n");
      break;
    }

    if (dir_entry[0] == 0) {
      break;
    }

    if (dir_entry[0] == DELETED_FLAG) {
      continue;
    }

    uint8_t file_name[FAT_NAME_MAX_LEN + 1];

    size_t name_len = GetFileName(fat_img, dir_entry, file_name);
    if (name_len == -1) {
      fprintf(stderr, "Failed get file name\n");
      break;
    }

    if (strncmp(file_name, file_path, name_len) == 0) {
      first_cluster = (dir_entry[27] << 8) | dir_entry[26];
      file_size = (dir_entry[31] << 24) | (dir_entry[30] << 16) | (dir_entry[29] << 8) | dir_entry[28];
      file_found = 1;
      break;
    }
  }

  if (!file_found) {
    printf("File %s don't found!\n", file_path);
    return;
  }

  uint8_t* cluster_data = (uint8_t*)calloc(layout->cluster_size, sizeof(uint8_t));
  uint32_t remaining_bytes = file_size;

  uint16_t cur_cluster = first_cluster;

  while (remaining_bytes > 0 || cur_cluster != EOF_FAT16) {
    uint32_t cluster_offset = layout->data_area_start + (cur_cluster - 2) * layout->cluster_size;
    lseek(fat_img, cluster_offset, SEEK_SET);

    int res = read(fat_img, cluster_data, layout->cluster_size);
    if (res == -1) {
      fprintf(stderr, "Failed read cluster data\n");
      break;
    }

    size_t bytes_to_write = (remaining_bytes < layout->cluster_size) ? 
                           remaining_bytes : layout->cluster_size;
    
    fwrite(cluster_data, 1, bytes_to_write, stdout);
    remaining_bytes -= bytes_to_write;

    cur_cluster = GetNextCluster(fat_img, layout, cur_cluster);
  }

  free(cluster_data);
}

int main(int argc, char* argv[]) {

  if (argc != 3) {
    printf("Invalid number of parameters: 3\nStart example: ./fat16_reader fat16.img file_name\n");
    return 0;
  }

  const char* fat_img_path = argv[1];
  const char* file_name = argv[2];

  int fat_img = open(fat_img_path, O_RDONLY);
  if (fat_img == -1) {
    fprintf(stderr, "Failed open file: %s\n", fat_img_path);
    return 1;
  }

  Fat16Layout layout = GetFat16Layout(fat_img);
  if (layout.root_dir_address == -1) {
    fprintf(stderr, "Failed get FAT16 layout of file: %s\n", fat_img_path);
    close(fat_img);
    return 1;
  }

  RootFilesInfo(fat_img, &layout);
  PrintFileContent(fat_img, &layout, file_name);

  close(fat_img);  
}