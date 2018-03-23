#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"

unsigned char *disk;


int main(int argc, char **argv) {

  if(argc != 2) {
    fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
    exit(1);
  }
  int fd = open(argv[1], O_RDWR);

  disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(disk == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }

  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
  printf("Inodes: %d\n", sb->s_inodes_count);
  printf("Blocks: %d\n", sb->s_blocks_count);

  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2048);
  printf("Block group:\n");
  printf("    block bitmap: %d\n", gd->bg_block_bitmap);
  printf("    inode bitmap: %d\n", gd->bg_inode_bitmap);
  printf("    inode table: %d\n", gd->bg_inode_table);
  printf("    free blocks: %d\n", gd->bg_free_blocks_count);
  printf("    free inodes: %d\n", gd->bg_free_inodes_count);
  printf("    used_dirs: %d\n", gd->bg_used_dirs_count);

  printf("Block bitmap: ");
  unsigned char *blockmap = (unsigned char *) (disk + 1024 * gd->bg_block_bitmap);
  unsigned char *cur = blockmap;
  int byte, bit;
  for (byte = 0; byte < (sb->s_blocks_count / 8); byte++) {
    for (bit = 0; bit < 8; bit++) {
      printf("%d", (*cur >> bit) & 1);
    }
    cur++;
    printf(" ");
  }
  printf("\n");

  printf("Inode bitmap: ");
  unsigned char *imap = (unsigned char*) (disk + 1024 * gd->bg_inode_bitmap);
  cur = imap;

  for (byte = 0; byte < (sb->s_blocks_count / 32); byte++) {
    for (bit = 0; bit < 8; bit++) {
      printf("%d", (*cur >> bit) & 1);
    }
    cur++;
    printf(" ");
  }
  printf("\n");
  printf("\n");

  printf("Inodes:\n");
  int inodes;
  for (inodes = 1; inodes < sb->s_inodes_count; inodes++) {
    struct ext2_inode *cur_inode = (struct ext2_inode *) (disk + (EXT2_BLOCK_SIZE * gd->bg_inode_table) + inodes * sizeof(struct ext2_inode));
    if (cur_inode->i_size > 0 && cur_inode->i_size <= EXT2_BLOCK_SIZE) {
      char type = 'f';
      if (cur_inode->i_mode & EXT2_S_IFDIR) {
        type = 'd';
      }
      printf("[%d] type: %c size: %d links: %d blocks: %d\n", inodes + 1, type, cur_inode->i_size, cur_inode->i_links_count, cur_inode->i_blocks);
      printf("[%d] Blocks:  %d\n", inodes + 1, cur_inode->i_block[0]);
    }
  }
  return 0;
}
