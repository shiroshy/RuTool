#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runs before AppRun adds bundled libraries to the search path. A system
 * runtime is safe to prefer only if it provides every bundled ABI version.
 * The GLIBCXX/CXXABI version-marker symbols have address zero: dlerror(),
 * rather than the return value of dlvsym(), determines whether they exist. */
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    FILE *versions = fopen(argv[1], "r");
    if (!versions) return 1;
    void *library = dlopen("libstdc++.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!library) { fclose(versions); return 1; }
    char version[128];
    int cpp = 0, abi = 0, compatible = 1;
    while (fgets(version, sizeof version, versions)) {
        version[strcspn(version, "\r\n")] = '\0';
        if (strncmp(version, "GLIBCXX_", 8) == 0) cpp = 1;
        else if (strncmp(version, "CXXABI_", 7) == 0) abi = 1;
        else { compatible = 0; break; }
        dlerror();
        (void)dlvsym(library, version, version);
        if (dlerror()) { compatible = 0; break; }
    }
    if (ferror(versions)) compatible = 0;
    fclose(versions);
    struct link_map *map = NULL;
    char *path = NULL;
    if (compatible && cpp && abi && dlinfo(library, RTLD_DI_LINKMAP, &map) == 0 && map)
        path = realpath(map->l_name, NULL);
    int result = 1;
    if (path && !strpbrk(path, " :\t\r\n") && puts(path) >= 0) result = 0;
    free(path);
    dlclose(library);
    return result;
}
