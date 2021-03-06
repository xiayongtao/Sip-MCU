dnl Process this file with autoconf to produce a configure script

dnl ------------------------------------------------
dnl Initialization and Versioning
dnl ------------------------------------------------

AC_INIT(libtheora,[1.0alpha7])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AM_CONFIG_HEADER([config.h])
AC_CONFIG_SRCDIR([lib/dct.c])
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE

dnl Library versioning

V_LIB_CURRENT=2
V_LIB_REVISION=0
V_LIB_AGE=2
AC_SUBST(V_LIB_CURRENT)
AC_SUBST(V_LIB_REVISION)
AC_SUBST(V_LIB_AGE)

dnl Extra linker options (for version script)
SHLIB_VERSION_ARG=""

dnl --------------------------------------------------  
dnl Check for programs
dnl --------------------------------------------------  

dnl save $CFLAGS since AC_PROG_CC likes to insert "-g -O2"
dnl if $CFLAGS is blank
cflags_save="$CFLAGS"
AC_PROG_CC
AC_PROG_CPP
CFLAGS="$cflags_save"

AM_PROG_LIBTOOL

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4")

dnl Check for doxygen
AC_CHECK_PROG(HAVE_DOXYGEN, doxygen, true, false)
AM_CONDITIONAL(HAVE_DOXYGEN,$HAVE_DOXYGEN)
if test $HAVE_DOXYGEN = "false"; then
        AC_MSG_WARN([*** doxygen not found, API documentation will not be built])
fi

dnl Check for python
AC_CHECK_PROG(HAVE_PYTHON, python, true, false)
AM_CONDITIONAL(HAVE_PYTHON,$HAVE_PYTHON)

dnl Check for valgrind
VALGRIND_ENVIRONMENT=""
ac_enable_valgrind=no
AC_ARG_ENABLE(valgrind-testing,
     [  --enable-valgrind-testing     enable running of tests inside Valgrind ],     [ ac_enable_valgrind=yes ], [ ac_enable_valgrind=no] )

if test "x${ac_enable_valgrind}" = xyes ; then
  if test "x${enable_shared}" = xyes ; then
    VALGRIND_ENVIRONMENT="libtool --mode=execute "
  fi

  AC_CHECK_PROG(HAVE_VALGRIND, valgrind, yes, no)
  if test "x$HAVE_VALGRIND" = xyes ; then
    VALGRIND_ENVIRONMENT="$VALGRIND_ENVIRONMENT valgrind -q --leak-check=yes --show-reachable=yes --num-callers=100"
    AC_SUBST(VALGRIND_ENVIRONMENT)
    TESTS_INFO="Type 'make check' to run test suite. Tests will be run under:
  ${VALGRIND_ENVIRONMENT}"
  else
    TESTS_INFO="Type 'make check' to run test suite (Valgrind not found)"
  fi
else
  TESTS_INFO="Type 'make check' to run test suite (Valgrind testing not enabled)"
fi

dnl --------------------------------------------------
dnl Set build flags based on environment
dnl --------------------------------------------------

dnl Set some target options

cflags_save="$CFLAGS"
if test -z "$GCC"; then
        case $host in 
        *)
                DEBUG="-g -DDEBUG"
                CFLAGS="-O"
                PROFILE="-g -p -DDEBUG" ;;
        esac
else

        case $host in 
        *)
                DEBUG="-g -Wall -DDEBUG -D__NO_MATH_INLINES"
                CFLAGS="-Wall -O3 -fforce-addr -fomit-frame-pointer -finline-functions -funroll-loops"
                PROFILE="-Wall -pg -g -O3 -fno-inline-functions -DDEBUG";;
        esac
fi
CFLAGS="$CFLAGS $cflags_save"

cpu_x86_64=no
cpu_x86_32=no
AC_ARG_ENABLE(asm,
    [  --disable-asm           disable assembly optimizations ],
    [ ac_enable_asm=$enableval ], [ ac_enable_asm=yes] )

if test "x${ac_enable_asm}" = xyes; then
  cpu_optimization="no optimization for your platform, please send a patch"
  case $target_cpu in
	i[[3456]]86)
		if test ! "x$target_vendor" = "xapple"; then
		  cpu_x86_32=yes 
		  cpu_optimization="32 bit x86"
  		AC_DEFINE([USE_ASM], [],  [make use of asm optimization])
		fi
    	;;
	x86_64)
		cpu_x86_64=yes
		cpu_optimization="64 bit x86"
  	AC_DEFINE([USE_ASM], [],  [make use of asm optimization])	
    ;;
  esac
else
  cpu_optimization="disabled"
fi
AM_CONDITIONAL([CPU_x86_64], [test x$cpu_x86_64 = xyes])
AM_CONDITIONAL([CPU_x86_32], [test x$cpu_x86_32 = xyes])

# Test whenever ld supports -version-script
AC_PROG_LD
AC_PROG_LD_GNU
if test "x$lt_cv_prog_gnu_ld" = "xyes"; then
   SHLIB_VERSION_ARG="Wl,--version-script=Version_script"

   dnl Set extra linker options
   case "$target_os" in
	linux* | solaris* )
		SHLIB_VERSION_ARG="-Wl,--version-script=Version_script"
		;;
	*)
		;;
   esac
fi

AC_SUBST(SHLIB_VERSION_ARG)

dnl --------------------------------------------------
dnl Checks for support libraries and headers
dnl --------------------------------------------------

dnl check for Ogg
HAVE_OGG=no

dnl first check through pkg-config since it's more flexible

dnl check for pkg-config itself so we don't try the m4 macro without pkg-config
AC_CHECK_PROG(HAVE_PKG_CONFIG, pkg-config, yes)
if test "x$HAVE_PKG_CONFIG" = "xyes"
then
  PKG_CHECK_MODULES(OGG, ogg >= 1.1, HAVE_OGG=yes, HAVE_OGG=no)
fi
if test "x$HAVE_OGG" = "xno"
then
  dnl fall back to the old school test
  XIPH_PATH_OGG(, AC_MSG_ERROR([
    libogg is required to build this package!
    please see http://www.xiph.org/ for how to
    obtain a copy.
  ]))
  cflags_save=$CFLAGS
  libs_save=$LIBS
  CFLAGS="$CFLAGS $OGG_CFLAGS"
  LIBS="$LIBS $OGG_LIBS"
  AC_CHECK_FUNC(oggpackB_read, , [
    AC_MSG_ERROR([newer libogg version (1.1 or later) required])
  ])
  CFLAGS=$cflags_save
  LIBS=$libs_save
fi


dnl check for Vorbis
HAVE_VORBIS=no

dnl first check through pkg-config since it's more flexible

if test "x$HAVE_PKG_CONFIG" = "xyes"
then
  PKG_CHECK_MODULES(VORBIS, vorbis >= 1.0.1, HAVE_VORBIS=yes, HAVE_VORBIS=no)
  dnl also set VORBISENC_LIBS since an examples needs it
  dnl the old .m4 sets this to a value to use on top of VORBIS_LIBS,
  dnl so we do the same here.
  VORBISENC_LIBS="-lvorbisenc"
  AC_SUBST(VORBISENC_LIBS)
fi
if test "x$HAVE_VORBIS" = "xno"
then
  dnl fall back to the old school test
  XIPH_PATH_VORBIS(HAVE_VORBIS=yes, HAVE_VORBIS=no)
fi

dnl check for SDL
HAVE_SDL=no

AM_PATH_SDL(,[
	HAVE_SDL=yes
	SDL_LIBS=`$SDL_CONFIG --libs`
],AC_MSG_WARN([*** Unable to find SDL -- Not compiling example players ***]))

dnl check for OSS
HAVE_OSS=no

AC_CHECK_HEADERS([sys/soundcard.h soundcard.h machine/soundcard.h],[
  HAVE_OSS=yes
  break
])
if test x$HAVE_OSS != xyes; then
  AC_MSG_WARN([OSS audio support not found -- not compiling player_example])
fi

dnl --------------------------------------------------
dnl Overall build configuration options
dnl --------------------------------------------------

dnl Configuration option for building of floating point code.

ac_enable_float=yes
AC_ARG_ENABLE(float,
     [  --disable-float         disable use of floating point code ],
     [ ac_enable_float=$enableval ], [ ac_enable_float=yes] )

if test "x${ac_enable_float}" != xyes ; then
    AC_DEFINE([THEORA_DISABLE_FLOAT], [], 
	[Define to exclude floating point code from the build])
fi
AM_CONDITIONAL(THEORA_DISABLE_FLOAT, [test "x${ac_enable_float}" != xyes])

dnl Configuration option for building of encoding support.

ac_enable_encode=yes
AC_ARG_ENABLE(encode,
     [  --disable-encode        disable encoding support ],
     [ ac_enable_encode=$enableval ], [ ac_enable_encode=yes] )

if test "x${ac_enable_encode}" != xyes ; then
    AC_DEFINE([THEORA_DISABLE_ENCODE], [],
	[Define to exclude encode support from the build])
else
    if test x$HAVE_VORBIS = xyes; then
      BUILDABLE_EXAMPLES="$BUILDABLE_EXAMPLES encoder_example\$(EXEEXT)"
    else
      AC_MSG_NOTICE([Vorbis missing, cannot build example encoder])
    fi
fi
AM_CONDITIONAL(THEORA_DISABLE_ENCODE, [test "x${ac_enable_encode}" != xyes])

dnl --------------------------------------------------
dnl Check for headers
dnl --------------------------------------------------

dnl none here

dnl --------------------------------------------------
dnl Check for typedefs, structures, etc
dnl --------------------------------------------------

dnl none

dnl --------------------------------------------------
dnl Check for library functions
dnl --------------------------------------------------

dnl substitute the included getopt if the system doesn't support long options
AC_CHECK_FUNC(getopt_long,
              [GETOPT_OBJS=''],
              [GETOPT_OBJS='getopt.$(OBJEXT) getopt1.$(OBJEXT)'])
AC_SUBST(GETOPT_OBJS)

if test x$HAVE_SDL = xyes -a x$HAVE_OSS = xyes -a x$HAVE_VORBIS = xyes; then
  BUILDABLE_EXAMPLES="$BUILDABLE_EXAMPLES player_example"
fi
AC_SUBST(BUILDABLE_EXAMPLES)

dnl --------------------------------------------------
dnl Do substitutions
dnl --------------------------------------------------

AC_SUBST(DEBUG)
AC_SUBST(PROFILE)

AC_OUTPUT([
  Makefile lib/Makefile
  include/Makefile include/theora/Makefile
  examples/Makefile
  doc/Makefile doc/Doxyfile
  tests/Makefile
  lib/Version_script
  m4/Makefile
  libtheora.spec
  theora.pc
  theora-uninstalled.pc
])

AS_AC_EXPAND(LIBDIR, ${libdir})
AS_AC_EXPAND(INCLUDEDIR, ${includedir})
AS_AC_EXPAND(BINDIR, ${bindir})
AS_AC_EXPAND(DOCDIR, ${datadir}/doc)

if test $HAVE_DOXYGEN = "false"; then
  doc_build="no"
else
  doc_build="yes"
fi
dnl need to handle spec build?

AC_MSG_RESULT([
------------------------------------------------------------------------
  $PACKAGE $VERSION:  Automatic configuration OK.

  General configuration:

    Encoding support: ........... ${ac_enable_encode}
    Floating point support: ..... ${ac_enable_float}
    Assembly optimization: ...... ${cpu_optimization}
    API Documentation: .......... ${doc_build}

  Installation paths:

    libtheora: ................... ${LIBDIR}
    C header files: .............. ${INCLUDEDIR}/theora
    Documentation: ............... ${DOCDIR}/$PACKAGE

  Building:

    Type 'make' to compile $PACKAGE.

    Type 'make install' to install $PACKAGE.

    ${TESTS_INFO}

  Example programs will be built but not installed.
------------------------------------------------------------------------
])

