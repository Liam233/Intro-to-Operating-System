#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include <assert.h>
#include <time.h>

#ifndef ZUOYU_EXT2_FUNCTIONS_H
#define ZUOYU_EXT2_FUNCTIONS_H

int get_file_inode(unsigned char *disk, char *src_path, char *fname, void *itable);

int get_dir_inode(unsigned char *disk, char *name, void *offset);

int attach_child_to_parent(unsigned char *disk, struct ext2_inode *inode, int child_inode, unsigned char type,
                           char *child_name, unsigned char *bmap);

int get_free_inode(unsigned char *imap);

int get_free_block(unsigned char *bmap);

void flip_map(unsigned char *map, int index);

int get_map(unsigned char *map, int index);

int get_slash_index(char *name);

char *get_parent_name(char *name);

char *get_child_name(char *name);

int is_root(char *name);

int check_file_path(unsigned char *disk, char *av, void *itable);

char *format_name(char *argvtwo);

#endif //ZUOYU_EXT2_FUNCTIONS_H