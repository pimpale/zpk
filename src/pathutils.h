#ifndef pathutils_h_INCLUDED
#define pathutils_h_INCLUDED

char *expandtilde(const char *input);
char *cleanpath(const char *path);
char *normalize(const char *path);
const char *basename(const char *path);
bool endswith(const char *str, const char *suffix);

#endif // pathutils_h_INCLUDED
