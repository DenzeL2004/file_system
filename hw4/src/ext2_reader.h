#pragma once

#include <ext2fs/ext2_fs.h>
#include <string>
#include <vector>
#include <stdint.h> 

#define SUPERBLOCK_OFFSET 1024
#define SYSTEM_BLOCK_LOG_SIZE 9 


#define EXT2_S_IFREG 0x8000 
#define EXT2_S_IFDIR 0x4000 

class Ext2Reader { 
  public:
    Ext2Reader(const std::string& img_path);
    ~Ext2Reader();

    uint64_t GetBlockBitmapOffset(uint32_t group_num) const;
    uint64_t GetInodeBitmapOffset(uint32_t group_num) const;
    uint64_t GetInodeTableOffset(uint32_t group_num) const;

    uint64_t GetBlockSize() const;

    bool IsInodeValid(uint32_t inode) const;
    uint32_t FindInode(const std::string& file_path) const;
    
    void ReadRawBlock(uint32_t block_num, uint8_t* buffer) const;
    uint32_t GetPointersPerBlock() const;

    void ShowFileInfo(const std::string& file_path) const;
    void ShowFileInfo(uint32_t inode_id) const;

    void ShowFileBlocks(const std::string& file_path) const;

  private:
    class InodeDataIterator {

      public: 
        InodeDataIterator(const ext2_inode& inode_, const Ext2Reader& ext2_reader_);
        ~InodeDataIterator(); 

        uint32_t GetBlockNum() const;

        void NextBlock();

        bool IsEnd() const;

      private:
        void ReadBlockNum();

        const ext2_inode& inode_;
        const Ext2Reader& ext2_reader_;
        
        size_t processed_blocks_count_;
        const size_t max_blocks_count_;

        uint32_t block_num_;

        uint32_t* layer1_buffer_;            
        uint32_t* layer2_buffer_;           
        uint32_t* layer3_buffer_;           

        uint32_t current_layer1_block_num_; 
        uint32_t current_layer2_block_num_;
        uint32_t current_layer3_block_num_; 
    };

    ext2_group_desc ReadGroupDescriptor(uint32_t group_num) const;

    ext2_inode ReadInode(uint32_t inode) const;

    void FileContent(const ext2_inode& inode) const;

    void DirContent(const ext2_inode& inode) const;
    
    int ing_fd_;
    ext2_super_block super_block_;
};