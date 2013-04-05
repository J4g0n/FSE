#include <stdio.h>
#include <stdlib.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "e2fs.h"

struct buffer
{
    void *data ;			/* les donnees du bloc */
    pblk_t blkno ;			/* numero de bloc physique */
    int valid ;				/* donnees valides */
    struct buffer *next ;
} ;

struct context
{
    int fd ;
    struct ext2_super_block sb ;
    int ngroups ;			/* nombre de groupes dans gd [] */
    struct ext2_group_desc *gd ;	/* c'est un tableau */
    /* ce qui suit concerne les lectures bufferisees */
    struct buffer *last ;		/* pointe sur dernier buffer */
    int bufstat_read ;			/* nombre de demandes de lecture */
    int bufstat_cached ;		/* nombre de lectures en cache */
} ;

struct ofile
{
    struct context *ctxt ;		/* eviter param a chaque e2_file_xxx */
    struct buffer *buffer ;		/* buffer contenant l'inode */
    struct ext2_inode *inode ;		/* l'inode proprement dit */
    lblk_t curblk ;			/* position en bloc */
    char *data ;			/* donnees */
    int len ;				/* longueur utile dans les donnees */
    int pos ;				/* position dans les donnees */
} ;

/******************************************************************************
 * Initialisation du contexte
 */

ctxt_t e2_ctxt_init (char *file, int maxbuf)
{
	int fd;
	int i=0;
	char buff[sizeof(struct ext2_group_desc)];
	struct context *ctxt=(struct context *) calloc(1,sizeof(struct context));
	buf_t bfCur;

	if((fd=open(file,O_RDONLY))==-1)
	{
		perror("open");
		exit(1);
	}

	ctxt->fd=fd;
	lseek(ctxt->fd,0x400,SEEK_SET);
	if(read(fd,&(ctxt->sb),sizeof(struct ext2_super_block))==-1)
	{
		perror("read");
		exit(1);
	}

	ctxt->ngroups=1+(ctxt->sb.s_blocks_count-ctxt->sb.s_first_data_block)/(ctxt->sb.s_blocks_per_group);
	ctxt->gd=(struct ext2_group_desc *)	calloc(ctxt->ngroups,sizeof(struct ext2_group_desc));
	while(i<ctxt->ngroups)
	{
		if(read(fd,buff,sizeof(struct ext2_group_desc))==-1)
		{
			perror("read");
			exit(1);
		}
		memcpy((void *)&ctxt->gd[i],(void *)buff,sizeof(struct ext2_group_desc));
		i++;
	}

	if(ctxt->sb.s_magic!=61267) 
	{
		fprintf(stderr,"Erreur la signature du super bloc ne correspond	pas\n");
		exit(1);
	}

	i=0;
	ctxt->bufstat_read=0;
	ctxt->bufstat_cached=0;
	while(i<maxbuf)
	{
		if(i==0)
		{
			ctxt->last=(buf_t) malloc(sizeof(struct buffer));
			ctxt->last->data=(void *) malloc(e2_ctxt_blksize(ctxt));
			ctxt->last->valid=0;
			ctxt->last->next=ctxt->last;
		}
		else
		{
			bfCur=(buf_t) malloc(sizeof(struct buffer));;
			bfCur->data=(void *) malloc(e2_ctxt_blksize(ctxt));
			bfCur->valid=0;
			bfCur->next=ctxt->last->next;
			ctxt->last->next=bfCur;
		}
		i++;
	}

	return ctxt;
}

void e2_ctxt_close (ctxt_t c)
{
	buf_t bfCur,bfSent;
	if(c!=NULL)
	{
		close(c->fd);
		if(c->gd!=NULL) free(c->gd);
		if((bfSent=c->last)!=NULL)
		{
			while((bfCur=bfSent->next)!=NULL)
			{
				if(bfSent->data!=NULL) free(bfSent->data);
				free(bfSent);
				bfSent=bfCur;			
			}
		}
		free(c);
	}
}

int e2_ctxt_blksize (ctxt_t c)
{
	return 1024;
}

/******************************************************************************
 * Fonctions de lecture non bufferisee d'un bloc
 */

int e2_block_fetch (ctxt_t c, pblk_t blkno, void *data)
{
	int blckSize=e2_ctxt_blksize(c);
	int count;
	lseek(c->fd,blkno*blckSize,SEEK_SET);

	if((count=read(c->fd,data,blckSize))==-1)
	{
		perror("read");
		exit(1);
	}

	return count;
}

/******************************************************************************
 * Gestion du buffer et lecture bufferisee
 */

/* recupere un buffer pour le bloc, le retire de la liste des buffers
 * et lit le contenu si necessaire
 */

buf_t e2_buffer_get (ctxt_t c, pblk_t blkno)
{
	buf_t bf=NULL;
	buf_t bfSent;
	if(c!=NULL)
	{
		c->bufstat_read++;
		bf=c->last;
		do
		{
			bfSent=bf;
			bf=bf->next;
		}
		while(bf!=c->last&&bf->blkno!=blkno) ;
		if(bf->blkno!=blkno)
		{
			bf=(buf_t) malloc(sizeof(struct buffer));
			bf->data=(void *) malloc(e2_ctxt_blksize(c));
			bf->valid=1;
			e2_block_fetch(c,blkno,bf->data);
		}
		else
		{
			bfSent->next=bf->next;			
			c->bufstat_cached++;
		}
	}
	return bf;
}
        
/* replace le buffer en premier dans la liste */
void e2_buffer_put (ctxt_t c, buf_t b)
{
	if(c!=NULL&&b!=NULL)
	{
		buf_t bf=c->last;
		b->next=bf->next;
		bf->next=b;
		c->last=b;
	}
}
        
/* recupere les donnees du buffer */
void *e2_buffer_data (buf_t b)
{
	return b->data;
}

/* affiche les statistiques */
void e2_buffer_stats (ctxt_t c)
{
	printf("Nombre de lecture: %d\nNombre de lecture en cache: %d\n",c->bufstat_read,c->bufstat_cached);
}

/******************************************************************************
 * Fonction de lecture d'un bloc dans un inode
 */

/* recupere le buffer contenant l'inode */
pblk_t e2_inode_to_pblk (ctxt_t c, inum_t i)
{
	int ipg=c->sb.s_inodes_per_group;
	pblk_t bn=-1;
	int inodeSize=sizeof(struct ext2_inode);
//	pblk_t bmInode=c->gd[i/ipg].bg_inode_bitmap;
	if(i<ipg*c->ngroups)
	{
		pblk_t inodeBlock=c->gd[i/ipg].bg_inode_table;
		bn=inodeBlock+(i*inodeSize)/e2_ctxt_blksize(c);
	}
	return bn;
}

/* extrait l'inode du buffer */
struct ext2_inode *e2_inode_read (ctxt_t c, inum_t i, buf_t b)
{
	i%=c->sb.s_inodes_per_group;
	struct ext2_inode *e2in=(struct ext2_inode *) malloc(sizeof(struct ext2_inode));
	int inodeSize=sizeof(struct ext2_inode);
	int offset=i*inodeSize;
	void *p=b+offset;	
	memcpy(p,(void *) e2in, inodeSize);
	return e2in;
}

/* numero de bloc physique correspondant au bloc logique blkno de l'inode in */
pblk_t e2_inode_lblk_to_pblk (ctxt_t c, struct ext2_inode *in, lblk_t blkno)
{
	int sizeAddrBlock=sizeof(pblk_t);
	int sizeBlock=e2_ctxt_blksize(c);
	int nbAddrPerBlock=sizeBlock/sizeAddrBlock;
	int k,q,r;
	int i=12;
	pblk_t *p;
	buf_t b;
	if(blkno<=i)
		return in->i_block[blkno];
	else if (blkno<(i+=nbAddrPerBlock))
	{
		//chercher l'adresse dans le bloc de premiere indirection
		b=e2_buffer_get(c,in->i_block[13]);
		p=(pblk_t *) b;
		return p[blkno-12];
	}
	else if (blkno<(i+=nbAddrPerBlock*nbAddrPerBlock))
	{
		//chercher l'adresse dans le block de deuxieme indirection
		q=(blkno-12)/nbAddrPerBlock;
		r=(blkno-12)%nbAddrPerBlock;
		b=e2_buffer_get(c,in->i_block[14]);
		p=(pblk_t *) b;	
		b=e2_buffer_get(c,p[q]);
		p=(pblk_t *) b;	
		return p[r];
	}
	else
	{
		//cherche lycos cherche
		k=(blkno-12)/(nbAddrPerBlock*nbAddrPerBlock);
		q=(blkno-12)/nbAddrPerBlock;
		r=(blkno-12)%nbAddrPerBlock;
		b=e2_buffer_get(c,in->i_block[15]);
		p=(pblk_t *) b;	
		b=e2_buffer_get(c,p[k]);
		p=(pblk_t *) b;	
		b=e2_buffer_get(c,p[q]);
		p=(pblk_t *) b;	
		return p[r];
	}
}

/******************************************************************************
 * Operation de haut niveau : affichage d'un fichier complet
 */

/* affiche les blocs d'un fichier */
int e2_cat (ctxt_t c, inum_t i, int disp_pblk)
{
}

/******************************************************************************
 * Simulation d'une ouverture de fichiers
 */

file_t e2_file_open (ctxt_t c, inum_t i)
{
}

void e2_file_close (file_t of)
{
}

/* renvoie EOF ou un caractere valide */
int e2_file_getc (file_t of)
{
}

/* renvoie nb de caracteres lus (0 lorsqu'on arrive en fin de fichier) */
int e2_file_read (file_t of, void *data, int len)
{
}

/******************************************************************************
 * Operations sur les repertoires
 */

/* retourne une entree de repertoire */
struct ext2_dir_entry_2 *e2_dir_get (file_t of)
{
}

/* recherche un composant de chemin dans un repertoire */
inum_t e2_dir_lookup (ctxt_t c, inum_t i, char *str, int len)
{
}

/******************************************************************************
 * Operation de haut niveau : affichage d'un repertoire complet
 */

/* affiche un repertoire donne par son inode */
int e2_ls (ctxt_t c, inum_t i)
{
}

/******************************************************************************
 * Traversee de repertoire
 */

/* recherche le fichier (l'inode) par son nom */
inum_t e2_namei (ctxt_t c, char *path)
{
}
