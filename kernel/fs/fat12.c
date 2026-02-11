#include <kernel/vfs.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/kernel.h>
#include <stdint.h>
#include <stddef.h>

typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];            
    char     oem_name[8];       
    uint16_t bytes_per_sector;  
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;  
    uint8_t  num_fats;          
    uint16_t root_entries;      
    uint16_t total_sectors_16;  
    uint8_t  media_type;        
    uint16_t sectors_per_fat;   
    uint16_t sectors_per_track; 
    uint16_t num_heads;         
    uint32_t hidden_sectors;    
    uint32_t total_sectors_32; 
    
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];   
} fat12_bpb_t;

typedef struct __attribute__((packed)) {
    char     name[8];           
    char     ext[3];            
    uint8_t  attr;              
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;     
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_low;     
    uint32_t file_size;
} fat12_dirent_t;

#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F

#define FAT12_FREE          0x000
#define FAT12_RESERVED      0x001
#define FAT12_BAD           0xFF7
#define FAT12_EOF_MIN       0xFF8
#define FAT12_EOF           0xFFF

typedef struct {
    uint8_t* fat;               
    uint32_t fat_size;          
    uint32_t root_dir_sector;   
    uint32_t root_dir_sectors;  
    uint32_t data_start_sector; 
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t root_entries;
    uint32_t total_clusters;
    int      mounted;
} fat12_fs_t;

static fat12_fs_t fat12_fs;

extern int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);

static int fat12_read_sector(uint32_t sector, void* buffer) {
    return ata_read_sectors(sector, 1, buffer);
}

static uint16_t fat12_get_fat_entry(uint16_t cluster) {
    if (!fat12_fs.fat) return FAT12_EOF;
    
    uint32_t offset = cluster + (cluster / 2);  /* cluster * 1.5 */
    uint16_t value = *(uint16_t*)(fat12_fs.fat + offset);
    
    if (cluster & 1) {
        /* Odd cluster: high 12 bits */
        value >>= 4;
    } else {
        /* Even cluster: low 12 bits */
        value &= 0x0FFF;
    }
    
    return value;
}

static uint32_t fat12_cluster_to_sector(uint16_t cluster) {
    return fat12_fs.data_start_sector + 
           (cluster - 2) * fat12_fs.sectors_per_cluster;
}

int fat12_mount(void) {
    uint8_t sector[512];
    
    if (fat12_read_sector(0, sector) < 0) {
        kprintf("FAT12: Failed to read boot sector\n");
        return -EIO;
    }
    
    fat12_bpb_t* bpb = (fat12_bpb_t*)sector;
    
    if (bpb->bytes_per_sector != 512) {
        kprintf("FAT12: Unsupported sector size %d\n", bpb->bytes_per_sector);
        return -EINVAL;
    }
    
    fat12_fs.bytes_per_sector    = bpb->bytes_per_sector;
    fat12_fs.sectors_per_cluster = bpb->sectors_per_cluster;
    fat12_fs.root_entries        = bpb->root_entries;
    
    uint32_t fat_start   = bpb->reserved_sectors;
    uint32_t fat_sectors = bpb->sectors_per_fat * bpb->num_fats;
    fat12_fs.fat_size    = bpb->sectors_per_fat * 512;
    
    fat12_fs.root_dir_sector   = fat_start + fat_sectors;
    fat12_fs.root_dir_sectors  = (bpb->root_entries * 32 + 511) / 512;
    fat12_fs.data_start_sector = fat12_fs.root_dir_sector + fat12_fs.root_dir_sectors;
    
    uint32_t total_sectors = bpb->total_sectors_16 ? 
                             bpb->total_sectors_16 : bpb->total_sectors_32;
    uint32_t data_sectors = total_sectors - fat12_fs.data_start_sector;
    fat12_fs.total_clusters = data_sectors / bpb->sectors_per_cluster;
    
    fat12_fs.fat = kmalloc(fat12_fs.fat_size);
    if (!fat12_fs.fat) {
        kprintf("FAT12: Failed to allocate FAT buffer\n");
        return -ENOMEM;
    }
    
    for (uint32_t i = 0; i < bpb->sectors_per_fat; i++) {
        if (fat12_read_sector(fat_start + i, fat12_fs.fat + i * 512) < 0) {
            kfree(fat12_fs.fat);
            fat12_fs.fat = NULL;
            return -EIO;
        }
    }
    
    fat12_fs.mounted = 1;
    kprintf("FAT12: Mounted, %d clusters, %d bytes/cluster\n",
            fat12_fs.total_clusters, 
            fat12_fs.sectors_per_cluster * 512);
    
    return 0;
}

int fat12_unmount(void) {
    if (fat12_fs.fat) {
        kfree(fat12_fs.fat);
        fat12_fs.fat = NULL;
    }
    fat12_fs.mounted = 0;
    return 0;
}

static int fat12_compare_name(const fat12_dirent_t* entry, const char* name) {
    char fat_name[12];
    int i, j;
    
    for (i = 0; i < 8 && entry->name[i] != ' '; i++) {
        fat_name[i] = entry->name[i];
    }
    
    if (entry->ext[0] != ' ') {
        fat_name[i++] = '.';
        for (j = 0; j < 3 && entry->ext[j] != ' '; j++) {
            fat_name[i++] = entry->ext[j];
        }
    }
    fat_name[i] = '\0';
    
    for (i = 0; fat_name[i] && name[i]; i++) {
        char a = fat_name[i];
        char b = name[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    
    return fat_name[i] == name[i];
}

static int fat12_find_in_dir(uint16_t dir_cluster, const char* name, 
                              fat12_dirent_t* result) {
    uint8_t sector[512];
    
    if (dir_cluster == 0) {
        for (uint32_t s = 0; s < fat12_fs.root_dir_sectors; s++) {
            if (fat12_read_sector(fat12_fs.root_dir_sector + s, sector) < 0) {
                return -EIO;
            }
            
            fat12_dirent_t* entries = (fat12_dirent_t*)sector;
            for (int i = 0; i < 16; i++) {  /* 16 entries per sector */
                if (entries[i].name[0] == 0x00) {
                    return -ENOENT;
                }
                if ((unsigned char)entries[i].name[0] == 0xE5) {
                    /* Deleted entry */
                    continue;
                }
                if (entries[i].attr == FAT_ATTR_LFN) {
                    /* Long filename entry - skip */
                    continue;
                }
                
                if (fat12_compare_name(&entries[i], name)) {
                    *result = entries[i];
                    return 0;
                }
            }
        }
    } else {
        uint16_t cluster = dir_cluster;
        while (cluster >= 2 && cluster < FAT12_EOF_MIN) {
            uint32_t sector_num = fat12_cluster_to_sector(cluster);
            
            for (uint32_t s = 0; s < fat12_fs.sectors_per_cluster; s++) {
                if (fat12_read_sector(sector_num + s, sector) < 0) {
                    return -EIO;
                }
                
                fat12_dirent_t* entries = (fat12_dirent_t*)sector;
                for (int i = 0; i < 16; i++) {
                    if (entries[i].name[0] == 0x00) return -ENOENT;
                    if ((unsigned char)entries[i].name[0] == 0xE5) continue;
                    if (entries[i].attr == FAT_ATTR_LFN) continue;
                    
                    if (fat12_compare_name(&entries[i], name)) {
                        *result = entries[i];
                        return 0;
                    }
                }
            }
            
            cluster = fat12_get_fat_entry(cluster);
        }
    }
    
    return -ENOENT;
}

int fat12_lookup(const char* path, fat12_dirent_t* result) {
    if (!fat12_fs.mounted) return -ENODEV;
    if (!path || path[0] != '/') return -EINVAL;
    
    if (path[1] == '\0') {
        result->name[0]     = '/';
        result->attr        = FAT_ATTR_DIRECTORY;
        result->cluster_low = 0;
        result->file_size   = 0;
        return 0;
    }
    
    uint16_t current_cluster = 0;
    const char* p = path + 1;
    char component[13];
    
    while (*p) {
        int i = 0;
        while (*p && *p != '/' && i < 12) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        if (*p == '/') p++;
        
        int rc = fat12_find_in_dir(current_cluster, component, result);
        if (rc < 0) return rc;
        
        if (*p && !(result->attr & FAT_ATTR_DIRECTORY)) {
            return -ENOTDIR;
        }
        
        current_cluster = result->cluster_low;
    }
    
    return 0;
}

ssize_t fat12_read(uint16_t start_cluster, uint32_t file_size,
                   void* buffer, size_t count, off_t offset) {
    if (!fat12_fs.mounted) return -ENODEV;
    if (offset >= (off_t)file_size) return 0;
    
    if (offset + count > file_size) {
        count = file_size - offset;
    }
    
    uint32_t cluster_size = fat12_fs.sectors_per_cluster * 512;
    uint8_t* buf = (uint8_t*)buffer;
    size_t bytes_read = 0;
    
    uint16_t cluster = start_cluster;
    uint32_t skip_clusters = offset / cluster_size;
    
    while (skip_clusters > 0 && cluster >= 2 && cluster < FAT12_EOF_MIN) {
        cluster = fat12_get_fat_entry(cluster);
        skip_clusters--;
    }
    
    uint32_t cluster_offset = offset % cluster_size;
    uint8_t sector_buf[512];
    
    while (bytes_read < count && cluster >= 2 && cluster < FAT12_EOF_MIN) {
        uint32_t sector = fat12_cluster_to_sector(cluster);
        
        for (uint32_t s = 0; s < fat12_fs.sectors_per_cluster && bytes_read < count; s++) {
            if (fat12_read_sector(sector + s, sector_buf) < 0) {
                return bytes_read > 0 ? (ssize_t)bytes_read : -EIO;
            }
            
            uint32_t sector_start = s * 512;
            uint32_t sector_end = sector_start + 512;
            
            if (sector_end <= cluster_offset) continue;
            
            uint32_t copy_start = (cluster_offset > sector_start) ? 
                                  cluster_offset - sector_start : 0;
            uint32_t copy_len = 512 - copy_start;
            if (copy_len > count - bytes_read) {
                copy_len = count - bytes_read;
            }
            
            for (uint32_t i = 0; i < copy_len; i++) {
                buf[bytes_read++] = sector_buf[copy_start + i];
            }
        }
        
        cluster_offset = 0;
        cluster = fat12_get_fat_entry(cluster);
    }
    
    return (ssize_t)bytes_read;
}

int fat12_readdir(uint16_t dir_cluster, int index, fat12_dirent_t* result) {
    if (!fat12_fs.mounted) return -ENODEV;
    
    uint8_t sector[512];
    int current_index = 0;
    
    if (dir_cluster == 0) {
        for (uint32_t s = 0; s < fat12_fs.root_dir_sectors; s++) {
            if (fat12_read_sector(fat12_fs.root_dir_sector + s, sector) < 0) {
                return -EIO;
            }
            
            fat12_dirent_t* entries = (fat12_dirent_t*)sector;
            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00) return -ENOENT;
                if ((unsigned char)entries[i].name[0] == 0xE5) continue;
                if (entries[i].attr == FAT_ATTR_LFN) continue;
                if (entries[i].attr & FAT_ATTR_VOLUME_ID) continue;
                
                if (current_index == index) {
                    *result = entries[i];
                    return 0;
                }
                current_index++;
            }
        }
    } else {
        /* Subdirectory */
        uint16_t cluster = dir_cluster;
        while (cluster >= 2 && cluster < FAT12_EOF_MIN) {
            uint32_t sector_num = fat12_cluster_to_sector(cluster);
            
            for (uint32_t s = 0; s < fat12_fs.sectors_per_cluster; s++) {
                if (fat12_read_sector(sector_num + s, sector) < 0) {
                    return -EIO;
                }
                
                fat12_dirent_t* entries = (fat12_dirent_t*)sector;
                for (int i = 0; i < 16; i++) {
                    if (entries[i].name[0] == 0x00) return -ENOENT;
                    if ((unsigned char)entries[i].name[0] == 0xE5) continue;
                    if (entries[i].attr == FAT_ATTR_LFN) continue;
                    if (entries[i].attr & FAT_ATTR_VOLUME_ID) continue;
                    
                    if (current_index == index) {
                        *result = entries[i];
                        return 0;
                    }
                    current_index++;
                }
            }
            
            cluster = fat12_get_fat_entry(cluster);
        }
    }
    
    return -ENOENT;
}

int fat12_is_mounted(void) {
    return fat12_fs.mounted;
}

ssize_t fat12_write(uint16_t cluster, const uint8_t* buf, size_t count, uint32_t offset) {
    (void)cluster; (void)buf; (void)count; (void)offset;
    return -EROFS;  /* Read-only filesystem */
}

int fat12_create(const char* name, uint8_t attr) {
    (void)name; (void)attr;
    return -EROFS;  /* Read-only filesystem */
}

int fat12_unlink(const char* name) {
    (void)name;
    return -EROFS;  /* Read-only filesystem */
}

int fat12_mkdir(const char* name) {
    (void)name;
    return -EROFS;  /* Read-only filesystem */
}
