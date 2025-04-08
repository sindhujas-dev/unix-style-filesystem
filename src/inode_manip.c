#include "filesys.h"

#include <string.h>
#include <assert.h>

#include "utility.h"
#include "debug.h"

#include <math.h>

#define INDIRECT_DBLOCK_INDEX_COUNT (DATA_BLOCK_SIZE / sizeof(dblock_index_t) - 1)
#define INDIRECT_DBLOCK_MAX_DATA_SIZE ( DATA_BLOCK_SIZE * INDIRECT_DBLOCK_INDEX_COUNT )

#define NEXT_INDIRECT_INDEX_OFFSET (DATA_BLOCK_SIZE - sizeof(dblock_index_t))

// ----------------------- UTILITY FUNCTION ----------------------- //

size_t write_data_in_direct_dblock(filesystem_t *fs, inode_t *inode, void *data, size_t n){
    // get the current file size
    size_t current_file_size = inode->internal.file_size;
    //info(1, "Current file size = %zu\n", current_file_size);

    // find the last claimed dblock
    size_t dblock_index = current_file_size / DATA_BLOCK_SIZE;
    //info(1, "Print out dblock index we will write in = %zu\n", dblock_index);

    // find the offset in the dblock to write to
    size_t offset_in_dblock = current_file_size % DATA_BLOCK_SIZE;
    size_t bytes_of_data_remaining = n;
    size_t total_bytes_written = 0;
    int blocks_allocated_count = 0;

    // change data to a byte pointer so we can access it as a sequence of bytes
    byte *data_ptr_inBytes = (byte *)data;

    //if there is space to write in the 4 dblocks of the inode
    while(dblock_index < INODE_DIRECT_BLOCK_COUNT && bytes_of_data_remaining > 0){
        // if we need to allocate a new dblock to write data
        if(inode->internal.direct_data[dblock_index] == 0){ //this checks if the dblock at the index is not allocated (value is 0)
            dblock_index_t new_dblock_index;
            fs_retcode_t new_dblock_return = claim_available_dblock(fs, &new_dblock_index);
            if(new_dblock_return  != SUCCESS){
                //info(1, "Error: did not successfully claim an available dblock\n");
                return -1;
            }
            blocks_allocated_count++;
            inode->internal.direct_data[dblock_index] = new_dblock_index;
        }

        // write into the current dblock 
        
        // get pointer to the current dblock
        byte *current_dblock = fs->dblocks + (inode->internal.direct_data[dblock_index] * DATA_BLOCK_SIZE);

        // calculate number of bytes to write until dblock is full
        size_t remaining_space_in_dblock = DATA_BLOCK_SIZE - offset_in_dblock;

        // if there is more space left in dblock then there are bytes we need to write
        if(remaining_space_in_dblock > bytes_of_data_remaining){
            remaining_space_in_dblock = bytes_of_data_remaining;
        }

        // copy the data into the dblock
        memcpy(current_dblock + offset_in_dblock, data_ptr_inBytes, remaining_space_in_dblock);

        bytes_of_data_remaining -= remaining_space_in_dblock;
        data_ptr_inBytes += remaining_space_in_dblock;
        total_bytes_written += remaining_space_in_dblock;

        dblock_index++;
        offset_in_dblock = 0;
    }

    inode->internal.file_size += total_bytes_written;
    return total_bytes_written;
}

fs_retcode_t write_data_in_indirect_dblock(filesystem_t *fs, inode_t *inode, void *data, size_t n, size_t remaining_bytes_to_write){
    /*
    // get the current file size
    size_t current_file_size = inode->internal.file_size;
    //info(1, "Current file size = %zu\n", current_file_size);

    byte *data_ptr_inBytes = (byte *)data;
    size_t bytes_of_data_remaining = remaining_bytes_to_write;
    size_t total_bytes_written = 0;

    // get the first indirect index dblock
    dblock_index_t indirect_dblock_index = inode->internal.indirect_dblock;

    // if the index dblock is not allocated, allocate it
    if(indirect_dblock_index == 0){
        // allocate a new indirect dblock
        fs_retcode_t new_indirect_dblock_return = claim_available_dblock(fs, &indirect_dblock_index);
        if(new_indirect_dblock_return != SUCCESS){
            //info(1, "Error: did not successfully claim an available dblock\n");
            return INSUFFICIENT_DBLOCKS;
        }
        inode->internal.indirect_dblock = indirect_dblock_index;
        // the dblock has 64 bytes of space, so we can store 15 pointer indices to direct dblocks, and one index to the next indirect dblock
        // fill the 64 bytes of the dblock with 0s 
        memset(fs->dblocks + indirect_dblock_index * DATA_BLOCK_SIZE, 0, DATA_BLOCK_SIZE);
    }

    // otherwise, check if the index dblock is full, by checking the indices of each dblock
    // if the index of the dblock is != 0, then it is already allocated
    dblock_index_t *datablock_index_ptr = cast_dblock_ptr(fs->dblocks + indirect_dblock_index * DATA_BLOCK_SIZE);

    // int i = 0;
    // while(datablock_index_ptr[i] != 0 && i < 15){
    //     i++;
    // }
    // if(i == 15){
    //     // get the next indirect dblock
    // }
    
    
    // go through the dblocks in the index dblock

    */

    /*

    1. Calculate the remaining bytes to write (after writing to direct blocks)
    2. Get or allocate the first indirect index D-block
    3. While there are bytes to write:
        a. Find the first available slot in the current index D-block
        b. If all slots are filled, follow the chain to the next index D-block or allocate a new one
        c. For each available slot:
            i. Allocate a new data D-block if needed
            ii. Write data to the data D-block
            iii. Update bytes written counter
            iv. Move to next slot if necessary
    4. Update the file size




    1. get the first indirect index dblock - access with a variable that keeps track of current indirect index block
    2. if the current indirect index dblock is not allocated, allocate
    3. if the current indirect index dblock has a bitmask where all 15 data dblocks are used, get pointer to next indirect index dblock
    5. repeat step 3 until you find an indirect index dblock that does not have all 15 data dblocks used according to bitmask

    6. once we found that indirect index dblock, we let the current pointer point to the first data dblock
    7. if the current pointer points to a dblock that is filled full 64 bytes, then we access the next dblock and update pointer
    8. repeat 7 until we find a dblock that is < 64 bytes
    9. if the dblock pointer is 0, allocate the memory for that dblock, and fill the dblock
    10. if the dblock pointer shows that the dblock has < 64 bytes, then fill the rest of those bytes
    11. update the amount of bytes written 
    12. if there are still remaining bytes, update the current pointer to the next dblock and repeat steps 6-12
    13. if there was no more space in the indirect dblock to write, like all 15 dblocks are filled, then allocate a new indirect index dblock
    14. repeat steps 2-13 until all bytes are written
    */

    return SUCCESS;
}

// ----------------------- CORE FUNCTION ----------------------- //

fs_retcode_t inode_write_data(filesystem_t *fs, inode_t *inode, void *data, size_t n)
{
    // display filesystem format and parameters
    //display_filesystem(fs, DISPLAY_FS_FORMAT);

    //Check for valid input
    if(fs == NULL || inode == NULL){
        return INVALID_INPUT;
    }

    //info(1, "# of Bytes We want to Write: %zu", n);

    // do we have enough dblocks to store the data. if not, error. 
    // find the number of dblocks needed to store n bytes
    size_t total_dblocks_needed = calculate_necessary_dblock_amount(inode->internal.file_size + n);
    size_t current_dblocks_used = calculate_necessary_dblock_amount(inode->internal.file_size);
    size_t remaining_dblocks_needed = total_dblocks_needed - current_dblocks_used;

    //info(1, "Total dblocks needed = %zu, current dblocks used = %zu, remaining dblocks needed = %zu\n", total_dblocks_needed, current_dblocks_used, remaining_dblocks_needed);
    
    // find the number of available dblocks in the filesystem
    size_t available_dblocks_infile = available_dblocks(fs);
    //info(1, "Available dblocks in filesystem = %zu\n", available_dblocks_infile);
    
    // if the number of dblocks needed is greater than the available dblocks, return INSUFFICIENT_SPACE
    if (remaining_dblocks_needed > available_dblocks_infile) {
        return INSUFFICIENT_DBLOCKS;
    }

    // fill the direct nodes if necessary (helper function) 
    int bytes_written = write_data_in_direct_dblock(fs, inode, data, n);
    if(bytes_written < 0){
        return INSUFFICIENT_DBLOCKS;
    }

    // fill in indirect nodes if necessary (helper function)
    int remaining_bytes_to_write = n - bytes_written;
    write_data_in_indirect_dblock(fs, inode, data, n, remaining_bytes_to_write);

    // we can store 64 bytes in the dblock?

    return SUCCESS;
}

fs_retcode_t inode_read_data(filesystem_t *fs, inode_t *inode, size_t offset, void *buffer, size_t n, size_t *bytes_read)
{
    (void)fs;
    (void)inode;
    (void)offset;
    (void)buffer;
    (void)n;
    (void)bytes_read;
    return NOT_IMPLEMENTED;
    
    //check to make sure inputs are valid

    //for 0 to n, use the helper function to read and copy 1 byte at a time
}

fs_retcode_t inode_modify_data(filesystem_t *fs, inode_t *inode, size_t offset, void *buffer, size_t n)
{
    (void)fs;
    (void)inode;
    (void)offset;
    (void)buffer;
    (void)n;
    return NOT_IMPLEMENTED;

    //check to see if the input is valid

    //calculate the final filesize and verify there are enough blocks to support it
    //use calculate_necessary_dblock_amount and available_dblocks


    //Write to existing data in your inode

    //For the new data, call "inode_write_data" and return
}

fs_retcode_t inode_shrink_data(filesystem_t *fs, inode_t *inode, size_t new_size)
{
    (void)fs;

    //display_filesystem(fs, DISPLAY_INODES);
    
    //check to see if inputs are in valid range
    if(fs == NULL || inode == NULL){
        return INVALID_INPUT;
    }

    // check if the new size exceeds the inode size
    if(new_size > inode->internal.file_size){
        return INVALID_INPUT;
    }

    /*

    //Calculate how many blocks to remove
    size_t current_file_size = inode->internal.file_size;
    size_t count_dblocks_currently_used = calculate_necessary_dblock_amount(current_file_size);
    size_t count_dblocks_needed_new_size = calculate_necessary_dblock_amount(new_size);
    size_t count_dblocks_remove = count_dblocks_currently_used - count_dblocks_needed_new_size;

    if(new_size < DATA_BLOCK_SIZE * INODE_DIRECT_BLOCK_COUNT){
        // free all indirect index dblocks
    }
    else{
        //free all indirect dblocks
    }

    */

    //helper function to free all indirect blocks

    /*
    find the last used indirect dblock
    free as many dblocks as needed based on the count_dblocks_remove
    if all dblocks are freed, free the indirect dblock
    go to the previous indirect dblock and repeat
    
    */

    //remove the remaining direct dblocks

    //update filesize and return
    return SUCCESS;
}

// make new_size to 0
fs_retcode_t inode_release_data(filesystem_t *fs, inode_t *inode)
{
    (void)fs;
    (void)inode;
    return NOT_IMPLEMENTED;
    //shrink to size 0
}
