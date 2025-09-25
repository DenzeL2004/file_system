#include "b_tree.h"

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

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

void BTreeGetKeysFromNode(int fd, const BTreeHeader* header, const DiskNode* node, KeyType* keys, size_t* pos) {
  for (size_t i = 0; i < node->payload->count; i++){
    if (!node->payload->is_leaf) {
      DiskNode* child = ReadNodeFromDisk(fd, header, node->payload->children[i]);
      BTreeGetKeysFromNode(fd, header, child, keys, pos);
      DeleteDiskNode(child);
    }

    if (node->payload->is_delete[i]) 
      continue;

    KeyCopy(&keys[*pos], &node->payload->keys[i]);
    *pos = *pos + 1;
  }

  if (!node->payload->is_leaf) {
    DiskNode* child = ReadNodeFromDisk(fd, header, node->payload->children[node->payload->count]);
    BTreeGetKeysFromNode(fd, header, child, keys, pos);
    DeleteDiskNode(child);
  }
}

void BTreeGetAllKeys(int fd, const BTreeHeader* header, KeyType* keys) {
  if (header->root_offset == 0) {
    return;
  }

  DiskNode* root = ReadNodeFromDisk(fd, header, header->root_offset);

  size_t pos = 0;
  BTreeGetKeysFromNode(fd, header, root, keys, &pos);

  DeleteDiskNode(root);
}

void BTreeConstructBySortedList(int fd, BTreeHeader* header, const KeyType* keys) {
  if (header->key_count == 0) {
    header->root_offset = 0;
    BTreeWriteHeader(fd, header);
    return;
  }

  const size_t max_keys_per_node = 2 * header->order - 1;
  const size_t min_keys_per_node = header->order - 1;

  KeyType* cur_level_keys = (KeyType*)calloc(header->key_count, sizeof(KeyType));
  memcpy(cur_level_keys, keys, sizeof(KeyType) * header->key_count);

  OffsetType* prev_level_nodes_offsets = (OffsetType*)calloc(header->key_count, sizeof(OffsetType));
  size_t prev_level_nodes_count = 0;

  size_t level = 0;
  size_t cur_keys_on_level = header->key_count;

  while (cur_keys_on_level > max_keys_per_node) {
    size_t level_nodes_count = 0;
    OffsetType* cur_level_nodes_offsets = (OffsetType*)calloc(header->key_count, sizeof(OffsetType));

    size_t offset_index = 0;
    size_t next_level_keys_count = 0;
    size_t key_index = 0;

    while (key_index + min_keys_per_node <= cur_keys_on_level) {
      DiskNode* node = AllocateNewNodeOnDisk(fd, header);
      if (level == 0) {
        node->payload->is_leaf = 1;
      }

      cur_level_nodes_offsets[level_nodes_count] = node->offset;

      size_t node_keys_count = MIN(max_keys_per_node, cur_keys_on_level - key_index);

      memcpy(node->payload->keys, cur_level_keys + key_index, node_keys_count * sizeof(KeyType));
      node->payload->count = node_keys_count;

      key_index += node_keys_count;
      if (key_index + 1 <= cur_keys_on_level) {
        KeyCopy(&cur_level_keys[next_level_keys_count], &cur_level_keys[key_index]);
        key_index++;
        next_level_keys_count++;
      }

      size_t i = 0;
      while (offset_index < prev_level_nodes_count && i <= max_keys_per_node) {
        node->payload->children[i] = prev_level_nodes_offsets[offset_index];
        i++;
        offset_index++;
      }

      level_nodes_count++;

      BTreeNodeWriteOnDisk(fd, header, node->payload, node->offset);
      DeleteDiskNode(node);
    }

    while (key_index < cur_keys_on_level) {
      KeyCopy(&cur_level_keys[next_level_keys_count], &cur_level_keys[key_index]);
      key_index++;
      next_level_keys_count++;
    }
    

    for (size_t i = 0; i < level_nodes_count; i++) {
      prev_level_nodes_offsets[i] = cur_level_nodes_offsets[i];
    }
    prev_level_nodes_count = level_nodes_count;
    
    free(cur_level_nodes_offsets);

    level++;
    cur_keys_on_level = next_level_keys_count;
  }

  // Root 

  DiskNode* root = AllocateNewNodeOnDisk(fd, header);
  if (level == 0) {
    root->payload->is_leaf = 1;
  }

  memcpy(root->payload->keys, cur_level_keys, cur_keys_on_level * sizeof(KeyType));
  root->payload->count = cur_keys_on_level;

  size_t i = 0;
  memcpy(root->payload->children, prev_level_nodes_offsets, prev_level_nodes_count * sizeof(OffsetType));
  
  header->root_offset = root->offset;
  BTreeWriteHeader(fd, header);

  BTreeNodeWriteOnDisk(fd, header, root->payload, root->offset);
  DeleteDiskNode(root);

  free(prev_level_nodes_offsets);
  free(cur_level_keys);
}

void BTreeMerge(int lhs_fd, int rhs_fd, int dst_fd, size_t dst_order) {
  BTreeHeader lhs_header;
  BTreeReadHeader(lhs_fd, &lhs_header);

  KeyType* lhs_keys = (KeyType*)calloc(lhs_header.key_count, sizeof(KeyType));
  BTreeGetAllKeys(lhs_fd, &lhs_header, lhs_keys);

  BTreeHeader rhs_header;
  BTreeReadHeader(rhs_fd, &rhs_header);

  KeyType* rhs_keys = (KeyType*)calloc(rhs_header.key_count, sizeof(KeyType));
  BTreeGetAllKeys(rhs_fd, &rhs_header, rhs_keys);

  BTreeHeader dst_header;
  dst_header.order = dst_order;

  KeyType* keys = (KeyType*)calloc(lhs_header.key_count + rhs_header.key_count, sizeof(KeyType));

  size_t i = 0, j = 0, k = 0;
  while (i < lhs_header.key_count && j < rhs_header.key_count) {
    int cmp = KeyCompare(&lhs_keys[i], &rhs_keys[j]);
    
    if (cmp < 0) {
      KeyCopy(&keys[k], &lhs_keys[i]);
      i++;
    } else if (cmp > 0) {
      KeyCopy(&keys[k], &rhs_keys[j]);
      j++;
    } else {
      KeyCopy(&keys[k], &lhs_keys[i]);
      i++;
      j++;
    }
    k++;
  }

  while (i < lhs_header.key_count) {
    KeyCopy(&keys[k], &lhs_keys[i]);
    k++;
    i++;
  }

  while (j < rhs_header.key_count) {
    KeyCopy(&keys[k], &rhs_keys[j]);
    k++;
    j++;
  }

  dst_header.key_count = k;

  // for (size_t ii = 0; ii < k; ii++) {
  //   printf("%s ", keys[ii].data);
  // }

  BTreeCreate(dst_fd, dst_order);
  BTreeConstructBySortedList(dst_fd, &dst_header, keys);

  free(lhs_keys);
  free(rhs_keys);
  free(keys);
}
