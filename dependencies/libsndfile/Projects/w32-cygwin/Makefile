TARGETS = test.exe

test.exe : test.c sndfile.h libsndfile-1.dll
	gcc -I . $< libsndfile-1.dll -o $@

# Need to patch the MinGW sndfile.h for Cygwin.
sndfile.h : ../../include/sndfile.h
	sed "s/typedef\s__int64\s/typedef int64_t /" $+ > $@

# Need the DLL in the local directory.
libsndfile-1.dll : ../../libsndfile-1.dll
	cp -f $+ $@

