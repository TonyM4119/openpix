// Minimal newlib hooks: a small fixed heap (only used by printf internals, if at all).
#include <stddef.h>
#include <errno.h>
#include <sys/stat.h>
static char heap[4096] __attribute__((aligned(8)));
static size_t heap_used;
void *_sbrk(ptrdiff_t inc) {
  if (inc < 0 || heap_used + inc > sizeof(heap)) { errno = ENOMEM; return (void *)-1; }
  void *p = &heap[heap_used]; heap_used += (inc + 7) & ~7; return p;
}
int _close(int f) { (void)f; return -1; }
int _lseek(int f, int o, int w) { (void)f; (void)o; (void)w; return -1; }
int _read(int f, char *b, int n) { (void)f; (void)b; (void)n; return -1; }
int _write(int f, char *b, int n) { (void)f; (void)b; return n; }
int _fstat(int f, struct stat *s) { (void)f; s->st_mode = S_IFCHR; return 0; }
int _isatty(int f) { (void)f; return 1; }
