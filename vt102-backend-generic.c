/*!
 *	\file	vt102-backend-generic.c
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
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "vt102-backend-generic.h"

/*
 *
 * local function prototypes follow
 *
 */
static void handle_linefeed(struct term_data * tdata);
/*
 *
 * local functions follow
 *
 */


/*
 *
 * vt102 emulator backend interface functions
 *
 */ 

/*!
 *	\fn	static void move_cursor_relative(struct term_data * tdata, int dx, int dy)
 *	\brief	moves the cursor relative to its current position
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\param	dx	offset to move the cursor in the horizontal
 *			direction, relative to the current cursor
 *			position; positive values move the cursor
 *			right, negative values move the cursor left,
 *			a value of zero (0) does nothing; example
 *			values:
 *				1 - move the cursor one position
 *					to the right
 *				-1 - move the cursor one position
 *					to the left
 *				0 - do not move the cursor in the
 *					horizontal direction
 *	\param	dy	offset to move the cursor in the vertical
 *			direction, relative to the current cursor
 *			position; positive values move the cursor
 *			down, negative values move the cursor up,
 *			a value of zero (0) does nothing; example
 *			values:
 *				1 - move the cursor one position down
 *				-1 - move the cursor one position up
 *				0 - do not move the cursor in the
 *					vertical direction
 *			
 *	\return	none */
static void move_cursor_relative(struct term_data * tdata, int dx, int dy)
{
int prev_x, prev_y;

	prev_x = tdata->cursor_x;
	prev_y = tdata->cursor_y;

	tdata->cursor_x += dx;
	tdata->cursor_y += dy;
	/* normalize cursor position - make sure it is in bounds */
	if (tdata->cursor_x < 0)
		tdata->cursor_x = 0;
	if (tdata->cursor_y < tdata->margin_top)
		tdata->cursor_y = tdata->margin_top;
	if (tdata->cursor_x >= tdata->con_width)
		tdata->cursor_x = tdata->con_width - 1;
	if (tdata->cursor_y > tdata->margin_bottom)
		tdata->cursor_y = tdata->margin_bottom;

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void display_char(struct term_data * tdata, unsigned int ch, struct vt102_state * state)
 *	\brief	puts a character in the vt102 screen buffer (at the current cursor position)
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\param	ch	the input character to put in the buffer
 *			(at the current cursor position)
 *	\param	state	the vt102_state variable associated with this terminal backend;
 *			the 'state' pointer variable is needed because
 *			the display_char() routine may need to scroll
 *			the display (in case the cursor moves outside
 *			the display bounds), and thus may need to invoke
 *			the handle_linefeed() routine - which, however
 *			may have been overridden in the vt102_backend_ops
 *			function pointer table; passing in
 *			the vt102_state variable makes it possible to
 *			retrieve the actual vt102_backend_ops function
 *			pointer table in effect, and thus makes it possible
 *			that the correct handle_linefeed() routine to be
 *			invoked (in all, this is a bit of a hack and
 *			as such is somewhat ugly...)
 *	\return	none */
static void display_char(struct term_data * tdata, unsigned int ch, struct vt102_state * state)
{
int i, cx, cy;

	/* store the character and its associated graphics
	 * rendition data */ 
	if (tdata->cursor_x >= tdata->con_width)
	{
		*(int *)0 = 0;
	}
	if (tdata->cursor_y >= tdata->con_height)
	{
		*(int *)0 = 0;
	}

	cx = tdata->cursor_x;
	cy = tdata->cursor_y;

	tdata->chbuf[cy * tdata->con_width + cx] = ch;
	i = tdata->grbuf[cy * tdata->con_width + cx]
		= tdata->cur_fg_gc_idx | (tdata->cur_bg_gc_idx << 4);
	/* schedule this line for updating */
	tdata->must_refresh_line_buf[cy] = true;

	/* advance cursor */
        tdata->cursor_x ++;
        if (tdata->cursor_x == tdata->con_width)
        {
                tdata->cursor_x = 0;
                tdata->cursor_y ++;
                if (tdata->cursor_y == tdata->con_height)
                {
                        /*! \todo	not really needed... move_cursor_absolute()
                         *		invoked by handle_linefeed() below will take care
                         *		of this, but anyway, this is safer... */
                        tdata->cursor_y --;
                        /*! \todo	is this correct? i(sgs) think there is no problem with this... */
                        vt102_get_backend_ops(state)->handle_linefeed(tdata);
                }
                else
                        tdata->must_refresh_line_buf[cy + 1] = true;
        }
        tdata->must_refresh = true;
}


/*!
 *	\fn	static void move_cursor_absolute(struct term_data * tdata, int x, int y)
 *	\brief	sets the cursor to an absolute position in the screen
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\param	x	column (horizontal) position to move the cursor to
 *	\param	y	row (vertical) position to move the cursor to
 *	\return	none */
static void move_cursor_absolute(struct term_data * tdata, int x, int y)
{
int prev_x, prev_y;

	prev_x = tdata->cursor_x;
	prev_y = tdata->cursor_y;

	tdata->cursor_x = x;
	tdata->cursor_y = y;
	/* normalize cursor position - make sure it is in bounds */
	if (tdata->cursor_x < 0)
		tdata->cursor_x = 0;
	if (tdata->cursor_y < tdata->margin_top)
		tdata->cursor_y = tdata->margin_top;
	if (tdata->cursor_x > tdata->con_width - 1)
		tdata->cursor_x = tdata->con_width - 1;
	if (tdata->cursor_y > tdata->margin_bottom)
		tdata->cursor_y = tdata->margin_bottom;

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void move_cursor_column_absolute(struct term_data * tdata, int x)
 *	\brief	sets the cursor to an absolute position in the same row
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\param	x	column (horizontal) position to move the cursor to
 *	\return	none */
static void move_cursor_column_absolute(struct term_data * tdata, int x)
{
        move_cursor_absolute(tdata, x, tdata->cursor_y);
}

/*!
 *	\fn	static void erase_line_at_cursor(struct term_data * tdata)
 *	\brief	erases the contents of the line containing the cursor
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void erase_line_at_cursor(struct term_data * tdata)
{
	memset(tdata->chbuf + tdata->cursor_y * tdata->con_width, ' ', tdata->con_width);
	memset(tdata->grbuf + tdata->cursor_y * tdata->con_width, 0, tdata->con_width);
	tdata->must_refresh_line_buf[tdata->cursor_y] = true;

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void erase_line_from_beginning_to_cursor(struct term_data * tdata)
 *	\brief	erases the contents in the line containing the cursor,
 *		from the beginning of the cursor, to the cursor position
 *		(inclusive)
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void erase_line_from_beginning_to_cursor(struct term_data * tdata)
{
	memset(tdata->chbuf + tdata->cursor_y * tdata->con_width, ' ', tdata->cursor_x + 1);
	memset(tdata->grbuf + tdata->cursor_y * tdata->con_width, 0, tdata->cursor_x + 1);
	tdata->must_refresh_line_buf[tdata->cursor_y] = true;

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void erase_line_from_cursor_to_end(struct term_data * tdata)
 *	\brief	erases the contents in the line containing the cursor,
 *		from the cursor position, to the end of the line (inclusive)
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void erase_line_from_cursor_to_end(struct term_data * tdata)
{
	memset(tdata->chbuf + tdata->cursor_y * tdata->con_width + tdata->cursor_x, ' ', tdata->con_width - tdata->cursor_x);
	memset(tdata->grbuf + tdata->cursor_y * tdata->con_width + tdata->cursor_x, 0, tdata->con_width - tdata->cursor_x);
	tdata->must_refresh_line_buf[tdata->cursor_y] = true;

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void erase_display(struct term_data * tdata)
 *	\brief	erases the entire screen
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void erase_display(struct term_data * tdata)
{
	memset(tdata->chbuf, ' ', tdata->con_height * tdata->con_width);
	memset(tdata->grbuf, 0, tdata->con_height * tdata->con_width);
	memset(tdata->must_refresh_line_buf, true, tdata->con_height);

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void erase_display_from_beginning_to_cursor(struct term_data * tdata)
 *	\brief	erases the screen, from the top left screen position,
 *		to the current cursor position (inclusive)
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void erase_display_from_beginning_to_cursor(struct term_data * tdata)
{
	memset(tdata->chbuf, ' ', tdata->cursor_y * tdata->con_width + tdata->cursor_x + 1);
	memset(tdata->grbuf, 0, tdata->cursor_y * tdata->con_width + tdata->cursor_x + 1);
	memset(tdata->must_refresh_line_buf, true, tdata->cursor_y + 1);
	/* erase (the part of) the line containing the cursor */
	erase_line_from_beginning_to_cursor(tdata);

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void erase_display_from_cursor_to_end(struct term_data * tdata)
 *	\brief	erases the screen, from the current cursor position
 *		to the bottom right screen position (inclusive)
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void erase_display_from_cursor_to_end(struct term_data * tdata)
{
	memset(tdata->chbuf + tdata->cursor_y * tdata->con_width + tdata->cursor_x, ' ', tdata->con_width * tdata->con_height - (tdata->cursor_y * tdata->con_width + tdata->cursor_x));
	memset(tdata->grbuf + tdata->cursor_y * tdata->con_width + tdata->cursor_x, 0, tdata->con_width * tdata->con_height - (tdata->cursor_y * tdata->con_width + tdata->cursor_x));
	memset(tdata->must_refresh_line_buf + tdata->cursor_y, true, tdata->con_height - tdata->cursor_y);
	/* erase (the part of) the line containing the cursor */
	erase_line_from_cursor_to_end(tdata);

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void handle_backspace(struct term_data * tdata)
 *	\brief	handles a backspace character
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void handle_backspace(struct term_data * tdata)
{
	move_cursor_relative(tdata, -1, 0);

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void handle_horiz_tab(struct term_data * tdata, struct vt102_state * state)
 *	\brief	handles a horizontal tab character
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\param	state	the vt102_state variable associated with this terminal backend;
 *			the 'state' variable is needed because
 *			the handle_horiz_tab() routine may need to invoke
 *			the display_char() routine - which, however
 *			may have been overridden in the vt102_backend_ops
 *			function pointer table; passing in
 *			the vt102_state variable makes it possible to
 *			retrieve the actual vt102_backend_ops function
 *			pointer table in effect, and thus makes it possible
 *			that the correct display_char() routine be
 *			invoked (in all, this is a bit of a hack and
 *			as such is somewhat ugly...)
 *	\return	none */
static void handle_horiz_tab(struct term_data * tdata, struct vt102_state * state)
{
int i;
	i = ((tdata->cursor_x + 8) & ~ 7) - tdata->cursor_x;
	/* insert spaces */
	while (i--)
                vt102_get_backend_ops(state)->display_char(tdata, ' ', state);

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void handle_linefeed(struct term_data * tdata)
 *	\brief	handles a linefeed character
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void handle_linefeed(struct term_data * tdata)
{
	if (tdata->cursor_y == tdata->margin_bottom)
	{
		/* scroll up */
		memmove(tdata->chbuf + tdata->margin_top * tdata->con_width,
				tdata->chbuf + (tdata->margin_top + 1) * tdata->con_width,
				(tdata->margin_bottom - tdata->margin_top) * tdata->con_width);
		memmove(tdata->grbuf + tdata->margin_top * tdata->con_width,
				tdata->grbuf + (tdata->margin_top + 1) * tdata->con_width,
				(tdata->margin_bottom - tdata->margin_top) * tdata->con_width);
		memset(tdata->chbuf + tdata->margin_bottom * tdata->con_width, ' ', tdata->con_width);
		memset(tdata->grbuf + tdata->margin_bottom * tdata->con_width, 0, tdata->con_width);
		memset(tdata->must_refresh_line_buf + tdata->margin_top, 1,
				tdata->margin_bottom - tdata->margin_top + 1);
	}
	move_cursor_absolute(tdata, tdata->cursor_x, tdata->cursor_y + 1);

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void handle_carriage_return(struct term_data * tdata)
 *	\brief	handles a carriage return character
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void handle_carriage_return(struct term_data * tdata)
{
	move_cursor_absolute(tdata, 0, tdata->cursor_y);

	tdata->must_refresh = true;
}

/*!
 *	\fn	static void set_top_and_bottom_margins(struct term_data * tdata, int top, int bottom)
 *	\brief	sets the screen top and bottom margins (consult the
 *		vt102 manual for details)
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\param	top	top margin; must be in the range
 *			0 <= top <= bottom - 1
 *	\param	bottom	bottom margin; must be in the range
 *			top + 1 <= top <= con_height - 1
 *	\return	none */
static void set_top_and_bottom_margins(struct term_data * tdata, int top, int bottom)
{
	/* normalize parameters */
	if (top < 0)
		top = 0;
	if (top >= tdata->con_height - 2)
		top = tdata->con_height - 2;
	if (bottom <= top)
		bottom = top + 1;
	if (bottom >= tdata->con_height - 1)
		bottom = tdata->con_height - 1;
	tdata->margin_top = top;
	tdata->margin_bottom = bottom;
}

/*!
 *	\fn	static void insert_lines_at_cursor(struct term_data * tdata, int nr_lines)
 *	\brief	inserts a number of lines at (before) the line containing the cursor
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\param	nr_lines	the number of new lines to
 *				insert at (before) the line
 *				containing the cursor
 *	\return	none */
static void insert_lines_at_cursor(struct term_data * tdata, int nr_lines)
{
int i;
	if (!(tdata->margin_top <= tdata->cursor_y
			&& tdata->cursor_y <= tdata->margin_bottom))
		/* cursor outside of scrolling region - do nothing */
		return;
	/* scroll */
	/* compute destination line number */
	i = tdata->cursor_y + nr_lines;
	if (i > tdata->margin_bottom)
		nr_lines = tdata->margin_bottom - tdata->cursor_y + 1;
	else
	{
		/* move lines */
		memmove(tdata->chbuf + i * tdata->con_width,
				tdata->chbuf + tdata->cursor_y * tdata->con_width,
				(tdata->margin_bottom - i + 1) * tdata->con_width);
		memmove(tdata->grbuf + i * tdata->con_width,
				tdata->grbuf + tdata->cursor_y * tdata->con_width,
				(tdata->margin_bottom - i + 1) * tdata->con_width);
	}
	memset(tdata->chbuf + tdata->cursor_y * tdata->con_width, ' ', nr_lines * tdata->con_width);
	memset(tdata->grbuf + tdata->cursor_y * tdata->con_width, 0, nr_lines * tdata->con_width);
	memset(tdata->must_refresh_line_buf + tdata->cursor_y, 1,
			tdata->margin_bottom - tdata->cursor_y + 1);
	tdata->must_refresh = true;
}


/*!
 *	\fn	static void delete_lines_at_cursor(struct term_data * tdata, int nr_lines)
 *	\brief	deletes a number of lines starting at the line containing the cursor
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\param	nr_lines	the number of lines to delete
 *				starting at the line
 *				containing the cursor
 *	\return	none */
static void delete_lines_at_cursor(struct term_data * tdata, int nr_lines)
{
int i;
	if (!(tdata->margin_top <= tdata->cursor_y
			&& tdata->cursor_y <= tdata->margin_bottom))
		/* cursor outside of scrolling region - do nothing */
		return;
	/* scroll */
	/* compute source line number */
	i = tdata->cursor_y + nr_lines;
	if (i > tdata->margin_bottom)
		nr_lines = tdata->margin_bottom - tdata->cursor_y + 1;
	else
	{
		/* move lines */
		memmove(tdata->chbuf + tdata->cursor_y * tdata->con_width,
				tdata->chbuf + i * tdata->con_width,
				(tdata->margin_bottom - i + 1) * tdata->con_width);
		memmove(tdata->grbuf + tdata->cursor_y * tdata->con_width,
				tdata->grbuf + i * tdata->con_width,
				(tdata->margin_bottom - i + 1) * tdata->con_width);
	}
	memset(tdata->chbuf + (tdata->margin_bottom - nr_lines + 1) * tdata->con_width,
			' ',
			nr_lines * tdata->con_width);
	memset(tdata->grbuf + (tdata->margin_bottom - nr_lines + 1) * tdata->con_width,
			0,
			nr_lines * tdata->con_width);
	memset(tdata->must_refresh_line_buf + tdata->cursor_y, 1,
			tdata->margin_bottom - tdata->cursor_y + 1);
	tdata->must_refresh = true;
}
/*!
 *	\fn	static void delete_characters_at_cursor(struct term_data * tdata, int nr_characters)
 *	\brief	deletes characters at the cursor position, shifting characters to the left
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\param	nr_characters	the number of characters to delete
 *				starting at the screen position
 *				containing the cursor
 *	\return	none */
static void delete_characters_at_cursor(struct term_data * tdata, int nr_characters)
{
int i;

        if (nr_characters <= 0)
                return;
        i = tdata->con_width - tdata->cursor_x;
        if (nr_characters > i)
                nr_characters = i;
        memmove(tdata->chbuf + tdata->cursor_y * tdata->con_width + tdata->cursor_x,
                      tdata->chbuf + tdata->cursor_y * tdata->con_width + tdata->cursor_x + nr_characters,
                      i - nr_characters);
        memset(tdata->chbuf + (tdata->cursor_y + 1) * tdata->con_width - nr_characters,
                      ' ',
                      nr_characters);
        tdata->must_refresh_line_buf[tdata->cursor_y] = true;
        tdata->must_refresh = true;

}

/*!
 *	\fn	static void cursor_reverse_index(struct term_data * tdata)
 *	\brief	move cursor up one line in the same column - scroll if necessary
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void cursor_reverse_index(struct term_data * tdata)
{
	if (tdata->cursor_y == tdata->margin_top)
	{
		/* scroll down */
		memmove(tdata->chbuf + (tdata->margin_top + 1) * tdata->con_width,
				tdata->chbuf + tdata->margin_top * tdata->con_width,
				(tdata->margin_bottom - tdata->margin_top) * tdata->con_width);
		memmove(tdata->grbuf + (tdata->margin_top + 1) * tdata->con_width,
				tdata->grbuf + tdata->margin_top * tdata->con_width,
				(tdata->margin_bottom - tdata->margin_top) * tdata->con_width);
		memset(tdata->chbuf + tdata->margin_top * tdata->con_width, ' ', tdata->con_width);
		memset(tdata->grbuf + tdata->margin_top * tdata->con_width, ' ', tdata->con_width);

		memset(tdata->must_refresh_line_buf + tdata->margin_top, 1,
				tdata->margin_bottom - tdata->margin_top + 1);
	}
	move_cursor_relative(tdata, tdata->cursor_x, -1);

	tdata->must_refresh = true;
}

/*!
 *	\fn	void select_graphic_rendition(struct term_data * tdata, unsigned int * cmd_params, int nr_params)
 *	\brief	handles a graphic rendition command request
 *
 *	\note	this function is invoked by the vt102 terminal
 *		emulator command parser module
 *
 *	\note	consult the DEC vt102 and/or the ecma 048
 *		standard for details
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\param	cmd_params	a pointer to a buffer holding the
 *				parameters for this command
 *	\param	nr_params	the number of parameters in the
 *				cmd_params buffer
 *	\return	none */
void select_graphic_rendition(struct term_data * tdata, unsigned int * cmd_params, int nr_params)
{
int i, t;

	for (i = 0; i < nr_params; i++)
	{
		switch (cmd_params[i])
		{
			case 0:
				/* revert to default */
				tdata->cur_fg_gc_idx = 7;
				tdata->cur_bg_gc_idx = 0;
				break;
			case 30 ... 37:
				t = cmd_params[i] - 30;
				tdata->cur_fg_gc_idx = t;
				break;
			case 39:
                                tdata->cur_fg_gc_idx = 7;
				break;
			case 40 ... 47:
				t = cmd_params[i] - 40;
				tdata->cur_bg_gc_idx = t;
				break;
			case 49:
				tdata->cur_bg_gc_idx = 0;
				break;
                        case 7:
                                /* negative image */
                                t = tdata->cur_bg_gc_idx;
                                tdata->cur_bg_gc_idx = tdata->cur_fg_gc_idx;
                                tdata->cur_fg_gc_idx = t;
                        default:
                                fprintf(stderr, "sgr: %i\n", cmd_params[i]);
		}
	}
}

/*!
 *	\fn	static void destroy_vt102_generic_backend(struct vt102_state * state)
 *	\brief	destroys a generic vt102 terminal emulator backend instance
 *
 *	\param	tdata	a pointer to the term_data
 *			structure holding the vt102 screen state
 *	\return	none */
static void destroy_vt102_generic_backend(struct term_data * tdata)
{
        /* just deallocate memory buffers malloc()-ed... */
        free(tdata->chbuf);
        free(tdata->grbuf);
        free(tdata->must_refresh_line_buf);
}

/*
 *
 * exported functions follow
 *
 */ 

/*!
 *	\fn	struct vt102_state * init_vt102_generic_backend(void)
 *	\brief	initializes the generic vt102 terminal emulator backend
 *
 *	\param	width	initial terminal screen width
 *	\param	height	initial terminal screen height
 *	\return	a pointer to the vt102 command parser associated
 *		with this backend instance, or 0 on error */
struct vt102_state * init_vt102_generic_backend(int width, int height)
{
struct vt102_backend_ops backend_ops =
{
	.display_char = display_char,
	.move_cursor_relative = move_cursor_relative,
	.move_cursor_absolute = move_cursor_absolute,
        .move_cursor_column_absolute = move_cursor_column_absolute,
        .erase_line_at_cursor = erase_line_at_cursor,
	.erase_line_from_beginning_to_cursor = erase_line_from_beginning_to_cursor,
	.erase_line_from_cursor_to_end = erase_line_from_cursor_to_end,
	.erase_display = erase_display,
	.erase_display_from_beginning_to_cursor = erase_display_from_beginning_to_cursor,
	.erase_display_from_cursor_to_end = erase_display_from_cursor_to_end,
	.handle_backspace = handle_backspace,
	.handle_horiz_tab = handle_horiz_tab,
	.handle_linefeed = handle_linefeed,
	.handle_carriage_return = handle_carriage_return,
	.set_top_and_bottom_margins = set_top_and_bottom_margins,
	/* this routine must be provided by another module */
	.query_terminal_id = 0,
	.insert_lines_at_cursor = insert_lines_at_cursor,
	.delete_lines_at_cursor = delete_lines_at_cursor,
        .delete_characters_at_cursor = delete_characters_at_cursor,
        .cursor_reverse_index = cursor_reverse_index,
	.select_graphic_rendition = select_graphic_rendition,
        .destroy_vt102_generic_backend = destroy_vt102_generic_backend,
};
struct vt102_state * vtstate;
struct term_data * tdata;

	/* sanity checks */
	if (width < NR_MIN_VT102_SCREEN_COLUMNS)
		width = NR_MIN_VT102_SCREEN_COLUMNS;
	if (height < NR_MIN_VT102_SCREEN_ROWS)
		height = NR_MIN_VT102_SCREEN_ROWS;

	if (!(tdata = calloc(1, sizeof * tdata)))
		return 0;

	/* initialize the main console variables */
	tdata->con_width = width;
	tdata->con_height = height;
	if (!(tdata->chbuf = malloc(tdata->con_width * tdata->con_height)))
	{
		printf("no core\n");
		exit(1);
	}
	if (!(tdata->grbuf = malloc(tdata->con_width * tdata->con_height)))
	{
		printf("no core\n");
		exit(1);
	}
	if (!(tdata->must_refresh_line_buf = malloc(tdata->con_height * sizeof(bool))))
	{
		printf("no core\n");
		exit(1);
	}
	memset(tdata->chbuf, 'E', tdata->con_width * tdata->con_height);
	memset(tdata->grbuf, 0, tdata->con_width * tdata->con_height);
	/*! \todo	this is broken */
	memset(tdata->must_refresh_line_buf, true, tdata->con_height * sizeof(bool));
	tdata->cursor_x = tdata->cursor_y = 0;
	tdata->margin_top = 0;
	tdata->margin_bottom = tdata->con_height - 1;

	tdata->cur_fg_gc_idx = 7;
	tdata->cur_bg_gc_idx = 0;
	tdata->must_refresh = true;

	/* initialize the vt102 emulator command parser state machine */
	backend_ops.param = tdata;
	vtstate = init_vt102(&backend_ops);
	if (!vtstate)
	{
		printf("no core\n");
		exit(1);
	}
	return vtstate;

}

/*!
 *	\fn	struct term_data * vt102_generic_backend_get_data(struct vt102_state * state);
 *	\brief	given a vt102 terminal emulator state variable, returns the term_data variable associated with it
 *
 *	\note	the primary use of this function is for a
 *		rendering module to obtain the screen buffer
 *		contents in order to produce a viewable image
 *		of the vt102 terminal screen
 *
 *	\param	state	the state variable of the vt102
 *			terminal emulator command parser
 *	\return	a pointer to the term_data structure containing
 *		the vt102 terminal screen state */
struct term_data * vt102_generic_backend_get_data(struct vt102_state * state)
{
	return (struct term_data *) vt102_get_backend_ops(state)->param;
}


/*!
 *	\fn	void vt102_generic_backend_resize_buffers(struct vt102_state * state, int new_width, int new_height);
 *	\brief	changes the screen dimensions (number of rows and columns) of a vt102 terminal screen
 *
 *	\param	state	the state variable of the vt102
 *			terminal emulator command parser
 *	\param	new_width	the new width of the vt102 terminal screen
 *	\param	new_height	the new height of the vt102 terminal screen
 *	\return	none */
void vt102_generic_backend_resize_buffers(struct vt102_state * state, int new_width, int new_height)
{
struct term_data * tdata;
char * chbuf, * grbuf;
int i, w, h;

	/* sanity checks */
	if (new_width < NR_MIN_VT102_SCREEN_COLUMNS)
		new_width = NR_MIN_VT102_SCREEN_COLUMNS;
	if (new_height < NR_MIN_VT102_SCREEN_ROWS)
		new_height = NR_MIN_VT102_SCREEN_ROWS;

	tdata = vt102_generic_backend_get_data(state);

	if (!(chbuf = malloc(new_width * new_height * sizeof * chbuf)))
	{
		printf("no core\n");
		exit(1);
	}
	if (!(grbuf = malloc(new_width * new_height * sizeof * grbuf)))
	{
		printf("no core\n");
		exit(1);
	}
	if (!(tdata->must_refresh_line_buf = realloc(tdata->must_refresh_line_buf, new_height * sizeof(bool))))
	{
		printf("no core\n");
		exit(1);
	}
	memset(chbuf, ' ', new_width * new_height);
	memset(grbuf, 0, new_width * new_height);

	/* this below is a (sorry) attempt to retain the previous
	 * console window contents... */
	w = (tdata->con_width > new_width) ? new_width : tdata->con_width;
	h = (tdata->con_height > new_height) ? new_height : tdata->con_height;

	for (i = 0; i < h; i++)
	{
		memcpy(chbuf + i * new_width, tdata->chbuf + i * tdata->con_width, w);
		memcpy(grbuf + i * new_width, tdata->grbuf + i * tdata->con_width, w);
	}

	free(tdata->chbuf);
	free(tdata->grbuf);

	tdata->chbuf = chbuf;
	tdata->grbuf = grbuf;

	memset(tdata->must_refresh_line_buf, true, new_height * sizeof(bool));

	tdata->must_refresh = true;

	tdata->con_width = new_width;
	tdata->con_height = new_height;

	//tdata->cursor_x = tdata->cursor_y = 0;
	if (tdata->cursor_x >= tdata->con_width)
		tdata->cursor_x = tdata->con_width - 1;
	if (tdata->cursor_y >= tdata->con_height)
		tdata->cursor_y = tdata->con_height - 1;
	tdata->margin_top = 0;
	tdata->margin_bottom = tdata->con_height - 1;
}
