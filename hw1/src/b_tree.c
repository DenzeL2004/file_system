#include "b_tree.h"

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

off_t GetFileSize(int fd) {
  struct stat st;
  if (fstat(fd, &st) == -1) {
      perror("fstat failed");
      return -1;
  }
  return st.st_size;
}

int KeyCompare(const KeyType* lhs, const KeyType* rhs) {
  return strncmp(lhs->data, rhs->data, BTREE_KEY_LEN);
}

void KeyCopy(KeyType* dst,const KeyType* src) {
  strncpy(dst->data, src->data, BTREE_KEY_LEN);
}

BTreeNode* CreateBTreeNode(uint32_t order) {
	BTreeNode* node = (BTreeNode*)calloc(1, sizeof(BTreeNode));

	node->is_leaf = 0;
	node->count = 0;
	
	node->keys = (KeyType*)calloc(2 * order - 1, sizeof(KeyType));
	node->is_delete = (uint8_t*)calloc(2 * order, sizeof(uint16_t));
  node->children = (OffsetType*)calloc(2 * order, sizeof(OffsetType));

	return node;
}

void DeleteBTreeNode(BTreeNode* node) { 
	assert(node != NULL);

	free(node->keys);
	free(node->is_delete);
  free(node->children);

	free(node);
}

void BTreeReadHeader(int fd, BTreeHeader* header) {
	assert(header != NULL);

	lseek(fd, 0, SEEK_SET);
	read(fd, header, sizeof(BTreeHeader));
}

void BTreeWriteHeader(int fd, BTreeHeader* header) {
	assert(header != NULL);

	lseek(fd, 0, SEEK_SET);
	write(fd, header, sizeof(BTreeHeader));
}

void BTreeCreate(int fd, uint32_t order) { 
  BTreeHeader header;

  header.root_offset = 0;     
  header.order = order;  
  header.key_count = 0;
  
	write(fd, &header, sizeof(BTreeHeader));
}

void BTreeNodeWriteOnDisk(int fd, const BTreeHeader* header, const BTreeNode* node, OffsetType offset) {
	assert(header != NULL);
	assert(node != NULL);

	lseek(fd, offset, SEEK_SET);

	write(fd, &node->is_leaf, sizeof(node->is_leaf));
	write(fd, &node->count, sizeof(node->count));

 	write(fd, node->keys, (header->order * 2 - 1) * sizeof(KeyType));
  write(fd, node->is_delete, (header->order * 2 - 1) * sizeof(uint8_t));
	write(fd, node->children, (header->order * 2) * sizeof(OffsetType));
}

void BTreeNodeReadFromDisk(int fd, const BTreeHeader* header, BTreeNode* node, OffsetType offset) {
	assert(header != NULL);
	assert(node != NULL);

	lseek(fd, offset, SEEK_SET);
	
	read(fd, &node->is_leaf, sizeof(node->is_leaf));
	read(fd, &node->count, sizeof(node->count));

	read(fd, node->keys, (header->order * 2 - 1) * sizeof(KeyType));
  read(fd, node->is_delete, (header->order * 2 - 1) * sizeof(uint8_t));
	read(fd, node->children, (header->order * 2) * sizeof(OffsetType));
}

DiskNode* AllocateNewNodeOnDisk(int fd, const BTreeHeader* header) {
	assert(header != NULL);

  DiskNode* node = (DiskNode*)calloc(1, sizeof(DiskNode));

  node->offset = GetFileSize(fd);
  node->payload = CreateBTreeNode(header->order);

  BTreeNodeWriteOnDisk(fd, header, node->payload, node->offset);

  return node;
}

DiskNode* ReadNodeFromDisk(int fd, const BTreeHeader* header, OffsetType offset) {
	assert(header != NULL);

  DiskNode* node = (DiskNode*)calloc(1, sizeof(DiskNode));

  node->offset = offset;
  node->payload = CreateBTreeNode(header->order);

  BTreeNodeReadFromDisk(fd, header, node->payload, node->offset);

  return node;
}

void DeleteDiskNode(DiskNode* node) {
  assert(node != NULL);

  DeleteBTreeNode(node->payload);
  free(node);
}


void BtreeSplitChildren(int fd, const BTreeHeader* header, 
                        DiskNode* node, DiskNode* left_child, int child_num) {
  DiskNode* right_child = AllocateNewNodeOnDisk(fd, header);
  right_child->payload->is_leaf = left_child->payload->is_leaf;
  right_child->payload->count = header->order - 1;

  for (int i = 0; i < (int)header->order - 1; i++) {
    right_child->payload->keys[i] = left_child->payload->keys[i + header->order];
  }

  if (left_child->payload->is_leaf == 0) {
    for (int i = 0; i < (int)header->order; i++) {
      right_child->payload->children[i] = left_child->payload->children[i + header->order];
    }
  }
  left_child->payload->count = header->order - 1;
  
  for (int i = node->payload->count; i > child_num; i--) {
    node->payload->children[i + 1] = node->payload->children[i];
  }

  node->payload->children[child_num + 1] = right_child->offset;
  
  for (int i = (int)node->payload->count - 1; i >= child_num; i--) {
    node->payload->keys[i + 1] = node->payload->keys[i];
  }
  node->payload->keys[child_num] = left_child->payload->keys[header->order - 1];
  
  node->payload->count++;
  
  BTreeNodeWriteOnDisk(fd, header, node->payload, node->offset);
  BTreeNodeWriteOnDisk(fd, header, left_child->payload, left_child->offset);
  BTreeNodeWriteOnDisk(fd, header, right_child->payload, right_child->offset);

  DeleteDiskNode(right_child);
}

size_t FindKeyIndex(const DiskNode* node, const KeyType* key) {
  for (size_t i = 0; i < node->payload->count; i++) {
    if (KeyCompare(&node->payload->keys[i], key) == 0) {
      return i;
    }
  }

  return -1;
}

void BTreeInsertNonfull(int fd, BTreeHeader* header, DiskNode* node, const KeyType* key) {  

  size_t key_index = FindKeyIndex(node, key);
  if (key_index != -1) {
    if (node->payload->is_delete[key_index]) {
      header->key_count++;
    }
    
    node->payload->is_delete[key_index] = 0;
    BTreeNodeWriteOnDisk(fd, header, node->payload, node->offset);
    return;
  }

  int i = (int)(node->payload->count - 1);
	
	if (node->payload->is_leaf) {
                      
		while (i >= 0 && KeyCompare(key, &node->payload->keys[i]) < 0) {
			node->payload->keys[i + 1] = node->payload->keys[i];
			i--;
		}

    i++;

    KeyCopy(&node->payload->keys[i], key);
    header->key_count++;
		node->payload->count++;

		BTreeNodeWriteOnDisk(fd, header, node->payload, node->offset);
	} else {
		while (i >= 0 && KeyCompare(key, &node->payload->keys[i]) < 0) {
			i--; 
		}

    i++;
    DiskNode* child = ReadNodeFromDisk(fd, header, node->payload->children[i]);
    if (child->payload->count == header->order * 2 - 1) {
      BtreeSplitChildren(fd, header, node, child, i);
      if (KeyCompare(key, &node->payload->keys[i]) > 0) {
        i++;
      } 
    }

    child->offset = node->payload->children[i];
    BTreeNodeReadFromDisk(fd, header, child->payload, child->offset);
    BTreeInsertNonfull(fd, header, child, key);

    DeleteDiskNode(child);
	}
}

void BTreeInsert(int fd, const KeyType* key) {
	BTreeHeader header;
	BTreeReadHeader(fd, &header);

  if (header.root_offset == 0) {
    DiskNode* root = AllocateNewNodeOnDisk(fd, &header);
    header.root_offset = root->offset;
    header.key_count = 1;

    root->payload->is_leaf = 1;
    root->payload->count = 1;
    KeyCopy(&root->payload->keys[0], key);

    BTreeNodeWriteOnDisk(fd, &header, root->payload, root->offset);

    DeleteDiskNode(root);
  }
  else {
    DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);

    size_t key_index = FindKeyIndex(root, key);
    if (key_index != -1) {

      if (root->payload->is_delete[key_index]) {
        header.key_count++;
      }

      root->payload->is_delete[key_index] = 0;
      BTreeNodeWriteOnDisk(fd, &header, root->payload, root->offset);
    } else {
      if (root->payload->count == header.order * 2 - 1) {
        DiskNode* new_root = AllocateNewNodeOnDisk(fd, &header);
        header.root_offset = new_root->offset;
        
        new_root->payload->is_leaf = 0;
        new_root->payload->count = 0;
        new_root->payload->children[0] = root->offset;
        
        BtreeSplitChildren(fd, &header, new_root, root, 0);
        BTreeInsertNonfull(fd, &header, new_root, key);

        DeleteDiskNode(new_root);
      } else {
        BTreeInsertNonfull(fd, &header, root, key);
      }
    }
    DeleteDiskNode(root);
  }

  BTreeWriteHeader(fd, &header); 
}

OffsetType BTreeFindNode(int fd, const BTreeHeader* header, DiskNode* node, const KeyType* key) {

  int i = (int)(node->payload->count - 1);

  int cmp_res = -1;
  while (i >= 0 && (cmp_res = KeyCompare(key, &node->payload->keys[i])) < 0) {
    i--;
  }

  if (i != -1 && cmp_res == 0) {
    if (node->payload->is_delete[i]) {
      return 0;
    } else {
      return node->offset;
    }
  }

  if (node->payload->is_leaf) {
    return 0;
  }

  i++;

  DiskNode* child = ReadNodeFromDisk(fd, header, node->payload->children[i]);

  OffsetType offset = BTreeFindNode(fd, header, child, key);

  DeleteDiskNode(child);

  return offset;
}

OffsetType BTreeFind(int fd, const KeyType* key) {
	BTreeHeader header;
	BTreeReadHeader(fd, &header);

  if (header.root_offset == 0) {
    return 0;
  }
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);

  OffsetType offset = BTreeFindNode(fd, &header, root, key);

  DeleteDiskNode(root);

  return offset;
}

void BTreeNodeDeleteKey(int fd, BTreeHeader* header, DiskNode* node, const KeyType* key) {

  int i = (int)(node->payload->count - 1);

  int cmp_res = -1;
  while (i >= 0 && (cmp_res = KeyCompare(key, &node->payload->keys[i])) < 0) {
    i--;
  }

  if (i != -1 && cmp_res == 0) {
    node->payload->is_delete[i] = 1;
    header->key_count--;
    BTreeNodeWriteOnDisk(fd, header, node->payload, node->offset);
    return;
  }

  if (node->payload->is_leaf) {
    return;
  }

  i++;

  DiskNode* child = ReadNodeFromDisk(fd, header, node->payload->children[i]);

  BTreeNodeDeleteKey(fd, header, child, key);

  DeleteDiskNode(child);
}

void BTreeDeleteKey(int fd, const KeyType* key) {
	BTreeHeader header;
	BTreeReadHeader(fd, &header);

  if (header.root_offset == 0) {
    return;
  }
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);

  BTreeNodeDeleteKey(fd, &header, root, key);

  DeleteDiskNode(root);

  BTreeWriteHeader(fd, &header); 
}

void GenerateDotRecursive(int fd, const BTreeHeader* header, 
                          OffsetType offset, FILE* dot_file) {
  if (offset == 0) return;
  
  DiskNode* node = ReadNodeFromDisk(fd, header, offset);
  if (node == NULL) return;
  
  fprintf(dot_file, "  node_%ld [label=\"", (long)offset);
  for (size_t i = 0; i < node->payload->count; i++) {
    fprintf(dot_file, "%s", node->payload->keys[i].data);
    if (i != node->payload->count - 1) {
      fprintf(dot_file, " | ");
    }
  }
  fprintf(dot_file, "\"];\n");
  
  if (!node->payload->is_leaf) {
    for (size_t i = 0; i <= node->payload->count; i++) {
      if (node->payload->children[i] != 0) {
        fprintf(dot_file, "  node_%ld -> node_%ld;\n", 
                (long)offset, (long)node->payload->children[i]);
        GenerateDotRecursive(fd, header, node->payload->children[i], dot_file);
      }
    }
  }
  
  DeleteDiskNode(node);
}

void BTreeVisualize(int fd, const char* dot_filename, const char* png_filename) {
  assert(dot_filename != NULL);
  assert(png_filename != NULL);

  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  
  if (header.root_offset == 0) {
    printf("Tree is empty\n");
    return;
  }
  
  FILE* dot_file = fopen(dot_filename, "w");
  if (dot_file == NULL) {
      perror("Failed to create DOT file");
      return;
  }
  
  fprintf(dot_file, "digraph BTree {\n");
  fprintf(dot_file, "  node [shape=record, height=.1];\n");
  
  GenerateDotRecursive(fd, &header, header.root_offset, dot_file);
  
  fprintf(dot_file, "}\n");
  fclose(dot_file);
  
  if (png_filename != NULL) {
    char command[256];
    snprintf(command, sizeof(command), "dot -Tpng %s -o %s", dot_filename, png_filename);
    system(command);
    printf("Visualization created: %s -> %s\n", dot_filename, png_filename);
  }
}
