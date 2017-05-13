#include <cstdio>

#include <sndfile.h>

int
main (void)
{
	char buf [1024] ;

	sf_command (NULL, SFC_GET_LIB_VERSION, buf, sizeof (buf)) ;
	puts (buf) ;

	return 0 ;
}