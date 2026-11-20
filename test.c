#include <stdio.h>
#include <stdlib.h>

void print_mem_list(void);

int main(void) {
    int *a = malloc(4 * sizeof(int));
    int *b = calloc(5, sizeof(int));
    a[0] = 42;
    printf("a[0]=%d, b[0]=%d\n", a[0], b[0]);
    print_mem_list();

    free(a); // middle block -> marked is_free=1
    printf("\nAfter freeing a:\n");
    print_mem_list();

    b = realloc(b, 20 * sizeof(int));
    printf("\nAfter reallocating b:\n");
    print_mem_list();

    free(b);
    return 0;
}