
#ifndef AUTH_H
#define AUTH_H

#include <cstdlib>
#include <cstring>
#include <security/pam_appl.h>

bool authenticate(const char *user, const char *password);

#endif /* AUTH_H*/
