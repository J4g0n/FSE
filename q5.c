#include <stdio.h>
#include <stdlib.h>

#include "e2fs.h"

#define	MAXBUF	100

/* Affiche le fichier reference par l'inode avec l'"ouverture de fichier" */

int main (int argc, char *argv [])
{
	ctxt_t c ;
	inum_t in=atoi(argv[2])-1;
	file_t f;

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
/*
	if((f=e2_file_open(c,in))==NULL)
	{
		fprintf(stderr,"impossible d'ouvrir le fichier\n");
		exit(EXIT_FAILURE);
	}

	while((car=e2_file_getc(f))!=EOF)
		printf("%c",car);

	e2_file_close(f);
*/
	if((f=e2_file_open(c,in))==NULL)
	{
		fprintf(stderr,"impossible d'ouvrir le fichier\n");
		exit(EXIT_FAILURE);
	}

	int k=1100;
	int i=2;
	void *data=(void *) calloc(k+1,sizeof(char));
	while(i>0)
	{
		e2_file_read(f,data,k);
		printf("%s",(char *) data);
		i--;
	}

	e2_file_close(f);

	e2_ctxt_close (c) ;

	exit (0) ;
}
