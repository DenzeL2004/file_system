#include "ext2_reader.h"


int main(int argc, char* argv[]) {

  if (argc != 3) {
    printf("Usage: ./ext2-reader ext2.img file_name\n");
    return 0;
  }

  const char* img_path = argv[1];
  const char* file_name = argv[2];

  Ext2Reader reader(img_path);
  reader.ShowFileInfo(file_name);
  reader.ShowFileBlocks(file_name);
}