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

size_t write_data_in_direct_dblock(filesystem_t *fs, inode_t *inode, void *data, size_t n){
    // get the current file size
    size_t original_file_size = inode->internal.file_size;

    //variable for total bytes written
    size_t bytes_to_write = n;
    size_t total_bytes_written = 0;

    // change data to a byte pointer so we can access it as a sequence of bytes
    byte *data_ptr_inBytes = (byte *)data;

    //check if writing bytes to original file size will exceet direct data blocks
    size_t max_direct_dblock_bytes = INODE_DIRECT_BLOCK_COUNT * DATA_BLOCK_SIZE;
    size_t available_direct_dblock_space = 0;

    //if there is still space to write in the data dblock
    if(original_file_size < max_direct_dblock_bytes){
        available_direct_dblock_space = max_direct_dblock_bytes - original_file_size;
        if(bytes_to_write > available_direct_dblock_space){
            bytes_to_write = available_direct_dblock_space;
        }
    }else{
        return 0;
    }

    //if there are no bytes to write to direct dblocks
    if(bytes_to_write == 0){
        return 0;
    }

    size_t current_offset = original_file_size;

    //while the numbero f bytes we wrote is less then bytes we have to write
    while(total_bytes_written < bytes_to_write){
        byte *dblock_ptr;
        size_t offset_within_dblock;

        //find the dblock that we should start writing to
        fs_retcode_t result = find_dblock_with_bytes(fs, inode, current_offset, &dblock_ptr, &offset_within_dblock, true);
        if(result != SUCCESS){
            //reset file size
            inode->internal.file_size = original_file_size;
            return -1;
        }

        //find how many bytes to write in this specific file (if remaining bytes to write is less than remaining size in dblock)
        size_t write_dblock_bytes = DATA_BLOCK_SIZE - offset_within_dblock;
        if(write_dblock_bytes > (bytes_to_write - total_bytes_written)){
            write_dblock_bytes = bytes_to_write - total_bytes_written;
        }  

        //copy data from buffer to dblock at current dblock + offset position
        memcpy(dblock_ptr + offset_within_dblock, data_ptr_inBytes, write_dblock_bytes);

        //move data pointer forward by number of bytes written
        data_ptr_inBytes += write_dblock_bytes;

        current_offset += write_dblock_bytes;
        total_bytes_written += write_dblock_bytes;
        
        inode->internal.file_size = current_offset;
    }
    return total_bytes_written;
}

fs_retcode_t write_data_in_indirect_dblock(filesystem_t *fs, inode_t *inode, void *data, size_t n, size_t r){
    //if remaining bytes to fill equals 0
    if(r == 0){
        return SUCCESS;
    }

    // get the current file size
    size_t current_file_size = inode->internal.file_size;
    size_t bytes_to_write = r;
    size_t total_bytes_written = 0;

    //find the offset to start from in the data
    byte *data_ptr_inBytes = (byte *)data + (n-r);
    size_t current_offset = current_file_size;

    //while there are still bytes to write
    while(total_bytes_written < bytes_to_write){
        //get the current dblock pointer within index dblock, and offset within that dblock
        byte *dblock_ptr;
        size_t offset_within_dblock;

        //call helper function
        fs_retcode_t result = find_dblock_with_bytes(fs, inode, current_offset, &dblock_ptr, &offset_within_dblock, true);
        if(result != SUCCESS){
            return result;
        }

        //find how many bytes to write in this specific block (if remaining bytes to write is less than remaining size in dblock)
        size_t write_dblock_bytes = DATA_BLOCK_SIZE - offset_within_dblock;
        if(write_dblock_bytes > (bytes_to_write - total_bytes_written)){
            write_dblock_bytes = bytes_to_write - total_bytes_written;
        }  
 
        //copy data from buffer to dblock at current dblock + offset position
        memcpy(dblock_ptr + offset_within_dblock, data_ptr_inBytes, write_dblock_bytes);
 
        //move data pointer forward by number of bytes written
        data_ptr_inBytes += write_dblock_bytes;
 
        current_offset += write_dblock_bytes;
        total_bytes_written += write_dblock_bytes;
         
        inode->internal.file_size = current_offset;
    }
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

    size_t original_file_size = inode->internal.file_size;

    // fill the direct nodes if necessary (helper function) 
    int bytes_written = write_data_in_direct_dblock(fs, inode, data, n);
    if(bytes_written < 0){
        inode->internal.file_size = original_file_size;
        return INSUFFICIENT_DBLOCKS;
    }

    // fill in indirect nodes if necessary (helper function)
    int remaining_bytes_to_write = n - bytes_written;

    //if there are more bytes to write
    if(remaining_bytes_to_write > 0){
        fs_retcode_t result = write_data_in_indirect_dblock(fs, inode, data, n, remaining_bytes_to_write);
        if(result != SUCCESS){
            //reset file size and return error
            inode->internal.file_size = original_file_size;
            return result;
        }
    }

    if(inode->internal.file_size != original_file_size + n){
        inode->internal.file_size = original_file_size + n;
    }
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
        find_dblock_with_bytes(fs, inode, current_offset, &dblock_ptr, &offset_within_dblock, false);

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

fs_retcode_t inode_modify_data(filesystem_t *fs, inode_t *inode, size_t offset, void *buffer, size_t n){
    //check to see if the input is valid
    if (fs == NULL || inode == NULL) {
        return INVALID_INPUT;
    }

    //get current file size
    size_t current_file_size = inode->internal.file_size;

    //if the offset is larger than the current file size, cannot fill
    if (offset > current_file_size) {
        return INVALID_INPUT;
    }

    //calculate the final filesize and check to make sure there are enough blocks
    size_t final_file_size;
    if(offset + n > current_file_size){
        final_file_size = offset + n;
    }else{
        final_file_size = current_file_size;
    }

    //use calculate_necessary_dblock_amount and available_dblocks
    size_t total_dblocks_needed = calculate_necessary_dblock_amount(final_file_size);
    size_t current_dblocks_used = calculate_necessary_dblock_amount(current_file_size);
    size_t new_dblocks_needed = total_dblocks_needed - current_dblocks_used;

    //get available remaining dblocks in fs
    size_t remaining_fs_dblocks = available_dblocks(fs);

    if(new_dblocks_needed > remaining_fs_dblocks){
        return INSUFFICIENT_DBLOCKS;
    }

    byte *buffer_destination = (byte *)buffer;
    size_t current_offset = offset;
    size_t remaining_bytes_to_modify = n;

    //while there are still remaining bytes to write
    while(remaining_bytes_to_modify > 0){
        byte *curr_dblock_ptr; //stores the pointer to the dblock we write from
        size_t offset_within_dblock; //stores the position within that dblock

        //find the dblock we should start modifying from
        fs_retcode_t result = find_dblock_with_bytes(fs, inode, current_offset, &curr_dblock_ptr, &offset_within_dblock, true);

        //if there was an error, return error
        if(result != SUCCESS){
            inode->internal.file_size = current_file_size;
            return result;
        }

        //calcualte how many bytes we write in this current dblock
        size_t bytes_to_write = DATA_BLOCK_SIZE - offset_within_dblock;
        if(bytes_to_write > remaining_bytes_to_modify){
            bytes_to_write = remaining_bytes_to_modify;
        }

        //copy data from buffer to dblock at current dblock + offset position
        memcpy(curr_dblock_ptr + offset_within_dblock, buffer_destination, bytes_to_write);

        //move buffer pointer forward by number of bytes written
        buffer_destination += bytes_to_write;

        current_offset += bytes_to_write;
        remaining_bytes_to_modify -= bytes_to_write;
        
        //if we are adding on to the original file size, update file size
        if(current_offset > inode->internal.file_size){
            inode->internal.file_size = current_offset;
        }

    }
    return SUCCESS;
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
