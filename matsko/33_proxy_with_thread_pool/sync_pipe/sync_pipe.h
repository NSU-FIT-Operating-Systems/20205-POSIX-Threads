#ifndef UTILITIES_SYNC_PIPE_H
#define UTILITIES_SYNC_PIPE_H

int sync_pipe_init();
void sync_pipe_close();
int sync_pipe_wait();
void sync_pipe_notify(int num_really_created_threads);
<<<<<<< Updated upstream
=======
int get_rfd_spipe();
>>>>>>> Stashed changes

#endif //UTILITIES_SYNC_PIPE_H
