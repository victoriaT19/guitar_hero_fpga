#include <stdio.h>	/* printf */
#include <stdlib.h>	/* malloc, atoi, rand... */
#include <string.h>	/* memcpy, strlen... */
#include <stdint.h>	/* uints types */
#include <sys/types.h>	/* size_t ,ssize_t, off_t... */
#include <unistd.h>	/* close() read() write() */
#include <fcntl.h>	/* open() */
#include <sys/ioctl.h>	/* ioctl() */
#include <errno.h>	/* error codes */

// ioctl commands defined for the pci driver header
#include "ioctl_cmds.h"

int main(int argc, char** argv)
{
	printf("hello world\n");

	int  fd, retval;
	fd = open("/dev/mydev", O_RDWR);

	unsigned int data = 0x0;
	unsigned int data1 = 0;

	ioctl(fd, WR_L_DISPLAY);
	retval = write(fd, &data, sizeof(data));
	printf("escreve %d bytes", retval);

	ioctl(fd, RD_SWITCHES);
	retval = read(fd,&data1, 1);
	printf("li %d bytes", retval);
	printf("new data1:%d\n",data1);


	close(fd);
	return 0;
}
