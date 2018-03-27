#include <u.h>
#include <libc.h>
#include <bio.h>

#include "riff.h"

static int
get4(Biobuf *bio, ulong *r)
{
	uchar buf[4];

	if(Bread(bio, buf, 4) != 4)
		return -1;
	*r = buf[0] | buf[1]<<8 | buf[2]<<16 | buf[3]<<24;
	return 0;
}

static char*
getcc(Biobuf *bio, char tag[4])
{
	if(Bread(bio, tag, 4) != 4)
		return nil;
	return tag;
}

int
riffread(RiffChunk *r, Biobuf *bio, void *buf, long len)
{
	Bseek(bio, r->offset, 0);
	if(Bread(bio, buf, len) != len)
		return -1;

	return 0;
}

void*
riffsnarf(RiffChunk *r, Biobuf *bio)
{
	char *buf;

	buf = malloc(r->size);
	if(buf == nil)
		return nil;

	if(riffread(r, bio, buf, r->size) < 0){
		free(buf);
		return nil;
	}

	return buf;
}

int
rifftype(RiffChunk *r, char *type)
{
	if(memcmp(r->type, type, 4) == 0)
		return 0;
	return -1;
}

int
riffchunk(RiffChunk *r, Biobuf *bio)
{
	vlong off;

	memset(r, 0, sizeof(*r));

	if(getcc(bio, r->type) == nil)
		return -1;

	if(get4(bio, &r->size) < 0)
		return -1;

	off = Boffset(bio);
	if(off < 0)
		return -1;

	r->offset = off;

	if(rifftype(r, "RIFF") == 0 || rifftype(r, "LIST") == 0){
		if(getcc(bio, r->form) == nil)
			return -1;

		// adjust to account for form
		r->size -= 4;
		r->offset += 4;
	}

	return 0;
}
