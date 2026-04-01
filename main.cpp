//****************************************************************************
// Program: Mandelbrot Set Renderer
// Version: 3.3
// Date:    1992-07-20
// Author:  Rohin Gosling
//
// Description:
//
//   Entry point for the Mandelbrot set renderer. Parses command line
//   arguments and launches the application.
//
//   All rendering settings (antialiasing, supersampling, debug mode,
//   image save path) are read from an INI configuration file. The
//   default configuration file is CONFIG.INI in the current directory.
//
// Usage:
//
//   FRACTAL                          Use default CONFIG.INI.
//   FRACTAL -config "path\file.ini"  Use specified configuration file.
//
// Compilation:
//
//   Borland Turbo C++ 3.1, large memory model.
//   VESA graphics library compiled as C with inline assembly.
//
//****************************************************************************

#include <string.h>
#include "app.h"

//----------------------------------------------------------------------------
// Function: main
//
// Description:
//
//   Program entry point. Parses the command line for the -config flag,
//   creates an Application instance, and runs it with the specified
//   configuration file.
//
// Arguments:
//
//   - argc : Number of command line arguments.
//   - argv : Array of command line argument strings.
//
// Returns:
//
//   - 0 on success.
//
//----------------------------------------------------------------------------

int main ( int argc, char *argv [] )
{
	const char *config_file;		// Path to the INI configuration file
	int i;							// Command line argument loop counter

	Application application;

	// Default configuration file.

	config_file = DEFAULT_CONFIG_FILE;

	// Scan command line arguments for the -config flag.

	for ( i = 1; i < argc; i++ )
	{
		// Check for the -config flag (case-insensitive).

		if ( stricmp ( argv [ i ], "-config" ) == 0 )
		{
			// Use the next argument as the configuration file path.

			if ( i + 1 < argc )
			{
				config_file = argv [ i + 1 ];
				i++;
			}
		}
	}

	// Run the application.

	application.Run ( config_file );

	// Exit with success.

	return 0;
}

//----------------------------------------------------------------------------
