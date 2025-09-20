#ifndef __B_TREE__
#define __B_TREE__

#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>

typedef uint32_t KeyType;
typedef off_t OffsetType;

typedef struct BTreeNode {
    uint8_t is_leaf;
    size_t count;    
    KeyType* keys;
    OffsetType* children;
} BTreeNode;

typedef struct DiskNode {
    OffsetType address;
    BTreeNode* payload;
} DiskNode;

typedef struct BTreeHeader {
    OffsetType root_address;
    uint32_t order;
} BTreeHeader;


void BTreeCreate(int fd, uint32_t order);
void BTreeInsert(int fd, const KeyType key);

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