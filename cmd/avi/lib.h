typedef struct BITMAPINFO BITMAPINFO;
typedef struct WAVEFORMATEX WAVEFORMATEX;
typedef struct AVIMainHeader AVIMainHeader;
typedef struct AVIStreamHeader AVIStreamHeader;
typedef struct AVIIndex AVIIndex;
typedef struct AVI AVI;

struct BITMAPINFO {
	u32int size;
	u32int width;
	u32int height;
	u16int planes;
	u16int bitcount;
	u32int compression;
	u32int sizeimage;
	u32int xppm;
	u32int yppm;
	u32int clrused;
	u32int clrimportant;
};

struct WAVEFORMATEX {
	u16int tag;
	u16int channels;
	u32int samplespersec;
	u32int avgbytespersec;
	u16int blockalign;
	u16int bitspersample;
	u16int extrasize;
};

struct AVIMainHeader {
	u32int microsperframe;
	u32int maxbytespersec;
	u32int reserved1;
	u32int flags;
	u32int totalframes;
	u32int initialframes;
	u32int streams;
	u32int suggestedbuffersize;
	u32int width;
	u32int height;
	u32int reserved2[4];
};

struct AVIStreamHeader {
	char type[4];
	char handler[4];
	u32int flags;
	u16int priority;
	u16int language;
	u32int initialframes;
	u32int scale;
	u32int rate;
	u32int start;
	u32int length;
	u32int suggestedbuffersize;
	u32int quality;
	u32int samplesize;

	// RECT madness
	int minx, miny, maxx, maxy;
};

struct AVIIndex {
	char id[4];
	u32int flags;
	u32int offset;
	u32int length;
};

struct AVI {
	char path[256];
	Biobuf *bio;

	AVIMainHeader header;

	// [0] is vids, [1] is auds
	AVIStreamHeader stream[2];

	BITMAPINFO bitmap;
	WAVEFORMATEX waveformat;

	// offset of 'movi' chunk in bio
	long movi;

	AVIIndex *index;
	long nindex;
};

void avidump(AVI*);
AVI *aviopen(char*);
