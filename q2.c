#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "e2fs.h"

#define	MAXBUF	100

/* Lit un bloc physique quelconque en passant par un buffer */

int main (int argc, char *argv [])
{
	if (argc < 3)
	{
		fprintf (stderr, "usage: %s fs blkno ... blkno\n", argv [0]) ;
		exit (1) ;
	}

	ctxt_t c=NULL;
	int i,j;
	buf_t bf;
	char data[e2_ctxt_blksize(c)];

	c = e2_ctxt_init (argv [1], MAXBUF) ;
	if (c == NULL)
	{
		perror ("e2_ctxt_init") ;
		exit (1) ;
	}

	for(i=2;i<argc;i++)
	{
		printf("Contenu block: %s\n",argv[i]);
		bf=e2_buffer_get(c,atoi(argv[i]));
		memcpy(data,e2_buffer_data(bf),e2_ctxt_blksize(c));
		for(j=0;j<e2_ctxt_blksize(c);j++)
		{
			printf("%c",data[j]);
			if(j%32==0) printf("\n");
		}
		printf("\n");
		e2_buffer_put(c,bf);
	}
	e2_buffer_stats(c);

	e2_ctxt_close (c) ;

	exit (0) ;
}
