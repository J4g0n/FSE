#include <stdio.h>
#include <stdlib.h>

#include "e2fs.h"

#define	MAXBUF	100

/* Affiche le fichier reference par l'inode avec e2_cat */

int main (int argc, char *argv [])
{
	if (argc != 3 && argc != 4)
	{
		fprintf (stderr, "usage: %s fs inode [dispblkno]\n", argv [0]) ;
		exit (1) ;
	}

	ctxt_t c ;
	int inNum=atoi(argv[2])-1;
	int dispblkno;
	if(argc==4) dispblkno=atoi(argv[3]);
	else dispblkno=0;

	c = e2_ctxt_init (argv [1], MAXBUF) ;
	if (c == NULL)
	{
		perror ("e2_ctxt_init") ;
		exit (1) ;
	}

	e2_cat(c,inNum,dispblkno);

	e2_ctxt_close (c) ;

	exit (0) ;
}
