/*!
 *	\file	vt102-backend-generic.h
 *	\brief	generic DEC vt102 terminal emulator backend driver
 *	\author	shopov
 *
 *	\note	even thought the DEC manual states that the
 *		screen home position is at line 1, column 1 -
 *		(1; 1) - here, it is assumed that the screen
 *		home position is at coordinate (0; 0)
 *
 *	this is a simple, generic backend driver that can be
 *	used alongside with the vt102.c terminal emulator command
 *	parser module and a graphics rendering module (to render the vt102
 *	display character data to some physical device, a file, etc.);
 *	no rendering to a physical device is done by this driver,
 *	its purpose is only to maintain an up-to-date state of
 *	a vt102 display state, including:
 *		- the screen cursor position and screen dimensions
 *			(width and height)
 *		- the screen character data contents - including
 *			both character codes and character graphic
 *			rendition attributes (foreground and background
 *			color)
 *		- flags denoting which lines should be refreshed by
 *			a renderer utilizing this module - note that
 *			these flags must be reset by the rendering
 *			module utilizing this module when it is
 *			done with the rendering of the data
 *
 *	\note	the line and column numbers start counting from zero
 *
 *	Revision summary:
 *
 *	$Log: $
 */

/*
 *
 * include section follows
 *
 */
#include "vt102.h"

/*
 *
 * exported constants follow
 *
 */

/*! general screen dimension limits */
enum 
{
	/*! minimum terminal window width (columns) */
	NR_MIN_VT102_SCREEN_COLUMNS	=	10,
	/*! minimum terminal window height (rows) */
        NR_MIN_VT102_SCREEN_ROWS	=	2,
};

/*
 *
 * exported data types follow
 *
 */

/*! the data structure holding the generic backend state
 *
 *
 * below, when referring to colors, the ansi color
 * indices are enumerated like this:
 * 0 - black
 * 1 - red
 * 2 - green
 * 3 - yellow
 * 4 - blue
 * 5 - magenta
 * 6 - cyan
 * 7 - white */
struct term_data
{
        /*! a pointer intended for generic use for linking data to this structure
         *
         * the vt102-backend-generic module does not use this, it is available
         * for use by code utilizing the vt102 and vt102-backend-generic
         * modules; its use is arbitrary, and - for example - can be used by
         * c++ code for storing pointers to c++ objects to be made available
         * to c++ routines invoked via the 'struct vt102_backend_ops' function
         * pointer table interface (this is the approved mechanism for hooking and
         * overriding vt102 backend functions) - in this case this pointer
         * is needed because only static c++ functions can be linked in
         * this table, so this generic pointer is used for passing the
         * otherwise unavailable 'this' pointer to these static c++ functions */
        void * generic_ptr;
	/*! currently selected foreground graphic rendition color index */
	int cur_fg_gc_idx;
	/*! currently selected background graphic rendition color index */
	int cur_bg_gc_idx;
	/*! console window width */
	int con_width;
	/*! console window height */
	int con_height;
	/*! the terminal character buffer
	 * 
	 * this must be of size
	 * con_width * con_height bytes (holds all
	 * characters on the screen) */
	unsigned char * chbuf;
	/*! the terminal character graphics rendition buffer
	 *
       	 * must be the same size as the terminal character
	 * buffer (chbuf) above (i.e. con_width * con_height elements);
	 * it holds, for each character,
	 * its associated graphics rendition attributes
	 *
	 * the bytes stored in this buffer have the
	 * following format:
	 *	- bits [0:3] - character foreground color index (see above)
	 *	- bits [4:7] - character background color index (see above) */
	unsigned char * grbuf;
	/*! cursor column (x) position - counting from 0 */
	int cursor_x;
	/*! cursor roy (y) position - counting from 0 */
	int cursor_y;
	/*! top screen row margin; consult the dec vt102 manual for details */
	int margin_top;
	/*! bottom screen row margin; consult the dec vt102 manual for details */
	int margin_bottom;
	/*! screen refresh-needed flag
	 *
	 * if true, the terminal window must be refreshed;
	 * this is set to true by the backend interface
         * functions when the terminal window must be
	 * refreshed and must be reset to false
	 * by the external rendering module when done with
	 * rendering the text */
	bool must_refresh;
	/*! row refresh-needed flags
	 * 
	 * a buffer, holding - for each line, if it
	 * must be refreshed in the terminal window when
         * updating the terminal window; this buffer has
	 * con_height number of entries;
	 * these must be reset by the external rendering
	 * module when done with the rendering the text */
	bool * must_refresh_line_buf;
};

/*
 *
 * exported function prototypes follow
 *
 */ 
struct term_data * vt102_generic_backend_get_data(struct vt102_state * state);
void vt102_generic_backend_resize_buffers(struct vt102_state * state, int new_width, int new_height);
struct vt102_state * init_vt102_generic_backend(int width, int height);
