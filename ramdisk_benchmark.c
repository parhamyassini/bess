
# include <stdio.h>
# include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <sys/time.h>   // for gettimeofday()
#include <unistd.h>

#define WRITE_SIZE_BYTES 100000000

uint8_t buffer [WRITE_SIZE_BYTES];
uint8_t buffer2 [WRITE_SIZE_BYTES];

void write_hdd(){
    struct timeval stop, start;

    FILE *fd_p = fopen("./write_test", "w");
    if (fd_p == NULL) {
        printf("HDD FD open failed \n");
        return;
    }
    gettimeofday(&start, NULL);
    if(WRITE_SIZE_BYTES != fwrite(buffer, sizeof(char), WRITE_SIZE_BYTES, fd_p)){
         printf("HDD Writing failed \n");
        return;
    }
    fsync(fileno(fd_p));
    gettimeofday(&stop, NULL);

    printf("HDD took %lu us\n", (stop.tv_sec - start.tv_sec) * 1000000 + stop.tv_usec - start.tv_usec);
}

void write_ramdisk(){
    struct timeval stop, start;
    gettimeofday(&start, NULL);

    FILE *fd_p = fopen("/mnt/ramdisk/write_test", "w");
    if (fd_p == NULL) {
        printf("Ramdisk FD open failed \n");
        return;
    }
    
    if(WRITE_SIZE_BYTES != fwrite(buffer, sizeof(char), WRITE_SIZE_BYTES, fd_p)){
         printf("Ramdisk Writing failed \n");
        return;
    }
    fsync(fileno(fd_p));
    gettimeofday(&stop, NULL);

    printf("RAMDISK took %lu us\n", (stop.tv_sec - start.tv_sec) * 1000000 + stop.tv_usec - start.tv_usec);
}

void write_ramdisk_seq(){
    struct timeval stop, start;
    gettimeofday(&start, NULL);

    FILE *fd_p = fopen("/mnt/ramdisk/write_test", "w");
    if (fd_p == NULL) {
        printf("Ramdisk FD open failed \n");
        return;
    }
    for(int i = 0; i < WRITE_SIZE_BYTES; i++){
        fwrite(buffer + i, sizeof(char), 1, fd_p);
    }
    // if(WRITE_SIZE_BYTES != fwrite(buffer, sizeof(char), WRITE_SIZE_BYTES, fd_p)){
    //      printf("Ramdisk Writing failed \n");
    //     return;
    // }
    fsync(fileno(fd_p));
    //fclose(fd_p);
    gettimeofday(&stop, NULL);

    printf("RAMDISK took %lu us\n", (stop.tv_sec - start.tv_sec) * 1000000 + stop.tv_usec - start.tv_usec);
}

void write_hdd_seq(){
    struct timeval stop, start;
    gettimeofday(&start, NULL);

    FILE *fd_p = fopen("./write_test", "w");
    if (fd_p == NULL) {
        printf("Ramdisk FD open failed \n");
        return;
    }
    for(int i = 0; i < WRITE_SIZE_BYTES; i++){
        fwrite(buffer + i, sizeof(char), 1, fd_p);
    }
    // if(WRITE_SIZE_BYTES != fwrite(buffer, sizeof(char), WRITE_SIZE_BYTES, fd_p)){
    //      printf("Ramdisk Writing failed \n");
    //     return;
    // }
    fsync(fileno(fd_p));
    //fclose(fd_p);
    gettimeofday(&stop, NULL);

    printf("HDD took %lu us\n", (stop.tv_sec - start.tv_sec) * 1000000 + stop.tv_usec - start.tv_usec);
}

int main (void) {
    struct timeval stop, start;
    
    for (int i = 0; i < WRITE_SIZE_BYTES; i++){
        buffer[i] = (uint8_t)(i + 125);
    }
    gettimeofday(&start, NULL);
    memcpy(buffer2, buffer, WRITE_SIZE_BYTES);
    gettimeofday(&stop, NULL);
    printf("RAM bulk took %lu us\n", (stop.tv_sec - start.tv_sec) * 1000000 + stop.tv_usec - start.tv_usec);
    //printf("Time taken for RAM is : %f \n" , time_taken);
    
    // struct timeval start, end;
    // gettimeofday(&start, NULL);

    // memcpy(buffer2, buffer, WRITE_SIZE_BYTES);
    // gettimeofday(&end, NULL);
    // long seconds = (end.tv_sec - start.tv_sec);
	// long micros = ((seconds * 1000000) + end.tv_usec) - (start.tv_usec);

	// printf("Time RAM is %ld seconds and %ld micros\n", seconds, micros);
    //write_hdd_seq();
    //write_hdd();
    write_ramdisk();
    //write_ramdisk_seq();
    return 0;
}