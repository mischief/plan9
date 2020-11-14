typedef struct IrcConf IrcConf;
typedef struct Irc Irc;
#pragma incomplete Irc
typedef struct IrcMsg IrcMsg;
typedef struct IrcHooks IrcHooks;

typedef void IrcCb(Irc*, IrcMsg*, void *aux);

struct IrcConf
{
	char 	*address;
	int		debug;
	void	*aux;
	int		(*dial)(Irc *irc, IrcConf *conf);
};

struct IrcMsg
{
	char	prefix[256];
	char	cmd[64];
	char	**args;
	int		nargs;
};

#define IRC_DISCONNECT 	"_DISCONNECTED"
#define IRC_CONNECT		"_CONNECTED"
#define IRC_DIALING		"_DIALING"
#define IRC_OOM			"_OOM"

#define IRC_PING		"PING"
#define IRC_NOTICE		"NOTICE"
#define IRC_PRIVMSG 	"PRIVMSG"

#define IRC_RPL_WELCOME			"001"
#define IRC_ERR_NICKNAMEINUSE	"433"	

Irc* ircinit(IrcConf *conf);
void ircfree(Irc*);
int ircrun(Irc*);
int irchook(Irc *irc, char *what, IrcCb *cb);
void ircterminate(Irc *irc);
IrcConf *ircconf(Irc*);
int ircraw(Irc *irc, char *msg);
int ircrawf(Irc *irc, char *fmt, ...);
int ircquit(Irc *irc, char *why);
int ircuser(Irc *irc, char *user, char *mode, char *realname);
int ircnick(Irc *irc, char *nickname);
int ircjoin(Irc *irc, char *channel);
int ircpart(Irc *irc, char *channel);
int ircprivmsg(Irc *irc, char *tgt, char *msg);
