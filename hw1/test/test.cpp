#include <gtest/gtest.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

extern "C" {
#include "../src/b_tree.h"
}

class BTreeTest : public ::testing::Test {
protected:
  void SetUp() override {
    fd = open("test_tree.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    ASSERT_NE(fd, -1);
  }

  void TearDown() override {
    if (fd != -1) {
      close(fd);
    }
    remove("test_tree.bin");
  }

  KeyType make_key(const char* str) {
    KeyType key;
    strncpy(key.data, str, BTREE_KEY_LEN - 1);
    key.data[BTREE_KEY_LEN - 1] = '\0';
    return key;
  }

  KeyType make_key(int num) {
    KeyType key;
    snprintf(key.data, BTREE_KEY_LEN, "%03d", num);
    return key;
  }

  int fd = -1;
};

TEST_F(BTreeTest, CreateBTree) {
  uint32_t order = 3;
  BTreeCreate(fd, order);
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  
  EXPECT_EQ(header.order, order);
  EXPECT_EQ(header.root_offset, 0);
}

TEST_F(BTreeTest, InsertSingleKey) {
  uint32_t order = 3;
  BTreeCreate(fd, order);
  
  KeyType key = make_key("test_key");
  BTreeInsert(fd, &key);
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  EXPECT_NE(header.root_offset, 0);
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);
  EXPECT_TRUE(root->payload->is_leaf);
  EXPECT_EQ(root->payload->count, 1);
  EXPECT_EQ(KeyCompare(&root->payload->keys[0], &key), 0);
  
  DeleteDiskNode(root);
}

TEST_F(BTreeTest, InsertMultipleKeys) {
  uint32_t order = 3;
  BTreeCreate(fd, order);
  
  const char* key_strings[] = {"a", "b", "c", "d", "e"};
  for (const char* key_str : key_strings) {
      KeyType key = make_key(key_str);
      BTreeInsert(fd, &key);
  }
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);
  EXPECT_GT(root->payload->count, 0);
  
  DeleteDiskNode(root);
}

TEST_F(BTreeTest, InsertNumericKeys) {
  uint32_t order = 3;
  BTreeCreate(fd, order);
  
  for (int i = 1; i <= 10; i++) {
    KeyType key = make_key(i);
    BTreeInsert(fd, &key);
  }
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);

  EXPECT_NE(header.root_offset, 0);
  EXPECT_EQ(header.key_count, 10);
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);
  EXPECT_GT(root->payload->count, 0);
  
  for (size_t i = 1; i < root->payload->count; i++) {
    EXPECT_LT(KeyCompare(&root->payload->keys[i-1], &root->payload->keys[i]), 0);
  }
  
  DeleteDiskNode(root);
}

TEST_F(BTreeTest, SplitNode) {
  uint32_t order = 2;
  BTreeCreate(fd, order);

  int keys[] = {3, 2, 1, 5, 4, 6};
  for (int k : keys) {
    KeyType key = make_key(k);
    BTreeInsert(fd, &key);
  }
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  EXPECT_NE(header.root_offset, 0);
  EXPECT_EQ(header.key_count, 6);

  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);
  EXPECT_FALSE(root->payload->is_leaf); 
  EXPECT_EQ(root->payload->count, 2);
  
  KeyType key2 = make_key(2);
  KeyType key4 = make_key(4);
  EXPECT_EQ(KeyCompare(&root->payload->keys[0], &key2), 0);
  EXPECT_EQ(KeyCompare(&root->payload->keys[1], &key4), 0);

  DiskNode* child_1 = ReadNodeFromDisk(fd, &header, root->payload->children[0]);
  EXPECT_TRUE(child_1->payload->is_leaf);
  EXPECT_EQ(child_1->payload->count, 1);
  KeyType key1 = make_key(1);
  EXPECT_EQ(KeyCompare(&child_1->payload->keys[0], &key1), 0);

  DiskNode* child_2 = ReadNodeFromDisk(fd, &header, root->payload->children[1]);
  EXPECT_TRUE(child_2->payload->is_leaf);
  EXPECT_EQ(child_2->payload->count, 1);
  KeyType key3 = make_key(3);
  EXPECT_EQ(KeyCompare(&child_2->payload->keys[0], &key3), 0);

  DiskNode* child_3 = ReadNodeFromDisk(fd, &header, root->payload->children[2]);
  EXPECT_TRUE(child_3->payload->is_leaf);
  EXPECT_EQ(child_3->payload->count, 2);
  KeyType key5 = make_key(5);
  KeyType key6 = make_key(6);
  EXPECT_EQ(KeyCompare(&child_3->payload->keys[0], &key5), 0);
  EXPECT_EQ(KeyCompare(&child_3->payload->keys[1], &key6), 0);

  DeleteDiskNode(child_3);
  DeleteDiskNode(child_2);
  DeleteDiskNode(child_1);
  DeleteDiskNode(root);
}

TEST_F(BTreeTest, LargeInsertion) {
  uint32_t order = 4;
  BTreeCreate(fd, order);
  
  for (int i = 1; i <= 20; ++i) {
    KeyType key = make_key(i);
    BTreeInsert(fd, &key);
  }
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  EXPECT_NE(header.root_offset, 0);
  EXPECT_EQ(header.key_count, 20);
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);
  EXPECT_FALSE(root->payload->is_leaf); 
  
  EXPECT_GT(root->payload->count, 0);
  
  for (size_t i = 0; i <= root->payload->count; i++) {
    EXPECT_NE(root->payload->children[i], 0);
    
    DiskNode* child = ReadNodeFromDisk(fd, &header, root->payload->children[i]);
    EXPECT_GT(child->payload->count, 0);
    
    for (size_t j = 1; j < child->payload->count; j++) {
      EXPECT_LT(KeyCompare(&child->payload->keys[j-1], &child->payload->keys[j]), 0);
    }
    
    DeleteDiskNode(child);
  }
  
  DeleteDiskNode(root);
}

TEST_F(BTreeTest, InsertRepeatKey) {
  uint32_t order = 3;
  BTreeCreate(fd, order);
  
  KeyType key = make_key("test_key");
  for (size_t i = 0; i < 10; i++) {
    BTreeInsert(fd, &key);
  }
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  EXPECT_NE(header.root_offset, 0);
  EXPECT_EQ(header.key_count, 1);
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);
  EXPECT_TRUE(root->payload->is_leaf);
  EXPECT_EQ(root->payload->count, 1);
  EXPECT_EQ(KeyCompare(&root->payload->keys[0], &key), 0);
  
  DeleteDiskNode(root);
}

TEST_F(BTreeTest, FindInEmptyTree) {
  uint32_t order = 3;
  BTreeCreate(fd, order);
  
  KeyType key = make_key("test_key");

  EXPECT_EQ(BTreeFind(fd, &key), 0);
}

TEST_F(BTreeTest, FindInSimpleTree) {
  uint32_t order = 3;
  BTreeCreate(fd, order);
  
  for (int i = 1; i <= 3; ++i) {
    KeyType key = make_key(i);
    BTreeInsert(fd, &key);
  }

  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  EXPECT_NE(header.root_offset, 0);
  EXPECT_EQ(header.key_count, 3);

  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);

  KeyType key = make_key(1);
  EXPECT_EQ(BTreeFind(fd, &key), root->offset);
  
  KeyType not_exist_key = make_key(10);
  EXPECT_EQ(BTreeFind(fd, &not_exist_key), 0);

  DeleteDiskNode(root);
}

TEST_F(BTreeTest, FindInTree) {
  uint32_t order = 2;
  BTreeCreate(fd, order);
  
  for (int i = 1; i <= 9; ++i) {
    KeyType key = make_key(i);
    BTreeInsert(fd, &key);
  }

  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  EXPECT_NE(header.root_offset, 0);
  EXPECT_EQ(header.key_count, 9);

  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_offset);

  KeyType key_1 = make_key(6);
  EXPECT_EQ(BTreeFind(fd, &key_1), root->payload->children[1]);

  DiskNode* left = ReadNodeFromDisk(fd, &header, root->payload->children[0]);

  KeyType key_2 = make_key(3);
  EXPECT_EQ(BTreeFind(fd, &key_2), left->payload->children[1]);
  
  KeyType not_exist_key = make_key(10);
  EXPECT_EQ(BTreeFind(fd, &not_exist_key), 0);

  DeleteDiskNode(left);
  DeleteDiskNode(root);
}

TEST_F(BTreeTest, DeleteInTree) {
  uint32_t order = 4;
  BTreeCreate(fd, order);
  
  const size_t kMaxCount = 20;
  for (int i = 1; i <= kMaxCount; ++i) {
    KeyType key = make_key(i);
    BTreeInsert(fd, &key);
  }

  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  EXPECT_NE(header.root_offset, 0);
  EXPECT_EQ(header.key_count, kMaxCount);

  KeyType key = make_key(1);
  EXPECT_NE(BTreeFind(fd, &key), 0);

  BTreeDeleteKey(fd, &key);
  EXPECT_EQ(BTreeFind(fd, &key), 0);
  
  BTreeReadHeader(fd, &header);
  EXPECT_EQ(header.key_count, kMaxCount - 1);

  BTreeInsert(fd, &key);
  EXPECT_NE(BTreeFind(fd, &key), 0);

  BTreeReadHeader(fd, &header);
  EXPECT_EQ(header.key_count, kMaxCount);

}


TEST_F(BTreeTest, DeleteMany) {
  uint32_t order = 4;
  BTreeCreate(fd, order);
  
  const size_t kMaxCount = 20;
  for (int i = 1; i <= kMaxCount; ++i) {
    KeyType key = make_key(i);
    BTreeInsert(fd, &key);
  }

  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  EXPECT_NE(header.root_offset, 0);
  EXPECT_EQ(header.key_count, kMaxCount);

  std::vector<int> keys = {1, 4, 7, 10, 13, 16};
  for (auto& k : keys) {
    KeyType key = make_key(k);
    BTreeDeleteKey(fd, &key);
  }

  BTreeReadHeader(fd, &header);
  EXPECT_EQ(header.key_count, kMaxCount - keys.size());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}