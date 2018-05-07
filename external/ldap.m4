dnl check for LDAP libraries
AC_DEFUN([AM_CHECK_OPENLDAP],
[
    for p in /usr/include/openldap /usr/local/include; do
        if test -f "${p}/ldap.h"; then
            OPENLDAP_CFLAGS="${OPENLDAP_CFLAGS} -I${p}"
            break;
        fi
    done

    for p in /usr/lib/openldap /usr/local/lib ; do
        if test -f "${p}/libldap.so"; then
            OPENLDAP_LIBS="${OPENLDAP_LIBS} -L${p}"
            break;
        fi
    done

    SAVE_CFLAGS=$CFLAGS
    SAVE_LIBS=$LIBS
    CFLAGS="$CFLAGS $OPENLDAP_CFLAGS"
    LIBS="$LIBS $OPENLDAP_LIBS"

    AC_CHECK_HEADERS([lber.h])
    AC_CHECK_HEADERS([ldap.h],
                    [],
                    AC_MSG_ERROR([could not locate <ldap.h>]),
                    [ #if HAVE_LBER_H
                    #include <lber.h>
                    #endif
                    ])

    AC_CHECK_LIB(ldap, ldap_search, with_ldap=yes)
    AC_CHECK_LIB(ldap-2.4, ldap_initialize, with_ldap_two_four=yes)
    dnl Check for other libraries we need to link with to get the main routines.
    test "$with_ldap" != "yes" && { AC_CHECK_LIB(ldap, ldap_open, [with_ldap=yes with_ldap_lber=yes], , -llber) }
    test "$with_ldap_lber" != "yes" && { AC_CHECK_LIB(lber, ber_pvt_opt_on, with_ldap_lber=yes) }

    if test "$with_ldap" = "yes"; then
        if test "$with_ldap_lber" = "yes" ; then
            OPENLDAP_LIBS="${OPENLDAP_LIBS} -llber"
        fi
        if test "$with_ldap_two_four" = "yes" ; then
            OPENLDAP_LIBS="${OPENLDAP_LIBS} -lldap-2.4"
        else
            OPENLDAP_LIBS="${OPENLDAP_LIBS} -lldap"
        fi
    else
        AC_MSG_ERROR([LDAP libraries not found])
    fi

    LIBS="$LIBS $OPENLDAP_LIBS"
    AC_CHECK_FUNCS([ldap_initialize ldap_start_tls ldap_str2dn ldap_dnfree ldapssl_client_init])

    CFLAGS=$SAVE_CFLAGS
    LIBS=$SAVE_LIBS

    AC_SUBST(OPENLDAP_LIBS)
    AC_SUBST(OPENLDAP_CFLAGS)
])
