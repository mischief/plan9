#include <u.h>
#include <libc.h>

#include "msg.h"

Msg*
parsemsg(char *b)
{
	char *tok[5], *args[20];
	int i, n, ntok, nargs;
	Msg *msg;

	if(b == nil || b[0] == 0)
		return nil;

	msg = mallocz(sizeof(*msg), 1);

	if(b[0] == ':'){
		ntok = gettokens(b, tok, 2, " ");
		if(ntok != 2)
			goto fail;
		strncpy(msg->prefix, tok[0]+1, sizeof(msg->prefix)-1);
		b = tok[1];
	}

	ntok = gettokens(b, tok, 2, ":");

	nargs = gettokens(tok[0], args, nelem(args), " ");

	strncpy(msg->cmd, args[0], sizeof(msg->cmd)-1);
	n = strlen(msg->cmd);
	for(i = 0; i < n; i++){
		msg->cmd[i] = tolower(msg->cmd[i]);
	}

	msg->nargs = nargs - 1 + (ntok > 1 ? 1 : 0);

	msg->args = mallocz(msg->nargs * sizeof(char*), 1);
	for(i = 0; i < msg->nargs - (ntok > 1 ? 1 : 0); i++){
		msg->args[i] =  strdup(args[i+1]);
	}

	if(ntok > 1){
		msg->args[i] = strdup(tok[1]);
	}

/*
	fprint(2, "prefix: %s\n", msg->prefix);
	fprint(2, "command: %s\n", msg->cmd);
	for(i = 0; i < msg->nargs; i++){
		fprint(2, " %s\n", msg->args[i]);
	}
*/

	return msg;
fail:
	free(msg);
	return nil;
}

void
freemsg(Msg *m)
{
	int i;

	if(m == nil)
		return;

	if(m->nargs > 0){
		for(i = 0; i < m->nargs; i++)
			free(m->args[i]);
		free(m->args);
	}

	free(m);
}

char*
findmsg(char *buf, char **rest)
{
	char *p;

	if((p = strstr(buf, "\r\n")) != nil){
		*p++ = 0;
		*p++ = 0;
		*rest = p;
		return strdup(buf);
	}

	if((p = strstr(buf, "\n")) != nil){
		*p++ = 0;
		*rest = p;
		return strdup(buf);
	}

	return nil;
}
