AC_INIT([mod_result_status_counter], 1.0)
AC_CHECKING(whether apxs is available)
AC_SUBST(APXS)
AC_SUBST(LIBEXECDIR)
AC_PROG_INSTALL


if test -z "$APXS"; then
  AC_MSG_CHECKING(for apxs in your PATH)
  AC_PATH_PROG(APXS, apxs)
  if test -n "$APXS"; then
    AC_MSG_RESULT([found $APXS.])
  fi
fi 

AC_MSG_CHECKING(for Apache libexec directory)
LIBEXECDIR=`${APXS} -q LIBEXECDIR`
AC_MSG_RESULT($LIBEXECDIR)

AC_OUTPUT(Makefile)
