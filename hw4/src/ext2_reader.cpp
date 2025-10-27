#include "ext2_reader.h"
#include <cassert> 
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <string.h>
#include <vector>
#include <cstring>
#include <iostream>

std::vector<std::string> SplitPath(const std::string& path) {
  std::vector<std::string> components;
  std::string current_path = path;
  
  const std::string kDelimiter = "/";
  
  size_t pos = 0;
  std::string token;

  if (current_path.length() > 0 && current_path[0] == '/') {
    current_path.erase(0, 1);
  }

  while ((pos = current_path.find(kDelimiter)) != std::string::npos) {
    token = current_path.substr(0, pos);
    
    if (!token.empty()) {
      components.push_back(token);
    }
    
    current_path.erase(0, pos + kDelimiter.length());
  }

  if (!current_path.empty()) {
    components.push_back(current_path);
  }

  return components;
}

std::string FileTypeToString(uint8_t file_type) {
  switch (file_type) {
    case EXT2_FT_REG_FILE:
      return "REG"; 
    case EXT2_FT_DIR:
      return "DIR";
    case EXT2_FT_SYMLINK:
      return "SYM";
    case EXT2_FT_FIFO:
      return "FIFO"; 
    case EXT2_FT_SOCK:
      return "SOCK"; 
    case EXT2_FT_CHRDEV:
      return "CHRDEV"; 
    case EXT2_FT_BLKDEV:
      return "BLKDev"; 
    case EXT2_FT_UNKNOWN:
      return "UNKNOWN";
    default:
      return "Other (" + std::to_string(file_type) + ")";
  }
}

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

  uint64_t gdt_start_block = static_cast<uint64_t>(super_block_.s_first_data_block) + 1;
  uint64_t gdt_start_offset = gdt_start_block * GetBlockSize();
  
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

ext2_inode Ext2Reader::ReadInode(uint32_t inode_id) const {
  if (inode_id == 0 || inode_id > super_block_.s_inodes_count) {
      throw std::out_of_range("invalid inode number: " + std::to_string(inode_id));
  }

  uint32_t group_num = (inode_id - 1) / super_block_.s_inodes_per_group;
  uint32_t index_in_table = (inode_id - 1) % super_block_.s_inodes_per_group;

  uint64_t inode_table_offset = GetInodeTableOffset(group_num); 

  uint64_t offset_in_table = static_cast<uint64_t>(index_in_table) * super_block_.s_inode_size;
  uint64_t physical_offset = inode_table_offset + offset_in_table;

  ext2_inode inode;
  size_t res = pread(ing_fd_, &inode, sizeof(ext2_inode), physical_offset);
  
  if (res != sizeof(ext2_inode)) {
    throw std::runtime_error("can't read inode " + std::to_string(inode_id) + " at offset " + std::to_string(physical_offset));
  }

  // std::cout << inode_id << " " << inode.i_uid << " " << group_num << " " << physical_offset << std::endl;

  return inode;
}

bool Ext2Reader::IsInodeValid(uint32_t inode_id) const {
  if (inode_id == 0 || inode_id > super_block_.s_inodes_count) {
      return false;
  }

  uint32_t bitmap_index = inode_id - 1;
  
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
  return processed_blocks_count_ >= max_blocks_count_ || block_num_ == 0;
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
    if (ind_block_num == 0) { 
      block_num_ = 0; return; 
    }
    
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
    if (dbl_indirect_block_num == 0) { 
      block_num_ = 0; 
      return; 
    }
    
    if (dbl_indirect_block_num != current_layer1_block_num_) {
      ext2_reader_.ReadRawBlock(dbl_indirect_block_num, reinterpret_cast<uint8_t*>(layer1_buffer_));
      current_layer1_block_num_ = dbl_indirect_block_num;
    }

    uint32_t ptr2_block_num = layer1_buffer_[ptr1_index]; 
    if (ptr2_block_num == 0) { 
      block_num_ = 0; 
      return; 
    }
    
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
    if (trp_indirect_block_num == 0) { 
      block_num_ = 0; 
      return; 
    }

    if (trp_indirect_block_num != current_layer1_block_num_) {
      ext2_reader_.ReadRawBlock(trp_indirect_block_num, reinterpret_cast<uint8_t*>(layer1_buffer_));
      current_layer1_block_num_ = trp_indirect_block_num;
    }
    
    uint32_t ptr2_block_num = layer1_buffer_[ptr1_index]; 
    if (ptr2_block_num == 0) { 
      block_num_ = 0; 
      return; 
    }

    if (ptr2_block_num != current_layer2_block_num_) {
      ext2_reader_.ReadRawBlock(ptr2_block_num, reinterpret_cast<uint8_t*>(layer2_buffer_));
      current_layer2_block_num_ = ptr2_block_num;
    }
    
    uint32_t ptr3_block_num = layer2_buffer_[ptr2_index]; 
    if (ptr3_block_num == 0) { 
      block_num_ = 0; 
      return; 
    }
    
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
    
  std::vector<std::string> components = SplitPath(file_path);

  const uint64_t block_size = GetBlockSize();

  uint32_t cur_inode_id = EXT2_ROOT_INO;

  uint8_t* buffer = new uint8_t[block_size]; 

  for (size_t i = 0; i < components.size(); i++) {
    const std::string& name = components[i];

    ext2_inode inode = ReadInode(cur_inode_id);
    InodeDataIterator blocks_iter(inode, *this);

    bool find_name = false;
    
    while(!blocks_iter.IsEnd()) {
      uint32_t block_num = blocks_iter.GetBlockNum();
      
      ReadRawBlock(block_num, buffer);
      uint64_t offset = 0;

      while(offset < block_size) {
        ext2_dir_entry_2 dir_entry;
        
        if (offset + sizeof(ext2_dir_entry_2) > block_size) 
          break;
        
        std::memcpy(&dir_entry, buffer + offset, sizeof(dir_entry));
        
        if (dir_entry.rec_len == 0 || dir_entry.inode == 0) 
          break;

        if (std::strncmp(name.c_str(), dir_entry.name, dir_entry.name_len) == 0) {
          find_name = true;
          cur_inode_id = dir_entry.inode;
          break;
        }

        offset += dir_entry.rec_len;
      }

      if (find_name) {
        break;
      }

      blocks_iter.NextBlock();
    }

    if (!find_name) {
      cur_inode_id = 0;
      break;
    }

  }

  delete[] buffer;

  return cur_inode_id;
}

void Ext2Reader::ShowFileInfo(const std::string& file_path) const {
  uint32_t inode_id = FindInode(file_path);
  if (inode_id == 0) {
    std::cout << "File " << file_path << " does not exist!" << std::endl;
    return;
  }

  ShowFileInfo(inode_id );
}

void Ext2Reader::ShowFileInfo(uint32_t inode_id) const {
  if (!IsInodeValid(inode_id)) {
    std::cout << "Inode " << inode_id << " does not exist!" << std::endl;
    return;
  }

  ext2_inode inode = ReadInode(inode_id);
  uint16_t file_type = inode.i_mode & 0xf000;

  if (file_type == EXT2_S_IFDIR) {
    DirContent(inode);
  } else if (file_type == EXT2_S_IFREG) {
    FileContent(inode);
  }
}

void Ext2Reader::FileContent(const ext2_inode& inode) const {
  const uint64_t block_size = GetBlockSize();

  uint8_t* buffer = new uint8_t[block_size]; 

  InodeDataIterator blocks_iter(inode, *this);

  while(!blocks_iter.IsEnd()) {
    uint32_t block_num = blocks_iter.GetBlockNum();

    ReadRawBlock(block_num, buffer);
    std::printf("%s", buffer); 

    blocks_iter.NextBlock();
  }

  delete[] buffer; 
}

void Ext2Reader::DirContent(const ext2_inode& inode) const {
  const uint64_t block_size = GetBlockSize();

  uint8_t* buffer = new uint8_t[block_size];

  InodeDataIterator blocks_iter(inode, *this);

  while(!blocks_iter.IsEnd()) {
    uint32_t block_num = blocks_iter.GetBlockNum();

    ReadRawBlock(block_num, buffer);
    uint64_t offset = 0;

    while(offset < block_size) {
      ext2_dir_entry_2 dir_entry;
      
      if (offset + sizeof(ext2_dir_entry_2) > block_size) 
        break;
      
      std::memcpy(&dir_entry, buffer + offset, sizeof(dir_entry));
     
      if (dir_entry.rec_len == 0 || dir_entry.inode == 0) 
        break;

      
      std::string type_name = FileTypeToString(dir_entry.file_type);
      std::printf("%.*s %s\n", dir_entry.name_len, dir_entry.name, type_name.c_str());

      offset += dir_entry.rec_len;
    }

    blocks_iter.NextBlock();
  }
  
  delete[] buffer;
}

void Ext2Reader::ShowFileBlocks(const std::string& file_path) const {
  uint32_t inode_id = FindInode(file_path);
  if (!IsInodeValid(inode_id)) {
    std::cout << "Inode " << inode_id << " does not exist!" << std::endl;
    return;
  }

  ext2_inode inode = ReadInode(inode_id);

  const uint64_t block_size = GetBlockSize();

  uint8_t* buffer = new uint8_t[block_size];

  InodeDataIterator blocks_iter(inode, *this);

  while(!blocks_iter.IsEnd()) {
    uint32_t block_num = blocks_iter.GetBlockNum();
    
    std::cout << "Block: " << block_num << std::endl;

    blocks_iter.NextBlock();
  }
  
  delete[] buffer;
}