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

    // find the last claimed dblock
    size_t dblock_index = current_file_size / DATA_BLOCK_SIZE;

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

fs_retcode_t write_data_in_indirect_dblock(filesystem_t *fs, inode_t *inode, void *data, size_t n, size_t r){
    // get the current file size
    size_t current_file_size = inode->internal.file_size;

    // get file size after putting all bytes in direct dblocks (bytes already in indirect blocks)
    size_t file_size_after_direct_dblocks = 0;
    if (current_file_size > 256) {
        file_size_after_direct_dblocks = current_file_size - 256;
    }

    // Calculate position within indirect blocks
    size_t current_index_block_number = file_size_after_direct_dblocks / 960;
    size_t dblock_in_current_index = (file_size_after_direct_dblocks / DATA_BLOCK_SIZE) % 15;
    size_t dblock_partial_bytes_count = file_size_after_direct_dblocks % DATA_BLOCK_SIZE;
    
    // get the starting position of data that we should write 
    byte *data_ptr = (byte *)data + 256;

    //get remaining bytes to write
    size_t remaining_bytes_to_write = r;

    //bytes to write for a specific dblock
    size_t bytes_to_write_individual_dblock;

    if(remaining_bytes_to_write == 0){
        return SUCCESS;
    }

    //total bytes written variable
    size_t total_bytes_written = 0;

    //get the first indirect (index) dblock index
    dblock_index_t indirect_dblock_index = inode->internal.indirect_dblock;

    //check if the first indirect (index) dblock index == 0 - meaning its not allocated
    if(indirect_dblock_index == 0){
        dblock_index_t new_indirect_dblock_index;
        fs_retcode_t new_dblock_return = claim_available_dblock(fs, &new_indirect_dblock_index);
        if(new_dblock_return  != SUCCESS){
            return INSUFFICIENT_DBLOCKS;
        }
        inode->internal.indirect_dblock = new_indirect_dblock_index;
        memset(fs->dblocks + (new_indirect_dblock_index * DATA_BLOCK_SIZE), 0, DATA_BLOCK_SIZE);
        indirect_dblock_index = new_indirect_dblock_index;
    }

    //find the index dblock that we should work with
    dblock_index_t curr_indirect_dblock_index = indirect_dblock_index;
    for(size_t i = 0; i < current_index_block_number; i++){
        // get a pointer to the current indirect dblock index
        dblock_index_t *curr_indirect_dblock_index_ptr = cast_dblock_ptr(fs->dblocks + (curr_indirect_dblock_index * DATA_BLOCK_SIZE));
        
        // get a pointer to the next indirect index dblock index 
        dblock_index_t next_index_dblock = curr_indirect_dblock_index_ptr[15];
        
        //if the next index dblock is not allocated
        if(next_index_dblock == 0){
            dblock_index_t new_indirect_dblock_index;
            fs_retcode_t new_dblock_return = claim_available_dblock(fs, &new_indirect_dblock_index);
            if(new_dblock_return != SUCCESS){
                return INSUFFICIENT_DBLOCKS;
            }
            curr_indirect_dblock_index_ptr[15] = new_indirect_dblock_index;
            memset(fs->dblocks + (new_indirect_dblock_index * DATA_BLOCK_SIZE), 0, DATA_BLOCK_SIZE);
            curr_indirect_dblock_index = new_indirect_dblock_index;
        }else{
            curr_indirect_dblock_index = next_index_dblock;
        }
    }

    while(remaining_bytes_to_write > 0){
        //get the pointer of the current index dblock
        dblock_index_t *curr_indirect_dblock_index_ptr = cast_dblock_ptr(fs->dblocks + (curr_indirect_dblock_index * DATA_BLOCK_SIZE));

        // go through all the dblocks and start writing to available dblocks
        for(size_t i = dblock_in_current_index; i < 15; i++){
            //get the index value at the certain dblock element
            dblock_index_t data_dblock_index = curr_indirect_dblock_index_ptr[i];

            if(data_dblock_index == 0){
                fs_retcode_t new_dblock_return = claim_available_dblock(fs, &data_dblock_index);
                if(new_dblock_return  != SUCCESS){
                    return INSUFFICIENT_DBLOCKS;
                }
                curr_indirect_dblock_index_ptr[i] = data_dblock_index;
            }

            // get pointer to the data dblock we just allocated
            byte *data_dblock_ptr = fs->dblocks + (data_dblock_index * DATA_BLOCK_SIZE);

            // calculate the number of bytes we need to write within this dblock
            if(i == dblock_in_current_index && dblock_partial_bytes_count > 0){
                // first block with partial content
                if(remaining_bytes_to_write > (DATA_BLOCK_SIZE - dblock_partial_bytes_count)){
                    bytes_to_write_individual_dblock = DATA_BLOCK_SIZE - dblock_partial_bytes_count;
                }else{
                    bytes_to_write_individual_dblock = remaining_bytes_to_write;
                }

                // get the address at which we should start writing in the data within the dblock
                byte *destination = data_dblock_ptr + dblock_partial_bytes_count;
                
                // copy the data from the data pointer to the destination
                memcpy(destination, data_ptr, bytes_to_write_individual_dblock);
            }else{
                // filling up bytes that are either full or a fully new block
                if(remaining_bytes_to_write > DATA_BLOCK_SIZE){
                    bytes_to_write_individual_dblock = DATA_BLOCK_SIZE;
                }else{
                    bytes_to_write_individual_dblock = remaining_bytes_to_write;
                }
                
                // copy the data from the data pointer to the destination
                memcpy(data_dblock_ptr, data_ptr, bytes_to_write_individual_dblock);
            }
           
            // update the data pointer so it points to the next byte of data
            data_ptr += bytes_to_write_individual_dblock;

            // update the remaining number of bytes to write
            remaining_bytes_to_write -= bytes_to_write_individual_dblock;

            // update the bytes written 
            total_bytes_written += bytes_to_write_individual_dblock;
            
            if(remaining_bytes_to_write == 0){
                break;
            }
        }

        // reset the dblock for the next index dblock
        dblock_in_current_index = 0;
        dblock_partial_bytes_count = 0;

        //reset the next index dblock
        if(remaining_bytes_to_write > 0){
            dblock_index_t next_index_dblock = curr_indirect_dblock_index_ptr[15];
            if(next_index_dblock == 0){
                dblock_index_t new_indirect_dblock_index;
                fs_retcode_t new_dblock_return = claim_available_dblock(fs, &new_indirect_dblock_index);
                if(new_dblock_return  != SUCCESS){
                    return INSUFFICIENT_DBLOCKS;
                }
                curr_indirect_dblock_index_ptr[15] = new_indirect_dblock_index;
                curr_indirect_dblock_index = new_indirect_dblock_index;
                memset(fs->dblocks + (new_indirect_dblock_index * DATA_BLOCK_SIZE), 0, DATA_BLOCK_SIZE);
            }else{
                curr_indirect_dblock_index = next_index_dblock;
            }
        }
    }
    inode->internal.file_size = current_file_size + total_bytes_written;
    return SUCCESS;
}

// ----------------------- CORE FUNCTION ----------------------- //

fs_retcode_t inode_write_data(filesystem_t *fs, inode_t *inode, void *data, size_t n)
{
    //Check for valid input
    if(fs == NULL || inode == NULL){
        return INVALID_INPUT;
    }

    // do we have enough dblocks to store the data. if not, error. 
    // find the number of dblocks needed to store n bytes
    size_t total_dblocks_needed = calculate_necessary_dblock_amount(inode->internal.file_size + n);
    size_t current_dblocks_used = calculate_necessary_dblock_amount(inode->internal.file_size);
    size_t remaining_dblocks_needed = total_dblocks_needed - current_dblocks_used;
    
    // find the number of available dblocks in the filesystem
    size_t available_dblocks_infile = available_dblocks(fs);
    
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
    //display_filesystem(fs, DISPLAY_FS_FORMAT);
    //display_filesystem(fs, DISPLAY_INODES);
    
    //check to see if inputs are in valid range
    if(fs == NULL || inode == NULL){
        return INVALID_INPUT;
    }

    // check if the new size exceeds the inode size
    if(new_size > inode->internal.file_size){
        return INVALID_INPUT;
    }

    // check if the size equals the current size
    if(new_size == inode->internal.file_size){
        return SUCCESS;
    }

    return SUCCESS;
}

// make new_size to 0
fs_retcode_t inode_release_data(filesystem_t *fs, inode_t *inode)
{
    if(fs == NULL || inode == NULL){
        return INVALID_INPUT;
    }
    // for(int i = 0; i < INODE_DIRECT_BLOCK_COUNT; i++){
    //     //info(1, "current loop index: %d\n", i);

    //     dblock_index_t direct_dblock_index = inode->internal.direct_data[i];
    //     //info(1, "current dblock index: %u\n", direct_dblock_index);

    //     if(direct_dblock_index != 0){
    //         //info(1, "Releasing dblock with index: %zu\n", direct_dblock_index);
    //         //find the memory address of the specific block, using the dblock index
    //         byte *dblock_ptr = fs->dblocks + (direct_dblock_index * DATA_BLOCK_SIZE);
    //         fs_retcode_t returnCode = release_dblock(fs, dblock_ptr);
    //         if(returnCode != SUCCESS){
    //             return returnCode;
    //         }
    //         inode->internal.direct_data[i] = 0;
    //     }
    // }
    return SUCCESS;
}
