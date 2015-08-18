typedef struct Msg Msg;
struct Msg
{
	char	prefix[256];
	char	cmd[64];
	char	**args;
	int		nargs;
};

Msg*	parsemsg(char*);
void	freemsg(Msg*);
char*	findmsg(char*, char**);
