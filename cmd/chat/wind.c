#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <thread.h>
#include <keyboard.h>
#include <mouse.h>
#include <control.h>

#include "dat.h"
#include "fns.h"

Wind*
windmk(Image *scr, char *label, char *target)
{
	int r;
	Wind *w;

	w = mallocz(sizeof(*w), 1);
	if(target != nil){
		for(r = 0; r < strlen(target); r++)
			target[r] = tolower(target[r]);

		w->id = jenkinshash(target, strlen(target));
	}
	w->keyboard = initkeyboard(nil);
	w->mouse = initmouse(nil, scr);
	w->cs = newcontrolset(scr, w->keyboard->c, w->mouse->c, w->mouse->resizec);
	w->column = createcolumn(w->cs, "column");
	w->top = createlabel(w->cs, "top");
	w->body = createtext(w->cs, "body");
	w->input = createentry(w->cs, "input");

	w->event = chancreate(sizeof(char*), 0);

	w->blines = 0;

	if(target != nil)
		w->target = strdup(target);

	ctlprint(w->top, "value %s", label);
	ctlprint(w->top, "border 1");
	ctlprint(w->top, "size %d %d %d %d", 10, font->height+1, 10000, font->height+1);

	ctlprint(w->body, "border 1");
	ctlprint(w->body, "scroll 1");

	ctlprint(w->input, "border 1");
	ctlprint(w->input, "size %d %d %d %d", 10, font->height+1, 10000, font->height+1);
	controlwire(w->input, "event", w->event);

	activate(w->body);
	activate(w->input);

	chanprint(w->cs->ctl, "column add top");
	chanprint(w->cs->ctl, "column add body");
	chanprint(w->cs->ctl, "column add input");

	return w;
}

void
windfree(Wind *w)
{
	qlock(w);

	/* close mouse first so that controlset is draining the channels */
	closecontrolset(w->cs);
	closemouse(w->mouse);
	closekeyboard(w->keyboard);
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
	ctlprint(w->body, "add %#q", msg);
	qlock(w);
	if(w->blines >= NHIST)
		ctlprint(w->body, "delete 0");
	else
		w->blines++;
	qunlock(w);
}
