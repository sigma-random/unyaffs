/*
 * unyaffs: extract files from yaffs2 file system image to current directory
 *
 * Created by Kai Wei <kai.wei.cn@gmail.com>
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "unyaffs.h"

#define CHUNK_SIZE 2048
#define SPARE_SIZE 64
#define MAX_OBJECTS 10000
#define MAX_WARN    20
#define YAFFS_OBJECTID_ROOT     1


unsigned char data[CHUNK_SIZE + SPARE_SIZE];
unsigned char *chunk_data = data;
unsigned char *spare_data = data + CHUNK_SIZE;
int chunk_no   = 0;
int warn_count = 0;
int img_file;

char *obj_list[MAX_OBJECTS];

int read_chunk(void);

int process_chunk(void)
{
	int out_file, remain, s;
	char *full_path_name;

	yaffs_PackedTags2 *pt = (yaffs_PackedTags2 *)spare_data;
	if (pt->t.byteCount == 0xffff)  {	//a new object 

		yaffs_ObjectHeader oh = *(yaffs_ObjectHeader *)chunk_data;
		if (pt->t.objectId >= MAX_OBJECTS) {
			fprintf(stderr, "ObjectId %u (%s) out of range.\n",
			        pt->t.objectId, oh.name);
			exit(1);
		}
		if (oh.parentObjectId >= MAX_OBJECTS || obj_list[oh.parentObjectId] == NULL) {
			fprintf(stderr, "Invalid parentObjectId %u in object %u (%s)\n",
			        oh.parentObjectId, pt->t.objectId, oh.name);
			exit(1);
		}

		full_path_name = (char *)malloc(strlen(oh.name) + strlen(obj_list[oh.parentObjectId]) + 2);
		if (full_path_name == NULL) {
			fprintf(stderr, "malloc full path name.\n");
			exit(1);
		}
		strcpy(full_path_name, obj_list[oh.parentObjectId]);
		strcat(full_path_name, "/");
		strcat(full_path_name, oh.name);
		obj_list[pt->t.objectId] = full_path_name;

		switch(oh.type) {
			case YAFFS_OBJECT_TYPE_FILE:
				remain = oh.fileSize;
				out_file = creat(full_path_name, oh.yst_mode);
				while(remain > 0) {
					if (read_chunk())
						return -1;
					s = (remain < pt->t.byteCount) ? remain : pt->t.byteCount;	
					if (write(out_file, chunk_data, s) == -1)
						return -1;
					remain -= s;
				}
				close(out_file);
				break;
			case YAFFS_OBJECT_TYPE_SYMLINK:
				symlink(oh.alias, full_path_name);
				break;
			case YAFFS_OBJECT_TYPE_DIRECTORY:
				mkdir(full_path_name, 0777);
				break;
			case YAFFS_OBJECT_TYPE_HARDLINK:
				if (oh.equivalentObjectId >= MAX_OBJECTS || obj_list[oh.equivalentObjectId] == NULL) {
					fprintf(stderr, "Invalid equivalentObjectId %u in object %u (%s)\n",
			        		oh.equivalentObjectId, pt->t.objectId, oh.name);
					exit(1);
				}
				link(obj_list[oh.equivalentObjectId], full_path_name);
				break;
			case YAFFS_OBJECT_TYPE_SPECIAL:
				break;
			case YAFFS_OBJECT_TYPE_UNKNOWN:
				break;
		}
	} else {
		fprintf(stderr, "Warning: Invalid header at chunk #%d, skipping...\n",
		        chunk_no);
		if (++warn_count >= MAX_WARN) {
			fprintf(stderr, "Giving up\n");
			exit(1);
		}
	}
	return 0;
}


int read_chunk(void)
{
	ssize_t s;
	int ret = -1;

	chunk_no++;
	memset(chunk_data, 0xff, sizeof(chunk_data));
	s = read(img_file, data, CHUNK_SIZE + SPARE_SIZE);
	if (s == -1) {
		perror("read image file\n");
	} else if (s == 0) {
		printf("end of image\n");
	} else if ((s == (CHUNK_SIZE + SPARE_SIZE))) {
		ret = 0;
	} else {
		fprintf(stderr, "broken image file\n");
	}
	return ret;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		printf("Usage: unyaffs image_file_name\n");
		exit(1);
	}
	img_file = open(argv[1], O_RDONLY);
	if (img_file == -1) {
		printf("open image file failed\n");
		exit(1);
	}

	obj_list[YAFFS_OBJECTID_ROOT] = ".";
	while(1) {
		if (read_chunk() == -1)
			break;
		process_chunk();
	}
	close(img_file);
	return 0;
}
