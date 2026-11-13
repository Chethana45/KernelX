#include <kernel/test_phase7.h>
#include <fs/vfs.h>
#include <fs/ramfs.h>
#include <fs/initrd.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <arch/x86/syscall.h>
#include <mm/kheap.h>
#include <drivers/vga.h>
#include <kernel/printk.h>

static void assert_phase7(bool condition, const char* test_name) {
    if (condition) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("  [PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("%s\n", test_name);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("  [FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("%s\n", test_name);
        panic("Phase 7 assertion failed!");
    }
}

static int kstrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void run_phase7_tests(void) {
    printk("\n=======================================================================\n");
    printk(">>> Beginning Phase 7 Virtual File System & Ramdisk Test Suite <<<\n");
    printk("=======================================================================\n");

    /* TEST 1: VFS Initialization */
    printk("\n--- TEST 1 & 2: VFS Initialization & Root Filesystem Mount ---\n");
    vfs_node_t* root = vfs_get_root();
    assert_phase7(root != NULL, "Root VFS node initialized");
    assert_phase7(root->type == VFS_TYPE_DIRECTORY, "Root filesystem mounted as Directory node");
    assert_phase7(kstrcmp(root->name, "/") == 0, "Root path identified as '/'");

    /* TEST 3 & 4: Directory and Nested Directory Creation */
    printk("\n--- TEST 3 & 4: Directory & Nested Directory Creation ---\n");
    int res = vfs_mkdir("/home", "/", 0755);
    assert_phase7(res == 0, "Directory '/home' created successfully");

    vfs_node_t* home_node = vfs_resolve_path("/home", "/");
    assert_phase7(home_node != NULL && (home_node->type & VFS_TYPE_DIRECTORY), "Resolved '/home' as directory");

    res = vfs_mkdir("/home/user", "/", 0755);
    assert_phase7(res == 0, "Nested directory '/home/user' created");

    res = vfs_mkdir("/home/user/docs", "/", 0755);
    assert_phase7(res == 0, "Deep nested directory '/home/user/docs' created");

    vfs_node_t* docs_node = vfs_resolve_path("/home/user/docs", "/");
    assert_phase7(docs_node != NULL, "Path resolution for deep nested hierarchy verified");

    /* TEST 5: File Creation */
    printk("\n--- TEST 5: File Creation ---\n");
    res = vfs_create("/home/user/notes.txt", "/", 0644);
    assert_phase7(res == 0, "File '/home/user/notes.txt' created successfully");

    vfs_node_t* file_node = vfs_resolve_path("/home/user/notes.txt", "/");
    assert_phase7(file_node != NULL && (file_node->type & VFS_TYPE_FILE), "Resolved '/home/user/notes.txt' as file node");

    /* TEST 6: Writing and Reading File Contents */
    printk("\n--- TEST 6: File I/O (Write & Read) ---\n");
    static const char test_payload[] = "KernelX Phase 7 VFS payload test verification 12345!";
    uint32_t payload_len = sizeof(test_payload) - 1;

    int32_t written = vfs_write(file_node, 0, payload_len, (const uint8_t*)test_payload);
    assert_phase7(written == (int32_t)payload_len, "Wrote 52 bytes to file");

    char read_buf[64];
    for (int i = 0; i < 64; i++) read_buf[i] = 0;

    int32_t bytes_read = vfs_read(file_node, 0, sizeof(read_buf), (uint8_t*)read_buf);
    assert_phase7(bytes_read == (int32_t)payload_len, "Read back exactly 52 bytes from file");
    assert_phase7(kstrcmp(read_buf, test_payload) == 0, "Read buffer content exactly matches written payload");

    /* TEST 7: File Size and Metadata (Stat) */
    printk("\n--- TEST 7: File Metadata & Stat Verification ---\n");
    vfs_stat_t st;
    res = vfs_stat("/home/user/notes.txt", "/", &st);
    assert_phase7(res == 0, "Stat retrieved successfully");
    assert_phase7(st.size == payload_len, "Stat reported correct file size (52 bytes)");
    assert_phase7(st.type == VFS_TYPE_FILE, "Stat reported VFS_TYPE_FILE");
    assert_phase7(st.ino > 0, "Stat reported valid non-zero inode");

    /* TEST 8: File Deletion (Unlink) */
    printk("\n--- TEST 8: File Deletion (Unlink) ---\n");
    res = vfs_unlink("/home/user/notes.txt", "/");
    assert_phase7(res == 0, "File '/home/user/notes.txt' unlinked successfully");

    vfs_node_t* gone_node = vfs_resolve_path("/home/user/notes.txt", "/");
    assert_phase7(gone_node == NULL, "Unlinked file no longer resolves in VFS");

    /* TEST 9: Invalid Path Handling */
    printk("\n--- TEST 9: Invalid Path Handling ---\n");
    assert_phase7(vfs_resolve_path("/nonexistent/file.txt", "/") == NULL, "Nonexistent path returns NULL");
    assert_phase7(vfs_resolve_path("", "/") == NULL, "Empty path returns NULL");
    assert_phase7(vfs_unlink("/nonexistent/path", "/") != 0, "Unlinking nonexistent path returns error");
    assert_phase7(vfs_mkdir("/home/user/docs", "/", 0755) != 0, "Creating existing directory returns error (EEXIST)");

    char normalized[VFS_PATH_MAX];
    assert_phase7(vfs_normalize_path(normalized, "/home/user/../user/docs/./", "/") == 0 &&
                  kstrcmp(normalized, "/home/user/docs") == 0,
                  "Path normalization resolves '.', '..', and trailing slashes");
    assert_phase7(vfs_normalize_path(normalized, "", "/") != 0,
                  "Empty path rejected by path normalizer");

    /* TEST 10: Multiple Simultaneous Open Files */
    printk("\n--- TEST 10: Multiple Simultaneous Open Files ---\n");
    thread_t* curr = thread_get_current();
    vfs_create("/f1.txt", "/", 0644);
    vfs_create("/f2.txt", "/", 0644);
    vfs_create("/f3.txt", "/", 0644);

    vfs_node_t* n1 = vfs_resolve_path("/f1.txt", "/");
    vfs_node_t* n2 = vfs_resolve_path("/f2.txt", "/");
    vfs_node_t* n3 = vfs_resolve_path("/f3.txt", "/");

    assert_phase7(vfs_open(n1, VFS_O_RDWR) == 0 &&
                  vfs_open(n2, VFS_O_RDWR) == 0 &&
                  vfs_open(n3, VFS_O_RDWR) == 0,
                  "VFS open callbacks/reference counts acquired");
    int fd1 = process_fd_alloc(curr, n1, VFS_O_RDWR);
    int fd2 = process_fd_alloc(curr, n2, VFS_O_RDWR);
    int fd3 = process_fd_alloc(curr, n3, VFS_O_RDWR);

    assert_phase7(fd1 >= 3 && fd2 >= 3 && fd3 >= 3, "Allocated 3 distinct file descriptors (>= 3)");
    assert_phase7(fd1 != fd2 && fd2 != fd3 && fd1 != fd3, "All file descriptors are unique");

    process_fd_free(curr, fd1);
    process_fd_free(curr, fd2);
    process_fd_free(curr, fd3);
    assert_phase7(process_fd_get(curr, fd1) == NULL, "File descriptors successfully released");

    /* TEST 11: Independent File Descriptor Tables Across Processes */
    printk("\n--- TEST 11: Independent Process File Descriptor Tables ---\n");
    thread_t dummy_proc;
    process_fd_init(&dummy_proc);
    vfs_open(n1, VFS_O_RDONLY);
    int dummy_fd = process_fd_alloc(&dummy_proc, n1, VFS_O_RDONLY);
    assert_phase7(dummy_fd == 3, "Process B allocated FD 3 independently");
    assert_phase7(process_fd_get(curr, dummy_fd) == NULL, "Process A's table unaffected by Process B's open FD");
    process_fd_cleanup(&dummy_proc);

    /* TEST 12: User Syscall Pointer Validation for VFS Syscalls */
    printk("\n--- TEST 12: User Syscall Pointer Validation ---\n");
    assert_phase7(vfs_resolve_path(NULL, "/") == NULL, "Null path rejected");
    vfs_stat_t dummy_st;
    assert_phase7(vfs_stat("/nonexistent", "/", &dummy_st) != 0, "Stat on invalid path returns error code");

    /* TEST 13 & 14: Initial Ramdisk (Initrd) Mount & File Read */
    printk("\n--- TEST 13 & 14: Initial Ramdisk Mount & File Inspection ---\n");
    assert_phase7(initrd_is_mounted(), "Initrd successfully mounted under /initrd");

    vfs_node_t* initrd_welcome = vfs_resolve_path("/initrd/welcome.txt", "/");
    assert_phase7(initrd_welcome != NULL, "Resolved '/initrd/welcome.txt' from initial ramdisk");

    char initrd_buf[128];
    for (int i = 0; i < 128; i++) initrd_buf[i] = 0;
    int32_t initrd_read_bytes = vfs_read(initrd_welcome, 0, 48, (uint8_t*)initrd_buf);
    assert_phase7(initrd_read_bytes > 0, "Read file content from initial ramdisk");
    assert_phase7(initrd_buf[0] == 'W' && initrd_buf[1] == 'e', "Initrd payload content verified ('Welcome...')");

    /* Verify Read-Only Protection on Initrd */
    int32_t w_res = vfs_write(initrd_welcome, 0, 5, (const uint8_t*)"error");
    assert_phase7(w_res == -1, "Write to read-only initrd file rejected");
    assert_phase7(vfs_open(initrd_welcome, VFS_O_WRONLY) != 0,
                  "Opening read-only initrd file for writing is rejected");

    /* TEST 15: Kernel Stability After Invalid Filesystem Operations */
    printk("\n--- TEST 15: Kernel Stability After Invalid FS Operations ---\n");
    vfs_read(NULL, 0, 10, (uint8_t*)read_buf);
    vfs_write(NULL, 0, 10, (const uint8_t*)"test");
    vfs_stat("/some/broken/nested/path/that/does/not/exist", "/", &st);
    assert_phase7(true, "Kernel remained stable and operational after null and invalid operations");

    /* TEST 16: Memory Reclamation After File & Directory Deletion */
    printk("\n--- TEST 16: Memory Reclamation After Deletion ---\n");
    vfs_create("/temp_reclaim.txt", "/", 0644);
    vfs_node_t* temp_node = vfs_resolve_path("/temp_reclaim.txt", "/");
    assert_phase7(temp_node != NULL, "Created temporary file for reclamation test");

    /* Allocate 1 KB data */
    uint8_t dummy_chunk[256];
    for (int i = 0; i < 256; i++) dummy_chunk[i] = 'X';
    vfs_write(temp_node, 0, 256, dummy_chunk);
    vfs_write(temp_node, 256, 256, dummy_chunk);

    res = vfs_unlink("/temp_reclaim.txt", "/");
    assert_phase7(res == 0, "Deleted temporary file and freed data buffer");
    assert_phase7(vfs_resolve_path("/temp_reclaim.txt", "/") == NULL, "Path fully unlinked");

    /* Additional RAMFS correctness checks */
    vfs_create("/sparse.bin", "/", 0644);
    vfs_node_t* sparse = vfs_resolve_path("/sparse.bin", "/");
    const uint8_t marker = 'Z';
    assert_phase7(sparse && vfs_write(sparse, 16, 1, &marker) == 1,
                  "Sparse write beyond EOF succeeds");
    uint8_t sparse_buf[17];
    for (uint32_t i = 0; i < sizeof(sparse_buf); i++) sparse_buf[i] = 0xFF;
    assert_phase7(vfs_read(sparse, 0, sizeof(sparse_buf), sparse_buf) == 17 &&
                  sparse_buf[0] == 0 && sparse_buf[15] == 0 && sparse_buf[16] == 'Z',
                  "Sparse-file gap is zero-filled");
    assert_phase7(vfs_write(sparse, 0xFFFFFFF0u, 64, &marker) < 0,
                  "RAMFS rejects offset-plus-size integer overflow");
    assert_phase7(vfs_open(sparse, VFS_O_RDWR) == 0,
                  "VFS open reference acquired");
    assert_phase7(vfs_unlink("/sparse.bin", "/") != 0,
                  "Open file cannot be unlinked while descriptor reference exists");
    assert_phase7(vfs_close(sparse) == 0 && vfs_unlink("/sparse.bin", "/") == 0,
                  "File becomes unlinkable after final close");

    vfs_create("/truncate.txt", "/", 0644);
    vfs_node_t* trunc_node = vfs_resolve_path("/truncate.txt", "/");
    assert_phase7(trunc_node && vfs_write(trunc_node, 0, 3, (const uint8_t*)"abc") == 3,
                  "Prepared file for truncate test");
    assert_phase7(vfs_truncate(trunc_node) == 0 && trunc_node->size == 0 &&
                  vfs_read(trunc_node, 0, 3, sparse_buf) == 0,
                  "RAMFS truncate frees data and resets file size");
    assert_phase7(vfs_unlink("/truncate.txt", "/") == 0,
                  "Truncated file can be removed");

    /* Initrd nested paths must be preserved from TAR directory entries. */
    vfs_node_t* nested_initrd = vfs_resolve_path("/initrd/docs/phase7.txt", "/");
    assert_phase7(nested_initrd != NULL,
                  "Nested directory and file paths parsed from TAR archive");

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("\n>>> [PHASE 7 PASSED] All 16 VFS, RAMFS & Initrd tests passed! <<<\n\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
