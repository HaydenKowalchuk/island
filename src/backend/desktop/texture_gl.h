#ifndef ISLAND_BACKEND_DESKTOP_TEXTURE_GL_H
#define ISLAND_BACKEND_DESKTOP_TEXTURE_GL_H

/* Installs the desktop texture loader. Requires a current GL context. */
void texture_gl_init(void);

/* Deletes every uploaded texture. */
void texture_gl_shutdown(void);

#endif
