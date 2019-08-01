/*!
 *	\file	vt102.h
 *	\brief	generic DEC vt102 terminal emulator command parser header file
 *	\author	shopov
 *
 *	\note	even thought the DEC manual states that the
 *		screen home position is at line 1, column 1 -
 *		(1; 1) - here, it is assumed that the screen
 *		home position is at coordinate (0; 0)
 *
 *	\note	the line and column numbers start counting from zero
 *
 *	Revision summary:
 *
 *	$Log: $
 */

/*
 *
 * opaque data types follow
 *
 */
struct vt102_state;

/*
 *
 * exported data types follow
 *
 */

/*! a structure describing the functional interface to a backend */
struct vt102_backend_ops
{
	/*! a generic parameter passed as the first parameter of all the functions within this data structure
	 *
	 * the value of this parameter must be set appropriately
	 * by the caller when initializing the vt102 terminal
	 * emulator when calling the init_vt102() function;
	 * this is used as a generic parameter passed to the
	 * backend interface functions */
	void * param;
	/*
	 *
	 * routines for character displaying
	 *
	 */
	/*! display a character (character is not a control character), move the cursor one position to the right
	 *
	 * the 'ch' parameter holds the character to display,
         * it will be a displayable (non-control) character
         *
         * \note	the 'state' pointer below is needed because
         *		the display_char() routine may need to scroll
         *		the display (in case the cursor moves outside
         *		the display bounds), and thus may need to invoke
         *		the handle_linefeed() routine - which, however
         *		may have been overridden in the vt102_backend_ops
         *		function pointer table (this table); passing in
         *		the vt102_state variable makes it possible to
         *		retrieve the actual vt102_backend_ops function
         *		pointer table in effect, and thus makes it possible
         *		that the correct handle_linefeed() routine be
         *		invoked (in all, this is a bit of a hack and
         *		as such is somewhat ugly...) */
        void (*display_char)(void * param, unsigned int ch, struct vt102_state * state);
	/*! SGR - select graphic rendition
	 * 
	 * selecting an attribute does not turn off other
	 * attributes already selected; after you select
	 * an attribute, all characters received by the
	 * terminal appear with that attribute;
	 * if you move the characters by scrolling,
	 * the attribute moves with the characters;
	 * for complete details on the parameters for this
	 * command, consult the ecma 048 standard
	 *
	 * in particular, these are some of the selection
	 * parameters:
	 *
	 *	30 black display
	 *	31 red display
	 *	32 green display
	 *	33 yellow display
	 *	34 blue display
	 *	35 magenta display
	 *	36 cyan display
	 *	37 white display
	 *	38 reserved
	 *	39 revert to default foreground color
	 *	40 black background
	 *	41 red background
	 *	42 green background
	 *	43 yellow background
	 *	44 blue background
	 *	45 magenta background
	 *	46 cyan background
	 *	47 white background
	 *	48 reserved
	 *	49 revert to default background color */
	void (*select_graphic_rendition)(void * param, unsigned int * cmd_params, int nr_params);
	/*
	 *
	 * routines for controlling the cursor
	 *
	 */
	/*! \note	even thought the DEC manual states that the
	 *		screen home position is at line 1, column 1 -
	 *		(1; 1) - here, it is assumed that the screen
	 *		home position is at coordinate (0; 0)
 	 * \note	the line and column numbers start counting from zero
	 *
	 * from the DEC manual:

		Cursor Positioning

		The cursor indicates the active screen position where the next character will appear. The cursor moves:

		    * One column to the right when a character appears
		    * One line down after a linefeed (LF, octal 012), form feed (FF, octal 014) or vertical tab (VT, octal 013) (Linefeed/new line may also move the cursor to the left margin)
		    * To the left margin after a carriage return (CR, octal 015)
		    * One column to the left after a backspace (BS, octal 010)
		    * To the next tab stop (or right margin if no tabs are set) after a horizontal tab character (HT, octal 011)
		    * To the home position when the top and bottom margins of the scrolling region (DECSTBM) or origin mode (DECOM) selection changes.
	*/	   

        /*! move cursor relative to the current position
	 *
	 * dx and dy hold the number of character positions
	 * to move away the cursor from its current position;
	 *	- positive values for dx move the cursor right
	 *	- positive values for dy move the cursor down
	 *	- negative values for dx move the cursor left
	 *	- negative values for dy move the cursor up */
	void (*move_cursor_relative)(void * param, int dx, int dy);
        /*! move cursor to absolute position (x; y) (screen home is assumed to be at (0; 0)) */
	void (*move_cursor_absolute)(void * param, int x, int y);
        /*! move cursor to absolute position x in the current row (first column is at index 0) */
        void (*move_cursor_column_absolute)(void * param, int x);
        /*! RI - reverse index; move cursor up one line in the same column - scroll if necessary */
	void (*cursor_reverse_index)(void * param);

	/*
	 *
	 * routines for erasing in line and in display
	 *
	 */
	/* from the DEC vt102 manual:

		Erasing removes characters from the screen without affecting other characters on the screen. Erased characters are lost. The cursor position does not change when erasing characters or lines.

		If you erase a line by using the erase in display (ED) sequence, the line attribute becomes single-height, single-width. If you erase a line by using the erase in line (EL) sequence, the line attribute is not affected.

		Erasing a character also erases any character attribute of the character.
	*/
	/*! erase the entire line at the cursor position */
	void (*erase_line_at_cursor)(void * param);
	/*! erase in the line containing the cursor, from the beginning of the line to the cursor position (inclusive) */
	void (*erase_line_from_beginning_to_cursor)(void * param);
	/*! erase in the line containing the cursor, from the cursor position to the end of the line (inclusive) */
	void (*erase_line_from_cursor_to_end)(void * param);

	/*! erase the entire screen */
	void (*erase_display)(void * param);
	/*! erase in display, from the beginning of the screen to the cursor position (inclusive) */
	void (*erase_display_from_beginning_to_cursor)(void * param);
	/*! erase in display, from the cursor position to the end of the screen (inclusive) */
	void (*erase_display_from_cursor_to_end)(void * param);

	/*
	 *
         * routines for adding/deleting lines and other editing
         * functions
	 *
	 */
	/*! insert lines starting at the line containing the cursor
	 *
	 * inserts nr_lines lines at line with cursor; lines displayed
	 * below cursor move down; lines moved past the bottom margin
	 * are lost; this sequence is ignored when cursor is outside
	 * scrolling region. */
	void (*insert_lines_at_cursor)(void * param, int nr_lines);
	/*! delete lines starting at the line containing the cursor
	 *
	 * deletes nr_lines lines, starting at line with cursor;
	 * as lines are deleted, lines displayed below cursor move up;
	 * lines added to bottom of screen have spaces with same
	 * character attributes as last line moved up;
	 * this sequence is ignored when cursor is outside
	 * scrolling region */
	void (*delete_lines_at_cursor)(void * param, int nr_lines);
        /*! delete characters at the cursor position
         *
         * deletes nr_characters characters, starting with the character at the
         * cursor position; when a character is deleted, all characters to the
         * right of cursor move left; this creates a space character at the right
         * margin; this character has the same character attribute as the
         * last character moved left */
        void (*delete_characters_at_cursor)(void * param, int nr_characters);

	/*
	 *
	 * routines for processing received control characters
	 *
	 */
	/*! handle a backspace character received */
	void (*handle_backspace)(void * param);
        /*! handle a horizontal tab character received
         *
         * \note	the 'state' pointer below is needed because
         *		the handle_horiz_tab() routine may need to invoke
         *		the display_char() routine - which, however
         *		may have been overridden in the vt102_backend_ops
         *		function pointer table (this table); passing in
         *		the vt102_state variable makes it possible to
         *		retrieve the actual vt102_backend_ops function
         *		pointer table in effect, and thus makes it possible
         *		that the correct display_char() routine be
         *		invoked (in all, this is a bit of a hack and
         *		as such is somewhat ugly...) */
        void (*handle_horiz_tab)(void * param, struct vt102_state * state);
	/*! handle a linefeed character received */
	void (*handle_linefeed)(void * param);
	/*! handle a carriage return character received */
	void (*handle_carriage_return)(void * param);

	/*
	 *
	 * routines for controlling the terminal settings
	 *
	 */
	/*! select the top and bottom margin line numbers (counting starts from zero) */
	void (*set_top_and_bottom_margins)(void * param, int top, int bottom);
	/*! a DA (device attributes) command has been received
	 *
	 *	\todo	maybe remove this altogether
	 *
	 * the backend must answer with the string identifying the
	 * terminal; for the vt102, this must be the string:
	 *
	 * ESC  [   ?   6   c
	 *(033 133 077 066 143)
	 */
	void (*query_terminal_id)(void * param);
        /*
         *
         * maintenance routines
         *
         */
        void (*destroy_vt102_generic_backend)(struct vt102_state * state);
};

/*
 *
 * exported function prototypes follow
 *
 */

void vt102_command_input_parser(struct vt102_state * state, unsigned int input_char);
struct vt102_backend_ops * vt102_get_backend_ops(struct vt102_state * state);
struct vt102_state * init_vt102(struct vt102_backend_ops * backend_ops);
void destroy_vt102(struct vt102_state * state);

