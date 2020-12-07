#include <string.h>
#include <stdlib.h>

void string_assign(char **dst, char *src)
{
    if( *dst ) {
        free(*dst);
        *dst = NULL;
        return;
    }

    if( src == NULL ) {
        *dst = NULL;
        return;
    }

    int len = strlen(src);
    char *p = malloc(len + 1);
    if( !p ) {
        return;
    }

    memcpy(p, src, len);
    p[len] = '\0';

    *dst = p;
}