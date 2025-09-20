#include <gtest/gtest.h>
#include <fcntl.h>
#include <unistd.h>

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

  int fd = -1;
};

TEST_F(BTreeTest, CreateBTree) {
  uint32_t order = 3;
  BTreeCreate(fd, order);
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  
  EXPECT_EQ(header.order, order);
  EXPECT_EQ(header.root_address, 0);
}

TEST_F(BTreeTest, InsertSingleKey) {
  uint32_t order = 3;
  BTreeCreate(fd, order);
  
  KeyType key = 42;
  BTreeInsert(fd, key);
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  EXPECT_NE(header.root_address, 0);
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_address);
  EXPECT_TRUE(root->payload->is_leaf);
  EXPECT_EQ(root->payload->count, 1);
  EXPECT_EQ(root->payload->keys[0], key);
  
  DeleteDiskNode(root);
}

TEST_F(BTreeTest, InsertMultipleKeys) {
  uint32_t order = 3;
  BTreeCreate(fd, order);
  
  KeyType keys[] = {10, 20, 5, 15, 25};
  for (KeyType key : keys) {
      BTreeInsert(fd, key);
  }
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_address);
  EXPECT_GT(root->payload->count, 0);
  
  DeleteDiskNode(root);
}

TEST_F(BTreeTest, SplitNode) {
  uint32_t order = 2;
  BTreeCreate(fd, order);
  
  KeyType keys[] = {3, 2, 1, 5, 4, 6};
  for (KeyType key : keys) {
    BTreeInsert(fd, key);
  }
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_address);
  EXPECT_FALSE(root->payload->is_leaf); 
  EXPECT_EQ(root->payload->count, 2);  
  EXPECT_EQ(root->payload->keys[0], 2);
  EXPECT_EQ(root->payload->keys[1], 4);

  DiskNode* child_1 = ReadNodeFromDisk(fd, &header, root->payload->children[0]);
  EXPECT_TRUE(child_1->payload->is_leaf);
  EXPECT_EQ(child_1->payload->count, 1);  
  EXPECT_EQ(child_1->payload->keys[0], 1);

  DiskNode* child_2 = ReadNodeFromDisk(fd, &header, root->payload->children[1]);
  EXPECT_TRUE(child_2->payload->is_leaf);
  EXPECT_EQ(child_2->payload->count, 1);  
  EXPECT_EQ(child_2->payload->keys[0], 3);

  DiskNode* child_3 = ReadNodeFromDisk(fd, &header, root->payload->children[2]);
  EXPECT_TRUE(child_3->payload->is_leaf);
  EXPECT_EQ(child_3->payload->count, 2);  
  EXPECT_EQ(child_3->payload->keys[0], 5);
  EXPECT_EQ(child_3->payload->keys[1], 6);


  DeleteDiskNode(child_3);
  DeleteDiskNode(child_2);
  DeleteDiskNode(child_1);
  DeleteDiskNode(root);
}

TEST_F(BTreeTest, LargeInsertion) {
  uint32_t order = 4;
  BTreeCreate(fd, order);
  
  for (KeyType i = 1; i <= 20; ++i) {
    BTreeInsert(fd, i);
  }
  
  BTreeHeader header;
  BTreeReadHeader(fd, &header);
  
  DiskNode* root = ReadNodeFromDisk(fd, &header, header.root_address);
  // EXPECT_GT(root->payload->count, 0);
  
  DeleteDiskNode(root);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}