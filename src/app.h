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

#ifndef _APPLICATION
#define _APPLICATION

#include "vesa.h"

//----------------------------------------------------------------------------
// Constants
//----------------------------------------------------------------------------

#define SCREEN_WIDTH            	1024    // Maximum screen width (for buffer sizing)
#define SCREEN_HEIGHT           	768     // Maximum screen height (for buffer sizing)

#define PALETTE_SIZE            	256
#define PALETTE_BYTES           	768     // 256 entries * 3 bytes (R, G, B)

#define MAX_PATH_LENGTH         	80      // Maximum length of a file path string
#define MAX_FILENAME_LENGTH     	128		// Maximum length of a full file path + name

#define DEFAULT_CONFIG_FILE     	"CONFIG.INI"

//----------------------------------------------------------------------------
// Key Constants
//----------------------------------------------------------------------------

#define KEY_ESCAPE                  27
#define KEY_ENTER                   13
#define KEY_TAB                     9
#define KEY_EQUALS                  61      // '=' character
#define KEY_MINUS                   45		// '-' character
#define KEY_EXTENDED                0       // First byte of extended keycode

//----------------------------------------------------------------------------
// Scancode Constants (second byte of extended keycodes)
//----------------------------------------------------------------------------

#define SCANCODE_UP                 72
#define SCANCODE_DOWN               80
#define SCANCODE_LEFT               75
#define SCANCODE_RIGHT              77
#define SCANCODE_F1                 59

//----------------------------------------------------------------------------
// Mandelbrot Constants
//----------------------------------------------------------------------------

#define MAX_ITERATIONS              255
#define ESCAPE_RADIUS_SQUARED       4.0

#define INITIAL_FRACTAL_X_MIN      -2.5
#define INITIAL_FRACTAL_X_MAX       1.5
#define INITIAL_FRACTAL_Y_MIN      -1.5
#define INITIAL_FRACTAL_Y_MAX       1.5

//----------------------------------------------------------------------------
// Palette Partition Constants
//----------------------------------------------------------------------------

#define FRACTAL_PALETTE_ENTRIES     254		// Usable fractal indices (1-254)
#define FRACTAL_PALETTE_ENTRIES_D   254.0	// Double-precision version for fmod/division
#define UI_COLOR                    255     // Single palette index for all UI overlays
#define MAX_PALETTE_CONTROL_VECTORS 16   	// Maximum gradient control vectors

//----------------------------------------------------------------------------
// Zoom Box Constants
//----------------------------------------------------------------------------

#define ZOOM_BOX_MIN_WIDTH          16
#define ZOOM_BOX_DEFAULT_WIDTH      256
#define ZOOM_BOX_SIZE_STEP          16
#define ZOOM_BOX_BORDER_WIDTH       2
#define ZOOM_BOX_BUFFER_SIZE        4096

#define ASPECT_RATIO_WIDTH          4
#define ASPECT_RATIO_HEIGHT         3

//----------------------------------------------------------------------------
// Arrow Key Acceleration Constants
//----------------------------------------------------------------------------

#define ACCELERATION_THRESHOLD      4		// BIOS ticks (~220ms) before speed resets
#define ACCELERATION_DIVISOR        10		// arrow_press_count / this = bonus speed

//----------------------------------------------------------------------------
// CEL File Constants
//----------------------------------------------------------------------------

#define CEL_MAGIC                   0x9119
#define CEL_BITS_PER_PIXEL          8
#define CEL_COMPRESSION_NONE        0
#define CEL_HEADER_SIZE             18
#define CEL_MAX_FILES               10000

//----------------------------------------------------------------------------
// Class: Application
//----------------------------------------------------------------------------

class Application
{
public:

	// Constructor / Destructor

	Application  ( void );
	~Application ( void );

	// Public Methods

	void Run ( const char *config_file );

private:

	// Private Methods - Palette

	void InitializeFractalPalette 	( void );
	void TestPalette              	( void );
	int  ParseIntegerList         	( const char *string, int *output, int max_count );

	// Private Methods - Mandelbrot

	int    ComputeIterations  		( double c_real, double c_imaginary, double *magnitude_squared );
	double ComputeSmoothValue 		( int iteration, double magnitude_squared );
	BYTE   MapColor           		( int iteration );
	BYTE   MapSmoothColor     		( int iteration, double magnitude_squared );
	int    RenderMandelbrot   		( int antialiasing, int supersampling );

	// Private Methods - Post-Processing

	void NormalizeColorRange 		( void );

	// Private Methods - Zoom Box

	void AllocateZoomBoxBuffers 	( void );
	void SaveBorderPixels       	( void );
	void RestoreBorderPixels    	( void );
	void DrawZoomBox            	( void );
	void EraseZoomBox           	( void );
	void ClampZoomBoxToScreen   	( void );
	void MoveZoomBox            	( int delta_x, int delta_y );
	void ResizeZoomBox          	( int delta_width );
	int  ComputeMovementSpeed   	( int scancode );
	void ProcessArrowKey        	( int scancode );
	void ComputeZoomViewport    	( void );

	// Private Methods - File I/O

	void Save             			( void );
	void Load             			( const char *filename );
	void FindNextFilename 			( char *filename );

	// Private Data - Palette

	int           palette_vector_count;
	unsigned char palette 				[ PALETTE_BYTES ];	
	int           palette_vector_index	[ MAX_PALETTE_CONTROL_VECTORS ];
	int           palette_vector_red   	[ MAX_PALETTE_CONTROL_VECTORS ];
	int           palette_vector_green 	[ MAX_PALETTE_CONTROL_VECTORS ];
	int           palette_vector_blue  	[ MAX_PALETTE_CONTROL_VECTORS ];

	// Private Data - Screen Resolution (runtime, from INI file)

	int screen_width;
	int screen_height;

	// Private Data - Viewport (complex plane coordinates)

	double viewport_real_min;
	double viewport_real_max;
	double viewport_imaginary_min;
	double viewport_imaginary_max;

	// Private Data - Zoom Box (screen pixel coordinates)

	int zoom_box_visible;
	int zoom_box_x;
	int zoom_box_y;
	int zoom_box_width;
	int zoom_box_height;

	// Private Data - Zoom Box Border Save Buffers (far heap)

	BYTE *border_top_buffer;
	BYTE *border_bottom_buffer;
	BYTE *border_left_buffer;
	BYTE *border_right_buffer;

	// Private Data - Arrow Key Acceleration

	int  last_arrow_direction;
	int  arrow_press_count;
	long last_arrow_timestamp;

	// Private Data - Stored Settings (from INI, modifiable at runtime)

	int stored_antialiasing;
	int stored_supersampling;
	int stored_debug_mode;
	int stored_normalize_color_range;
	int ui_red;
	int ui_green;
	int ui_blue;

	// Private Data - Image Save Path

	char image_path [ MAX_PATH_LENGTH ];
};

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
