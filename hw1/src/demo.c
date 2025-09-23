#include "b_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {

	int fd = open("demo_tree.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		perror("Failed to create file");
		return 1;
	}

	uint32_t order = 4;
	BTreeCreate(fd, order);

	printf("Building B-Tree with order %u\n", order);

	for (size_t i = 1; i <= 20; i++) {
		KeyType key;
		snprintf(key.data, BTREE_KEY_LEN, "%02ld", i);
		BTreeInsert(fd, &key);
	}

	printf("\nGenerating visualization...\n");
	BTreeVisualize(fd, "b_tree.dot", "b_tree.png");

	close(fd);
	return 0;
}