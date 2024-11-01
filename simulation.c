//  CITS2002 Project 2 2024
//  Student1:   24250666   Wei Shen Hong
//  Student2:   24122057   Yize Sun
//  Platform:   Ubuntu Linux
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>  // Required for variadic arguments in DebugPrintf

#define RAM_SIZE 16
#define VIRT_MEM_SIZE 32
#define PAGE_SIZE 2
#define FRAME_SIZE (RAM_SIZE / PAGE_SIZE)
#define PROCESSES 4
#define PAGES_PER_PROCESS 4
#define DISK 99

// Define the Page structure
struct Page {
    int process_id;
    int page_num;
    int last_accessed;
};

// Define pointers to dynamically allocate memory for RAM and virtual memory
struct Page** ram;           // Pointer to array of pointers for RAM
struct Page** virtual_mem;   // Pointer to array of pointers for virtual memory

int page_table[PROCESSES][PAGES_PER_PROCESS];
int current_page[PROCESSES] = {0};  // Track the next page number for each process
int time_step = 0;

static char m_strMsgBuff[1024];  // Buffer for DebugPrintf and FatalPrintf messages

// Function declarations
void initialize_memory();
void load_page(int process_id, int page_num);
void print_page_tables(FILE* out);
void print_ram(FILE* out);
int find_free_frame();
int find_lru_page(int process_id);
void evict_page(int frame);
int find_lru_page_global();
void cleanup_memory();  // Function to clean up dynamically allocated memory

// DebugPrintf function
static void DebugPrintf(const char* pformat, ...) {
    va_list args;
    va_start(args, pformat);
    vsnprintf(m_strMsgBuff, sizeof(m_strMsgBuff) - 1, (const char* __restrict)pformat, args);
    va_end(args);
    fputs("@ Debug: ", stderr);
    fputs(m_strMsgBuff, stderr);
    fputs("\n", stderr);
    fflush(stderr);
}

// FatalPrintf function
static void FatalPrintf(const char* pformat, ...) {
    va_list args;
    va_start(args, pformat);
    vsnprintf(m_strMsgBuff, sizeof(m_strMsgBuff) - 1, (const char* __restrict)pformat, args);
    va_end(args);
    fputs("! Error: ", stderr);
    fputs(m_strMsgBuff, stderr);
    fputs("\n", stderr);
    fflush(stderr);
    exit(1);  // Exit the program on fatal error
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s in.txt out.txt\n", argv[0]);
        return 1;
    }
    FILE *in = fopen(argv[1], "r");
    FILE *out = fopen(argv[2], "w");

    if (in == NULL || out == NULL) {
        FatalPrintf("Error opening file. Input: %s, Output: %s", argv[1], argv[2]);
    }
    // Initialize virtual memory and RAM
    DebugPrintf("Initializing virtual memory and RAM\n");
    initialize_memory();

    // Simulate paging requests
    int process_id;
    while (fscanf(in, "%d", &process_id) == 1) {
        int page_num = current_page[process_id];  // Get the next page number for the process
        DebugPrintf("Process %d is requesting page %d at time %d\n", process_id, page_num, time_step);
        load_page(process_id, page_num);          // Load the next page of the process
        current_page[process_id] = (current_page[process_id] + 1) % PAGES_PER_PROCESS;  // Move to the next page
        time_step++;
    }
    // Print results
    print_page_tables(out);
    print_ram(out);
    // Clean up dynamically allocated memory
    cleanup_memory();

    fclose(in);
    fclose(out);
    return 0;
}

void initialize_memory() {
    // Dynamically allocate memory for RAM and virtual memory
    ram = (struct Page**)malloc(RAM_SIZE * sizeof(struct Page*));
    virtual_mem = (struct Page**)malloc(VIRT_MEM_SIZE * sizeof(struct Page*));

    if (ram == NULL || virtual_mem == NULL) {
        FatalPrintf("Failed to allocate memory for RAM or virtual memory.");
    }
    // Initialize RAM and virtual memory with NULL
    for (int i = 0; i < RAM_SIZE; i++) {
        ram[i] = NULL;
    }
    for (int i = 0; i < VIRT_MEM_SIZE; i++) {
        virtual_mem[i] = NULL;
    }
    // Initialize page tables and virtual memory
    for (int i = 0; i < PROCESSES; i++) {
        for (int j = 0; j < PAGES_PER_PROCESS; j++) {
            // Allocate a new page in virtual memory
            struct Page* new_page = (struct Page*)malloc(sizeof(struct Page));
            if (new_page == NULL) {
                FatalPrintf("Failed to allocate memory for page %d of process %d", j, i);
            }
            new_page->process_id = i;
            new_page->page_num = j;
            new_page->last_accessed = 0;
            virtual_mem[i * PAGES_PER_PROCESS + j] = new_page;
            DebugPrintf("Initialized process %d, page %d in virtual memory\n", i, j);
            // Initialize page table (all pages in virtual memory initially)
            page_table[i][j] = DISK;
        }
    }
}

void load_page(int process_id, int page_num) {
    // Check if the page is already in RAM
    int current_frame = page_table[process_id][page_num];
    if (current_frame != DISK) {
        // Page is already in RAM, update the last accessed time and return, this allowed process to be read more than once
        for (int i = 0; i < PAGE_SIZE; i++) {
            ram[current_frame * PAGE_SIZE + i]->last_accessed = time_step;
        }
        DebugPrintf("Page %d of process %d is already in RAM (frame %d), updated last accessed time at %d\n",
                    page_num, process_id, current_frame, time_step);
        return;  // No need to load the page again
    }
    // Find a free frame in RAM
    int frame = find_free_frame();  // Look for a free frame in RAM
    if (frame == -1) {  // No free frame, perform LRU eviction
        frame = find_lru_page(process_id);  // Try to evict a page of the same process first
        if (frame == -1) {  // No LRU page for the same process, check globally
            frame = find_lru_page_global();
        }
        if (frame == -1) {  // No LRU page found
            FatalPrintf("Failed to find a frame to evict. No free frames or LRU pages available.");
        }
        DebugPrintf("Evicting page from frame %d for process %d\n", frame, process_id);
        evict_page(frame);  // Evict the page from the chosen frame
    }
    // Validate that the virtual memory page exists
    if (virtual_mem[process_id * PAGES_PER_PROCESS + page_num] == NULL) {
        FatalPrintf("Invalid page in virtual memory for process %d, page %d", process_id, page_num);
    }
    // Load the page from virtual memory to RAM
    for (int i = 0; i < PAGE_SIZE; i++) {
        ram[frame * PAGE_SIZE + i] = virtual_mem[process_id * PAGES_PER_PROCESS + page_num];
        // Update the last accessed time for the page
        ram[frame * PAGE_SIZE + i]->last_accessed = time_step;
    }
    // Update the page table to reflect that this page is now in RAM
    page_table[process_id][page_num] = frame;

    DebugPrintf("Loaded process %d, page %d into frame %d at time %d\n", process_id, page_num, frame, time_step);
}

void print_page_tables(FILE* out) {
    for (int i = 0; i < PROCESSES; i++) {
        for (int j = 0; j < PAGES_PER_PROCESS; j++) {
            if (j == PAGES_PER_PROCESS - 1) {
                fprintf(out, "%d", page_table[i][j]);  // No comma after the last element
            } else {
                fprintf(out, "%d, ", page_table[i][j]);
            }
        }
        fprintf(out, "\n");
    }
}
// prints the ram eg, 0,0,5; 0,0,5; 0,1,6; 0,1,6;...
void print_ram(FILE* out) {
    for (int i = 0; i < RAM_SIZE; i++) {
        if (ram[i] != NULL) {
            fprintf(out, "%d,%d,%d; ", ram[i]->process_id, ram[i]->page_num, ram[i]->last_accessed);
        } else {
            fprintf(out, "EMPTY; ");
        }
    }
    fprintf(out, "\n");
}

int find_free_frame() {
    for (int i = 0; i < FRAME_SIZE; i++) {
        if (ram[i * PAGE_SIZE] == NULL) {
            return i;
        }
    }
    return -1;
}

int find_lru_page(int process_id) {
    int lru_frame = -1;
    int oldest_time = time_step;

    for (int i = 0; i < FRAME_SIZE; i++) {
        int frame_start = i * PAGE_SIZE;
        // Check if the page belongs to the given process and is the least recently used
        if (ram[frame_start] != NULL && ram[frame_start]->process_id == process_id) {
            if (ram[frame_start]->last_accessed < oldest_time) {
                oldest_time = ram[frame_start]->last_accessed;
                lru_frame = i;
            }
        }
    }
    return lru_frame;
}

int find_lru_page_global() {
    int lru_frame = -1;
    int oldest_time = time_step;

    for (int i = 0; i < FRAME_SIZE; i++) {
        int frame_start = i * PAGE_SIZE;
        // Check if the frame is occupied and is the least recently used globally
        if (ram[frame_start] != NULL) {
            if (ram[frame_start]->last_accessed < oldest_time) {
                oldest_time = ram[frame_start]->last_accessed;
                lru_frame = i;
            }
        }
    }
    return lru_frame;
}

void evict_page(int frame) {
    if (frame == -1) {
        FatalPrintf("Error: No frame to evict.");
        return;
    }
    // Calculate the starting position of the frame in RAM
    int frame_start = frame * PAGE_SIZE;
    // Ensure we have a valid page before attempting to evict
    if (ram[frame_start] == NULL) {
        FatalPrintf("Error: Attempted to evict a non-existent page at frame %d.", frame);
        return;
    }
    // Extract process and page information before eviction
    int process_id = ram[frame_start]->process_id;
    int page_num = ram[frame_start]->page_num;
    // Mark the page as evicted by updating the page table to indicate that the page is now in virtual memory (disk)
    page_table[process_id][page_num] = DISK;
    // Log detailed information for debugging purposes
    DebugPrintf("Evicted process %d, page %d from frame %d (last accessed at time %d)", 
                process_id, page_num, frame, ram[frame_start]->last_accessed);
    // Clear the frame in RAM by setting all slots in the frame to NULL
    for (int i = 0; i < PAGE_SIZE; i++) {
        ram[frame_start + i] = NULL;
    }
}

void cleanup_memory() {
    // Free dynamically allocated memory for RAM and virtual memory
    for (int i = 0; i < VIRT_MEM_SIZE; i++) {
        if (virtual_mem[i] != NULL) {
            free(virtual_mem[i]);
        }
    }
    free(virtual_mem);
    free(ram);
}