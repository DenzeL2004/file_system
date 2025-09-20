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

	uint32_t order = 3;
	BTreeCreate(fd, order);

	printf("Building B-Tree with order %u\n", order);
	printf("Inserting keys: 10, 20, 5, 15, 25, 30, 35, 40, 45, 50, 3, 7, 12, 18, 22, 28\n");

	KeyType keys[] = {10, 20, 5, 15, 25, 30, 35, 40, 45, 50, 3, 7, 12, 18, 22, 28};
	for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		BTreeInsert(fd, keys[i]);
		printf("Inserted key: %u\n", keys[i]);
	}

	printf("\nGenerating visualization...\n");
	BTreeVisualize(fd, "b_tree.dot", "b_tree.png");

	close(fd);

	printf("\nFiles created:\n");
	printf("  - b_tree.dot: Graphviz DOT file\n");
	printf("  - b_tree.png: Rendered tree visualization\n");
	printf("  - demo_tree.bin: Binary B-Tree storage\n");

	printf("\nTo view the tree:\n");
	printf("  - Install Graphviz: sudo apt-get install graphviz\n");
	printf("  - Or use online DOT viewer for b_tree.dot\n");

	return 0;
}