#include <stdio.h>
#include <stdlib.h>

#include <linux/fs.h>
#include <linux/ext2_fs.h>

#include "e2fs.h"

#define	MAXBUF	100

/* Affiche le numero de bloc de l'inode n et la taille du fichier */

int main (int argc, char *argv [])
{
	if (argc != 3)
	{
		fprintf (stderr, "usage: %s fs inode\n", argv [0]) ;
		exit (1) ;
	}

	int j;
	ctxt_t c ;
	inum_t i=atoi(argv[2])-1;
	pblk_t bNum;
	buf_t b;
	struct ext2_inode *e2in;

	c = e2_ctxt_init (argv [1], MAXBUF) ;
	if (c == NULL)
	{
		perror ("e2_ctxt_init") ;
		exit (1) ;
	}

	if((bNum=e2_inode_to_pblk(c,i))<0) 
	{
		fprintf(stderr,"Numero d'erreur invalide\n");
		exit(EXIT_FAILURE);
	}

	b=e2_buffer_get(c,bNum);
	e2in=e2_inode_read(c,i,b);
	printf("%d:size\n",e2in->i_size);
	for(j=0;j<e2in->i_blocks;j++)
	{
		bNum=e2_inode_lblk_to_pblk(c,e2in,j);
		if(bNum!=0)
			printf("%d:bloc %d\n",bNum,j);
		else break;
	}
	e2_buffer_put(c,b);

	e2_ctxt_close (c) ;

	exit (0) ;
}
