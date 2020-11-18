#include <u.h>
#include <libc.h>
#include <bio.h>

#include "riff.h"
#include "lib.h"

void
avidump(AVI *avi)
{
	int i;
	AVIMainHeader *avih;
	AVIStreamHeader *strh;
	WAVEFORMATEX *wf;

	avih = &avi->header;

	fprint(2, "AVI main header\n");
	fprint(2, "\tframes = %ud, ms/frame = %ud\n",
		avih->totalframes, avih->microsperframe/1000);
	fprint(2, "\t%ud streams, %udx%ud\n",
		avih->streams, avih->width, avih->height);
	fprint(2, "\tflags = %b\n", avih->flags);

	//vdelay = avih.microsperframe/1000;
	//print("Delay = %dms\n", vdelay);

	// dump stream header
	for(i = 0; i < nelem(avi->stream); i++){
		strh = &avi->stream[i];
		fprint(2, "AVI Stream: '%.4s' '%.4s'\n", strh->type, strh->handler);
		fprint(2, "\tflags %b\n", strh->flags);
		fprint(2, "\trate %ud/%ud\n", strh->rate, strh->scale);
		fprint(2, "\tstart %ud length %ud quality %ud samplesize %ud\n",
			strh->start, strh->length, strh->quality, strh->samplesize);
	}

	wf = &avi->waveformat;
	fprint(2, "AVI Wave format: tag %hux %hud channels %hud bit @ %udHz depth\n",
				wf->tag, wf->channels, wf->bitspersample, wf->samplespersec);

	fprint(2, "AVI Index: %lud entries\n", avi->nindex);
}

static int
avichunk(AVI *avi, RiffChunk *c)
{
	AVIStreamHeader strh;

	//fprint(2, "Chunk: '%.4s' form '%.4s' %lud bytes @ %lld\n",
	//	c->type, c->form, c->size, c->offset);

	if(memcmp(c->form, "movi", 4) == 0)
		avi->movi = c->offset;

	if(rifftype(c, "avih") == 0){
		if(riffread(c, avi->bio, &avi->header, sizeof(AVIMainHeader)) < 0){
			werrstr("failed to read avi header: %r");
			return -1;
		}
	}

	// messy, but ¯\_(ツ)_/¯
	if(rifftype(c, "strh") == 0){
		if(riffread(c, avi->bio, &strh, sizeof(strh)) < 0){
			werrstr("failed to read stream header: %r");
			return -1;
		}

		if(memcmp(strh.type, "vids", 4) == 0){
			avi->stream[0] = strh;
			Bseek(avi->bio, c->offset+c->size+(c->size&1), 0);
			if(riffchunk(c, avi->bio) < 0)
				return -1;

			if(rifftype(c, "strf") != 0)
				return -1;

			if(riffread(c, avi->bio, &avi->bitmap, sizeof(avi->bitmap)) < 0)
				return -1;
		}

		if(memcmp(strh.type, "auds", 4) == 0){
			avi->stream[1] = strh;
			Bseek(avi->bio, c->offset+c->size+(c->size&1), 0);
			if(riffchunk(c, avi->bio) < 0)
				return -1;

			if(rifftype(c, "strf") != 0)
				return -1;

			if(riffread(c, avi->bio, &avi->waveformat, sizeof(avi->waveformat)) < 0)
				return -1;
		}
	}

	if(rifftype(c, "idx1") == 0){
		avi->index = riffsnarf(c, avi->bio);
		if(avi->index == nil){
			werrstr("failed to read index block: %r");
			return -1;
		}

		avi->nindex = c->size/sizeof(AVIIndex);
	}

	return 0;
}

static int
aviload(AVI *avi, RiffChunk *outer)
{
	RiffChunk chunk;
	vlong start, off, end;

	start = off = Boffset(avi->bio);
	end = start + outer->size;

	while(off != end){
		if(riffchunk(&chunk, avi->bio) < 0){
			werrstr("failed to read riff chunk: %r");
			return -1;
		}

		if(avichunk(avi, &chunk) < 0){
			werrstr("failed to read avi chunk: %r");
			return -1;
		}

		if(rifftype(&chunk, "LIST") == 0 && memcmp(chunk.form, "movi", 4) != 0){
			if(aviload(avi, &chunk) < 0){
				werrstr("failed to load movi chunk: %r");
				return -1;
			}
		}

		// &1 to account for RIFF padding
		off = Bseek(avi->bio, chunk.offset+chunk.size+(chunk.size&1), 0);
	}

	return 0;
}

AVI*
aviopen(char *name)
{
	Biobuf *bio;
	RiffChunk r;
	AVI *avi;

	bio = Bopen(name, OREAD);
	if(bio == nil)
		return nil;

	if(riffchunk(&r, bio) < 0){
		Bterm(bio);
		return nil;
	}

	if(rifftype(&r, "RIFF") != 0){
		Bterm(bio);
		werrstr("%s: not a RIFF", name);
		return nil;
	}

	avi = mallocz(sizeof(AVI), 1);
	if(avi == nil){
		Bterm(bio);
		return nil;
	}

	snprint(avi->path, sizeof(avi->path), "%s", name);
	avi->bio = bio;

	if(aviload(avi, &r) < 0){
		free(avi);
		Bterm(bio);
		return nil;
	}

	return avi;
}
