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

	uint32_t order = 2;
	BTreeCreate(fd, order);

	printf("Building B-Tree with order %u\n", order);

	for (size_t i = 1; i <= 10; i++) {
		KeyType key;
		snprintf(key.data, BTREE_KEY_LEN, "%02ld", i);
		BTreeInsert(fd, &key);
	}

	printf("\nGenerating visualization...\n");
	BTreeVisualize(fd, "b_tree.dot", "b_tree.png");


	int fd_2 = open("demo_tree_2.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd_2 == -1) {
		perror("Failed to create file");
		return 1;
	}

	uint32_t order_2 = 3;
	BTreeCreate(fd_2, order_2);

	printf("Building B-Tree with order %u\n", order_2);

	for (size_t i = 11; i <= 99; i++) {
		KeyType key;
		snprintf(key.data, BTREE_KEY_LEN, "%02ld", i);
		BTreeInsert(fd_2, &key);
	}

	printf("\nGenerating visualization...\n");
	BTreeVisualize(fd_2, "b_tree_2.dot", "b_tree_2.png");

	int fd_3 = open("demo_tree_3.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd_3 == -1) {
		perror("Failed to create file");
		return 1;
	}

	BTreeMerge(fd, fd_2, fd_3, 3);
	printf("\nGenerating visualization...\n");
	BTreeVisualize(fd_3, "b_tree_3.dot", "b_tree_3.png");

	close(fd_2);
	close(fd);
	return 0;
}