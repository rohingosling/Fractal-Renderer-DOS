//****************************************************************************
// Program: Mandelbrot Set Renderer
// Version: 3.3
// Date:    1992-07-20
// Author:  Rohin Gosling
//
// Description:
//
//   Application class for the Mandelbrot set renderer. Sets VESA SVGA
//   mode 105h (1024x768, 256 colors), generates a fractal-optimized
//   color palette, and renders the full Mandelbrot set using the
//   escape-time algorithm.
//
// Features
//
//   - Smooth iteration count antialiasing, which uses the normalized 
//     iteration count formula to produce continuous color values that 
//     eliminate banding at iteration boundaries:
//
//                    log ( log ( |z| ) )
//     mu = n + 1  -  -------------------
//                         log ( 2 )  
//
//   - Supersampling anti-aliasing (2x2 through 5x5), which takes multiple 
//     sub-samples per pixel and averages their smooth iteration values to 
//     reduce spatial aliasing artifacts in high-detail regions of the fractal.
//
//   - Interactive zoom via an on-screen zoom box overlay that can be 
//     positioned and resized to select a sub-region of the fractal for 
//     re-rendering at higher magnification.
//
//   - The palette uses a configurable multi-ramp gradient. 
//     - RGB control vectors are loaded from CONFIG.INI 
//       (palette_control_vector_0, palette_control_vector_1, ...);
//     - Indices are computed automatically by evenly spacing the control
//       vectors across palette entries 1-254.
//     - Default gradient: 
//       Black -> Blue -> Cyan -> Green -> Yellow -> Red -> Magenta -> Black.
//
//****************************************************************************

#include <conio.h>
#include <dos.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vesa.h"
#include "ini.h"
#include "app.h"

//----------------------------------------------------------------------------
// Method: Application::Application
//
// Description:
//
//   Default constructor. Initializes the palette, viewport, zoom box
//   state, and acceleration state to their default values.
//
//----------------------------------------------------------------------------

Application::Application ( void )
{
	memset ( palette, 0, PALETTE_BYTES );

	// Default screen resolution (may be overridden by INI file).

	screen_width  = SCREEN_WIDTH;
	screen_height = SCREEN_HEIGHT;

	// Initialize viewport to the default full Mandelbrot view.

	viewport_real_min      = INITIAL_FRACTAL_X_MIN;
	viewport_real_max      = INITIAL_FRACTAL_X_MAX;
	viewport_imaginary_min = INITIAL_FRACTAL_Y_MIN;
	viewport_imaginary_max = INITIAL_FRACTAL_Y_MAX;

	// Zoom box starts hidden with default size (4:3 aspect ratio).

	zoom_box_visible = 0;
	zoom_box_x       = ( screen_width  - ZOOM_BOX_DEFAULT_WIDTH ) / 2;
	zoom_box_y       = ( screen_height - ZOOM_BOX_DEFAULT_WIDTH * ASPECT_RATIO_HEIGHT / ASPECT_RATIO_WIDTH ) / 2;
	zoom_box_width   = ZOOM_BOX_DEFAULT_WIDTH;
	zoom_box_height  = ZOOM_BOX_DEFAULT_WIDTH * ASPECT_RATIO_HEIGHT / ASPECT_RATIO_WIDTH;

	// Border save buffers start as null; allocated on first use.

	border_top_buffer    = 0;
	border_bottom_buffer = 0;
	border_left_buffer   = 0;
	border_right_buffer  = 0;

	// Acceleration state.

	last_arrow_direction = 0;
	arrow_press_count    = 0;
	last_arrow_timestamp = 0;

	// Stored settings (set properly in Run from INI file).

	stored_antialiasing          = 0;
	stored_supersampling         = 0;
	stored_debug_mode            = 0;
	stored_normalize_color_range = 0;
	ui_red                       = 0;
	ui_green                     = 20;
	ui_blue                      = 0;

	// Default palette control vectors (may be overridden by INI file).
	//
	// - Only RGB values are stored;
	// - Indices are computed dynamically in InitializeFractalPalette() based 
	//   on the control vector count.

	palette_vector_count = 8;

	palette_vector_red [ 0 ] =  0;  palette_vector_green [ 0 ] =  0;  palette_vector_blue [ 0 ] =  0;	// Black
	palette_vector_red [ 1 ] =  0;  palette_vector_green [ 1 ] =  0;  palette_vector_blue [ 1 ] = 63;	// Blue
	palette_vector_red [ 2 ] =  0;  palette_vector_green [ 2 ] = 63;  palette_vector_blue [ 2 ] = 63;	// Cyan
	palette_vector_red [ 3 ] =  0;  palette_vector_green [ 3 ] = 63;  palette_vector_blue [ 3 ] =  0;	// Green
	palette_vector_red [ 4 ] = 63;  palette_vector_green [ 4 ] = 63;  palette_vector_blue [ 4 ] =  0;	// Yellow
	palette_vector_red [ 5 ] = 63;  palette_vector_green [ 5 ] =  0;  palette_vector_blue [ 5 ] =  0;	// Red
	palette_vector_red [ 6 ] = 32;  palette_vector_green [ 6 ] =  0;  palette_vector_blue [ 6 ] = 32;	// Dark Magenta (Violet)
	palette_vector_red [ 7 ] =  0;  palette_vector_green [ 7 ] =  0;  palette_vector_blue [ 7 ] =  0;	// Black

	// Image save path defaults to current directory.

	image_path [ 0 ] = '\0';
}

//----------------------------------------------------------------------------
// Method: Application::~Application
//
// Description:
//
//   Destructor. Releases far-heap border save buffers.
//
//----------------------------------------------------------------------------

Application::~Application ( void )
{
	if ( border_top_buffer    ) delete [] border_top_buffer;
	if ( border_bottom_buffer ) delete [] border_bottom_buffer;
	if ( border_left_buffer   ) delete [] border_left_buffer;
	if ( border_right_buffer  ) delete [] border_right_buffer;
}

//----------------------------------------------------------------------------
// Method: Application::ParseIntegerList
//
// Description:
//
//   Parses a comma-separated string of integers into an output array.
//   Leading and trailing whitespace around each value is tolerated
//   because atoi() skips leading whitespace.
//
// Arguments:
//
//   - string    : Null-terminated comma-separated string (e.g. "1,37,73").
//   - output    : Array to receive parsed integers.
//   - max_count : Maximum number of integers to parse (size of output).
//
// Returns:
//
//   - The number of integers successfully parsed.
//
//----------------------------------------------------------------------------

int Application::ParseIntegerList ( const char *string, int *output, int max_count )
{
	// Initialise local variables.

	int         count;		// Number of integers parsed so far
	const char *current;	// Current read position in the input string

	count   = 0;
	current = string;

	// Walk the string, converting each comma-separated token to an integer.

	while ( *current != '\0' && count < max_count )
	{
		// Convert the current token and store it in the output array.

		output [ count ] = atoi ( current );
		count++;

		// Advance past the current number to the next comma or end.

		while ( *current != '\0' && *current != ',' )
		{
			current++;
		}

		// Skip the comma delimiter.

		if ( *current == ',' )
		{
			current++;
		}
	}

	// Return the total number of integers parsed.

	return count;
}

//----------------------------------------------------------------------------
// Method: Application::InitializeFractalPalette
//
// Description:
//
//   Generates a multi-ramp color gradient optimized for Mandelbrot set
//   rendering. 
//
//   - Palette index 0 is reserved as black for points inside the set. 
//   - Indices 1-254 contain a smooth gradient defined by the palette control 
//     vector arrays (loaded from CONFIG.INI or set to defaults in the constructor), 
//     connected by linear interpolation.
//
//   Note:
//   - When setting up a palette, it is usefull to set palette color control 
//     vectors to create a gradient that wraps smoothly from black back to black, 
//     to ensure that adjacent iteration counts always produce visually distinct
//     colors without harsh discontinuities.
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

void Application::InitializeFractalPalette ( void )
{
	int  number_segments;	// Number of interpolation segments (control vectors - 1)
	int  segment;			// Current segment index during interpolation
	int  i;					// Loop counter for palette indices
	int  start_index;		// First palette index of the current segment
	int  end_index;			// Last palette index of the current segment
	int  start_red;			// Red channel of the segment's starting control vector
	int  start_green;		// Green channel of the segment's starting control vector
	int  start_blue;		// Blue channel of the segment's starting control vector
	int  end_red;			// Red channel of the segment's ending control vector
	int  end_green;			// Green channel of the segment's ending control vector
	int  end_blue;			// Blue channel of the segment's ending control vector
	int  segment_length;	// Number of palette indices spanned by the current segment
	int  position;			// Offset of the current index within the segment
	long index_long;		// 32-bit loop counter for palette index computation (prevents 16-bit overflow)

	// Palette index 0: black (Mandelbrot set interior).

	palette [ 0 ] = 0;
	palette [ 1 ] = 0;
	palette [ 2 ] = 0;

	// Compute evenly-spaced palette indices for each control vector.
	// - The first control vector maps to index 1 and the last to FRACTAL_PALETTE_ENTRIES (254).

	number_segments = palette_vector_count - 1;

	for ( i = 0; i < palette_vector_count; i++ )
	{
		index_long = i;
		palette_vector_index [ i ] = 1 + (int) ( index_long * ( FRACTAL_PALETTE_ENTRIES - 1 ) / number_segments );
	}

	// Interpolate linearly between consecutive control vectors to fill palette 
	// indices 1 through FRACTAL_PALETTE_ENTRIES (254).

	for ( segment = 0; segment < number_segments; segment++ )
	{
		start_index = palette_vector_index [ segment ];
		end_index   = palette_vector_index [ segment + 1 ];

		start_red   = palette_vector_red   [ segment ];
		start_green = palette_vector_green [ segment ];
		start_blue  = palette_vector_blue  [ segment ];

		end_red     = palette_vector_red   [ segment + 1 ];
		end_green   = palette_vector_green [ segment + 1 ];
		end_blue    = palette_vector_blue  [ segment + 1 ];

		// Compute the number of palette indices in this segment.

		segment_length = end_index - start_index;

		// Linearly interpolate each RGB channel across the segment.

		for ( i = start_index; i <= end_index; i++ )
		{
			position = i - start_index;

			palette [ i * 3 + 0 ] = (unsigned char) ( start_red   + ( end_red   - start_red   ) * position / segment_length );
			palette [ i * 3 + 1 ] = (unsigned char) ( start_green + ( end_green - start_green ) * position / segment_length );
			palette [ i * 3 + 2 ] = (unsigned char) ( start_blue  + ( end_blue  - start_blue  ) * position / segment_length );
		}
	}

	// Reserve palette index UI_COLOR (255) for all UI overlays (zoom
	// box, scanline indicator, HUD). 
	// 
	// - Defaults to dark green and may be overridden by INI file settings in Run().

	palette [ UI_COLOR * 3 + 0 ] = 0;
	palette [ UI_COLOR * 3 + 1 ] = 20;
	palette [ UI_COLOR * 3 + 2 ] = 0;
}

//----------------------------------------------------------------------------
// Method: Application::TestPalette
//
// Description:
//
//   Displays a static gradient test screen showing all 256 palette entries
//   as horizontal bands.
//
//   - Each band spans the full screen width and is 3 pixels tall (768 / 256 = 3), 
//     filling the entire display with a smooth representation of the current palette.
//
//   Used in debug mode to visually validate the palette before rendering.
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

void Application::TestPalette ( void )
{
	int i;				// Current palette index
	int band_height;	// Height of each color band in pixels
	int y0;				// Top edge of the current band
	int y1;				// Bottom edge of the current band

	band_height = screen_height / PALETTE_SIZE;

	// Draw one horizontal band per palette entry.

	for ( i = 0; i < PALETTE_SIZE; i++ )
	{
		y0 = i * band_height;
		y1 = y0 + band_height - 1;

		FillRectangle ( 0, y0, screen_width - 1, y1, (BYTE) i );
	}

	// Label the test pattern.

	DrawText ( 16, 16, "Palette Test - Press any key ...", 255, 1 );
}

//----------------------------------------------------------------------------
// Method: Application::ComputeIterations
//
// Description:
//
//   Performs the Mandelbrot iteration z = z^2 + c for a single point in
//   the complex plane. Starting from z = 0, iterates until |z| exceeds
//   the escape radius or the maximum iteration count is reached.
//
//   On return, magnitude_squared contains the final |z|^2 value, which
//   is needed by the smooth coloring algorithm to compute a continuous
//   iteration count.
//
// Arguments:
//
//   - c_real             : Real component of the complex point c.
//   - c_imaginary        : Imaginary component of the complex point c.
//   - magnitude_squared  : Pointer to receive the final |z|^2 at escape.
//
// Returns:
//
//   - The number of iterations before escape, or MAX_ITERATIONS if the
//     point did not escape (inside the Mandelbrot set).
//
//----------------------------------------------------------------------------

int Application::ComputeIterations ( double c_real, double c_imaginary, double *magnitude_squared )
{
	double z_real;					// Real part of z
	double z_imaginary;				// Imaginary part of z
	double z_real_squared;			// Cached z_real^2 (avoids redundant multiply)
	double z_imaginary_squared;		// Cached z_imaginary^2 (avoids redundant multiply)
	int    iteration;				// Current iteration count

	// Initialise z to the origin and reset the iteration counter.

	z_real      = 0.0;
	z_imaginary = 0.0;
	iteration   = 0;

	// Iterate z = z^2 + c until escape or maximum iterations reached.

	while ( iteration < MAX_ITERATIONS )
	{
		// Cache the squared components for the escape test and update step.

		z_real_squared      = z_real * z_real;
		z_imaginary_squared = z_imaginary * z_imaginary;

		// Compute |z|^2 for the escape test and for the caller's smooth coloring.

		*magnitude_squared = z_real_squared + z_imaginary_squared;

		// Check if |z|^2 > escape radius squared.

		if ( *magnitude_squared > ESCAPE_RADIUS_SQUARED )
		{
			break;
		}

		// z = z^2 + c
		//
		// Real part:      z_real      = z_real^2 - z_imaginary^2 + c_real
		// Imaginary part: z_imaginary = 2 * z_real * z_imaginary + c_imaginary

		z_imaginary = 2.0 * z_real * z_imaginary + c_imaginary;
		z_real      = z_real_squared - z_imaginary_squared + c_real;

		iteration++;
	}

	// Return the iteration count at which z escaped, or MAX_ITERATIONS if it did not.

	return iteration;
}

//----------------------------------------------------------------------------
// Method: Application::MapColor
//
// Description:
//
//   Maps a raw integer iteration count to a palette color index. Points
//   inside the Mandelbrot set (iteration >= MAX_ITERATIONS) are mapped
//   to palette index 0 (black).
//
//   - Escaped points are mapped to palette indices 1-251 using modular 
//     arithmetic.
//
//   - Palette indices 252-253 are reserved for UI overlays and are never 
//     produced by this mapping.
//
// Arguments:
//
//   - iteration : The escape iteration count from ComputeIterations.
//
// Returns:
//
//   - Palette color index (0-251).
//
//----------------------------------------------------------------------------

BYTE Application::MapColor ( int iteration )
{
	BYTE color;

	// Interior points (no escape) map to palette index 0 (black).

	if ( iteration >= MAX_ITERATIONS )
	{
		return 0;
	}

	// Map escaped iteration count to palette indices 1-254 using modular arithmetic.

	color = (BYTE) ( ( iteration % FRACTAL_PALETTE_ENTRIES ) + 1 );

	return color;
}

//----------------------------------------------------------------------------
// Method: Application::ComputeSmoothValue
//
// Description:
//
//   Computes the continuous (smooth) iteration count from the integer
//   iteration count and final escape magnitude.
//
//   Returns a raw floating-point value suitable for averaging across multiple
//   samples during supersampled rendering.
//
//   Uses the normalized iteration count formula:
//
//     mu = n + 1 - log ( 0.5 * log( magnitude_squared ) ) / log ( 2 )
//
//   Points inside the Mandelbrot set (iteration >= MAX_ITERATIONS) are
//   indicated by returning -1.0, which callers must handle.
//
// Arguments:
//
//   - iteration         : The escape iteration count from ComputeIterations.
//   - magnitude_squared : The final |z|^2 at escape from ComputeIterations.
//
// Returns:
//
//   - The continuous iteration count, or -1.0 for interior points.
//
//----------------------------------------------------------------------------

double Application::ComputeSmoothValue ( int iteration, double magnitude_squared )
{
	double smooth_value;
	double iteration_d;		// Double-precision iteration count for the smooth coloring formula

	// Interior points return -1.0 as a sentinel (no meaningful smooth value).

	if ( iteration >= MAX_ITERATIONS )
	{
		return -1.0;
	}

	// Apply the normalized iteration count formula for continuous coloring.

	iteration_d  = iteration;
	smooth_value = iteration_d + 1.0 - log ( 0.5 * log ( magnitude_squared ) ) / log ( 2.0 );

	return smooth_value;
}

//----------------------------------------------------------------------------
// Method: Application::MapSmoothColor
//
// Description:
//
//   Maps a smooth (continuous) iteration count to a palette color index,
//   eliminating the hard color banding that occurs with integer iteration
//   counts.
//
//   Uses ComputeSmoothValue to obtain the continuous iteration count,
//   then maps it to palette indices 1-255 using modular arithmetic.
//
// Arguments:
//
//   - iteration         : The escape iteration count from ComputeIterations.
//   - magnitude_squared : The final |z|^2 at escape from ComputeIterations.
//
// Returns:
//
//   - Palette color index (0-255).
//
//----------------------------------------------------------------------------

BYTE Application::MapSmoothColor ( int iteration, double magnitude_squared )
{
	double smooth_value;	// Continuous iteration count from the smooth coloring formula
	int    color_index;		// Palette index derived from the smooth value
	BYTE   color;			// Final mapped palette color

	// Compute the continuous iteration count for this point.

	smooth_value = ComputeSmoothValue ( iteration, magnitude_squared );

	if ( smooth_value < 0.0 )
	{
		return 0;
	}

	// Map the smooth value to palette indices 1-FRACTAL_PALETTE_ENTRIES
	// using modular arithmetic. fmod handles the floating-point modulus,
	// and the guard corrects for any negative remainder.

	color_index = (int) fmod ( smooth_value, FRACTAL_PALETTE_ENTRIES_D );

	if ( color_index < 0 )
	{
		color_index += FRACTAL_PALETTE_ENTRIES;
	}

	// Shift into the palette range 1-254 and return.

	color = (BYTE) ( color_index + 1 );

	return color;
}

//----------------------------------------------------------------------------
// Method: Application::RenderMandelbrot
//
// Description:
//
//   Renders the Mandelbrot set by iterating over every pixel on screen,
//   computing the escape-time iteration count for the corresponding
//   complex plane coordinate, and plotting the result using the current
//   palette.
//
//   If antialiasing is enabled, smooth iteration coloring is applied to
//   eliminate color banding at iteration boundaries. Otherwise, the raw
//   integer iteration count is used for color mapping.
//
//   If supersampling is enabled (2 for 2x2, 3 for 3x3), multiple
//   sub-samples are taken within each pixel and their smooth iteration
//   values are averaged before mapping to a palette color. This reduces
//   spatial aliasing artifacts ("sparkling") in high-detail regions of
//   the fractal.
//
//   The complex plane viewport is defined by the member variables
//   viewport_real_min, viewport_real_max, viewport_imaginary_min, and
//   viewport_imaginary_max.
//
//   The ESC key may be pressed between scanlines to abort the render.
//   The TAB key may be pressed between scanlines to interrupt the
//   render and activate the zoom box, allowing the user to select a
//   region even from a partially rendered image.
//
// Arguments:
//
//   - antialiasing  : Non-zero to enable smooth iteration coloring;
//                     zero for standard integer coloring.
//   - supersampling : Supersampling grid size (0 = disabled, 2 = 2x2,
//                     3 = 3x3). Implicitly enables smooth coloring.
//
// Returns:
//
//   - 0 if the render completed normally, 1 if ESC was pressed, or
//     2 if TAB was pressed to activate the zoom box.
//
//----------------------------------------------------------------------------

int Application::RenderMandelbrot ( int antialiasing, int supersampling )
{
	int    pixel_x;					// Current pixel column
	int    pixel_y;					// Current pixel row (scanline)
	int    iteration;				// Escape iteration count for the current point
	int    key_check;				// Keystroke value for ESC/TAB abort check
	double c_real;					// Real component of the complex point c
	double c_imaginary;				// Imaginary component of the complex point c
	double x_scale;					// Pixels-to-complex-plane scale factor (horizontal)
	double y_scale;					// Pixels-to-complex-plane scale factor (vertical)
	double magnitude_squared;		// Final |z|^2 at escape for smooth coloring
	BYTE   color;					// Mapped palette index for the current pixel

	// Supersampling variables.

	int    sample_x;				// Sub-sample column index within the NxN grid
	int    sample_y;				// Sub-sample row index within the NxN grid
	int    escaped_count;			// Number of sub-samples that escaped
	int    color_index;				// Palette index derived from averaged smooth value
	double smooth_value;			// Smooth iteration value for one sub-sample
	double smooth_sum;				// Running sum of escaped sub-sample smooth values
	double sub_c_real;				// Real component of the sub-sample point
	double sub_c_imaginary;			// Imaginary component of the sub-sample point
	double sub_magnitude_squared;	// Final |z|^2 for the sub-sample
	int    sub_iteration;			// Escape iteration count for the sub-sample

	// Double-precision shadow variables for cast-free arithmetic.

	double screen_width_d;			// Double-precision screen width for coordinate mapping
	double screen_height_d;			// Double-precision screen height for coordinate mapping
	double pixel_x_d;				// Double-precision pixel column for coordinate mapping
	double pixel_y_d;				// Double-precision pixel row for coordinate mapping
	double sample_x_d;				// Double-precision sub-sample column for sub-pixel offset
	double sample_y_d;				// Double-precision sub-sample row for sub-pixel offset
	double supersampling_d;			// Double-precision supersampling grid size for sub-pixel division
	double escaped_count_d;			// Double-precision escaped count for smooth value averaging

	// Compute the mapping from screen coordinates to complex plane
	// coordinates. Each pixel maps to a unique point c = (c_real, c_imaginary).

	screen_width_d  = screen_width;
	screen_height_d = screen_height;
	supersampling_d = supersampling;

	x_scale = ( viewport_real_max - viewport_real_min ) / screen_width_d;
	y_scale = ( viewport_imaginary_max - viewport_imaginary_min ) / screen_height_d;

	// Iterate over every pixel on screen.

	for ( pixel_y = 0; pixel_y < screen_height; pixel_y++ )
	{
		pixel_y_d = pixel_y;

		// Check for ESC or TAB key between scanlines. ESC aborts the
		// render; TAB interrupts to activate the zoom box overlay.

		if ( kbhit () )
		{
			key_check = getch ();

			if ( key_check == KEY_ESCAPE )
			{
				return 1;
			}

			if ( key_check == KEY_TAB )
			{
				return 2;
			}
		}

		// Render each pixel in the current scanline.

		for ( pixel_x = 0; pixel_x < screen_width; pixel_x++ )
		{
			pixel_x_d = pixel_x;

			// Select the supersampled or single-sample rendering path.

			if ( supersampling >= 2 )
			{
				// Supersampled Path
				//
				// - Take NxN sub-samples within this pixel.
				//
				// - Each sub-sample is offset by (sample_i + 0.5) / N
				//   within the pixel, producing a uniform grid.
				//
				// - For N=2 the offsets are 0.25 and 0.75;
				//   For N=3 they are 1/6, 3/6, and 5/6.

				smooth_sum    = 0.0;
				escaped_count = 0;

				for ( sample_y = 0; sample_y < supersampling; sample_y++ )
				{
					// Compute the imaginary component for this sub-sample row.

					sample_y_d          = sample_y;
					sub_c_imaginary     = viewport_imaginary_min + ( pixel_y_d + ( sample_y_d + 0.5 ) / supersampling_d ) * y_scale;

					// Iterate over sub-sample columns in this row.

					for ( sample_x = 0; sample_x < supersampling; sample_x++ )
					{
						// Compute the real component for this sub-sample column.

						sample_x_d = sample_x;
						sub_c_real = viewport_real_min
							+ ( pixel_x_d + ( sample_x_d + 0.5 ) / supersampling_d )
							* x_scale;

						// Evaluate the Mandelbrot iteration and smooth value for this sub-sample.

						sub_iteration = ComputeIterations ( sub_c_real, sub_c_imaginary, &sub_magnitude_squared );
						smooth_value  = ComputeSmoothValue ( sub_iteration, sub_magnitude_squared );

						// Accumulate escaped sub-samples for averaging.

						if ( smooth_value >= 0.0 )
						{
							smooth_sum += smooth_value;
							escaped_count++;
						}
					}
				}

				// Map the averaged smooth value to a palette index.

				if ( escaped_count == 0 )
				{
					// All sub-samples are inside the set.

					color = 0;
				}
				else
				{
					// Average the escaped samples' smooth values.

					escaped_count_d = escaped_count;
					smooth_value    = smooth_sum / escaped_count_d;

					// Map to palette indices 1-FRACTAL_PALETTE_ENTRIES
					// using modular arithmetic.

					color_index = (int) fmod ( smooth_value, FRACTAL_PALETTE_ENTRIES_D );

					if ( color_index < 0 )
					{
						color_index += FRACTAL_PALETTE_ENTRIES;
					}

					color = (BYTE) ( color_index + 1 );
				}
			}
			else
			{
				// Single-Sample Path (original behavior)

				c_real      = viewport_real_min + pixel_x_d * x_scale;
				c_imaginary = viewport_imaginary_min + pixel_y_d * y_scale;

				iteration = ComputeIterations ( c_real, c_imaginary, &magnitude_squared );

				// Map the iteration count to a palette color.

				if ( antialiasing )
				{
					color = MapSmoothColor ( iteration, magnitude_squared );
				}
				else
				{
					color = MapColor ( iteration );
				}
			}

			// Write the computed color to video memory.

			PutPixel ( pixel_x, pixel_y, color );
		}

		// Draw a scanline indicator on the next scanline.
		//
		// - We do this to provides a visual indicator of render progress.
		//
		// - The line will be overwritten when the next scanline renders, and 
		//   the last scanline has no line below it, so the final image is 
		//   left clean.
		//
		// - WaitRetrace synchronizes with the vertical blank to prevent tearing.

		if ( pixel_y + 1 < screen_height )
		{
			WaitRetrace   ();
			FillRectangle ( 0, pixel_y + 1, screen_width - 1, pixel_y + 1, UI_COLOR );
		}
	}

	// Render completed without interruption.

	return 0;
}

//----------------------------------------------------------------------------
// Method: Application::NormalizeColorRange
//
// Description:
//
//   Post-processing pass that remaps the fractal color indices in video
//   memory so they span the full palette range (1-254).
//
//   Because the renderer maps iteration counts to palette indices using
//   modular arithmetic (iteration % 254 + 1), a narrow band of high
//   iteration counts can wrap around the palette and produce a split
//   distribution. For example, iterations 240-254 map to indices
//   {1, 241-254}, spanning both ends of the palette with a large
//   gap in between.
//
//   To handle this, Pass 1 builds a histogram of which palette indices
//   are in use, then walks the circular range 1-254 to find the
//   largest contiguous gap of unused indices. The occupied band is
//   the complement of that gap. The band start is the first used
//   index after the gap, and the band length is the number of
//   distinct positions in the circular walk from band start to the
//   last used index before the gap.
//
//   Pass 2 remaps every fractal pixel so that the used indices are
//   linearly stretched to fill 1-254:
//
//                                  position * 253
//     new_index  =  1  +  floor ( ---------------- )
//                                  used_count - 1
//
//   where position is the ordinal rank (0-based) of the current
//   index among all used indices, ordered by their circular walk
//   starting from band_start.
//
//   Pixels with index 0 (Mandelbrot set interior) and index 255
//   (UI overlay) are left unchanged.
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

void Application::NormalizeColorRange ( void )
{
	int  pixel_x;							// Current pixel column during remap scan
	int  pixel_y;							// Current pixel row during remap scan
	BYTE color;								// Palette index read from video memory
	BYTE new_index;							// Remapped palette index
	BYTE scanline_buffer [ SCREEN_WIDTH ];	// Prefetched scanline buffer for safe remap

	// Histogram of which fractal palette indices (1-254) are in use.
	//
	// - Entry i corresponds to palette index i.
	//
	// - Only indices 1-254 are examined; entries 0 and 255 are unused.

	BYTE used [ PALETTE_SIZE ];				// Per-index usage flags (1 = present in image)
	int  used_count;						// Total number of distinct palette indices in use
	int  i;									// Loop counter

	// Circular gap detection variables.

	int  gap_start;							// Start position of the current gap in the circular walk
	int  gap_length;						// Length of the current gap in unused indices
	int  best_gap_start;					// Start position of the largest gap found so far
	int  best_gap_length;					// Length of the largest gap found so far
	int  band_start;						// First used index after the largest gap (circular walk origin)
	int  band_length;						// Number of occupied positions in the circular band

	// Remap lookup table and position counter.

	BYTE remap [ PALETTE_SIZE ];			// Lookup table mapping old palette index to new index
	int  position;							// Ordinal rank of the current used index in the circular walk
	long position_long;						// 32-bit position for normalized index computation (prevents 16-bit overflow)
	long used_count_minus_one;				// 32-bit denominator for normalized index computation

	// ------------------------------------------------------------------
	// Pass 1:
	//
	// Build the histogram and find the occupied circular band.
	//
	// ------------------------------------------------------------------

	// Clear the histogram.

	for ( i = 0; i < PALETTE_SIZE; i++ )
	{
		used [ i ] = 0;
	}

	used_count = 0;

	// Scan all pixels and mark which fractal indices are present.

	for ( pixel_y = 0; pixel_y < screen_height; pixel_y++ )
	{
		for ( pixel_x = 0; pixel_x < screen_width; pixel_x++ )
		{
			color = GetPixel ( pixel_x, pixel_y );

			if ( color >= 1 && color <= 254 && !used [ color ] )
			{
				used [ color ] = 1;
				used_count++;
			}
		}
	}

	// If fewer than 2 distinct fractal colors exist, normalization
	// is not possible or not meaningful.

	if ( used_count < 2 )
	{
		return;
	}

	// If all 254 palette entries are in use, the palette is already
	// fully occupied and no remapping can improve the distribution.

	if ( used_count == FRACTAL_PALETTE_ENTRIES )
	{
		return;
	}

	// Find the largest contiguous gap of unused indices in the
	// circular range 1-254. Walk 254 positions starting from
	// index 1, wrapping from 254 back to 1.

	best_gap_start  = 0;
	best_gap_length = 0;
	gap_start       = -1;
	gap_length      = 0;

	for ( i = 0; i < FRACTAL_PALETTE_ENTRIES; i++ )
	{
		// Map position i to palette index: 1, 2, ..., 254.

		color = (BYTE) ( ( i % FRACTAL_PALETTE_ENTRIES ) + 1 );

		if ( !used [ color ] )
		{
			// Extend or start a gap.

			if ( gap_length == 0 )
			{
				gap_start = i;
			}

			gap_length++;
		}
		else
		{
			// End of a gap. Check if it is the longest so far.

			if ( gap_length > best_gap_length )
			{
				best_gap_start  = gap_start;
				best_gap_length = gap_length;
			}

			gap_length = 0;
		}
	}

	// Check the final gap, which may wrap from the end of the
	// circular walk back to the beginning.

	if ( gap_length > best_gap_length )
	{
		best_gap_start  = gap_start;
		best_gap_length = gap_length;
	}

	// The occupied band starts at the first used index after the
	// largest gap. Band length is all positions not in the gap.

	band_start  = ( best_gap_start + best_gap_length ) % FRACTAL_PALETTE_ENTRIES;
	band_length = FRACTAL_PALETTE_ENTRIES - best_gap_length;

	// If the band is the entire palette or only one entry,
	// normalization is not possible or not needed.

	if ( band_length <= 1 || band_length >= FRACTAL_PALETTE_ENTRIES )
	{
		return;
	}

	// Build a lookup table that maps each used palette index to its
	// normalized index. Walk the circular band in order and assign
	// evenly spaced positions only to indices that are actually in
	// use, so the full 1-254 range is covered regardless of interior
	// gaps within the band. Unused indices map to 0 as a sentinel.

	for ( i = 0; i < PALETTE_SIZE; i++ )
	{
		remap [ i ] = 0;
	}

	position             = 0;
	used_count_minus_one = used_count - 1;

	for ( i = 0; i < FRACTAL_PALETTE_ENTRIES; i++ )
	{
		color = (BYTE) ( ( ( band_start + i ) % FRACTAL_PALETTE_ENTRIES ) + 1 );

		if ( used [ color ] )
		{
			position_long       = position;
			remap [ color ] = (BYTE) ( 1 + position_long * 253L / used_count_minus_one );
			position++;
		}
	}

	// ------------------------------------------------------------------
	// Pass 2:
	//
	// Remap all fractal color indices using the lookup table.
	//
	// - A scanline progress indicator is drawn on the next row after
	//   each remapped scanline.
	//
	// - Because the indicator overwrites pixel data, the next row is 
	//   prefetched into a buffer before the indicator is drawn. 
	//
	// - The remapping loop reads from this buffer rather than from 
	//   video memory, ensuring no pixel data is lost.
	//
	// ------------------------------------------------------------------

	// Prefetch the first row into the buffer.

	for ( pixel_x = 0; pixel_x < screen_width; pixel_x++ )
	{
		scanline_buffer [ pixel_x ] = GetPixel ( pixel_x, 0 );
	}

	// Remap all scanlines using the prefetched buffer.

	for ( pixel_y = 0; pixel_y < screen_height; pixel_y++ )
	{
		// Remap the current row from the prefetched buffer.

		for ( pixel_x = 0; pixel_x < screen_width; pixel_x++ )
		{
			// Read the original color from the prefetched buffer.

			color = scanline_buffer [ pixel_x ];

			// Remap fractal indices; leave interior and UI pixels unchanged.

			if ( color >= 1 && color <= 254 )
			{
				PutPixel ( pixel_x, pixel_y, remap [ color ] );
			}
			else
			{
				// Restore non-fractal pixels (index 0 for set interior,
				// index 255 for UI overlay) to overwrite any progress
				// indicator pixels left on this row by the previous
				// iteration.

				PutPixel ( pixel_x, pixel_y, color );
			}
		}

		// Prefetch the next row into the buffer, then draw the
		// progress indicator over it. The prefetch preserves the
		// original pixel data so it can be remapped on the next
		// iteration. The last scanline has no row below it, so
		// the final image is left clean.

		if ( pixel_y + 1 < screen_height )
		{
			// Prefetch the next row before drawing the progress indicator.

			for ( pixel_x = 0; pixel_x < screen_width; pixel_x++ )
			{
				scanline_buffer [ pixel_x ] = GetPixel ( pixel_x, pixel_y + 1 );
			}

			// Draw the progress indicator on the next scanline.

			WaitRetrace   ();
			FillRectangle ( 0, pixel_y + 1, screen_width - 1, pixel_y + 1, UI_COLOR );
		}
	}
}

//----------------------------------------------------------------------------
// Method: Application::AllocateZoomBoxBuffers
//
// Description:
//
//   Allocates the four border pixel save buffers on the far heap if they
//   have not yet been allocated. Called on the first TAB keypress to
//   activate the zoom box.
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

void Application::AllocateZoomBoxBuffers ( void )
{
	if ( !border_top_buffer )
	{
		border_top_buffer    = new BYTE [ ZOOM_BOX_BUFFER_SIZE ];
		border_bottom_buffer = new BYTE [ ZOOM_BOX_BUFFER_SIZE ];
		border_left_buffer   = new BYTE [ ZOOM_BOX_BUFFER_SIZE ];
		border_right_buffer  = new BYTE [ ZOOM_BOX_BUFFER_SIZE ];
	}
}

//----------------------------------------------------------------------------
// Method: Application::SaveBorderPixels
//
// Description:
//
//   Reads every pixel under the four border strips of the zoom box
//   from video memory and stores them in the save buffers. This allows
//   the zoom box overlay to be erased later by restoring the original
//   pixel values.
//
//   Each border strip is ZOOM_BOX_BORDER_WIDTH pixels thick. The left
//   and right strips are inset vertically to avoid overlapping the
//   corners already covered by the top and bottom strips.
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

void Application::SaveBorderPixels ( void )
{
	int x;		// Horizontal pixel coordinate within the border strip
	int y;		// Vertical pixel coordinate within the border strip
	int index;	// Current position in the save buffer

	int left      = zoom_box_x;							// Left edge of the zoom box
	int top       = zoom_box_y;							// Top edge of the zoom box
	int right     = zoom_box_x + zoom_box_width  - 1;	// Right edge of the zoom box (inclusive)
	int bottom    = zoom_box_y + zoom_box_height - 1;	// Bottom edge of the zoom box (inclusive)
	int thickness = ZOOM_BOX_BORDER_WIDTH;				// Border thickness in pixels

	// Save top border strip.

	index = 0;

	for ( y = top; y < top + thickness; y++ )
	{
		for ( x = left; x <= right; x++ )
		{
			border_top_buffer [ index++ ] = GetPixel ( x, y );
		}
	}

	// Save bottom border strip.

	index = 0;

	for ( y = bottom - thickness + 1; y <= bottom; y++ )
	{
		for ( x = left; x <= right; x++ )
		{
			border_bottom_buffer [ index++ ] = GetPixel ( x, y );
		}
	}

	// Save left border strip (excluding corners already in top/bottom).

	index = 0;

	for ( y = top + thickness; y <= bottom - thickness; y++ )
	{
		for ( x = left; x < left + thickness; x++ )
		{
			border_left_buffer [ index++ ] = GetPixel ( x, y );
		}
	}

	// Save right border strip (excluding corners).

	index = 0;

	for ( y = top + thickness; y <= bottom - thickness; y++ )
	{
		for ( x = right - thickness + 1; x <= right; x++ )
		{
			border_right_buffer [ index++ ] = GetPixel ( x, y );
		}
	}
}

//----------------------------------------------------------------------------
// Method: Application::RestoreBorderPixels
//
// Description:
//
//   Writes the saved pixel values back to video memory, erasing the zoom
//   zoom box overlay and restoring the original fractal image beneath it.
//
//   Uses the same traversal order and buffer indexing as SaveBorderPixels
//   to ensure each pixel is restored to its correct position.
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

void Application::RestoreBorderPixels ( void )
{
	int x;		// Horizontal pixel coordinate within the border strip
	int y;		// Vertical pixel coordinate within the border strip
	int index;	// Current position in the save buffer

	int left      = zoom_box_x;							// Left edge of the zoom box
	int top       = zoom_box_y;							// Top edge of the zoom box
	int right     = zoom_box_x + zoom_box_width  - 1;	// Right edge of the zoom box (inclusive)
	int bottom    = zoom_box_y + zoom_box_height - 1;	// Bottom edge of the zoom box (inclusive)
	int thickness = ZOOM_BOX_BORDER_WIDTH;				// Border thickness in pixels

	// Restore top border strip.

	index = 0;

	for ( y = top; y < top + thickness; y++ )
	{
		for ( x = left; x <= right; x++ )
		{
			PutPixel ( x, y, border_top_buffer [ index++ ] );
		}
	}

	// Restore bottom border strip.

	index = 0;

	for ( y = bottom - thickness + 1; y <= bottom; y++ )
	{
		for ( x = left; x <= right; x++ )
		{
			PutPixel ( x, y, border_bottom_buffer [ index++ ] );
		}
	}

	// Restore left border strip.

	index = 0;

	for ( y = top + thickness; y <= bottom - thickness; y++ )
	{
		for ( x = left; x < left + thickness; x++ )
		{
			PutPixel ( x, y, border_left_buffer [ index++ ] );
		}
	}

	// Restore right border strip.

	index = 0;

	for ( y = top + thickness; y <= bottom - thickness; y++ )
	{
		for ( x = right - thickness + 1; x <= right; x++ )
		{
			PutPixel ( x, y, border_right_buffer [ index++ ] );
		}
	}
}

//----------------------------------------------------------------------------
// Method: Application::DrawZoomBox
//
// Description:
//
//   Saves the pixels under the zoom box border and then draws the
//   border overlay using the reserved dark grey palette color.
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

void Application::DrawZoomBox ( void )
{
	// Save the pixels that will be overwritten by the border overlay.

	SaveBorderPixels ();

	// Draw the zoom box border rectangle.

	DrawRectangleOutline
	(
		zoom_box_x,							// Left edge
		zoom_box_y,							// Top edge
		zoom_box_x + zoom_box_width  - 1,	// Right edge (inclusive)
		zoom_box_y + zoom_box_height - 1,	// Bottom edge (inclusive)
		UI_COLOR,							// Border color
		ZOOM_BOX_BORDER_WIDTH				// Border thickness
	);
}

//----------------------------------------------------------------------------
// Method: Application::EraseZoomBox
//
// Description:
//
//   Restores the saved pixels under the zoom box border, removing
//   the overlay and revealing the original fractal image beneath it.
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

void Application::EraseZoomBox ( void )
{
	// Restore the original pixels beneath the zoom box border.

	RestoreBorderPixels ();
}

//----------------------------------------------------------------------------
// Method: Application::ClampZoomBoxToScreen
//
// Description:
//
//   Ensures the zoom box stays fully within the screen bounds
//   (0..SCREEN_WIDTH-1 horizontally, 0..SCREEN_HEIGHT-1 vertically).
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

void Application::ClampZoomBoxToScreen ( void )
{
	// Clamp left edge.

	if ( zoom_box_x < 0 )
	{
		zoom_box_x = 0;
	}

	// Clamp top edge.

	if ( zoom_box_y < 0 )
	{
		zoom_box_y = 0;
	}

	// Clamp right edge.

	if ( zoom_box_x + zoom_box_width > screen_width )
	{
		zoom_box_x = screen_width - zoom_box_width;
	}

	// Clamp bottom edge.

	if ( zoom_box_y + zoom_box_height > screen_height )
	{
		zoom_box_y = screen_height - zoom_box_height;
	}
}

//----------------------------------------------------------------------------
// Method: Application::MoveZoomBox
//
// Description:
//
//   Moves the zoom box by the given delta in screen pixels and
//   clamps the result to the screen bounds.
//
// Arguments:
//
//   - delta_x : Horizontal movement in pixels (positive = right).
//   - delta_y : Vertical movement in pixels (positive = down).
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void Application::MoveZoomBox ( int delta_x, int delta_y )
{
	// Apply the movement delta to the zoom box position.

	zoom_box_x += delta_x;
	zoom_box_y += delta_y;

	// Ensure the zoom box remains within the screen bounds.

	ClampZoomBoxToScreen ();
}

//----------------------------------------------------------------------------
// Method: Application::ResizeZoomBox
//
// Description:
//
//   Grows or shrinks the zoom box by delta_width pixels in width,
//   deriving the height from the screen's 4:3 aspect ratio. The zoom box
//   resizes symmetrically around its current center point and is clamped
//   to the minimum and maximum allowable sizes and screen bounds.
//
// Arguments:
//
//   - delta_width : Change in width (positive = grow, negative = shrink).
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void Application::ResizeZoomBox ( int delta_width )
{
	int new_width;		// Proposed zoom box width after resize
	int new_height;		// Proposed zoom box height after resize
	int center_x;		// Horizontal center of the zoom box before resize
	int center_y;		// Vertical center of the zoom box before resize

	// Compute the new width, clamped to valid range.

	new_width = zoom_box_width + delta_width;

	// Enforce minimum width.

	if ( new_width < ZOOM_BOX_MIN_WIDTH )
	{
		new_width = ZOOM_BOX_MIN_WIDTH;
	}

	// Enforce maximum width (cannot exceed screen).

	if ( new_width > screen_width )
	{
		new_width = screen_width;
	}

	// Derive height from the 4:3 aspect ratio.

	new_height = new_width * ASPECT_RATIO_HEIGHT / ASPECT_RATIO_WIDTH;

	// If height exceeds the screen, clamp and recompute width.

	if ( new_height > screen_height )
	{
		new_height = screen_height;
		new_width  = new_height * ASPECT_RATIO_WIDTH / ASPECT_RATIO_HEIGHT;
	}

	// Record the current center point for symmetric resizing.

	center_x = zoom_box_x + zoom_box_width  / 2;
	center_y = zoom_box_y + zoom_box_height / 2;

	// Apply the new dimensions, re-centering around the original midpoint.

	zoom_box_width  = new_width;
	zoom_box_height = new_height;
	zoom_box_x      = center_x - zoom_box_width  / 2;
	zoom_box_y      = center_y - zoom_box_height / 2;

	// Ensure the resized zoom box remains within the screen bounds.

	ClampZoomBoxToScreen ();
}

//----------------------------------------------------------------------------
// Method: Application::ComputeMovementSpeed
//
// Description:
//
//   Computes the movement speed for an arrow key press based on how long
//   the same direction has been held. If the same arrow key is pressed
//   again within ACCELERATION_THRESHOLD BIOS ticks (~220ms), the press
//   counter increments and the speed increases. If a different direction
//   is pressed or the gap exceeds the threshold, the counter resets.
//
// Arguments:
//
//   - scancode : The arrow key scancode (72, 75, 77, or 80).
//
// Returns:
//
//   - Movement speed in pixels (1 or more).
//
//----------------------------------------------------------------------------

int Application::ComputeMovementSpeed ( int scancode )
{
	long current_timestamp;
	long elapsed;
	int  speed;

	current_timestamp = clock ();

	// clock() in Turbo C++ returns ticks at CLK_TCK (18.2 Hz).
	// ACCELERATION_THRESHOLD ticks (~220ms) is the gap threshold.

	elapsed = current_timestamp - last_arrow_timestamp;

	// If the same direction was pressed within the threshold, accelerate.

	if ( scancode == last_arrow_direction && elapsed <= ACCELERATION_THRESHOLD )
	{
		arrow_press_count++;
	}
	else
	{
		arrow_press_count = 0;
	}

	// Update the direction and timestamp for the next call.

	last_arrow_direction = scancode;
	last_arrow_timestamp = current_timestamp;

	// Speed starts at 1 pixel and increases by 1 for every
	// ACCELERATION_DIVISOR consecutive presses.

	speed = 1 + arrow_press_count / ACCELERATION_DIVISOR;

	return speed;
}

//----------------------------------------------------------------------------
// Method: Application::ProcessArrowKey
//
// Description:
//
//   Handles one arrow key event by computing the accelerated movement
//   speed, erasing the current zoom box overlay, moving the zoom box
//   by the appropriate delta, and redrawing it at the new position.
//
// Arguments:
//
//   - scancode : The arrow key scancode (72=up, 80=down, 75=left, 77=right).
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void Application::ProcessArrowKey ( int scancode )
{
	int speed;		// Movement speed in pixels for this keypress
	int delta_x;	// Horizontal movement delta
	int delta_y;	// Vertical movement delta

	// Compute the accelerated movement speed for this keypress.

	speed   = ComputeMovementSpeed ( scancode );
	delta_x = 0;
	delta_y = 0;

	// Convert the arrow scancode to a directional delta.

	switch ( scancode )
	{
		case SCANCODE_UP:    delta_y = -speed; break;
		case SCANCODE_DOWN:  delta_y =  speed; break;
		case SCANCODE_LEFT:  delta_x = -speed; break;
		case SCANCODE_RIGHT: delta_x =  speed; break;
	}

	// Erase the zoom box, move it, and redraw at the new position.

	WaitRetrace  ();
	EraseZoomBox ();
	MoveZoomBox  ( delta_x, delta_y );
	DrawZoomBox  ();
}

//----------------------------------------------------------------------------
// Method: Application::ComputeZoomViewport
//
// Description:
//
//   Maps the zoom box's screen pixel coordinates to complex plane
//   coordinates, replacing the current viewport with the selected
//   sub-region. After this call, the next RenderMandelbrot will render
//   only the portion of the fractal that was enclosed by the zoom box.
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

void Application::ComputeZoomViewport ( void )
{
	double pixel_real_scale;		// Pixels-to-complex-plane scale factor (horizontal)
	double pixel_imaginary_scale;	// Pixels-to-complex-plane scale factor (vertical)
	double new_real_min;			// New viewport left boundary (real axis)
	double new_real_max;			// New viewport right boundary (real axis)
	double new_imaginary_min;		// New viewport bottom boundary (imaginary axis)
	double new_imaginary_max;		// New viewport top boundary (imaginary axis)

	// Double-precision shadow variables for cast-free arithmetic.

	double screen_width_d;			// Double-precision screen width for pixel-to-complex mapping
	double screen_height_d;			// Double-precision screen height for pixel-to-complex mapping
	double zoom_box_x_d;			// Double-precision zoom box left edge
	double zoom_box_y_d;			// Double-precision zoom box top edge
	double zoom_box_right_d;		// Double-precision zoom box right edge (x + width)
	double zoom_box_bottom_d;		// Double-precision zoom box bottom edge (y + height)

	// Assign shadow variables.

	screen_width_d    = screen_width;
	screen_height_d   = screen_height;
	zoom_box_x_d      = zoom_box_x;
	zoom_box_y_d      = zoom_box_y;
	zoom_box_right_d  = zoom_box_x + zoom_box_width;
	zoom_box_bottom_d = zoom_box_y + zoom_box_height;

	// Compute the current mapping from pixels to complex plane units.

	pixel_real_scale      = ( viewport_real_max      - viewport_real_min      ) / screen_width_d;
	pixel_imaginary_scale = ( viewport_imaginary_max  - viewport_imaginary_min ) / screen_height_d;

	// Map the zoom box corners to complex plane coordinates.

	new_real_min      = viewport_real_min      + zoom_box_x_d      * pixel_real_scale;
	new_real_max      = viewport_real_min      + zoom_box_right_d  * pixel_real_scale;
	new_imaginary_min = viewport_imaginary_min + zoom_box_y_d      * pixel_imaginary_scale;
	new_imaginary_max = viewport_imaginary_min + zoom_box_bottom_d * pixel_imaginary_scale;

	// Replace the current viewport with the zoomed sub-region.

	viewport_real_min      = new_real_min;
	viewport_real_max      = new_real_max;
	viewport_imaginary_min = new_imaginary_min;
	viewport_imaginary_max = new_imaginary_max;
}

// NOTE:
//
// HUD visual display was removed due to BIOS ROM font incompatibility with VESA SVGA modes. 
// Keyboard controls for real-time setting changes ('a', '0'-'5') are retained.
//
// I'll try fix this later.

//----------------------------------------------------------------------------
// Method: Application::FindNextFilename
//
// Description:
//
//   Scans for the lowest available filename in the sequence FRAC0000.CEL
//   through FRAC9999.CEL, prepending the image_path if one has been
//   configured. A filename is available if no file with that name exists.
//
//   This allows files to be saved without overwriting existing saves,
//   and fills gaps left by deleted files before appending new numbers.
//
// Arguments:
//
//   - filename : Pointer to a buffer of at least MAX_FILENAME_LENGTH bytes
//                to receive the generated path and filename.
//
// Returns:
//
//   - None. The filename buffer is filled with the result.
//
//----------------------------------------------------------------------------

void Application::FindNextFilename ( char *filename )
{
	int   number;								// Current file number in the sequence (0-9999)
	int   path_length;							// Length of the path prefix string
	FILE *test;									// File handle for existence check
	char  path_prefix [ MAX_PATH_LENGTH + 2 ];	// Directory prefix with trailing backslash

	// Build path prefix from image_path, adding a trailing backslash
	// if the path is non-empty and does not already end with one.

	path_prefix [ 0 ] = '\0';

	if ( image_path [ 0 ] != '\0' )
	{
		// Copy the configured image path and measure its length.

		strcpy ( path_prefix, image_path );
		path_length = strlen ( path_prefix );

		// Append a trailing backslash if one is not already present.

		if ( path_prefix [ path_length - 1 ] != '\\' && path_prefix [ path_length - 1 ] != '/' )
		{
			path_prefix [ path_length ]     = '\\';
			path_prefix [ path_length + 1 ] = '\0';
		}
	}

	// Scan for the first available filename in the sequence.

	for ( number = 0; number < CEL_MAX_FILES; number++ )
	{
		// Build the candidate filename and attempt to open it.

		sprintf ( filename, "%sFRAC%04d.CEL", path_prefix, number );
		test = fopen ( filename, "rb" );

		// If the file does not exist, this slot is available.

		if ( !test )
		{
			// File does not exist. This is the next available name.

			return;
		}

		// File exists. Close it and try the next number.

		fclose ( test );
	}

	// All 10000 slots are occupied. Fall back to the last slot.

	sprintf ( filename, "%sFRAC9999.CEL", path_prefix );
}

//----------------------------------------------------------------------------
// Method: Application::Save
//
// Description:
//
//   Saves the current screen contents and palette to an Autodesk Animator
//   CEL file. The file is named using the next available slot in the
//   sequence FRAC0000.CEL through FRAC9999.CEL.
//
//   The CEL file format:
//
//     Offset   Size   Field
//     ------   ----   ---------------------------
//       0        2    Magic number (0x9119)
//       2        2    Width
//       4        2    Height
//       6        2    X offset (0)
//       8        2    Y offset (0)
//      10        2    Bits per pixel (8)
//      12        2    Compression (0 = none)
//      14        4    Data size (width * height)
//      18      768    Palette (256 RGB triplets, VGA DAC 0-63 format)
//     786      W*H    Raw pixel data, row-major order
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

void Application::Save ( void )
{
	char          filename [ MAX_FILENAME_LENGTH ];	// Output file path
	FILE         *file;								// File handle for binary writing
	int           pixel_x;							// Current pixel column during scanline read
	int           pixel_y;							// Current pixel row (scanline index)
	unsigned char scanline [ SCREEN_WIDTH ];		// One scanline of pixel data read from video memory
	WORD          magic;							// CEL file magic number (0x9119)
	WORD          width;							// Image width in pixels
	WORD          height;							// Image height in pixels
	WORD          x_offset;							// CEL x offset (always 0)
	WORD          y_offset;							// CEL y offset (always 0)
	WORD          bits_per_pixel;					// Bits per pixel (always 8)
	WORD          compression;						// Compression type (always 0 = none)
	unsigned long data_size;						// Total pixel data size in bytes (width * height)
	unsigned long screen_width_ul;					// 32-bit screen width for data size computation
	unsigned long screen_height_ul;					// 32-bit screen height for data size computation

	// Determine the next available filename.

	FindNextFilename ( filename );

	// Open the file for binary writing.

	file = fopen ( filename, "wb" );

	if ( !file )
	{
		return;
	}

	// Write the CEL file header.

	magic            = CEL_MAGIC;
	width            = screen_width;
	height           = screen_height;
	x_offset         = 0;
	y_offset         = 0;
	bits_per_pixel   = CEL_BITS_PER_PIXEL;
	compression      = CEL_COMPRESSION_NONE;
	screen_width_ul  = screen_width;
	screen_height_ul = screen_height;
	data_size        = screen_width_ul * screen_height_ul;

	fwrite ( &magic,          2, 1, file );
	fwrite ( &width,          2, 1, file );
	fwrite ( &height,         2, 1, file );
	fwrite ( &x_offset,       2, 1, file );
	fwrite ( &y_offset,       2, 1, file );
	fwrite ( &bits_per_pixel, 2, 1, file );
	fwrite ( &compression,    2, 1, file );
	fwrite ( &data_size,      4, 1, file );

	// Write the palette (768 bytes, VGA DAC format 0-63 per channel).

	fwrite ( palette, 1, PALETTE_BYTES, file );

	// Write pixel data one scanline at a time, reading each pixel
	// from video memory via GetPixel.

	for ( pixel_y = 0; pixel_y < screen_height; pixel_y++ )
	{
		for ( pixel_x = 0; pixel_x < screen_width; pixel_x++ )
		{
			scanline [ pixel_x ] = GetPixel ( pixel_x, pixel_y );
		}



		fwrite ( scanline, 1, screen_width, file );
	}


	// Close the output file.

	fclose ( file );
}

//----------------------------------------------------------------------------
// Method: Application::Load
//
// Description:
//
//   Loads an Autodesk Animator CEL file and displays it on screen. Reads
//   the file header to obtain the image dimensions, loads the embedded
//   palette into the VGA DAC, and writes the pixel data to video memory.
//
//   If the image dimensions differ from the screen resolution, only the
//   overlapping region is displayed.
//
// Arguments:
//
//   - filename : Path to the CEL file to load.
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void Application::Load ( const char *filename )
{
	FILE         *file;							// File handle for binary reading
	int           pixel_x;						// Current pixel column during scanline write
	int           pixel_y;						// Current pixel row (scanline index)
	unsigned char scanline [ SCREEN_WIDTH ];	// One scanline of pixel data read from file
	WORD          magic;						// CEL file magic number read from header
	WORD          width;						// Image width read from header
	WORD          height;						// Image height read from header
	int           read_width;					// Effective scanline width (min of image and screen)

	// Open the file for binary reading.

	file = fopen ( filename, "rb" );

	if ( !file )
	{
		return;
	}

	// Read and validate the magic number.

	fread ( &magic, 2, 1, file );

	if ( magic != CEL_MAGIC )
	{
		fclose ( file );
		return;
	}

	// Read image dimensions.

	fread ( &width,  2, 1, file );
	fread ( &height, 2, 1, file );

	// Skip x_offset, y_offset, bits_per_pixel, compression, data_size
	// (2 + 2 + 2 + 2 + 4 = 12 bytes).

	fseek ( file, 12, SEEK_CUR );

	// Read the palette and upload it to the VGA DAC.

	fread ( palette, 1, PALETTE_BYTES, file );
	SetPalette ( 0, PALETTE_SIZE, FP_SEG ( palette ), FP_OFF ( palette ) );

	// Read pixel data one scanline at a time. Clip to screen bounds
	// if the image is larger than the display.

	read_width = ( width < screen_width ) ? width : screen_width;

	for ( pixel_y = 0; pixel_y < height && pixel_y < screen_height; pixel_y++ )
	{
		fread ( scanline, 1, width, file );

		for ( pixel_x = 0; pixel_x < read_width; pixel_x++ )
		{
			PutPixel ( pixel_x, pixel_y, scanline [ pixel_x ] );
		}
	}


	// Close the input file.

	fclose ( file );
}

//----------------------------------------------------------------------------
// Method: Application::Run
//
// Description:
//
//   Main application entry point. Loads settings from the specified INI
//   configuration file, sets VESA mode 105h (1024x768, 256 colors),
//   initializes the fractal palette, optionally displays a palette test
//   screen, renders the initial Mandelbrot set, and then enters the
//   interactive main loop.
//
//   The interactive loop supports:
//
//     TAB         Toggle the zoom box overlay on/off.
//     Arrow keys  Move the zoom box (accelerates when held).
//     = / -       Increase / decrease zoom box size (4:3 aspect ratio).
//     Enter       Zoom into the selected zoom box region and re-render.
//     F1          Toggle the heads-up display (HUD) on/off.
//     a           Toggle antialiasing on/off.
//     0-5         Set supersampling level (0=off, 2=2x2, ... 5=5x5).
//     S           Save the current image to a CEL file.
//     ESC         Exit the program.
//
//   ESC may also be pressed during a render to abort and exit after the
//   current scanline completes. TAB may be pressed during a render to
//   interrupt it and activate the zoom box, allowing the user to select
//   a region to zoom into even from a partially rendered image.
//
// Arguments:
//
//   - config_file : Path to the INI configuration file.
//
// Returns:
//
//   - None.
//
//----------------------------------------------------------------------------

void Application::Run ( const char *config_file )
{
	int   finished;			// Main loop exit flag
	int   key;				// Keystroke value from getch()
	int   scancode;			// Extended key scancode (second byte)
	int   render_result;	// Return code from RenderMandelbrot (0=done, 1=ESC, 2=TAB)
	int   i;				// Loop counter for INI parsing
	WORD  vesa_mode;		// VESA mode number for the selected resolution
	INI   config;			// INI file parser instance
	char *value;			// Pointer to the current INI value string

	// Load configuration from the INI file. 
	// If the file cannot be opened, default values (all zeros, empty path) are used.

	if ( config.Load ( config_file ) == 0 )
	{
		value = config.GetValue ( "Settings", "width" );
		if ( value ) screen_width = atoi ( value );

		value = config.GetValue ( "Settings", "height" );
		if ( value ) screen_height = atoi ( value );

		value = config.GetValue ( "Settings", "antialiasing" );
		if ( value ) stored_antialiasing = atoi ( value );

		value = config.GetValue ( "Settings", "supersampling" );
		if ( value ) stored_supersampling = atoi ( value );

		value = config.GetValue ( "Settings", "debug" );
		if ( value ) stored_debug_mode = atoi ( value );

		value = config.GetValue ( "Settings", "normalize_color_range" );
		if ( value ) stored_normalize_color_range = atoi ( value );

		value = config.GetValue ( "Settings", "ui_red" );
		if ( value ) ui_red = atoi ( value );

		value = config.GetValue ( "Settings", "ui_green" );
		if ( value ) ui_green = atoi ( value );

		value = config.GetValue ( "Settings", "ui_blue" );
		if ( value ) ui_blue = atoi ( value );

		value = config.GetValue ( "Settings", "image_path" );
		if ( value && value [ 0 ] != '\0' )
		{
			strncpy ( image_path, value, MAX_PATH_LENGTH - 1 );
			image_path [ MAX_PATH_LENGTH - 1 ] = '\0';
		}

		// Load palette control vectors from INI.
		//
		// - Each control vector is specified as a separate key
		//   (palette_control_vector_0, palette_control_vector_1, ...) with a 
		//   comma-separated R,G,B value.
		//
		// - Scanning stops at the first missing key. If at least 2 control 
		//   vectors are found, they replace the defaults set in the constructor.

		{
			char key_name [ 32 ];
			int  rgb [ 3 ];
			int  count;
			int  channel_count;

			count = 0;

			for ( i = 0; i < MAX_PALETTE_CONTROL_VECTORS; i++ )
			{
				// Build the INI key name for this control vector.

				sprintf ( key_name, "palette_control_vector_%d", i );
				value = config.GetValue ( "Settings", key_name );

				// Stop scanning if the key is missing.

				if ( !value )
				{
					break;
				}

				// Parse the comma-separated R,G,B value.

				channel_count = ParseIntegerList ( value, rgb, 3 );

				if ( channel_count != 3 )
				{
					break;
				}

				// Store the parsed control vector.

				palette_vector_red   [ i ] = rgb [ 0 ];
				palette_vector_green [ i ] = rgb [ 1 ];
				palette_vector_blue  [ i ] = rgb [ 2 ];
				count++;
			}

			// Accept the INI vectors only if at least 2 were found.

			if ( count >= 2 )
			{
				palette_vector_count = count;
			}
		}
	}

	// Select the VESA mode based on the configured screen resolution.

	if ( screen_width == 640 && screen_height == 400 )
	{
		vesa_mode = VESA_MODE_640x400;
	}
	else if ( screen_width == 640 && screen_height == 480 )
	{
		vesa_mode = VESA_MODE_640x480;
	}
	else if ( screen_width == 800 && screen_height == 600 )
	{
		vesa_mode = VESA_MODE_800x600;
	}
	else
	{
		// Default to 1024x768 for any unrecognized resolution.

		screen_width  = 1024;
		screen_height = 768;
		vesa_mode     = VESA_MODE_1024x768;
	}

	// Re-center the zoom box for the actual screen resolution.

	zoom_box_x = ( screen_width  - zoom_box_width  ) / 2;
	zoom_box_y = ( screen_height - zoom_box_height ) / 2;

	// Attempt to set the selected VESA mode.

	if ( !SetVesaMode ( vesa_mode ) )
	{
		printf ( "Error: VESA mode 0x%04X (%dx%dx256) is not supported.\n",
			vesa_mode, screen_width, screen_height );
		printf ( "Please ensure your SVGA adapter supports VESA VBE 1.2.\n" );
		return;
	}

	// Generate the fractal-optimized palette and set the UI overlay
	// color from the INI settings.

	InitializeFractalPalette ();

	palette [ UI_COLOR * 3 + 0 ] = (unsigned char) ui_red;
	palette [ UI_COLOR * 3 + 1 ] = (unsigned char) ui_green;
	palette [ UI_COLOR * 3 + 2 ] = (unsigned char) ui_blue;

	SetPalette ( 0, PALETTE_SIZE, FP_SEG ( palette ), FP_OFF ( palette ) );

	// If debug mode is enabled, display the palette test screen and
	// wait for a keypress before continuing to render.

	if ( stored_debug_mode )
	{
		TestPalette ();
		getch ();
	}

	// Render the initial Mandelbrot set. If ESC is pressed during
	// rendering, exit immediately.

	render_result = RenderMandelbrot ( stored_antialiasing, stored_supersampling );

	if ( render_result == 1 )
	{
		SetTextMode ( TEXT_MODE_25_ROWS );
		return;
	}

	// Post-processing: normalize the color range if enabled.
	// Skipped if the render was interrupted by TAB, since the
	// image is incomplete.

	if ( render_result == 0 && stored_normalize_color_range )
	{
		NormalizeColorRange ();

		// Flush the keyboard buffer. Keys pressed during the
		// normalization pass accumulate in the BIOS buffer and
		// would be processed as stale input by the main loop.

		while ( kbhit () ) getch ();
	}

	// If TAB was pressed during the initial render, activate the
	// zoom box so the user can select a region immediately.

	if ( render_result == 2 )
	{
		AllocateZoomBoxBuffers ();
		zoom_box_visible = 1;
		DrawZoomBox ();
	}

	// Interactive main loop.

	finished = 0;

	while ( !finished )
	{
		if ( kbhit () )
		{
			key = getch ();

			// Check for extended keycode (arrow keys, function keys).

			if ( key == KEY_EXTENDED )
			{
				scancode = getch ();

				switch ( scancode )
				{
					case SCANCODE_UP:
					case SCANCODE_DOWN:
					case SCANCODE_LEFT:
					case SCANCODE_RIGHT:

						if ( zoom_box_visible )
						{
							ProcessArrowKey ( scancode );
						}
						break;

				}
			}
			else
			{
				// Standard (non-extended) keys.

				switch ( key )
				{
					case KEY_ESCAPE:

						finished = 1;
						break;

					case KEY_TAB:

						// Toggle zoom box visibility.

						WaitRetrace ();

						if ( zoom_box_visible )
						{
							EraseZoomBox ();
							zoom_box_visible = 0;
						}
						else
						{
							AllocateZoomBoxBuffers ();
							zoom_box_visible = 1;
							DrawZoomBox ();
						}
						break;

					case KEY_EQUALS:

						// Increase zoom box size.

						if ( zoom_box_visible )
						{
							WaitRetrace ();
							EraseZoomBox ();
							ResizeZoomBox ( ZOOM_BOX_SIZE_STEP );
							DrawZoomBox ();
						}
						break;

					case KEY_MINUS:

						// Decrease zoom box size.

						if ( zoom_box_visible )
						{
							WaitRetrace ();
							EraseZoomBox ();
							ResizeZoomBox ( -ZOOM_BOX_SIZE_STEP );
							DrawZoomBox ();
						}
						break;

					case 'a':
					case 'A':

						// Toggle antialiasing.

						stored_antialiasing = stored_antialiasing ? 0 : 1;
						break;

					case '0':
					case '1':
					case '2':
					case '3':
					case '4':
					case '5':

						// Set supersampling level.

						stored_supersampling = key - '0';
						break;

					case KEY_ENTER:

						// Zoom into the selected region.

						if ( zoom_box_visible )
						{
							WaitRetrace ();

							// Erase zoom box before re-render.

							EraseZoomBox ();
							zoom_box_visible = 0;

							// Compute the new viewport from the zoom box.

							ComputeZoomViewport ();

							// Re-render the fractal.

							render_result = RenderMandelbrot (
								stored_antialiasing,
								stored_supersampling
							);

							if ( render_result == 0 && stored_normalize_color_range )
							{
								NormalizeColorRange ();

								// Flush the keyboard buffer. Keys pressed
								// during normalization would be processed
								// as stale input by the main loop.

								while ( kbhit () ) getch ();
							}

							if ( render_result == 1 )
							{
								finished = 1;
							}

							// If TAB was pressed during the zoom render,
							// activate the zoom box so the user can select
							// a new region from the partial render.

							if ( render_result == 2 )
							{
								AllocateZoomBoxBuffers ();
								zoom_box_visible = 1;
								DrawZoomBox ();
							}
						}
						break;

					case 's':
					case 'S':

						// Save the current image. If the zoom box is
						// visible, erase it before saving so the CEL file
						// contains only fractal data, then redraw it.

						if ( zoom_box_visible )
						{
							WaitRetrace ();
							EraseZoomBox ();
							Save ();
							DrawZoomBox ();
						}
						else
						{
							Save ();
						}
						break;
				}
			}
		}
	}

	// Restore 80x25 text mode.

	SetTextMode ( TEXT_MODE_25_ROWS );
}

//----------------------------------------------------------------------------
