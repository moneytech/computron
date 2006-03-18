/* vomit.c
 * Main initialization procedures
 *
 */

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "vomit.h"
#include "debug.h"

bool	verbose,
		disklog, trapint, rmpeek,
		iopeek, mempeek,
		callpeek, iplog;

bool g_try_run = false;
bool g_break_pressed = false;

static bool exiting = 0;

#ifdef VM_DEBUG
	word BCS, BIP;
#endif

#define FLAGARG( a, b ) else if( !strcmp( argv[1], a )) { b = true; argc--; argv++; }

int
main( int argc, char **argv )
{
	const char *try_path = 0L;

	while( argc > 1 )
	{
		if( 0 ) {}
		#ifdef VM_DEBUG
		FLAGARG( "--callpeek", callpeek )
		FLAGARG( "--verbose",  verbose )
		FLAGARG( "--disklog",  disklog )
		FLAGARG( "--trapint",  trapint )
		FLAGARG( "--mempeek",  mempeek )
		FLAGARG( "--rmpeek",   rmpeek )
		FLAGARG( "--iopeek",   iopeek )
		FLAGARG( "--iplog",    iplog )
		#endif

		else if( argc > 2 && !strcmp( argv[1], "--try" ))
		{
			try_path = argv[2];
			g_try_run = true;
			argc -= 2, argv += 2;
		}
		else
		{
			fprintf( stderr, "Unknown option: %s\n", argv[1] );
			return 1;
		}
	}

	vm_init();
	vm_loadconf();
	cpu_genmap();

	vlog( VM_INITMSG, "Registering SIGINT handler" );
	signal( SIGINT, vm_cbreak );

	cpu_state = CPU_ALIVE;

	if( g_try_run )
	{
		FILE *fp = fopen( try_path, "rb" );
		if( !fp )
		{
			perror( try_path );
			return 1;
		}

		printf( "Loading %s into 1000:0000.\n", try_path );

		/* Read up to MAX_FILESIZE bytes into 1000:0000. */
		int x = fread( mem_space + 0x10000, 1, MAX_FILESIZE, fp );
		printf( "%d bytes read.\n", x );

		fclose( fp );

		IF = 0;
		CS = 0x1000;
		IP = 0x0000;
		StackPointer = 0x1000;
	}

	cpu_main();

	vm_exit( 0 );

	return 0;
}

void vm_init() {
	dword i;
	vlog( VM_INITMSG, "Initializing memory" );
    mem_init();
	vlog( VM_INITMSG, "Initializing CPU" );
    cpu_init();
	int_init();
	vlog( VM_INITMSG, "Initializing video BIOS" );
	video_bios_init();

	vlog( VM_INITMSG, "Initializing user interface" );
	ui_init();

	memset( &g_last_diskaction, 0, sizeof(diskaction_t) );

	for ( i = 0; i < 0xffff; ++i )
		vm_listen( i, &vm_ioh_nin, &vm_ioh_nout );

	vga_init();
}

void
vm_kill()
{
	vlog( VM_KILLMSG, "Killing VM" );
	vga_kill();
	cpu_kill();
	mem_kill();
	ui_kill();
}

void
vm_exit( int ec )
{
	exiting = true;
#ifdef VM_DEBUG
	if( verbose ) {
		dump_all();
		/* No "--verbose" messages while exiting. */
		verbose = false;
	}
#endif
	vm_kill();
	exit( ec );
}

void
vm_cbreak( int sig )
{
	(void) sig;
#ifdef VM_BREAK
	g_break_pressed = true;
#endif
#ifndef VM_DEBUG
	vm_exit(0x66);
#endif
}
