#include <stdio.h>
#include <stdlib.h>

#include <linux/fs.h>
#include <linux/ext2_fs.h>

#include "e2fs.h"

#define	MAXBUF	100

/* Affiche le numero de bloc de l'inode n et la taille du fichier */

int main (int argc, char *argv [])
{
	ctxt_t c ;
	inum_t i=atoi(argv[2]);
	pblk_t bNum;
	buf_t b;
	struct ext2_inode *e2in;
	int j;

	if (argc != 3)
	{
		fprintf (stderr, "usage: %s fs inode\n", argv [0]) ;
		exit (1) ;
	}

	c = e2_ctxt_init (argv [1], MAXBUF) ;
	if (c == NULL)
	{
		perror ("e2_ctxt_init") ;
		exit (1) ;
	}

	bNum=e2_inode_to_pblk(c,i);
	printf("%d,%d\n",i,bNum);
	b=e2_buffer_get(c,bNum);
/*	for(j=0;j<1024;j++) 
	{ 
		printf("%3x ",(unsigned char *) b->data[j]); 
		if((j!=0)&&(j%16==0))	printf("\n");
	}*/
	e2in=e2_inode_read(c,i,b);
	printf("%s\n",(char *) e2in);
	printf("%d:size\n",e2in->i_size);
	printf("%d:nbBlocks\n",e2in->i_blocks);
	printf("%d:idBlock1\n",e2in->i_block[0]);
/*	q=(unsigned char *) e2in;
	for(j=0;j<256;j++) 
	{ 
		printf("%3x ",q[j]); 
		if((j%16==0)&&(j!=0))	printf("\n");
	}*/

	e2_ctxt_close (c) ;

	exit (0) ;
}
