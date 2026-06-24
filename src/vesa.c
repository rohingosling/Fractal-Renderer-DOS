//****************************************************************************
// Program: VESA Graphics Library
// Version: 1.0
// Date:    1992-04-12
// Author:  Rohin Gosling
//
// Description:
//
//   Simple VESA SVGA graphics library.
//
//   - Provides VESA mode setting, pixel plotting with automatic bank
//     switching, rectangle filling, text rendering using the BIOS ROM font,
//     palette control, and vertical retrace synchronization.
//
//   - Uses a 64 KB sliding window at segment A000h, switching banks via
//     VESA VBE function 05h as needed.
//
//   - Text rendering uses the BIOS ROM 8x16 font, accessed through INT 10h
//     function 1130h.
//
//   - Compile with Borland Turbo C++ 3.1 and a large memory model.
//
// NOTE:
//
//   I have been having some difficulty getting text to display correctly.
//
// TO-DO:
//
//  1. Fix text display issue.  
//
//****************************************************************************

#include <dos.h>
#include "vesa.h"

//----------------------------------------------------------------------------
// File-Scope Variables
//----------------------------------------------------------------------------

static WORD      bytes_per_scanline =  0;		// Bytes per horizontal scanline (from VBE mode info)
static int       current_bank       = -1;		// Currently active 64 KB bank (-1 = none selected)
static BYTE far *bios_font          =  0;		// Far pointer to the BIOS ROM 8x16 font table

//----------------------------------------------------------------------------
// Function: InitBiosFont
//
// Description:
//
//   Retrieves the pointer to the BIOS ROM 8x16 font table by calling
//   INT 10h AX=1130h with BH=06h. 
//
//   The font covers all 256 characters,
//   each defined by 16 bytes (one byte per scanline row, MSB = leftmost
//   pixel).
//
//   Called automatically by SetVesaMode after the video mode is set.
//
// Arguments:
//
//   - None.
//
// Returns:
//
//   - None. Sets the file-scope bios_font pointer.
//
//----------------------------------------------------------------------------

static void InitBiosFont ( void )
{
	WORD font_segment;		// Segment address of the ROM font table
	WORD font_offset;		// Offset address of the ROM font table

	// Call INT 10h AX=1130h BH=06h to retrieve the 8x16 font pointer.

	asm {

		PUSH    ES
		PUSH    BP
		MOV     AX, 0x1130
		MOV     BH, 0x06
		INT     0x10
		MOV     [font_offset],  BP
		MOV     [font_segment], ES
		POP     BP
		POP     ES
	}

	// Construct the far pointer from the returned segment and offset.

	bios_font = (BYTE far *) MK_FP ( font_segment, font_offset );
}

//----------------------------------------------------------------------------
// Function: SetVesaMode
//
// Description:
//
//   Sets a VESA SVGA video mode using VBE function 02h. Before setting the
//   mode, queries the mode information block (VBE function 01h) to obtain
//   the bytes-per-scanline value needed for pixel offset calculations.
//
//   Assumes 64 KB window granularity, which is standard for most SVGA
//   adapters and DOSBox.
//
// Arguments:
//
//   - mode : VESA mode number (e.g. 0x0105 for 1024x768x256).
//
// Returns:
//
//   - 1 on success, 0 on failure.
//
//----------------------------------------------------------------------------

int SetVesaMode ( WORD mode )
{
	static unsigned char mode_info [ 256 ];		// VBE mode information block (256 bytes)

	WORD info_segment;							// Segment address of mode_info buffer
	WORD info_offset;							// Offset address of mode_info buffer
	WORD result;								// VBE function return code

	// Decompose the mode_info buffer address into segment and offset.

	info_segment = FP_SEG ( (void far *) mode_info );
	info_offset  = FP_OFF ( (void far *) mode_info );

	// Query VESA mode information (VBE function 01h).
	//
	// INT 10h  AX = 4F01h
	//          CX = mode number
	//       ES:DI = pointer to 256-byte mode info buffer
	//
	// Returns  AX = 004Fh on success.

	asm {
		PUSH    ES
		MOV     AX, [info_segment]
		MOV     ES, AX
		MOV     DI, [info_offset]
		MOV     AX, 0x4F01
		MOV     CX, [mode]
		INT     0x10
		MOV     [result], AX
		POP     ES
	}

	if ( result != 0x004F ) return 0;

	// Extract bytes per scanline from offset 10h of the mode info block.

	bytes_per_scanline = *( (WORD *) ( mode_info + 0x10 ) );

	// Set the VESA mode (VBE function 02h).
	//
	// INT 10h  AX = 4F02h
	//          BX = mode number
	//
	// Returns  AX = 004Fh on success.

	asm {
		MOV     AX, 0x4F02
		MOV     BX, [mode]
		INT     0x10
		MOV     [result], AX
	}

	if ( result != 0x004F ) return 0;

	// Reset bank tracking and initialize the BIOS font pointer.

	current_bank = -1;

	InitBiosFont ();

	// Mode set successfully.

	return 1;
}

//----------------------------------------------------------------------------
// Function: SetTextMode
//
// Description:
//
//   Sets the video adapter to 80-column text mode (BIOS mode 3) with a
//   configurable number of text rows.
//
//   - 25 rows: Standard VGA text mode with the default 8x16 ROM font.
//              (400 scanlines / 16 pixels per character = 25 rows.)
//
//   - 43 rows: EGA-compatible extended text mode. Selects 350 vertical
//              scanlines via INT 10h AX=1201h BL=30h, then loads the 8x8
//              ROM font via INT 10h AX=1112h.
//              (350 scanlines / 8 pixels per character = 43 rows.)
//
//   - 50 rows: VGA extended text mode. Uses the default 400 vertical
//              scanlines and loads the 8x8 ROM font via INT 10h AX=1112h.
//              (400 scanlines / 8 pixels per character = 50 rows.)
//
//   Any value other than 43 or 50 defaults to 25 rows.
//
// Arguments:
//
//   - rows : Number of text rows. Use the constants TEXT_MODE_25_ROWS (25),
//            TEXT_MODE_43_ROWS (43), or TEXT_MODE_50_ROWS (50).
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void SetTextMode ( BYTE rows )
{
	asm {

		// For 43-line mode, select 350 vertical scanlines before setting
		// the video mode.
		//
		// The VGA BIOS defaults to 400 scanlines, which would produce 50 
		// rows with an 8x8 font. By switching to 350 scanlines first, the 
		// subsequent 8x8 font load yields 43 rows.

		CMP     BYTE PTR [rows], 43
		JNE     SETTEXTMODE_SETMODE

		MOV     AX, 0x1201
		MOV     BL, 0x30
		INT     0x10

	} SETTEXTMODE_SETMODE: asm {

		// Set 80-column text mode (BIOS mode 3).

		MOV     AX, 0x0003
		INT     0x10

		// For 25-row mode we are done. For 43 or 50 rows, fall through
		// to load the 8x8 ROM font.

		CMP     BYTE PTR [rows], 25
		JE      SETTEXTMODE_DONE
		CMP     BYTE PTR [rows], 43
		JE      SETTEXTMODE_LOADFONT
		CMP     BYTE PTR [rows], 50
		JE      SETTEXTMODE_LOADFONT
		JMP     SETTEXTMODE_DONE

	} SETTEXTMODE_LOADFONT: asm {

		// Load the 8x8 ROM font into the active character generator.
		//
		// This halves the character cell height, doubling the number of
		// visible text rows (from 25 to 50 at 400 scanlines, or from
		// ~21 to 43 at 350 scanlines).

		MOV     AX, 0x1112
		MOV     BL, 0x00
		INT     0x10

	} SETTEXTMODE_DONE:;
}

//----------------------------------------------------------------------------
// Function: SetPalette
//
// Description:
//
//   Uploads a block of RGB color entries to the VGA DAC (Digital-to-Analog
//   Converter). Each entry consists of three 6-bit values (0-63) for the
//   red, green, and blue channels.
//
//   Writes the starting index to port 3C8h, then streams the RGB bytes
//   to port 3C9h using REP OUTSB for maximum throughput.
//
// Arguments:
//
//   - start   : First palette index to set (0-255).
//   - count   : Number of consecutive entries to write.
//   - segment : Segment address of the palette data array.
//   - offset  : Offset address of the palette data array.
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void SetPalette ( BYTE start, WORD count, WORD segment, WORD data_offset )
{
	asm {

		PUSH    DS

		// Write the starting palette index to the DAC write-address
		// register (port 3C8h).

		MOV     AL, [start]
		MOV     DX, 0x3C8
		OUT     DX, AL

		// Point DS:SI to the caller's palette data array.

		MOV     AX, [segment]
		MOV     SI, [data_offset]
		MOV     DS, AX

		// Compute the total number of bytes to stream: count * 3
		// (one byte each for red, green, and blue per entry).

		MOV     CX, [count]
		MOV     BX, CX
		SHL     CX, 1
		ADD     CX, BX

		// Stream the RGB data to the DAC data register (port 3C9h).

		MOV     DX, 0x3C9
		REP     OUTSB

		POP     DS
	}
}

//----------------------------------------------------------------------------
// Function: PutPixel
//
// Description:
//
//   Plots a single pixel at the given (x, y) coordinate in the current
//   VESA video mode. Computes the linear framebuffer offset, determines
//   which 64 KB bank contains the pixel, switches the VESA memory window
//   if necessary, and writes the color byte to video memory.
//
//   Bank switching is cached: the VESA window is only reprogrammed when
//   the pixel falls in a different bank than the previous call.
//
// Arguments:
//
//   - x     : Horizontal pixel coordinate.
//   - y     : Vertical pixel coordinate.
//   - color : Palette color index (0-255).
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void PutPixel ( int x, int y, BYTE color )
{
	long offset;			// Linear byte offset into the framebuffer
	int  bank;				// 64 KB bank number containing the pixel
	WORD window_offset;		// Byte offset within the active bank window
	long y_long;			// 32-bit y coordinate for framebuffer offset computation
	long x_long;			// 32-bit x coordinate for framebuffer offset computation

	// Calculate the linear byte offset into the framebuffer.

	y_long = y;
	x_long = x;
	offset = y_long * bytes_per_scanline + x_long;

	// Determine which 64 KB bank contains this offset, and the
	// position within that bank's window.

	bank          = (int) ( offset >> 16 );
	window_offset = (WORD) ( offset & 0xFFFFL );

	// Switch the VESA memory window if the pixel falls in a different
	// bank than the most recent access.
	//
	// VBE function 05h:
	//   AX = 4F05h
	//   BX = 0000h  (select window A)
	//   DX = bank number

	if ( bank != current_bank )
	{
		current_bank = bank;

		asm {
			MOV     AX, 0x4F05
			XOR     BX, BX
			MOV     DX, [bank]
			INT     0x10
		}
	}

	// Write the pixel color to video memory at A000:window_offset.

	asm {
		PUSH    ES
		MOV     BX, 0xA000
		MOV     ES, BX
		MOV     DI, [window_offset]
		MOV     AL, [color]
		STOSB
		POP     ES
	}
}

//----------------------------------------------------------------------------
// Function: GetPixel
//
// Description:
//
//   Reads the palette color index of a single pixel at the given (x, y)
//   coordinate in the current VESA video mode. Computes the linear
//   framebuffer offset, switches the VESA memory window if necessary,
//   and reads the color byte from video memory.
//
// Arguments:
//
//   - x : Horizontal pixel coordinate.
//   - y : Vertical pixel coordinate.
//
// Returns:
//
//   - Palette color index (0-255) of the pixel.
//
//----------------------------------------------------------------------------

BYTE GetPixel ( int x, int y )
{
	long offset;			// Linear byte offset into the framebuffer
	int  bank;				// 64 KB bank number containing the pixel
	WORD window_offset;		// Byte offset within the active bank window
	BYTE color;				// Palette index read from video memory
	long y_long;			// 32-bit y coordinate for framebuffer offset computation
	long x_long;			// 32-bit x coordinate for framebuffer offset computation

	// Calculate the linear byte offset into the framebuffer.

	y_long = y;
	x_long = x;
	offset = y_long * bytes_per_scanline + x_long;

	// Determine which 64 KB bank contains this offset, and the
	// position within that bank's window.

	bank          = (int) ( offset >> 16 );
	window_offset = (WORD) ( offset & 0xFFFFL );

	// Switch the VESA memory window if the pixel falls in a different
	// bank than the most recent access.

	if ( bank != current_bank )
	{
		current_bank = bank;

		asm {
			MOV     AX, 0x4F05
			XOR     BX, BX
			MOV     DX, [bank]
			INT     0x10
		}
	}

	// Read the pixel color from video memory at A000:window_offset.

	asm {
		PUSH    ES
		MOV     BX, 0xA000
		MOV     ES, BX
		MOV     BX, [window_offset]
		MOV     AL, ES:[BX]
		MOV     [color], AL
		POP     ES
	}

	return color;
}

//----------------------------------------------------------------------------
// Function: FillRectangle
//
// Description:
//
//   Fills a rectangular region of the screen with a solid color. The
//   rectangle is defined by its top-left (x0, y0) and bottom-right
//   (x1, y1) corners, inclusive.
//
// Arguments:
//
//   - x0    : Left edge of the rectangle.
//   - y0    : Top edge of the rectangle.
//   - x1    : Right edge of the rectangle (inclusive).
//   - y1    : Bottom edge of the rectangle (inclusive).
//   - color : Palette color index (0-255).
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void FillRectangle ( int x0, int y0, int x1, int y1, BYTE color )
{
	int x;	// Current horizontal pixel coordinate
	int y;	// Current vertical pixel coordinate

	// Fill the rectangle row by row, pixel by pixel.

	for ( y = y0; y <= y1; y++ )
	{
		for ( x = x0; x <= x1; x++ )
		{
			PutPixel ( x, y, color );
		}
	}
}

//----------------------------------------------------------------------------
// Function: DrawRectangleOutline
//
// Description:
//
//   Draws a hollow rectangle outline with a configurable border thickness.
//   The rectangle is defined by its top-left (x0, y0) and bottom-right
//   (x1, y1) corners, inclusive. The border is drawn inward from these
//   edges.
//
//   Uses four FillRectangle calls for the top, bottom, left, and right
//   border strips. The left and right strips are inset vertically by the
//   border thickness to avoid overlapping the corners already drawn by
//   the top and bottom strips.
//
// Arguments:
//
//   - x0        : Left edge of the rectangle.
//   - y0        : Top edge of the rectangle.
//   - x1        : Right edge of the rectangle (inclusive).
//   - y1        : Bottom edge of the rectangle (inclusive).
//   - color     : Palette color index (0-255).
//   - thickness : Border width in pixels.
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void DrawRectangleOutline ( int x0, int y0, int x1, int y1, BYTE color, int thickness )
{
	// Top border strip.

	FillRectangle ( x0, y0, x1, y0 + thickness - 1, color );

	// Bottom border strip.

	FillRectangle ( x0, y1 - thickness + 1, x1, y1, color );

	// Left border strip (inset to avoid overlapping corners).

	FillRectangle ( x0, y0 + thickness, x0 + thickness - 1, y1 - thickness, color );

	// Right border strip (inset to avoid overlapping corners).

	FillRectangle ( x1 - thickness + 1, y0 + thickness, x1, y1 - thickness, color );
}

//----------------------------------------------------------------------------
// Function: DrawText
//
// Description:
//
//   Draws a null-terminated text string on the graphics screen at the
//   given (x, y) position using the BIOS ROM 8x16 font. Each character
//   is rendered pixel-by-pixel: set bits in the font bitmap are drawn
//   in the specified color, and cleared bits are left transparent.
//
//   The scale parameter controls the size of each font pixel. At scale 1,
//   characters are 8x16 pixels (native font size). At scale 2, each font
//   pixel is drawn as a 2x2 block, producing 16x32 pixel characters.
//
//   Characters are spaced (8 * scale) pixels apart horizontally. No
//   clipping or line wrapping is performed.
//
// Arguments:
//
//   - x     : Horizontal pixel coordinate of the first character.
//   - y     : Vertical pixel coordinate of the top of the text.
//   - text  : Pointer to a null-terminated character string.
//   - color : Palette color index (0-255) for the text foreground.
//   - scale : Size multiplier (1 = native 8x16, 2 = 16x32, etc.).
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void DrawText ( int x, int y, const char *text, BYTE color, int scale )
{
	int           row;				// Current glyph row (0-15)
	int           column;			// Current glyph column (0-7)
	int           scale_x;			// Horizontal offset within the scaled pixel block
	int           scale_y;			// Vertical offset within the scaled pixel block
	BYTE far     *glyph;			// Pointer to the 16-byte bitmap for the current character
	BYTE          bits;				// Bit pattern for the current glyph row
	unsigned char character_code;	// Unsigned character code for glyph table lookup

	// Enforce a minimum scale of 1.

	if ( scale < 1 ) scale = 1;

	// Render each character in the string.

	while ( *text )
	{
		// Look up the 16-byte glyph bitmap for this character.

		character_code = *text;
		glyph = bios_font + ( character_code * 16 );

		// Render the 8x16 glyph one row at a time, scaling each
		// font pixel into a (scale x scale) block.

		for ( row = 0; row < 16; row++ )
		{
			// Read the bit pattern for this row of the glyph.

			bits = glyph [ row ];

			for ( column = 0; column < 8; column++ )
			{
				// Test if the current pixel in the glyph is set (MSB first).

				if ( bits & ( 0x80 >> column ) )
				{
					// Draw the scaled pixel block for this set bit.

					for ( scale_y = 0; scale_y < scale; scale_y++ )
					{
						for ( scale_x = 0; scale_x < scale; scale_x++ )
						{
							PutPixel
							(
								x + column * scale + scale_x,
								y + row * scale + scale_y,
								color
							);
						}
					}
				}
			}
		}

		// Advance to the next character position.

		x += 8 * scale;
		text++;
	}
}

//----------------------------------------------------------------------------
// Function: WaitRetrace
//
// Description:
//
//   Waits for the start of the next vertical retrace interval by polling
//   the VGA Input Status Register 1 (port 3DAh, bit 3).
//
//   First waits for any current retrace to finish, then waits for the
//   next retrace to begin. This synchronizes screen updates to the
//   display refresh to avoid visible tearing.
//
// Arguments:
//
//   - None.
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void WaitRetrace ( void )
{
	asm MOV     DX, 0x3DA

WAITRETRACE_ACTIVE:;

	asm IN      AL, DX
	asm TEST    AL, 0x08
	asm JNZ     WAITRETRACE_ACTIVE

WAITRETRACE_IDLE:;

	asm IN      AL, DX
	asm TEST    AL, 0x08
	asm JZ      WAITRETRACE_IDLE
}

//----------------------------------------------------------------------------
