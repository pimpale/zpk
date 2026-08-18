#ifndef pathutils_h_INCLUDED
#define pathutils_h_INCLUDED

char *expandtilde(const char *input);
char *normalize(const char *path);
char *basename_m(char *input);
bool startswith(const char *str, const char *prefix);
bool endswith(const char *str, const char *suffix);
char *joinstr2(const char *s1, const char *s2);
char *joinpath(const char *s1, const char* s2);

#endif // pathutils_h_INCLUDED
