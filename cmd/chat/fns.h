/* conn.c */
Conn*	connmk(char*, void(*)(Conn*));
int	connwrite(Conn*, char*, ...);
void	connproc(void*);	/* Conn* */

/* util.c */
u64int	jenkinshash(char*, int);
void	riolabel(char*);
void	rioclose(void);
int	riowindow(char*);

/* wind.c */
Wind	*windmk(char*);
void	windfree(Wind*);
void	windlink(Wind*, Wind*);
void	windunlink(Wind*, Wind*);
Wind*	windfind(Wind*, char*);
void	windappend(Wind*, char*);
