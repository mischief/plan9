typedef struct Dec Dec;
typedef struct Decreq Decreq;
typedef struct Decresp Decresp;
typedef struct Decproc Decproc;

enum {
	DINVALID = 0,
	DAUDIO = 1,
	DIMAGE = 2,
};

struct Dec {
	int type;
	union {
		Memimage *image;
		struct {
			uchar *buf;
			long nbuf;
		};
	};
};

struct Decreq {
	int index; /* into AVI->index */
	int count; /* nr chunks to decode */
};

struct Decresp {
	Dec *dec;
	int ndec;
	char err[ERRMAX];
};

struct Decproc {
	int pid;
	int fd;
	Channel *in;	// Decreq
	Channel *out;	// Decresp*

	AVI *avi;
};

Decproc *decproc(AVI*);

//Decresp *decdo(Decproc*, Decreq*);

Memimage *decone(Decproc *p, long offset);
