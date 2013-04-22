#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "e2fs.h"

#define	MAXBUF	100

/* Lit un bloc physique quelconque (sans passer par les buffers) */

int main (int argc, char *argv [])
{
	if (argc != 3)
	{
		fprintf (stderr, "usage: %s fs blkno\n", argv [0]) ;
		exit (1) ;
	}

	ctxt_t c ;
	int count;
	int i=0;
	int blno=atoi(argv[2]);
	char *data;

	c = e2_ctxt_init (argv [1], MAXBUF) ;
	if (c == NULL)
	{
		perror ("e2_ctxt_init") ;
		exit (1) ;
	}

	data=(char *) malloc(e2_ctxt_blksize(c)*sizeof(char));

	count=e2_block_fetch(c,blno,(void *) data);

	printf("%d %d\n",count,e2_ctxt_blksize(c));
	while(i<count)
	{
		printf("%c",data[i]);
		if(i%16==0) printf("\n");
		i++;
	}
	printf("\n");

	e2_ctxt_close (c) ;

	exit (0) ;
}
