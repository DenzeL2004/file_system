#ifndef __B_TREE__
#define __B_TREE__

#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>

#define BTREE_KEY_LEN 256

typedef uint64_t OffsetType;

typedef struct KeyType {
	char data[BTREE_KEY_LEN];
} KeyType;

typedef struct BTreeNode {
	uint8_t is_delete;
	uint8_t is_leaf;
	size_t count;    
	KeyType* keys;
	OffsetType* children;
} BTreeNode;

typedef struct DiskNode {
	OffsetType offset;
	BTreeNode* payload;
} DiskNode;

typedef struct BTreeHeader {
	OffsetType root_offset;
	uint32_t order;
} BTreeHeader;

int KeyCompare(const KeyType* lhs, const KeyType* rhs);
void KeyCopy(KeyType* dst,const KeyType* src);

void BTreeCreate(int fd, uint32_t order);
void BTreeInsert(int fd, const KeyType* key);
OffsetType BTreeFind(int fd, const KeyType* key);

BTreeNode* CreateBTreeNode(uint32_t order);
void DeleteBTreeNode(BTreeNode* node);

void BTreeReadHeader(int fd, BTreeHeader* header);
void BTreeWriteHeader(int fd, BTreeHeader* header);

void BTreeNodeWriteOnDisk(int fd, const BTreeHeader* header, const BTreeNode* node, OffsetType offset);
void BTreeNodeReadFromDisk(int fd, const BTreeHeader* header, BTreeNode* node, OffsetType offset);

DiskNode* AllocateNewNodeOnDisk(int fd, const BTreeHeader* header);
DiskNode* ReadNodeFromDisk(int fd, const BTreeHeader* header, OffsetType offset);
void DeleteDiskNode(DiskNode* node);

void BTreeVisualize(int fd, const char* dot_filename, const char* png_filename);
#endif