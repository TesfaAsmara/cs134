#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include "ext2_fs.h"
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdint.h>


struct ext2_super_block * sb;

struct ext2_group_desc * block_group_desc;

int block_size;

__u32 num_groups;


void read_directory_entries(int fd, struct ext2_inode * inode, __u32 parent_inode_number) {
  if (!S_ISDIR(inode -> i_mode)) return; // skip if it's not a directory

  for (int i = 0; i < EXT2_NDIR_BLOCKS; i++) { // only direct blocks
    if (inode -> i_block[i]) { // if the block has a value
      unsigned char * block = malloc(block_size);
      pread(fd, block, block_size, inode -> i_block[i] * block_size);

      __u32 offset = 0;
      while (offset < (__u32) block_size) {
        struct ext2_dir_entry * entry = (struct ext2_dir_entry * )(block + offset);
        if (entry -> inode != 0) { // if the entry is valid
          printf("DIRENT,%u,%u,%u,%u,%u,'%s'\n",
            parent_inode_number,
            offset,
            entry -> inode,
            entry -> rec_len,
            entry -> name_len,
            entry -> name);
        }
        offset += entry -> rec_len;
        if (entry -> rec_len == 0) break; // this would be an error condition
      }
      free(block);
    }
  }
}

void read_inode_table(int fd, struct ext2_group_desc * group, __u32 group_number) {
  __u32 inode_table_block = group -> bg_inode_table;
  // error handle this
  struct ext2_inode * inode_table = malloc(sb -> s_inodes_per_group * sizeof(struct ext2_inode));
  // error handle this
  pread(fd, inode_table, sb -> s_inodes_per_group * sizeof(struct ext2_inode), inode_table_block * block_size);

  for (__u32 i = 0; i < sb -> s_inodes_per_group; ++i) {
    struct ext2_inode * inode = & inode_table[i];
    if (inode -> i_mode != 0 && inode -> i_links_count != 0) { // inode is allocated
      char file_type = '?';
      if (S_ISREG(inode -> i_mode)) file_type = 'f'; // regular file
      else if (S_ISDIR(inode -> i_mode)) file_type = 'd'; // directory
      else if (S_ISLNK(inode -> i_mode)) file_type = 's'; // symbolic link

      __u32 inode_num = i + 1 + (group_number * sb -> s_inodes_per_group);

      // gmtime function to convert these timestamps into the tm structure, 
      // which can then be formatted into a string with strftime.

      // for converting times
      struct tm tm_atime, tm_ctime, tm_mtime;
      char atime_buff[20], ctime_buff[20], mtime_buff[20];

      // convert __u32 values to time_t
      time_t rawtime_atime = inode -> i_atime;
      time_t rawtime_ctime = inode -> i_ctime;
      time_t rawtime_mtime = inode -> i_mtime;

      // access time
      gmtime_r( & rawtime_atime, & tm_atime);
      strftime(atime_buff, sizeof(atime_buff), "%m/%d/%y %H:%M:%S", & tm_atime);

      // change time
      gmtime_r( & rawtime_ctime, & tm_ctime);
      strftime(ctime_buff, sizeof(ctime_buff), "%m/%d/%y %H:%M:%S", & tm_ctime);

      // modification time
      gmtime_r( & rawtime_mtime, & tm_mtime);
      strftime(mtime_buff, sizeof(mtime_buff), "%m/%d/%y %H:%M:%S", & tm_mtime);

      uint64_t file_size = inode -> i_size;
      uint64_t num_blocks = inode -> i_blocks;

      printf("INODE,%u,%c,%o,%d,%d,%d,%s,%s,%s,%lu,%lu\n",
        inode_num, file_type, inode -> i_mode & 0xFFF, inode -> i_uid, inode -> i_gid,
        inode -> i_links_count, ctime_buff, mtime_buff, atime_buff,
        file_size, num_blocks);

      if (file_type == 'd') { // if the inode is a directory
        read_directory_entries(fd, inode, inode_num);
      }

    }
  }

  free(inode_table);
}

int main(int argc, char * argv[]) {

  if (argc != 2) {

    fprintf(stderr, "Please specify one argument, the file system image\n");

    exit(1);

  }

  int imgfd = open(argv[1], O_RDONLY);

  if (imgfd == -1) {

    fprintf(stderr, "Invalid file name\n");

    exit(2);

  }

  void * superblock_buffer = (void * ) malloc(1025);

  int a = pread(imgfd, superblock_buffer, 1024, 1024);

  if (a == -1) {

    fprintf(stderr, "Error reading superblock\n");

    exit(2);

  }

  sb = (struct ext2_super_block * ) superblock_buffer;

  block_size = 1024 << sb -> s_log_block_size;

  printf("SUPERBLOCK,%u,%u,%d,%u,%u,%u,%u\n", sb -> s_blocks_count, sb -> s_inodes_count, block_size, sb -> s_inode_size, sb -> s_blocks_per_group, sb -> s_inodes_per_group, sb -> s_first_ino);

  num_groups = (__u32) ceil((double) sb -> s_blocks_count / (double) sb -> s_blocks_per_group);

  for (__u32 i = 0; i < num_groups; i++) {

    __u32 block_count;

    __u32 inode_count;

    if (i == num_groups - 1) {

      block_count = sb -> s_blocks_count - ((num_groups - 1) * sb -> s_blocks_per_group);

      inode_count = sb -> s_inodes_count - ((num_groups - 1) * sb -> s_inodes_per_group);

    } else {

      block_count = sb -> s_blocks_per_group;

      inode_count = sb -> s_inodes_per_group;

    }

    void * block_group_desc_buf = (void * ) malloc(33);

    a = pread(imgfd, block_group_desc_buf, 32, 2048);

    if (a == -1) {

      fprintf(stderr, "Error reading block group descriptor entry\n");

      exit(2);

    }

    block_group_desc = (struct ext2_group_desc * ) block_group_desc_buf;

    printf("GROUP,%u,%u,%u,%u,%u,%u,%u,%u\n", i, block_count, inode_count, block_group_desc -> bg_free_blocks_count, block_group_desc -> bg_free_inodes_count, block_group_desc -> bg_block_bitmap, block_group_desc -> bg_inode_bitmap, block_group_desc -> bg_inode_table);

    // Read block bitmap
    unsigned char * block_bitmap = (unsigned char * ) malloc(block_size);
    pread(imgfd, block_bitmap, block_size, block_group_desc -> bg_block_bitmap * block_size);

    // Process block bitmap
    for (__u32 j = 0; j < block_count; j++) {
      __u32 byte_index = j / 8;
      __u32 bit_index = j % 8;
      __u32 bit = block_bitmap[byte_index] & (1 << bit_index);
      if (bit == 0) { // if 0, the block is free
        printf("BFREE,%u\n", i * sb -> s_blocks_per_group + j + 1); // +1 as block numbers start at 1
      }
    }

    free(block_bitmap);

    // Read inode bitmap
    unsigned char * inode_bitmap = (unsigned char * ) malloc(block_size);
    pread(imgfd, inode_bitmap, block_size, block_group_desc -> bg_inode_bitmap * block_size);

    // Process inode bitmap
    for (__u32 j = 0; j < inode_count; j++) {
      __u32 byte_index = j / 8;
      __u32 bit_index = j % 8;
      __u32 bit = inode_bitmap[byte_index] & (1 << bit_index);
      if (bit == 0) { // if 0, the inode is free
        printf("IFREE,%u\n", i * sb -> s_inodes_per_group + j + 1); // +1 as inode numbers start at 1
      }
    }

    free(inode_bitmap);

    read_inode_table(imgfd, block_group_desc, i);
  }

  free(sb);

  free(block_group_desc);

}
