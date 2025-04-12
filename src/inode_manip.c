#include "filesys.h"

#include <string.h>
#include <assert.h>

#include "utility.h"
#include "debug.h"

#include <math.h>

#include <stdbool.h>

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

//helper function made to reach the certain dblock we should work with, given an offset in bytes
fs_retcode_t find_dblock_with_bytes(filesystem_t *fs, inode_t *inode, size_t offset, byte **dblock_ptr, size_t *offset_within_dblock_ptr, bool need_to_write){
    if(fs == NULL || inode == NULL || dblock_ptr == NULL || offset_within_dblock_ptr == NULL){
        return INVALID_INPUT;
    }

    //if the offset in bytes is bigger than the file size in bytes, and we are not writing data, throw error
    //not possible to read after file size
    if(offset >= inode->internal.file_size && need_to_write == false){
        return INVALID_INPUT;
    }

    //dblock_ptr keeps track of the dblock that we want to return
    //offset_within_dblock_ptr keep track of how many bytes are already in the dblock, and what we should do next
    *dblock_ptr = NULL;
    *offset_within_dblock_ptr = 0;

    //check if the offset is within the direct D-blocks
    if(offset < INODE_DIRECT_BLOCK_COUNT * DATA_BLOCK_SIZE){
        // find the last claimed dblock
        size_t dblock_index = offset / DATA_BLOCK_SIZE;

        // find the offset in the dblock to write to
        *offset_within_dblock_ptr = offset % DATA_BLOCK_SIZE;

        // check if dblock is allocated
        if(inode->internal.direct_data[dblock_index] == 0){
            //if we are only reading, throw error
            if(need_to_write == false){
                return INVALID_INPUT;
            }

            dblock_index_t new_dblock_index;
            fs_retcode_t new_dblock_return = claim_available_dblock(fs, &new_dblock_index);
            if(new_dblock_return  != SUCCESS){
                return INSUFFICIENT_DBLOCKS;
            }
            inode->internal.direct_data[dblock_index] = new_dblock_index;
        }

        // set the output pointer to the start of the dblock
        *dblock_ptr = fs->dblocks + (inode->internal.direct_data[dblock_index] * DATA_BLOCK_SIZE);
        return SUCCESS;
    }

    //the offset is not in direct dblocks, so check indirect dblocks

    //check if the first indirect dblock is not allocated
    if(inode->internal.indirect_dblock == 0){
        //if we are only reading, then throw error
        if(need_to_write == false){
            return INVALID_INPUT;
        }

        //if we are writing, need to allocate new indirect dblock
        dblock_index_t new_indirect_dblock_index;
        fs_retcode_t new_dblock_return = claim_available_dblock(fs, &new_indirect_dblock_index);
        if(new_dblock_return  != SUCCESS){
            return INSUFFICIENT_DBLOCKS;
        }
        memset(fs->dblocks + (new_indirect_dblock_index * DATA_BLOCK_SIZE), 0, DATA_BLOCK_SIZE);
        inode->internal.indirect_dblock = new_indirect_dblock_index;
    }

    //find the position within the indirect dblocks

    //find the offset after removing the bytes within the direct dblocks
    size_t indirect_offset = offset - (INODE_DIRECT_BLOCK_COUNT * DATA_BLOCK_SIZE);

    //find the index dblock number that we are working with
    size_t curr_index_block_number = indirect_offset / INDIRECT_DBLOCK_MAX_DATA_SIZE;

    //find the byte offset within the current index block's range of data
    size_t offset_within_index_block = indirect_offset % INDIRECT_DBLOCK_MAX_DATA_SIZE;

    //find the data block number within the current index block 
    size_t data_block_index_in_current_index = offset_within_index_block / DATA_BLOCK_SIZE;

    //calculates the byte offset within the specific data dblock
    *offset_within_dblock_ptr = offset_within_index_block % DATA_BLOCK_SIZE;

    //start with the first indirect dblock
    dblock_index_t curr_indirect_dblock_index = inode->internal.indirect_dblock;

    // go through all the index dblocks and find the last one we use
    for(size_t i = 0; i < curr_index_block_number; i++){
        //get a pointer to the current indirect index dblock
        dblock_index_t *curr_indirect_dblock_index_ptr = cast_dblock_ptr(fs->dblocks + (curr_indirect_dblock_index * DATA_BLOCK_SIZE));

        //if the next index dblock is not allocated
        dblock_index_t next_index_dblock = curr_indirect_dblock_index_ptr[15];
        if(next_index_dblock == 0){
            //if we need to just read, throw error
            if(need_to_write == false){
                return INVALID_INPUT;
            }

            //if we need to write, create new indirect dblock
            dblock_index_t new_indirect_dblock_index;
            fs_retcode_t new_dblock_return = claim_available_dblock(fs, &new_indirect_dblock_index);
            if(new_dblock_return != SUCCESS){
                return INSUFFICIENT_DBLOCKS;
            }
            memset(fs->dblocks + (new_indirect_dblock_index * DATA_BLOCK_SIZE), 0, DATA_BLOCK_SIZE);
            curr_indirect_dblock_index_ptr[15] = new_indirect_dblock_index;
            curr_indirect_dblock_index = new_indirect_dblock_index;
        }else{
            curr_indirect_dblock_index = next_index_dblock;
        }
    }

    // now the current indirect dblock index points to the last index dblock used
    dblock_index_t *curr_indirect_dblock_index_ptr = cast_dblock_ptr(fs->dblocks + (curr_indirect_dblock_index * DATA_BLOCK_SIZE));

    //check if the data dblock is allocated
    if(curr_indirect_dblock_index_ptr[data_block_index_in_current_index] == 0){
        if(need_to_write == false){
            return INVALID_INPUT;
        }

        //to write, allocate a new dblock
        dblock_index_t new_data_dblock_index;
        fs_retcode_t result = claim_available_dblock(fs, &new_data_dblock_index);
        if(result != SUCCESS){
            return INSUFFICIENT_DBLOCKS;
        }

        //update hte index dblock with the new data dblock index
        curr_indirect_dblock_index_ptr[data_block_index_in_current_index] = new_data_dblock_index;
    }

    //set the output pointer to the start of the data dblock
    *dblock_ptr = fs->dblocks + (curr_indirect_dblock_index_ptr[data_block_index_in_current_index] * DATA_BLOCK_SIZE);

    return SUCCESS;
}


// ----------------------- CORE FUNCTION ----------------------- //

fs_retcode_t inode_write_data(filesystem_t *fs, inode_t *inode, void *data, size_t n){
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
    //check to make sure inputs are valid
    if(fs == NULL || inode == NULL || bytes_read == NULL){
        return INVALID_INPUT;
    }

    *bytes_read = 0;

    //get current file size
    size_t current_file_size = inode->internal.file_size;

    //check if the offset is larger than the file size, nothign to read past file size
    if(offset >= current_file_size){
        return SUCCESS;
    }

    //start reading at offset
    size_t file_bytes_to_read = current_file_size - offset;
    if(n > file_bytes_to_read){
        n = file_bytes_to_read;
    }

    //if offset equaled current file size
    if(n == 0){
        return SUCCESS;
    }

    //need to access memory through a byte pointer to increment by bytes, and read data
    byte *buffer_destination = (byte *)buffer;
    size_t remaining_bytes_to_read = n;
    size_t current_offset = offset;

    //until we read all the bytes
    while(remaining_bytes_to_read > 0){
        byte *dblock_ptr;
        size_t offset_within_dblock;
        fs_retcode_t result = find_dblock_with_bytes(fs, inode, current_offset, &dblock_ptr, &offset_within_dblock, false);
        if(result != SUCCESS){ //if some error occurs just return success
            return SUCCESS;
        }
        //calculate how many bytes we can read from this dblock
        size_t curr_bytes_in_dblock = DATA_BLOCK_SIZE - offset_within_dblock;
        if(curr_bytes_in_dblock > remaining_bytes_to_read){
            curr_bytes_in_dblock = remaining_bytes_to_read;
        }

        //copy data into the buffer destination
        memcpy(buffer_destination, dblock_ptr + offset_within_dblock, curr_bytes_in_dblock);

        buffer_destination += curr_bytes_in_dblock;
        current_offset += curr_bytes_in_dblock;
        remaining_bytes_to_read -= curr_bytes_in_dblock;
        *bytes_read += curr_bytes_in_dblock;

    }
    return SUCCESS;

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
    return SUCCESS;
}
