#include <u.h>
#include <libc.h>
#include <thread.h>

#include "dat.h"
#include "fns.h"

static void
kbdproc(void *v)
{
	char line[512];
	int r;
	Wind *w;

	w = v;

	threadsetname("kbdproc");

	while((r = read(w->kfd, line, sizeof line - 1)) > 0){
		if(r > 0 && line[r] == '\n')
			r--;

		line[r] = 0;

		chanprint(w->event, "%s", line);
	}

	chanclose(w->event);
}

Wind*
windmk(char *label, char *target)
{
	int r;
	Wind *w;

	w = mallocz(sizeof(*w), 1);
	if(target != nil){
		for(r = 0; r < strlen(target); r++)
			target[r] = tolower(target[r]);

		w->id = jenkinshash(target, strlen(target));
	}

	if(target != nil)
		w->target = strdup(target);

	w->event = chancreate(sizeof(char*), 0);
	w->kpid = -1;

	if((w->kfd = open("/dev/cons", ORDWR)) < 0){
		sysfatal("open: %r");
	}

	w->kpid = proccreate(kbdproc, w, 8192);
	return w;
}

void
windfree(Wind *w)
{
	qlock(w);

	if(w->kpid > 0)
		postnote(PNPROC, w->kpid, "die yankee pig dog");

	if(w->event != nil)
		chanfree(w->event);

	free(w->target);
	free(w);
}

void
windlink(Wind *l, Wind *w)
{
	qlock(l);
	w->prev = l;
	w->next = l->next;
	l->next = w;
	qunlock(l);
}

void
windunlink(Wind *l, Wind *w)
{
	qlock(l);
	if(w->next != nil)
		w->next->prev = w->prev;
	if(w->prev != nil)
		w->prev->next = w->next;
	qunlock(l);
}

Wind*
windfind(Wind *l, char *target)
{
	int r;
	u64int id;
	Wind *w;

	for(r = 0; r < strlen(target); r++)
		target[r] = tolower(target[r]);

	id = jenkinshash(target, strlen(target));

	qlock(l);
	for(w = l; w != nil; w = w->next){
		if(w->id == id)
			break;
	}
	qunlock(l);

	return w;
}

void
windappend(Wind *w, char *msg)
{
	qlock(w);

	write(w->kfd, msg, strlen(msg));
	write(w->kfd, "\n", 1);

	qunlock(w);
}
