/* conn.c */
Conn*	connmk(char*, void(*)(Conn*));
int		connwrite(Conn*, char*, ...);
void	connproc(void*);	/* Conn* */

/* util.c */
u64int	jenkinshash(char*, int);

/* wind.c */
Wind	*windmk(Image*, char*, char*);
void	windfree(Wind*);
void	windlink(Wind*, Wind*);
void	windunlink(Wind*, Wind*);
Wind*	windfind(Wind*, char*);
void	windappend(Wind*, char*);
