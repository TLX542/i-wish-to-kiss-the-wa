#include "../vm.c"

int main(int argc, char **argv) {
    initOS();
    fprintf(stdout, "Occupied memory after OS load:\n");
    fprintf_mem_nonzero(stdout, mem, UINT16_MAX);
    int retCreate = createProc("bin_programs/simple_code.obj", "bin_programs/simple_heap.obj");
    printf("createProc returned: %d\n", retCreate);
    fprintf(stdout, "Occupied memory after program load:\n");
    fprintf_mem_nonzero(stdout, mem, UINT16_MAX);    
    fprintf_reg_all(stdout, reg, RCNT);
    return 0;
}