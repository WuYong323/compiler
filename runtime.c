#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cool 运行时：内存分配 + IO + 字符串工具。用 clang 与生成的 .ll 一起链接。 */

void* cool_alloc(int n) { return malloc(n); }

void* cool_copy(void* src, int size) {
  void* p = malloc(size);
  memcpy(p, src, size);
  return p;
}

void print_int(int v) { printf("%d", v); fflush(stdout); }

/* Cool 的 out_string：把 \t 变 tab、\n 变换行，其它 \c 保持字面 */
void print_str(const char* s) {
  for (; *s; s++) {
    if (*s == '\\') {
      s++;
      if (*s == '\0') break;
      if (*s == 't') putchar('\t');
      else if (*s == 'n') putchar('\n');
      else { putchar('\\'); putchar(*s); }
    } else {
      putchar(*s);
    }
  }
  fflush(stdout);
}

int read_int(void) {
  int v;
  int ok = (scanf("%d", &v) == 1);
  /* Cool 规范：in_int 丢弃整数之后、直到行尾的字符（否则会影响后续 in_string） */
  int c;
  while ((c = getchar()) != '\n' && c != EOF) { }
  return ok ? v : 0;
}

char* read_str(void) {
  char buf[4096];
  if (!fgets(buf, sizeof buf, stdin)) return strdup("");
  size_t n = strlen(buf);
  if (n && buf[n - 1] == '\n') buf[n - 1] = '\0';
  return strdup(buf);
}

int str_len(const char* s) { return (int)strlen(s); }

char* str_concat(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  char* r = (char*)malloc(la + lb + 1);
  memcpy(r, a, la);
  memcpy(r + la, b, lb + 1);
  return r;
}

char* str_substr(const char* s, int i, int l) {
  int n = (int)strlen(s);
  if (i < 0 || l < 0 || i + l > n) { fprintf(stderr, "substring out of range\n"); exit(1); }
  char* r = (char*)malloc(l + 1);
  memcpy(r, s + i, l);
  r[l] = '\0';
  return r;
}

int str_cmp(const char* a, const char* b) { return strcmp(a, b); }

void cool_abort(void) { fprintf(stderr, "abort\n"); exit(1); }

/* 手册 §13 的运行时错误：带原因退出。codegen 的守卫块调用它。 */
void cool_rt_error(const char* msg) {
  fflush(stdout);
  fprintf(stderr, "runtime error: %s\n", msg);
  exit(1);
}
