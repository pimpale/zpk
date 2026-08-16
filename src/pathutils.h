#ifndef pathutils_h_INCLUDED
#define pathutils_h_INCLUDED

char *expandtilde(const char *input);
char *normalize(const char *path);
char *basename_m(char *input);
bool endswith(const char *str, const char *suffix);
char *joinstr2(const char *s1, const char *s2);
char *joinstr3(const char *s1, const char *s2, const char* s3);

#endif // pathutils_h_INCLUDED
