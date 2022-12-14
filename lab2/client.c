#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "kernelmodule.h"

static void print_program_usage() {
    printf("Command: sudo ./client [-vma|-netdev] <PID>\n");
}

int main( int argc, char *argv[] ) {
    //FILE *vma_file = fopen(DEBUGFS_VMA_FILE_PATH, "r");
    FILE *vma_arg_file = fopen(DEBUGFS_VMA_FILE_PATH, "w");
    FILE *nds_file = fopen(DEBUGFS_NDS_FILE_PATH, "r");
    if ( !nds_file || !vma_arg_file ) {
        printf("Cannot open files for debugfs driver\n");
        if (nds_file) fclose(nds_file);
        if (vma_arg_file) fclose(vma_arg_file);
        return -1;
    }
    if ( argc < 2 || argc > 3 ) {
        print_program_usage();
        fclose(vma_arg_file);
        fclose(nds_file);
        return -1;
    }
    char *line = NULL;
    size_t length = 0;
    if (strcmp(argv[1], "-vma") == 0 ) {
        printf("Sending your PID to kernel module...\n");
        char buf[1024];
        sprintf(buf, "%s\n", argv[2]);
        fwrite(&buf, 1, sizeof(buf), vma_arg_file);
        fclose(vma_arg_file);
        printf("Getting vm_area_struct information from kernel module...\n ");
        FILE* vma_file = fopen(DEBUGFS_VMA_FILE_PATH, "r");
        int flag = 1;
        while (getline(&line, &length, vma_file) != -1) {
            if (flag == 1) {
                flag = 0;
                continue;
            }
            printf("%s", line);
        }
    } else if (strcmp(argv[1], "-netdev") == 0) {
        printf("Getting net_device struct information from kernel module...\n ");
        while (getline(&line, &length, nds_file) != -1) {
            printf("%s", line);
        }
    } else {
        print_program_usage();
        fclose(vma_arg_file);
        fclose(nds_file);
        return -1;
    }
    fclose(nds_file);
    fclose(vma_arg_file);
    return 0;
}
