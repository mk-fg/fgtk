/**************************************************************************
 * f2fs-label - get and set the label of a mounted f2fs filesystem        *
 * Copyright (C) 2019 Thomas Rohloff <v10lator@myway.de>                  *
 *                                                                        *
 * This program is free software: you can redistribute it and/or modify   *
 * it under the terms of the GNU General Public License as published by   *
 * the Free Software Foundation, either version 3 of the License, or      *
 * (at your option) any later version.                                    *
 *                                                                        *
 * This program is distributed in the hope that it will be useful,        *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 * GNU General Public License for more details.                           *
 *                                                                        *
 * You should have received a copy of the GNU General Public License      *
 * along with this program.  If not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <fcntl.h>

#define F2FS_LABEL_VERSION "0.1"

// This prints the tools usage
void print_help(char* toolname)
{
	printf("To get the current label use:\n");
	printf("\t%s [mountpoint]\n\n", toolname);
	printf("To set a new label use:\n");
	printf("\t%s [mountpoint] [new label]\n", toolname);
}

// Use this to print out the current label. Mountpoint needs to be a valid file descriptor.
int print_label(int mountpoint)
{
	// Give us some memory
	char* label = malloc(sizeof(char) * FSLABEL_MAX);
	if(label == NULL)
	{
		printf("Out of memory!\n");
		return 4;
	}
	// Get the current label via ioctl. Print any error.
	if(ioctl(mountpoint, FS_IOC_GETFSLABEL, label) < 0)
	{
		perror("ioctl error");
		free(label);
		return errno;
	}

	// If no error print out the label
	printf("%s\n", label);
	free(label);
	return 0;
}

// Use this to set a new label.
// Mountpoint needs to be a valid file descriptor.
// argv should be an array of words forming the new label
// while argc should be the length of the argv array.
int set_label(int mountpoint, int argc, char* argv[])
{
	// Give us some memory.
	char* label = malloc(sizeof(char) * FSLABEL_MAX);
	if(label == NULL)
	{
		printf("Out of memory!\n");
		return 4;
	}

	label[0] = '\0'; // Set an empty string so strcat will work
	int length = 0; // It's actually 1 but oh well...
	for(int i = 0; i < argc; i++) // Loop through all words forming the label
	{
		if(length > 0) // Not the first word: We need a space before the word
		{
			if(length + 2 > FSLABEL_MAX) // Error out in case the label gets too long
			{
				printf("Label too long (max %i characters)\n", FSLABEL_MAX);
				free(label);
				return 3;
			}
			// Finally set a space and termiate the string (so strcat will work)
			label[length++] = ' ';
			label[length] = '\0';
		}

		// Now add the current word to the label
		length += strlen(argv[i]);
		if(length >= FSLABEL_MAX) // Error out in case the label gets too long
		{
			printf("Label too long (max %i characters)\n", FSLABEL_MAX);
			free(label);
			return 3;
		}
		strcat(label, argv[i]); // If all goes well use strcat to copy the new word at the end of the label
	}

	// Set the new label with the ioctl FS_IOC_SETFSLABEL - Print any errors
	if(ioctl(mountpoint, FS_IOC_SETFSLABEL, label) < 0)
	{
		perror("ioctl error");
		free(label);
		return errno;
	}
	printf("New label: %s\n", label); // If no errors print out the new label
	free(label);
	return 0;
}

// This is the programs entry point
int main(int argc, char* argv[])
{
	char *toolname = argv[0]; // The first argument is the tools name itself..-
	printf("%s v%s\n\n", toolname, F2FS_LABEL_VERSION); //print it on screen

	// Check if we are root - else we can't do ioctls, right?
	//TODO: Somehow it works even if we're not root... Why?!?
/*	if(geteuid() != 0)
	{
		printf("This needs to be executed as root!\n");
		return 5;
	}
*/
	if(argc < 2) // If there isn't at least a mount point given error out
	{
		print_help(toolname);
		return 1;
	}

	// Open the mount point and get a file descritor - TODO: Check if mount point is valid
	int mountpoint = open(argv[1], O_RDONLY);
	if(mountpoint < 0) // Error out in case we couldn't open it for whatever reason
	{
		printf("Invalid mount point: %s\n\n", argv[1]);
		print_help(toolname);
		return 1;
	}

	// Cut off the first two arguments (tool name and mount point) so the rest should be the new label (if any)
	argc -= 2;
	argv = &argv[2];

	// If we have a new label call set_label, else call print_label
	int ret = argc == 0 ? print_label(mountpoint) : set_label(mountpoint, argc, argv);
	close(mountpoint); // Close the file descriptor
	return ret; // Return what set_label/get_label returned
}
