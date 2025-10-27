#include "ext2_reader.h"
#include <cassert> 
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <string.h>
#include <vector>

Ext2Reader::Ext2Reader(const std::string& img_path) {
  ing_fd_ = open(img_path.c_str(), O_RDONLY);

  if (ing_fd_ == -1) {
    throw std::runtime_error("can't open file: " + img_path);
  }

  size_t res = pread(ing_fd_, &super_block_, sizeof(super_block_), SUPERBLOCK_OFFSET);
  if (res != sizeof(super_block_)) {
    throw std::runtime_error("can't read superblock's image: " + img_path);
  }

  if (super_block_.s_magic != EXT2_SUPER_MAGIC) {
    throw std::runtime_error("it's not a ext file system: " + img_path);
  }
}

Ext2Reader::~Ext2Reader() {
  close(ing_fd_);
}

uint64_t Ext2Reader::GetBlockSize() const {
  return 1ull << (EXT2_MIN_BLOCK_LOG_SIZE + super_block_.s_log_block_size);
}

void Ext2Reader::ReadRawBlock(uint32_t block_num, uint8_t* buffer) const {
    uint64_t block_size = GetBlockSize();
    if (block_num == 0) {
        memset(buffer, 0, block_size);
        return;
    }
    uint64_t physical_offset = static_cast<uint64_t>(block_num) * block_size;
    
    size_t res = pread(ing_fd_, buffer, block_size, physical_offset);
    if (res != block_size) {
        throw std::runtime_error("Error reading raw block: " + std::to_string(block_num));
    }
}

uint32_t Ext2Reader::GetPointersPerBlock() const {
    return static_cast<uint32_t>(GetBlockSize() / sizeof(uint32_t)); 
}


ext2_group_desc Ext2Reader::ReadGroupDescriptor(uint32_t group_num) const {
   
  uint32_t num_groups = (super_block_.s_blocks_count + super_block_.s_blocks_per_group - 1) / super_block_.s_blocks_per_group;

  if (group_num >= num_groups) {
      throw std::out_of_range("invalid block group number");
  }

  
  uint64_t gdt_start_offset = static_cast<uint64_t>(super_block_.s_first_data_block) * GetBlockSize();
  
  uint64_t descriptor_offset = gdt_start_offset + static_cast<uint64_t>(group_num) * super_block_.s_desc_size;

  ext2_group_desc group_desc;
  
  size_t res = pread(ing_fd_, &group_desc, sizeof(group_desc), descriptor_offset);
  
  if (res != sizeof(group_desc)) {
      throw std::runtime_error("can't read Group Descriptor " + std::to_string(group_num));
  }

  return group_desc;
}

uint64_t Ext2Reader::GetBlockBitmapOffset(uint32_t group_num) const {
  ext2_group_desc group_desc = ReadGroupDescriptor(group_num);
  return static_cast<uint64_t>(group_desc.bg_block_bitmap) * GetBlockSize();
}

uint64_t Ext2Reader::GetInodeBitmapOffset(uint32_t group_num) const {
  ext2_group_desc group_desc = ReadGroupDescriptor(group_num);
  return static_cast<uint64_t>(group_desc.bg_inode_bitmap) * GetBlockSize();
}

uint64_t Ext2Reader::GetInodeTableOffset(uint32_t group_num) const {
  ext2_group_desc group_desc = ReadGroupDescriptor(group_num);
  return static_cast<uint64_t>(group_desc.bg_inode_table) * GetBlockSize();
}

ext2_inode Ext2Reader::ReadInode(uint32_t inode) const {
    if (inode == 0 || inode > super_block_.s_inodes_count) {
        throw std::out_of_range("invalid inode number: " + std::to_string(inode));
    }

  uint32_t group_num = (inode - 1) / super_block_.s_inodes_per_group;
  uint32_t index_in_table = (inode - 1) % super_block_.s_inodes_per_group;

  uint64_t inode_table_offset = GetInodeTableOffset(group_num); 

  uint64_t offset_in_table = static_cast<uint64_t>(index_in_table) * super_block_.s_inode_size;
  uint64_t physical_offset = inode_table_offset + offset_in_table;

  ext2_inode inode_info;
  
  size_t res = pread(ing_fd_, &inode_info, super_block_.s_inode_size, physical_offset);
  
  if (res != super_block_.s_inode_size) {
    throw std::runtime_error("can't read inode " + std::to_string(inode) + " at offset " + std::to_string(physical_offset));
  }

  return inode_info;
}

bool Ext2Reader::IsInodeValid(uint32_t inode) const {
  if (inode == 0 || inode > super_block_.s_inodes_count) {
      return false;
  }

  uint32_t bitmap_index = inode - 1;
  
  uint32_t group_num = bitmap_index / super_block_.s_inodes_per_group;

  uint64_t bitmap_offset = GetInodeBitmapOffset(group_num); 

  uint32_t index_in_group = bitmap_index % super_block_.s_inodes_per_group;
  
  uint32_t byte_num = index_in_group / 8;
  uint32_t bit_num = index_in_group % 8;

  uint64_t physical_offset = bitmap_offset + byte_num;

  uint8_t bitmap_byte;

  size_t res = pread(ing_fd_, &bitmap_byte, sizeof(bitmap_byte), physical_offset);
  
  if (res != sizeof(bitmap_byte)) {
      throw std::runtime_error("can't read byte from inode bitmap for group " + std::to_string(group_num));
  }

  return (bitmap_byte & (1 << bit_num)) != 0;
}


Ext2Reader::InodeDataIterator::InodeDataIterator(const ext2_inode& inode, const Ext2Reader& ext2_reader) :
  inode_(inode), ext2_reader_(ext2_reader), processed_blocks_count_(0), 
  max_blocks_count_(inode.i_blocks / (ext2_reader.GetBlockSize() >> SYSTEM_BLOCK_LOG_SIZE)), 
  block_num_(0),

  layer1_buffer_(nullptr),
  layer2_buffer_(nullptr),
  layer3_buffer_(nullptr),
  current_layer1_block_num_(0),
  current_layer2_block_num_(0),
  current_layer3_block_num_(0)
{
    if (inode_.i_block[EXT2_IND_BLOCK] != 0 || inode_.i_block[EXT2_DIND_BLOCK] != 0 || inode_.i_block[EXT2_TIND_BLOCK] != 0) {

        size_t num_pointers = ext2_reader.GetBlockSize() / sizeof(uint32_t);
        layer1_buffer_ = new uint32_t[num_pointers];
        layer2_buffer_ = new uint32_t[num_pointers];
        layer3_buffer_ = new uint32_t[num_pointers];
    }
    
    ReadBlockNum();
}

Ext2Reader::InodeDataIterator::~InodeDataIterator() {
    delete[] layer1_buffer_;
    delete[] layer2_buffer_;
    delete[] layer3_buffer_;
}

uint32_t Ext2Reader::InodeDataIterator::GetBlockNum() const {
    return block_num_;
}

void Ext2Reader::InodeDataIterator::NextBlock() {
  processed_blocks_count_++;
  if (processed_blocks_count_ < max_blocks_count_) {
      ReadBlockNum();
  } else {
      block_num_ = 0; 
  }
}

bool Ext2Reader::InodeDataIterator::IsEnd() const{
  return processed_blocks_count_ >= max_blocks_count_;
}

void Ext2Reader::InodeDataIterator::ReadBlockNum() {
    if (processed_blocks_count_ >= max_blocks_count_) {
        block_num_ = 0;
        return;
    }
    
    const size_t current_index = processed_blocks_count_;
    const uint32_t ptrs_per_block = ext2_reader_.GetPointersPerBlock();
    
    if (current_index < EXT2_NDIR_BLOCKS) {
        block_num_ = inode_.i_block[current_index];
        return;
    }

    const size_t indirect_index = current_index - EXT2_NDIR_BLOCKS;
    
    if (indirect_index < ptrs_per_block) {
        uint32_t ind_block_num = inode_.i_block[EXT2_IND_BLOCK];
        if (ind_block_num == 0) { block_num_ = 0; return; }
        
        if (ind_block_num != current_layer1_block_num_) {
            ext2_reader_.ReadRawBlock(ind_block_num, reinterpret_cast<uint8_t*>(layer1_buffer_));
            current_layer1_block_num_ = ind_block_num;
        }
        
        block_num_ = layer1_buffer_[indirect_index];
        return;
    }

    const size_t dbl_indirect_limit = static_cast<size_t>(ptrs_per_block) * ptrs_per_block;
    const size_t dbl_indirect_base = static_cast<size_t>(ptrs_per_block);
    
    if (indirect_index < dbl_indirect_base + dbl_indirect_limit) {
        
        size_t index_in_dbl = indirect_index - dbl_indirect_base;
        size_t ptr1_index = index_in_dbl / ptrs_per_block; 
        size_t ptr2_index = index_in_dbl % ptrs_per_block; 

        uint32_t dbl_indirect_block_num = inode_.i_block[EXT2_DIND_BLOCK];
        if (dbl_indirect_block_num == 0) { block_num_ = 0; return; }
        
        if (dbl_indirect_block_num != current_layer1_block_num_) {
            ext2_reader_.ReadRawBlock(dbl_indirect_block_num, reinterpret_cast<uint8_t*>(layer1_buffer_));
            current_layer1_block_num_ = dbl_indirect_block_num;
        }

        uint32_t ptr2_block_num = layer1_buffer_[ptr1_index]; 
        if (ptr2_block_num == 0) { block_num_ = 0; return; }
        
        if (ptr2_block_num != current_layer2_block_num_) {
            ext2_reader_.ReadRawBlock(ptr2_block_num, reinterpret_cast<uint8_t*>(layer2_buffer_));
            current_layer2_block_num_ = ptr2_block_num;
        }
        
        block_num_ = layer2_buffer_[ptr2_index];
        return;
    }

    const size_t trp_indirect_limit = dbl_indirect_limit * ptrs_per_block;
    const size_t trp_indirect_base = dbl_indirect_base + dbl_indirect_limit;

    if (indirect_index < trp_indirect_base + trp_indirect_limit) {
        size_t index_in_trp = indirect_index - trp_indirect_base;

        size_t ptr1_index = index_in_trp / dbl_indirect_limit;
        size_t remaining_index = index_in_trp % dbl_indirect_limit; 
        size_t ptr2_index = remaining_index / ptrs_per_block;
        size_t ptr3_index = remaining_index % ptrs_per_block;

        uint32_t trp_indirect_block_num = inode_.i_block[EXT2_TIND_BLOCK];
        if (trp_indirect_block_num == 0) { block_num_ = 0; return; }

        if (trp_indirect_block_num != current_layer1_block_num_) {
            ext2_reader_.ReadRawBlock(trp_indirect_block_num, reinterpret_cast<uint8_t*>(layer1_buffer_));
            current_layer1_block_num_ = trp_indirect_block_num;
        }
        
        uint32_t ptr2_block_num = layer1_buffer_[ptr1_index]; 
        if (ptr2_block_num == 0) { block_num_ = 0; return; }

        if (ptr2_block_num != current_layer2_block_num_) {
            ext2_reader_.ReadRawBlock(ptr2_block_num, reinterpret_cast<uint8_t*>(layer2_buffer_));
            current_layer2_block_num_ = ptr2_block_num;
        }
        
        uint32_t ptr3_block_num = layer2_buffer_[ptr2_index]; 
        if (ptr3_block_num == 0) { block_num_ = 0; return; }
        
        if (ptr3_block_num != current_layer3_block_num_) {
            ext2_reader_.ReadRawBlock(ptr3_block_num, reinterpret_cast<uint8_t*>(layer3_buffer_));
            current_layer3_block_num_ = ptr3_block_num;
        }
        
        block_num_ = layer3_buffer_[ptr3_index];
        return;
    }

    block_num_ = 0; 
}


uint32_t Ext2Reader::FindInode(const std::string& file_path) const { 
    uint32_t cur_inode = EXT2_ROOT_INO;
    return cur_inode;
}