#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

typedef struct Paths {
    char* src;
    char* dest;
} Paths;


Paths* build_new_paths(Paths* old_paths, const char *add_str) {
    size_t additionalLen = strlen(add_str);
    size_t sourcePathLen = strlen(old_paths->src) + additionalLen + 1;
    size_t destinationPathLen = strlen(old_paths->dest) + additionalLen + 1;

    Paths* paths = (Paths*)malloc(sizeof(Paths));
    paths->src = (char*)malloc(sourcePathLen * sizeof(char));
    paths->dest = (char*)malloc(destinationPathLen * sizeof(char));

    strcpy(paths->src, old_paths->src);
    strcat(paths->src, "/");
    strcat(paths->src, add_str);

    strcpy(paths->dest, old_paths->dest);
    strcat(paths->dest, "/");
    strcat(paths->dest, add_str);
    return paths;
}

void* copyFile(void* args) {
   Paths* paths = (Paths*)args;

   int fin;
   if ((fin = open(paths->src, O_RDONLY)) == -1) {
       perror("open src");
       free(paths);
       pthread_exit(NULL);
   }

   
   int fout;
   mode_t mode = 0777;
   if ((fout = open(paths->dest, O_WRONLY | O_CREAT | O_EXCL, mode)) == -1) {
      perror("open dest");
      free(paths);
      close(fin);
      pthread_exit(NULL);
   }

   char buf[BUFSIZ];
   int read_bytes;
   while ((read_bytes = read(fin, buf, BUFSIZ)) > 0) {
       if (write(fout, buf, read_bytes) < 0) {
           perror("write");
           free(paths);
           close(fin);
           close(fout);
           pthread_exit(NULL);
       }
   } 

   if (read_bytes < 0) {
       perror("read");
       close(fin);
       close(fout);
       free(paths);
       pthread_exit(NULL);
   }

   //printf("stoped copy file %s\n", paths->dest);
   close(fin);
   close(fout);
   free(paths);
   pthread_exit(NULL);
}

void* copyDir(void* args) {
    Paths* paths = (Paths*)args;

    DIR* dir;
    if ((dir = opendir(paths->src)) == NULL) {
	perror("opendir");
        free(paths);
        pthread_exit(NULL);
    }

    struct dirent* dirent;
    while ((dirent = readdir(dir)) != NULL) {
        if (strcmp(dirent->d_name, ".") == 0  || strcmp(dirent->d_name, "..") == 0) {
            continue;
        }

        Paths* new_paths = build_new_paths(paths, dirent->d_name);
	    //printf("NAME = %s\n", dirent->d_name);

        int status;
        pthread_t tid;
        bool success = false;
        if (dirent->d_type == DT_DIR) {
            mode_t mode = 0777;
            if (mkdir(new_paths->dest, mode) != 0) {
                perror("mkdir");
                free(new_paths);
                free(paths);
                closedir(dir);
                pthread_exit(NULL);
            }
            while(!success) {
                status = pthread_create(&tid, NULL, copyDir, (void*)new_paths);
                if (status != EAGAIN) {
                    if (status != 0) {
                        printf("can't create thread: %s\n", strerror(status));
                        free(new_paths);
                        free(paths);
                        closedir(dir);
                        pthread_exit(NULL);
                    }
		            success = true;
                }
            }
        }
        else if (dirent->d_type == DT_REG) {
            while(!success) {
                status = pthread_create(&tid, NULL, copyFile, (void*)new_paths);
                if (status != EAGAIN) {
                    if (status != 0) {
                        printf("can't create thread: %s\n", strerror(status));
                        free(new_paths);
                        free(paths);
                        closedir(dir);
                        pthread_exit(NULL);
                    }
                    success = true;
                }
            }
        }
    }

    closedir(dir);
    free(paths);
    pthread_exit(NULL);
}


Paths* make_paths(char *src, char *dest) {
    struct stat buf;

    if (stat(src, &buf) == -1) {
        printf("Problem with filling stat");
        return NULL;
    }

    size_t len_source_path = strlen(src);
    size_t len_destination_path = strlen(dest);
    Paths* paths = (Paths*)malloc(sizeof(Paths));
    paths->src = malloc(sizeof(char) * len_source_path);
    paths->dest = malloc(sizeof(char) * len_destination_path);
    strncpy(paths->src, src, len_source_path);
    strncpy(paths->dest, dest, len_destination_path);

    if (paths->src[len_source_path - 1] == '/'){
        paths->src[len_source_path - 1] = 0;
    }

    if (paths->dest[len_destination_path - 1] == '/'){
        paths->dest[len_destination_path - 1] = 0;
    }

    return paths;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Invalid number of arguments. Needs two: src, dest\n");
	    pthread_exit(NULL);
    }

    Paths* paths = make_paths(argv[1], argv[2]);
    copyDir(paths);

    free(paths);
    pthread_exit(NULL);
}

