#include <stdio.h>
#include <stdlib.h>

#include "e2fs.h"

#define	MAXBUF	100

/* Affiche le repertoire reference par l'inode */

int main (int argc, char *argv [])
{
	ctxt_t c ;
	inum_t in=atoi(argv[2])-1;

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

	e2_ls(c,in);

	e2_ctxt_close (c) ;

	exit (0) ;
}
