#include "filesys.h"
#include "debug.h"
#include "utility.h"

#include <string.h>

#define DIRECTORY_ENTRY_SIZE (sizeof(inode_index_t) + MAX_FILE_NAME_LEN)
#define DIRECTORY_ENTRIES_PER_DATABLOCK (DATA_BLOCK_SIZE / DIRECTORY_ENTRY_SIZE)

// ----------------------- CORE FUNCTION ----------------------- //
int new_file(terminal_context_t *context, char *path, permission_t perms)
{
    (void) context;
    (void) path;
    (void) perms;
    return -2;
}

int new_directory(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
}

int remove_file(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
}

// we can only delete a directory if it is empty!!
int remove_directory(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
}

int change_directory(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
}

int list(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
}

char *get_path_string(terminal_context_t *context)
{
    (void) context;

    return NULL;
}

int tree(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
}

//Part 2
void new_terminal(filesystem_t *fs, terminal_context_t *term)
{
    //check if inputs are valid
    if(fs == NULL || term == NULL){
        return;
    }

    //assign file system and root inode.
    term->fs = fs;
    term->working_directory = &fs->inodes[0];
}

fs_file_t fs_open(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;

    //confirm path exists, leads to a file
    
    //allocate space for the file, assign its fs and inode. Set offset to 0.
    //return file

    return (fs_file_t)0;
}

void fs_close(fs_file_t file)
{
    if(file == NULL){
        return;
    }
    free(file);
}

size_t fs_read(fs_file_t file, void *buffer, size_t n)
{
    //if file is invalid
    if(file == NULL){
        return 0;
    }

    //filesystem pointer
    filesystem_t *file_ptr = file->fs;

    //inode pointer
    inode_t *inode_ptr = file->inode;

    //get current file offset
    size_t curr_offset = file->offset;

    //number of bytes to read
    size_t bytes_to_read = n;

    //current file size
    size_t current_file_size = inode_ptr->internal.file_size;

    if(curr_offset + n > current_file_size){
        bytes_to_read = current_file_size - curr_offset;
    }

    size_t total_bytes_read = 0;
    fs_retcode_t result = inode_read_data(file_ptr, inode_ptr, curr_offset, buffer, bytes_to_read, &total_bytes_read);
    if(result != SUCCESS){
        return 0;
    }
    curr_offset += total_bytes_read;
    file->offset = curr_offset;
    
    return total_bytes_read;
}

size_t fs_write(fs_file_t file, void *buffer, size_t n)
{
    //if file is invalid
    if(file == NULL){
        return 0;
    }

    //filesystem pointer
    filesystem_t *file_ptr = file->fs;

    //inode pointer
    inode_t *inode_ptr = file->inode;

    //get current file offset
    size_t curr_offset = file->offset;

    //number of bytes to write
    size_t total_bytes_written = 0;

   
    fs_retcode_t result = inode_modify_data(file_ptr, inode_ptr, curr_offset, buffer, n);
    if(result != SUCCESS){
        return 0;
    }

    total_bytes_written = n;

    curr_offset += total_bytes_written;
    file->offset = curr_offset;
    
    return total_bytes_written;

}

int fs_seek(fs_file_t file, seek_mode_t seek_mode, int offset)
{
    if(file == NULL){
        return -1;
    }

    if((seek_mode != FS_SEEK_END) && (seek_mode != FS_SEEK_CURRENT) && (seek_mode != FS_SEEK_START)){
        return -1;
    }

    //to store updated offset
    int updated_offset;

    inode_t *inode_ptr = file->inode;
    size_t curr_offset = file->offset;
    size_t current_file_size = inode_ptr->internal.file_size;

    //if seekmode is fs_seek_start
    if(seek_mode == FS_SEEK_START){
        updated_offset = offset;
    }

    // if seekmode is fs_seek_current
    if(seek_mode == FS_SEEK_CURRENT){
        updated_offset = curr_offset + offset;
    }

    // if seekmode is fs_seek_end
    if(seek_mode == FS_SEEK_END){
        updated_offset = offset + current_file_size;
    }

    //if final offset is less than 0
    if(updated_offset < 0){
        return -1;
    }

    //if final offset is greater than file size, set it to file size, successful
    if((size_t)updated_offset > current_file_size){
        updated_offset = current_file_size;
    }

    file->offset = (size_t)updated_offset;

    return 0;
}

