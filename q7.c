#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include "e2fs.h"

#define	MAXBUF	100

/* Affiche le fichier ou le repertoire reference par l'inode */

int main (int argc, char *argv [])
{
	if (argc != 3)
	{
		fprintf (stderr, "usage: %s fs chemin\n", argv [0]) ;
		exit (1) ;
	}

	ctxt_t c ;
	char *path=argv[2];

	c = e2_ctxt_init (argv [1], MAXBUF) ;
	if (c == NULL)
	{
		perror ("e2_ctxt_init") ;
		exit (1) ;
	}

	e2_namei(c,path);

	e2_ctxt_close (c) ;

	exit (0) ;
}
