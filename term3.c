#define LOCAL_TERM	1

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <string.h>

#ifdef LOCAL_TERM
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <unistd.h>
#endif

#include <X11/Xlib.h>

/* these needed for passing the terminal
 * window resize hints to the window manager */
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "vt102-backend-generic.h"

#define panic(msg) do { printf("%i\n", __LINE__); while(1); } while(0)

#define NR_ANSI_COLORS		8
#define MAX_CON_WIDTH_IN_CHARS		(255 * 3)
#define MAX_CON_HEIGHT_IN_CHARS		(255 * 3)


/* the terminal window data structure */
struct xterm_data
{
	/* the terminal emulator data structure - this
	 * holds the screen character buffer, cursor position data,
	 * screen dimensions, etc. */
	struct term_data * tdata;
	/* the display variable identifying the
	 * x server connection */  
	Display * disp;
	/* the main terminal window */
	Window win;
	/* currently active (selected) graphics context */
	GC gc;
	/* default graphics context
	 *  
	 * \todo	this is currently unused
	 */
	GC default_gc;
	/* graphics contexts - the indices correspond to
	 * ansi foreground color:
	 * 0 - black
	 * 1 - red
	 * 2 - green
	 * 3 - yellow
	 * 4 - blue
	 * 5 - magenta
	 * 6 - cyan
	 * 7 - white
	 *
	 * background is black */
	GC ansi_color_gcs[NR_ANSI_COLORS];
	/* the x server connection file descriptor */
	int x_fd;
	/* the socket file descriptor used for
	 * communicating with the remote (shell)
         * process */
	int comm_fd;	
	/* font dimensions, a monospaced font is assumed */
	int font_width, font_height, lbearing, ascent;
	/* the primary pixmap canvas used for refreshing the
	 * terminal windows and a temporary working one used
	 * when updating the primary pixmap canvas contents
	 * (e.g. when scrolling)
	 *
	 * rendering is not done directly in the terminal window
	 * but rather in the primary pixmap canvas, which
	 * is then copied to the terminal window - this reduces
	 * flicker and is generally faster
	 *
	 * \todo	the pixmap_tmp pixmap is currently
	 *		unused - remove it */
	Pixmap pixmap_canvas, pixmap_tmp;
};

/* updates the terminal window pixmap canvas at character position
 * (x; y) by writing out the text pointed to by the 'text'
 * parameter (with length 'text_len'); all of the text is
 * rendered with the graphics context data foreground and
 * background colors specified as indices in the xdata->ansi_color_gcs[]
 * array by the 'fg_gc_idx' and 'bg_gc_idx' parameters */
static void update_term_pixmap_stride(struct xterm_data * xdata, int x, int y, int fg_gc_idx, int bg_gc_idx, char * text, int text_len)
{
	/* update the pixmap canvas */
	/* optimize for the most common case - black background */
	if (bg_gc_idx == 0)
	{
		/* the background is black - use a stock
		 * graphics context */
		XDrawImageString(xdata->disp,
				xdata->pixmap_canvas,
				(xdata->ansi_color_gcs[fg_gc_idx]/* , xdata->ansi_color_gcs[7]*/),
				xdata->lbearing + x * xdata->font_width,
				xdata->ascent + y * xdata->font_height,
				text,
				text_len);
	}
	else
	{
		/* non-black background - first render
		 * the background, then the character image */
		/* first, draw the background */
		XFillRectangle(xdata->disp,
				xdata->pixmap_canvas,
				(xdata->ansi_color_gcs[bg_gc_idx]/* , xdata->ansi_color_gcs[7]*/),
				x * xdata->font_width,
				y * xdata->font_height,
				xdata->font_width * text_len,
				xdata->font_height);
		/* then, display the character (without rendereing
		 * the background) */
		XDrawString(xdata->disp,
				xdata->pixmap_canvas,
				xdata->ansi_color_gcs[fg_gc_idx],
				xdata->lbearing + x * xdata->font_width,
				xdata->ascent + y * xdata->font_height,
				text,
				text_len);
	}
	xdata->tdata->must_refresh = true;
}

static void update_term_pixmap(struct xterm_data * xdata)
{
int i, j, k, grdata;

	for (i = 0; i < xdata->tdata->con_height; i++)
	{
		if (!xdata->tdata->must_refresh_line_buf[i])
			continue;
		for (j = 0; j < xdata->tdata->con_width; j = k)
		{
			grdata = xdata->tdata->grbuf[i * xdata->tdata->con_width + j];
			for (k = j + 1; k < xdata->tdata->con_width; k++)
				if (xdata->tdata->grbuf[i * xdata->tdata->con_width + k]
						!= grdata)
					break;
			update_term_pixmap_stride(xdata,
					j,
					i,
					grdata & 7,
					(grdata >> 4) & 7,
					xdata->tdata->chbuf + i * xdata->tdata->con_width + j,
					k - j);
		}
		xdata->tdata->must_refresh_line_buf[i] = false;
	}
}

static int
connect_to (uint16_t port)
{
int socket_fd;
struct sockaddr_in addr;

	if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		return -1;
	}
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	//addr.sin_addr.s_addr = inet_addr("192.168.0.37");
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (connect(socket_fd, (struct sockaddr *) &addr, sizeof addr) == -1)
	{
		perror("connect");
		return -1;
	}
	printf("ok, connection established\n");
	return socket_fd;
}



/*!
 *	\fn	static void query_terminal_id(struct term_data * tdata)
 *	\brief	handles a terminal identification request command
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\todo	this routine is broken - besides, it does not
 *		belong here
 *	\return	none */
static void query_terminal_id(struct term_data * tdata)
{
static const char vt102_id_str[] = "\x1b[?6c";

	printf("term id requested\n");
	/* return vt102 identification string */
	/*
	if (write(tdata->comm_fd, vt102_id_str, strlen(vt102_id_str))
			!= strlen(vt102_id_str))
		panic("");
		*/
}



int main(void)
{
/* timeout used when invoking select() when
 * the terminal window needs refreshing; this
 * is used for optimizing the update of the
 * terminal window and generally speeding up
 * the displaying of the terminal window */
struct timeval timeout, * ptimeout;	
static const struct
{
	KeySym keysym;
	const char * xlat_str;
}
key_xlat_tab[] =
{
	{	XK_Up,		"\033[A"	},
	{	XK_Down,	"\033[B"	},
	{	XK_Right,	"\033[C"	},
	{	XK_Left,	"\033[D"	},
};
static const unsigned int key_filter_tab[] =
{
	XK_Control_L,
	XK_Control_R,
	XK_Shift_L,
	XK_Shift_R,
};

static const char * ansi_color_strings[NR_ANSI_COLORS] =
{
/* 0 - black */		"black",
/* 1 - red */		"red",
/* 2 - green */		"green",
/* 3 - yellow */	"yellow",
/* 4 - blue */		"cyan",
/* 5 - magenta */	"magenta",
/* 6 - cyan */		"cyan",
/* 7 - white */		"white",
};
XEvent xevt;
int i;
unsigned long black_pixel, white_pixel;
XGCValues gcvals;
XFontStruct * font;
/* used for telling the window manager to resize
 * the terminal window by x and y steps equal
 * to the selected font dimensions */ 
XSizeHints wm_size_hint;
struct xterm_data xdata;
/* file descriptor set used when calling select() */
fd_set descriptor_set;
/* the vt102 emulator state variable, used when calling into the emulator
 * command parser */
struct vt102_state * vtstate;
/* log file descriptor */
int log_fd;

#ifdef LOCAL_TERM
	switch (forkpty(&xdata.comm_fd, 0, 0, 0))
	{
		case -1:
			perror("fork()");
			exit(1);
		case 0:
			/* this is the child process */
			execl("./test", "./test", NULL);
			perror("execl()");
			exit(1);
	}
	/* this is the parent process */
#endif

	/* open the log file */
	if ((log_fd = open("term-log.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE))
			== -1)
	{
		printf("could not create log file");
		exit(1);
	}

#ifndef LOCAL_TERM
	/* connect to the remote host */
	if ((xdata.comm_fd = connect_to(1234)) == -1)
	{
		printf("error connecting to remote host\n");
		exit(1);
	}
#endif
	if (!(vtstate = init_vt102_generic_backend(80, 24)))
	{
		printf("error initializing the vt102 terminal emulator\n");
		exit(1);
	}
	xdata.tdata = vt102_generic_backend_get_data(vtstate);
	/* override the terminal id query backend function */
	vt102_get_backend_ops(vtstate)->query_terminal_id = query_terminal_id;

	if (!(xdata.disp = XOpenDisplay(0)))
	{
		printf("could not open display\n");
		exit(1);
	}

	/* load the font to be used for the terminal window */
	if (!(font = XLoadQueryFont(xdata.disp, "-misc-fixed-bold-*-*-*-*-*-*-*-*-*-*-*")))
	{
		XCloseDisplay(xdata.disp);
		printf("error loading font\n");
		exit(1);
	}
	/* store the font dimensions */
	xdata.font_width = font->max_bounds.rbearing - font->min_bounds.lbearing;
	xdata.font_height = font->max_bounds.ascent + font->max_bounds.descent;
	xdata.lbearing = - font->min_bounds.lbearing;
	xdata.ascent = font->max_bounds.ascent;

	xdata.x_fd = ConnectionNumber(xdata.disp);
	black_pixel = BlackPixel(xdata.disp, DefaultScreen(xdata.disp));
	white_pixel = WhitePixel(xdata.disp, DefaultScreen(xdata.disp));
	xdata.win = XCreateSimpleWindow(xdata.disp, DefaultRootWindow(xdata.disp), 100, 100, xdata.tdata->con_width * xdata.font_width, xdata.tdata->con_height * xdata.font_height, 2, black_pixel, black_pixel);
	if (!XSelectInput(xdata.disp, xdata.win,
				StructureNotifyMask
				| ButtonPressMask
				| KeyPressMask
				| SubstructureRedirectMask
				| ExposureMask))
	{
		XCloseDisplay(xdata.disp);
		printf("error setting window input mask\n");
		exit(1);
	}
	/* set properties for the window manager */
	/* set the size hints so that resizing the terminal
	 * window is character-dimensions-granular (dependent
         * on the terminal window font dimensions) */	 
	wm_size_hint.flags = PResizeInc;
	wm_size_hint.width_inc = xdata.font_width;
	wm_size_hint.height_inc = xdata.font_height;
	XSetWMNormalHints(xdata.disp, xdata.win, &wm_size_hint);
/*
	if (!XSelectInput(xdata.disp, DefaultRootWindow(xdata.disp),
				SubstructureRedirectMask))
		printf("error\n");
		*/
	if (!XMapWindow(xdata.disp, xdata.win))
	{
		XCloseDisplay(xdata.disp);
		printf("error mapping window\n");
		exit(1);
	}
	/* wait for the window to get mapped */
	while (1)
	{
		XNextEvent(xdata.disp, &xevt);
		if (xevt.type == MapNotify && xevt.xmapping.window == xdata.win)
			break;
	}

	/* construct the graphics contexts */
	/* construct the default graphics context */
	gcvals.foreground = black_pixel;
	gcvals.background = white_pixel;
	gcvals.font = font->fid;
	xdata.default_gc = XCreateGC(xdata.disp, xdata.win, GCBackground | GCForeground | GCFont, &gcvals);
	gcvals.background = black_pixel;
	gcvals.foreground = white_pixel;
	xdata.gc = XCreateGC(xdata.disp, xdata.win, GCBackground | GCForeground | GCFont, &gcvals);
	/* construct the ansi color graphics contexts */
{
	XColor xc, dummy;
	for (i = 0; i < NR_ANSI_COLORS; i++)
	{
		if (!XAllocNamedColor(xdata.disp, DefaultColormap(xdata.disp, DefaultScreen(xdata.disp)),
					ansi_color_strings[i],
					&dummy, &xc))
			printf("error allocating color\n");
		gcvals.foreground = xc.pixel;
		xdata.ansi_color_gcs[i] = XCreateGC(xdata.disp, xdata.win, GCBackground | GCForeground | GCFont, &gcvals);
	}
}

	/* doesnt need the font anymore... */
	XFreeFont(xdata.disp, font);
	/* create the main and working pixmap canvases */
	i = DefaultDepth(xdata.disp, DefaultScreen(xdata.disp));
	xdata.pixmap_canvas = XCreatePixmap(xdata.disp,
			xdata.win,
			MAX_CON_WIDTH_IN_CHARS * xdata.font_width,
			MAX_CON_HEIGHT_IN_CHARS * xdata.font_height,
			i);
	xdata.pixmap_tmp = XCreatePixmap(xdata.disp,
			xdata.win,
			xdata.tdata->con_width * xdata.font_width,
			xdata.tdata->con_height * xdata.font_height,
			i);
	if (!xdata.pixmap_canvas || !xdata.pixmap_tmp)
	{
		XCloseDisplay(xdata.disp);
		printf("error creating terminal window pixmaps\n");
		exit(1);
	}
	/* enter main loop */
	while (1)
	{
		while (XPending(xdata.disp))
		{
			XNextEvent(xdata.disp, &xevt);
			switch (xevt.type)
			{
				case Expose:
					/* update the terminal window from the
					 * primary pixmap canvas */
					break;
					XCopyArea(xdata.disp,
							xdata.pixmap_canvas,
							xdata.win,
							xdata.gc,
							0, 0,
							xdata.tdata->con_width * xdata.font_width,
							xdata.tdata->con_height * xdata.font_height,
							0, 0);
					break;
				case KeyPress:
				{
					char c;
					XKeyEvent * xkey;
					KeySym ksym;

					xkey = (XKeyEvent *) &xevt;
					XLookupString(xkey,
							&c,
							1,
							&ksym,
							0);
					/* filter out some control
					 * keys */
					switch (xkey->keycode)
					{
						case XK_Shift_L:
						case XK_Shift_R:
						case XK_Control_L:
						case XK_Control_R:
							c = 0;
					}
					/* translate some control
					 * keys */
					printf("kcode %i\n", (int) xkey->keycode);
					for (i = sizeof key_xlat_tab
							/ sizeof * key_xlat_tab - 1;
							i >= 0;
							i--)
						if (ksym == key_xlat_tab[i].keysym)
						{
							printf("match found\n");
							break;
						}
					if (i != -1)
					{
						/* translate key to a
						 * control sequence */
						if (write(xdata.comm_fd,
									key_xlat_tab[i].xlat_str,
									strlen(key_xlat_tab[i].xlat_str))
								!= strlen(key_xlat_tab[i].xlat_str))
						{
							/* normal keycodes */
							XCloseDisplay(xdata.disp);
							perror("read");
							exit(1);
						}
					}
					else if (c && write(xdata.comm_fd, &c, 1) != 1)
					{
						/* normal keycodes */
						XCloseDisplay(xdata.disp);
						perror("read");
						exit(1);
					}
				}
					break;
				case ConfigureNotify:
				{
					XConfigureEvent * xconf;
					int w, h;
					/* compress ConfigureNotify events */
					while (XPending(xdata.disp))
					{
						XEvent lxevt;
						XPeekEvent(xdata.disp, &lxevt);
						if (lxevt.type == ConfigureNotify)
							XNextEvent(xdata.disp, &xevt);
						else
							break;
					}

					xconf = (XConfigureEvent *) &xevt;
					w = xconf->width / xdata.font_width;
					h = xconf->height / xdata.font_height;
					/* resize the data buffers */
					vt102_generic_backend_resize_buffers(vtstate, w, h);


#ifdef LOCAL_TERM
				{
				struct winsize winsz;
					if (ioctl(xdata.comm_fd,
							TIOCGWINSZ,
							&winsz))
						perror("ioctl(): cannot obtain terminal window size");
					else
					{
						winsz.ws_col = w;
						winsz.ws_row = h;
						if (ioctl(xdata.comm_fd,
								TIOCSWINSZ,
								&winsz))
							perror("ioctl(): cannot set terminal window size");
					}
				}
#else
				{
				unsigned char buf[5];

					buf[0] = 0;
					buf[1] = w;
					buf[2] = w >> 8;
					buf[3] = h;
					buf[4] = h >> 8;
					/* send a resize request to the remote host */
					if (write(xdata.comm_fd, buf, sizeof buf) != sizeof buf)
					{
						XCloseDisplay(xdata.disp);
						perror("write");
						exit(1);
					}
				}
#endif /* LOCAL_TERM */
				}
					break;
				case ConfigureRequest:
				{
					XConfigureRequestEvent * cevt;
					cevt = (XConfigureRequestEvent *) &xevt;
					printf("cfg request\n");
				}
					break;
			}
		}
		FD_ZERO(&descriptor_set);
		FD_SET(xdata.comm_fd, &descriptor_set);
		FD_SET(xdata.x_fd, &descriptor_set);

		if (xdata.tdata->must_refresh)
		{
			/* set up the timeout - if there
			 * is no new data until the timeout
		         * elapses - update the terminal
			 * window, otherwisee (new data arrived
			 * and the timeout has not elapsed -
			 * process the new data before updating
			 * the terminal window
			 *
			 * this is here for speeding up the
			 * updating of the terminal window */
			timeout.tv_sec = 0;
			timeout.tv_usec = 10000;
			ptimeout = &timeout;
		}
		else
			ptimeout = 0;
		if ((i = select(FD_SETSIZE, &descriptor_set, NULL, NULL, ptimeout)) < 0)
		{
			XCloseDisplay(xdata.disp);
			perror("select");
			exit(1);
		}
		/* see if the timeout has elapsed */
		if (i == 0)
		{
			/* refresh any lines marked for update */
			update_term_pixmap(&xdata);
			/* update the terminal window from the
			 * primary pixmap canvas */
			XCopyArea(xdata.disp,
					xdata.pixmap_canvas,
					xdata.win,
					xdata.gc,
					0, 0,
					xdata.tdata->con_width * xdata.font_width,
					xdata.tdata->con_height * xdata.font_height,
					0, 0);
			/* draw the cursor */
			XDrawRectangle(xdata.disp,
					xdata.win,
					xdata.ansi_color_gcs[7],
					xdata.tdata->cursor_x * xdata.font_width,
					xdata.tdata->cursor_y * xdata.font_height,
					xdata.font_width - 1,
					xdata.font_height - 1);
			xdata.tdata->must_refresh = false;
		}
		/* see if there are characters pending from
		 * the remote host */
		if (FD_ISSET(xdata.comm_fd, &descriptor_set))
		{
			unsigned char buf[128];
			int nr_bytes;
			if ((nr_bytes = read(xdata.comm_fd, buf, sizeof buf)) <= 0)
			{
				XCloseDisplay(xdata.disp);
				perror("read");
				exit(1);
			}
			if (0) if (write(0, buf, nr_bytes) != nr_bytes)
				;
			if (write(log_fd, buf, nr_bytes) != nr_bytes)
			{
				XCloseDisplay(xdata.disp);
				perror("write");
				exit(1);
			}
			for (i = 0; i < nr_bytes; i++)
				vt102_command_input_parser(vtstate, buf[i]);
		}
	}
}

