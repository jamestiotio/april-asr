#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include "april.h"

int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

int main(int argc, char *argv[]){
    if(argc != 3){
        printf("Usage: %s [file] [modelpath]\n", argv[0]);
        printf(" - [file] must be a 16000Hz raw PCM16 file\n");
        printf(" - [modelpath] must be a path to the models\n");
        return 1;
    }

    FILE *fd = fopen(argv[1], "r");
    
    if(fd == 0){
        printf("Failed to open file %s\n", argv[1]);
        return 2;
    }

    fseek(fd, 0L, SEEK_END);
    size_t sz = ftell(fd);
    

    if(ends_with(argv[1], ".wav")) {
        fseek(fd, 44L, SEEK_SET);
    } else {
        fseek(fd, 0L, SEEK_SET);
    }


    void *file_data = malloc(sz);
    size_t sz1 = fread(file_data, 1, sz, fd);

    fclose(fd);
    

    if(sz1 % 2 != 0){
        printf("File size not divisible by two, is the file raw pcm16?\nSize: %llu\n", sz1);
        return 4;
    }

    printf("Read file, %llu bytes\n", sz1);


    aam_api_init();
    AprilASRModel model = aam_create_model(argv[2]);
    aam_feed_pcm16(model, (short *)file_data, sz1/2);

    free(file_data);

    printf("End :)\n");
    return 0;
}