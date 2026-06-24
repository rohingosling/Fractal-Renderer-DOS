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

#ifndef _VESA_GRAPHICS
#define _VESA_GRAPHICS

//----------------------------------------------------------------------------
// Typedefs
//----------------------------------------------------------------------------

typedef unsigned char BYTE;
typedef unsigned      WORD;

//----------------------------------------------------------------------------
// VESA Mode Constants
//----------------------------------------------------------------------------

#define VESA_MODE_640x400    0x0100
#define VESA_MODE_640x480    0x0101
#define VESA_MODE_800x600    0x0103
#define VESA_MODE_1024x768   0x0105

//----------------------------------------------------------------------------
// Text Mode Row Constants
//----------------------------------------------------------------------------

#define TEXT_MODE_25_ROWS    25
#define TEXT_MODE_43_ROWS    43
#define TEXT_MODE_50_ROWS    50

//----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// Mode Functions

int  SetVesaMode ( WORD mode );
void SetTextMode ( BYTE rows );

// Palette Functions

void SetPalette ( BYTE start, WORD count, WORD segment, WORD data_offset );

// Drawing Functions

void PutPixel             ( int x,  int y,  BYTE color );
BYTE GetPixel             ( int x,  int y );
void FillRectangle        ( int x0, int y0, int x1, int y1, BYTE color );
void DrawRectangleOutline ( int x0, int y0, int x1, int y1, BYTE color, int thickness );
void DrawText             ( int x,  int y,  const char *text, BYTE color, int scale );

// Utility Functions

void WaitRetrace    ( void );

#ifdef __cplusplus
}
#endif

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
