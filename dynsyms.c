/* Runtime support library for dynamically linking DLLs with
   dynamic symbols */

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <assert.h>
#include "dynsyms.h"

/* Emulate a dlopen-like interface */

void *dlopen(char *libname, int for_execution) {
  HMODULE m;
  m = LoadLibraryEx(libname, NULL,
                    for_execution ? 0 : DONT_RESOLVE_DLL_REFERENCES);
  /* Under Win 95/98/ME, LoadLibraryEx can fail in cases where LoadLibrary
     would succeed.  Just try again with LoadLibrary for good measure. */
  if (m == NULL) m = LoadLibrary(libname);
  return (void *) m;
}

void dlclose(void *handle) { 
  FreeLibrary((HMODULE) handle); 
}

void *dlsym(void *handle, char *name) { 
  return (void *) GetProcAddress((HMODULE) handle, name); 
}

char * dlerror(void)
{
  static char buffer[256];
  DWORD msglen =
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL,           /* message source */
                  GetLastError(), /* error number */
                  0,              /* default language */
                  buffer,         /* destination */
                  sizeof(buffer), /* size of destination */
                  NULL);          /* no inserts */
  if (msglen == 0) return "unknown error";
  else return buffer;
}


/** Relocation tables **/

void dump_reloctbl(reloc_entry *tbl) {
  if (!tbl) { printf("No relocation table\n"); return; }
  printf("Dynamic relocation table found at %lx\n", tbl);

  for (; tbl->kind; tbl++)
    printf(" %s: %08lx (kind:%04lx)  (now:%08lx)\n", 
	   tbl->name,
	   tbl->addr,
	   tbl->kind,
	   *((uintnat*) tbl->addr)
	   );
}

static void allow_write(char *begin, char *end) {
  static long int pagesize = 0;
  long int old;
  int res;
  SYSTEM_INFO si;

  if (0 == pagesize) {
    GetSystemInfo (&si);
    pagesize = si.dwPageSize;
  }

  begin -= (uintnat) begin % pagesize;
  res = VirtualProtect(begin, end - begin, PAGE_EXECUTE_WRITECOPY, &old);
  if (0 == res) {
    fprintf(stderr, "natdynlink: VirtualProtect failed  %s\n", dlerror());
    exit(2);
  }
}

void relocate(resolver f, void *data, reloc_entry *tbl) {
  int i,j,n,m;
  uintnat *reloc, s;
  char *name;
  uintnat absolute;

  if (!tbl) return;
  for (; tbl->kind; tbl++) {
    if (tbl->kind & RELOC_DONE) continue;
    s = (uintnat) f(data,tbl->name);
    if (!s) { printf("Cannot resolve %s\n", tbl->name); exit(1); }
    allow_write((char*)tbl->addr,(char*)tbl->addr);
    switch (tbl->kind & 0xff) {
    case RELOC_ABS: *(tbl->addr) = s; break;
    case RELOC_REL: *(tbl->addr) = s - (uintnat) (tbl->addr) - 4; break;
    default: assert(0);
    }
    tbl->kind |= RELOC_DONE;
  }
}


/* Symbol tables */

void dump_symtbl(symtbl *tbl)
{
  int i;

  if (!tbl) { printf("No symbol table\n"); return; }
  printf("Dynamic symbol table found at %lx\n", tbl);

  for (i = 0; i < tbl->size; i++)
    printf(" %s : %08lx\n", tbl->entries[i].name, tbl->entries[i].addr);
}

int compare_dynsymbol(const void *s1, const void *s2) {
  return strcmp(((dynsymbol*) s1) -> name, ((dynsymbol*) s2) -> name);
}

void add_symbols(dynsymtable *table, symtbl *tbl) {
  int i,needed,size;

  if (!tbl) return;
  needed = table->used + tbl->size;
  size = table->size;
  if (needed > size) {
    while (needed > size) size = size * 2 + 10;
    table->slots = realloc(table->slots, size * sizeof(dynsymbol));
    if (NULL == table->slots) {
      printf("Cannot allocate memory for symbol table\n");
      exit(1);
    }
  }
  memcpy(&table->slots[table->used], &tbl->entries, 
	 tbl->size * sizeof(dynsymbol));
  table->used = needed;
  table->size = size;
  table->sorted = 0;
}

void *find_symbol(dynsymtable *table, char *name) {
  static dynsymbol s;
  dynsymbol *sym;

  if (!table->used) return NULL;
  if (!table->sorted) {
    qsort(table->slots, table->used, sizeof(dynsymbol), &compare_dynsymbol);
    table->sorted = 1;
  }

  s.name = name;
  sym = (dynsymbol*) 
    bsearch(&s,table->slots,table->used, sizeof(dynsymbol),&compare_dynsymbol);

  return (sym ? sym->addr : NULL);
}
