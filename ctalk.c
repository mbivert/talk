/*
 * XXX not yet usable but for quick test.
 * inspiration from http://c9x.me/irc/src/irc.c
 * TODO:
 * 	- rotate buffers
 * 	- resize
 * 	- highlights
 * 	- unicode?
 * 	- multiple windows?
 */
#include <arpa/inet.h>
#include <curses.h>
#include <errno.h>
#include <locale.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define NICK "ctalker"

enum {
	Maxline = 256,
	Maxlines = 128,
};

/* sub-windows and associated buffers */
struct {
	WINDOW *w;
	char b[Maxline];
} wbar;
struct {
	WINDOW *w;
	char b[Maxlines][Maxline];
	int nl, cl;
} wserv;
struct {
	WINDOW *w;
	char b[Maxline];
} wsay;

int sserve, sbroad;

/* window size */
int X, Y;

int
xatoi(char *s)
{
	int n;

	errno = 0;
	n = strtol(s, NULL, 10);
	if (errno) {
		fprintf(stderr, "error: '%s' NaN\n", s);
		return -1;
	}

	return n;
}

int
dial(char *host, char *port)
{
	struct sockaddr_in sin;
	struct hostent *serv;
	int s, p;

	p = xatoi(port);
	if (p == -1)
		return -1;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == -1) {
		perror("socket");
		return -1;
	}

	serv = gethostbyname(host);
	if (serv == NULL) {
		perror("gethostbyname");
		return -1;
	}

	memset(&sin, '\0', sizeof(sin));
	sin.sin_family = AF_INET;
	memcpy((char *)&sin.sin_addr.s_addr,
			(char *)serv->h_addr,
			serv->h_length);

	sin.sin_port = htons(p);

	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		perror("connect");
		return -1;
	}

	return s;
}

void
bye(int n)
{
	close(sserve);
	close(sbroad);

	delwin(wbar.w);
	delwin(wserv.w);
	delwin(wsay.w);

	endwin();

	exit(n);
}

void
drawbar(char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vsnprintf(wbar.b, sizeof(wbar.b)-1, format, ap);
	va_end(ap);

	werase(wbar.w);
	wmove(wbar.w, 0, 0);
	waddstr(wbar.w, wbar.b);
	wrefresh(wbar.w);
}

void
drawserv(void)
{
	int i;

	werase(wserv.w);
	wmove(wserv.w, 0, 0);

	for (i = 0; i < wserv.cl; i++) {
		fprintf(stderr, "'%s'\n", wserv.b[i]);
		waddstr(wserv.w, wserv.b[i]);
	}

	fprintf(stderr, "DBG drawserv\n");
	wrefresh(wserv.w);
}

void
drawsay(void)
{
	werase(wsay.w);
	waddstr(wsay.w, wsay.b);
	wmove(wsay.w, 0, strlen(wsay.b));
	wrefresh(wsay.w);
}

int
initui(void)
{
	// signal(SIGWINCH, sigwinch);
	initscr();
	raw(); // cbreak + control over ^C, ^Z, etc.
	getmaxyx(stdscr, Y, X);
	
	/* bar at the top */
	wbar.w = newwin(1, X, 0, 0);
	/* data from server */
	wserv.w = newwin(Y-2, X, 1, 0);
	/* client's input */
	wsay.w = newwin(1, X, Y-1, 0);

	if (wbar.w == NULL || wserv.w == NULL || wsay.w == NULL) {
		fprintf(stderr, "error: can't create windows\n");
		return -1;
	}

	/* empty buffers */
	memset(wbar.b, '\0', sizeof(wbar.b));
	memset(wserv.b, '\0', sizeof(wserv.b));
	memset(wsay.b, '\0', sizeof(wsay.b));

	wserv.nl = 0;
	wserv.cl = 0;

	/* get KEY_UP, KEY_DOWN, KEY_NPAGE, KEY_PPAGE */
	keypad(wsay.w, 1);

	/* use color for the top bar if available */
	if (has_colors()) {
		start_color();
		init_pair(1, COLOR_WHITE, COLOR_BLUE);
		if (wbkgd(wbar.w, COLOR_PAIR(1)) == ERR)
			fprintf(stderr, "warning: can't set bg color\n");
	}

	return 0;
}

void
redraw(void)
{
	/* no need to redraw top bar */
	/* redraw body */
	drawserv();
	/* redraw input */
	drawsay();
}

/*
 * peek a character from stdin.
 * return 1 when a line is ready to be sent, else 0;
 * XXX make it unicode aware
 */
int
peek(void)
{
	static int n = 0;
	int c;

	c = wgetch(wsay.w);

	switch (c) {
	case CTRL('u'):
		memset(wsay.b, '\0', sizeof(wsay.b));
		n = 0;
		break;
	case CTRL('h'):
	case KEY_BACKSPACE:
		if (n > 0)
			wsay.b[--n] = '\0';
		break;
	case '\n':
		wsay.b[n++] = (char)c;
		n = 0;
		break;
	default:
		if (c <= CHAR_MAX)
		/* -2 for '\n\0' */
		if (n < sizeof(wsay.b)-2)
			wsay.b[n++] = (char)c;
		break;
	}

	return c == '\n';
}

/*
 * hear something from the server.
 */
int
hear(int fd)
{
	static int partial = 0;
	char buf[Maxline];
	char *p, *q;
	int n, m;

	memset(buf, '\0', sizeof(buf));
	n = read(fd, buf, sizeof(buf)-1);
	if (n <= 0)
		return n;

	for (q = buf; ; q = p+1) {
		p = strrchr(q, '\n');
		if (p == NULL)
			break;
		// +1 for '\n'
		if (partial) {
			partial = 0;
			strncat(wserv.b[wserv.cl], q, p-q+1);
		} else
			strncpy(wserv.b[wserv.cl], q, p-q+1);
		wserv.cl = (wserv.cl+1) % Maxlines;
		if (wserv.nl < Maxlines)
			wserv.nl++;
	}

	if (q-buf < n) {
		strncpy(wserv.b[wserv.cl], q, n-(q-buf));
		partial = 1;
	}

	return n;
}

/*
 * say something to the server. return negative
 * numbers upon error/on quitting.
 */
int
say(void)
{
	int n;

	/* prepend '!say' if not a command */
	if (wsay.b[0] != '!')
		write(sserve, "!say ", 5);

	/* send message */
	fprintf(stderr, "DBG say: '%s'\n", wsay.b);
	n = write(sserve, wsay.b, strlen(wsay.b));

	/* want to quit anyway */
	if (strncmp(wsay.b, "!quit", 5) == 0 || strncmp(wsay.b, "!leave", 6) == 0)
		return -1;

	/* reset the buffer */
	memset(wsay.b, '\0', sizeof(wsay.b));

	return n;
}

void
fdset(fd_set *rfs, fd_set *wfs)
{
	FD_ZERO(wfs);
	FD_ZERO(rfs);

	/* write message to sserve */
	FD_SET(sserve, wfs);

	/* read chars from stdin */
	FD_SET(0, rfs);
	/* read non-broadcasted message */
	FD_SET(sserve, rfs);
	/* read broadcasted message */
	FD_SET(sbroad, rfs);
}

void
loop(char *nick, char *host)
{
	fd_set rfs, wfs;
	FILE *fs, *fb;
	int hasline;
	int m;

	fs = fdopen(sserve, "r+");
	fb = fdopen(sbroad, "r+");

#	define max(a,b) ((a) > (b) ? (a) : (b))
	m = max(sserve, sbroad);

	snprintf(wsay.b, sizeof(wsay.b)-2, "!iam %s\n", nick);
	hasline = 1;

	drawbar("connected as %s@%s\n", nick, host);

	for (;;) {
		fdset(&rfs, &wfs);
		if (select(m+1, &rfs, &wfs, NULL, NULL) == -1) {
			perror("select");
			return;
		}
		if (FD_ISSET(sserve, &wfs))
		if (hasline) {
			hasline = 0;
			if (say() < 0)
				return;
			redraw();
		}
		if (FD_ISSET(0, &rfs))
			hasline = peek(), redraw();
		if (FD_ISSET(sserve, &rfs))
			hear(sserve), redraw();
		if (FD_ISSET(sbroad, &rfs))
			hear(sbroad), redraw();
	}
}

int
main(int argc, char *argv[])
{
	int sport, dport;
	char *nick;

	nick = NICK;

	if (argc < 4 || strcmp(argv[1], "-h") == 0) {
		fprintf(stderr, "%s host sport dport [nick]\n", argv[0]);
		return 1;
	}

	if (initui() == -1)
		bye(-1);
	drawbar("connecting...");

	if (argc >= 5)
		nick = argv[4];

	sserve = dial(argv[1], argv[2]);
	sbroad = dial(argv[1], argv[3]);

	if (sserve == -1 || sbroad == -1)
		return -1;

	loop(nick, argv[1]);

	bye(0);

	return 0;
}
