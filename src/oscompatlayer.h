#ifndef oscompatlayer_h_INCLUDED
#define oscompatlayer_h_INCLUDED

char* expandtilde(const char* input);
char* getcwd_portable(void);
int mkdir_portable(const char* path, int mode);

#endif // oscompatlayer_h_INCLUDED
