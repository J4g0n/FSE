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
			while((bfCur=bfSent->next)!=c->last)
			{
				if(bfSent->data!=NULL) free(bfSent->data);
				free(bfSent);
				bfSent=bfCur;			
			}
			if(c->last->data!=NULL) free(c->last->data);
			free(c->last);
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
	if(i<ipg*c->ngroups)
	{
		pblk_t inodeBlock=c->gd[i/ipg].bg_inode_table;
		bn=inodeBlock+i/(e2_ctxt_blksize(c)/inodeSize);
	}
	return bn;
}

/* extrait l'inode du buffer */
struct ext2_inode *e2_inode_read (ctxt_t c, inum_t i, buf_t b)
{
	int inodeSize=sizeof(struct ext2_inode);
	int inodesPerBlock=e2_ctxt_blksize(c)/inodeSize;
	i%=inodesPerBlock;
	struct ext2_inode *e2in;
	int offset=i*(inodeSize);
	e2in=(struct ext2_inode *) (b->data+offset);	
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
int e2_cat (ctxt_t c, inum_t in, int disp_pblk)
{
	pblk_t bNum;
	buf_t buffer;
	struct ext2_inode *e2in;
	int size,nbBlocks,i;

	if((bNum=e2_inode_to_pblk(c,in))<0) 
	{
		fprintf(stderr,"Erreur aucun fichier pour cet inode\n");
		exit(EXIT_FAILURE);
	}
	buffer=e2_buffer_get(c,bNum);
	e2in=e2_inode_read(c,in,buffer);
	e2_buffer_put(c,buffer);
	size=e2in->i_size;
	nbBlocks=((e2in->i_size)/(e2_ctxt_blksize(c)))+1;

	if(disp_pblk==0)
	{
		for(i=0;i<nbBlocks;i++)
		{
			bNum=e2_inode_lblk_to_pblk(c,e2in,i);
			buffer=e2_buffer_get(c,bNum);
			printf("%s\n",(char *) buffer->data);
			e2_buffer_put(c,buffer);
		}
	}
	else
	{
		printf("Size: %d\n",size);
		for(i=0;i<nbBlocks;i++)
		{
			bNum=e2_inode_lblk_to_pblk(c,e2in,i);
			printf("Bloc N°%d: %d\n",i,bNum);
		}
	}
	return 0;
}

/******************************************************************************
 * Simulation d'une ouverture de fichiers
 */

file_t e2_file_open (ctxt_t c, inum_t i)
{
	int bNum;
	if((bNum=e2_inode_to_pblk(c,i))<0) 
	{
		fprintf(stderr,"Numero de bloc invalide\n");
		return NULL;
	}
	
	file_t f=(file_t) malloc(sizeof(struct ofile));
	buf_t b=e2_buffer_get(c,bNum);
	struct ext2_inode *e2in=e2_inode_read(c,i,b);
	buf_t bData=e2_buffer_get(c,e2in->i_block[0]);

	f->ctxt=c;
	f->buffer=b;
	f->inode=e2in;
	f->curblk=0;
	f->data=(char *) calloc(e2_ctxt_blksize(c),sizeof(char));
	memcpy((void *) f->data,bData->data,e2_ctxt_blksize(c));
	if(e2in->i_size<e2_ctxt_blksize(c)) f->len=e2in->i_size;
	else f->len=e2_ctxt_blksize(c);
	f->pos=0;

	return f;
}

void e2_file_close (file_t f)
{
	buf_t b=e2_buffer_get(f->ctxt,e2_inode_lblk_to_pblk(f->ctxt,f->inode,f->curblk));
	free(b->data);
	b->data=f->data;
	e2_buffer_put(f->ctxt,b);
	e2_buffer_put(f->ctxt,f->buffer);
	free(f);
}

/* renvoie EOF ou un caractere valide */
int e2_file_getc (file_t f)
{
	buf_t bOld,bNew;
	char retChar=f->data[f->pos];
	if(++f->pos>f->len) return EOF;
	if(f->pos>=e2_ctxt_blksize(f->ctxt)) 
	{
		bOld=e2_buffer_get(f->ctxt,e2_inode_lblk_to_pblk(f->ctxt,f->inode,f->curblk));
		bNew=e2_buffer_get(f->ctxt,e2_inode_lblk_to_pblk(f->ctxt,f->inode,++f->curblk));
		free(bOld->data);
		bOld->data=f->data;
		e2_buffer_put(f->ctxt,bOld);
		f->data=(char *) calloc(e2_ctxt_blksize(f->ctxt),sizeof(char));
		memcpy((void *) f->data,bNew->data,e2_ctxt_blksize(f->ctxt));
		e2_buffer_put(f->ctxt,bNew);
		if((f->len=f->inode->i_size-f->curblk*e2_ctxt_blksize(f->ctxt))>=e2_ctxt_blksize(f->ctxt))
			f->len=e2_ctxt_blksize(f->ctxt);
		f->pos=0;
		retChar=f->data[f->pos++];
	}
	return retChar;
}

/* renvoie nb de caracteres lus (0 lorsqu'on arrive en fin de fichier) */
int e2_file_read (file_t f, void *data, int len)
{
	buf_t bOld,bNew;
	int blkSize=e2_ctxt_blksize(f->ctxt);
	int lenCur=len;
	void *pData=data;
	if(lenCur+f->pos<blkSize)
	{
		memcpy(pData,f->data+f->pos,lenCur);
		f->pos+=lenCur;
	}
	else
	{
		memcpy(pData,f->data+f->pos,blkSize-f->pos);
		lenCur-=blkSize-f->pos;
		pData+=lenCur;
		while(lenCur>0)
		{
			bOld=e2_buffer_get(f->ctxt,e2_inode_lblk_to_pblk(f->ctxt,f->inode,f->curblk));
			bNew=e2_buffer_get(f->ctxt,e2_inode_lblk_to_pblk(f->ctxt,f->inode,++f->curblk));
			free(bOld->data);
			bOld->data=f->data;
			e2_buffer_put(f->ctxt,bOld);
			f->data=(char *) calloc(blkSize,sizeof(char));
			memcpy((void *) f->data,bNew->data,blkSize);
			e2_buffer_put(f->ctxt,bNew);

			if(lenCur>blkSize)
			{
				f->pos=0;
				memcpy(pData,f->data,blkSize);
				pData-=blkSize;
			}
			else
			{
				memcpy(pData,f->data,lenCur);
				f->pos=lenCur;
				break;
			}
		}
	}
}

/******************************************************************************
 * Operations sur les repertoires
 */

/* retourne une entree de repertoire */
struct ext2_dir_entry_2 *e2_dir_get (file_t f)
{
	unsigned char c;
	int i=0;
	struct ext2_dir_entry_2 *e2de=(struct ext2_dir_entry_2 *)	malloc(sizeof(struct ext2_dir_entry_2));
	unsigned char *p=e2de;

	do
	{
		if((c=e2_file_getc(f))==EOF) return NULL;
		p[i]=c;
		printf("%x ",p[i]);
		i++;
		//printf("%d ",f->pos);
	}
	while(i<8);

	do
	{
		if((c=e2_file_getc(f))==EOF) return NULL;
		p[i]=c;
		printf("%x ",p[i]);
		i++;
		//printf("%d\t ",f->pos);
	}
	while(i<e2de->rec_len);

	return e2de;	
}

/* recherche un composant de chemin dans un repertoire */
inum_t e2_dir_lookup (ctxt_t c, inum_t i, char *str, int len)
{
}

/******************************************************************************
 * Operation de haut niveau : affichage d'un repertoire complet
 */

/* affiche un repertoire donne par son inode */
int e2_ls (ctxt_t c, inum_t in)
{
	file_t f;
	int i=0;
	struct ext2_dir_entry_2 *e2de;

	if((f=e2_file_open(c,in))==NULL)
	{
		fprintf(stderr,"impossible d'ouvrir le fichier\n");
		exit(EXIT_FAILURE);
	}

	while(((e2de=e2_dir_get(f))!=NULL)&&(e2de->rec_len<263))
	{
		printf("\n%d",e2de->inode);
		printf("\t%d",e2de->rec_len);
		printf("\t%d",e2de->name_len);
		printf("\t%d",e2de->file_type);
		printf("\n%s\n",&e2de->name);
	}

	e2_file_close(f);

	return 0;
}

/******************************************************************************
 * Traversee de repertoire
 */

/* recherche le fichier (l'inode) par son nom */
inum_t e2_namei (ctxt_t c, char *path)
{
}
