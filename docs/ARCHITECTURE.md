# UnixOS - Documentation Technique

## Introduction

UnixOS est un système d'exploitation éducatif développé from scratch pour l'architecture x86 32-bit. Il s'inspire des principes de conception de Linux, Minix et des systèmes Unix traditionnels.

## Architecture Générale

### Vue d'Ensemble

```
┌─────────────────────────────────────────────────────────────────┐
│                     ESPACE UTILISATEUR                          │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐            │
│  │  init   │  │   sh    │  │   ls    │  │  cat    │  ...       │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘            │
│       │            │            │            │                  │
│  ┌────┴────────────┴────────────┴────────────┴────┐            │
│  │                    libc.a                       │            │
│  └────────────────────────┬────────────────────────┘            │
│                           │                                     │
│  ┌────────────────────────┴────────────────────────┐            │
│  │              ld.so (dynamic linker)             │            │
│  └────────────────────────┬────────────────────────┘            │
├───────────────────────────┼─────────────────────────────────────┤
│                      int 0x80                                   │
├───────────────────────────┼─────────────────────────────────────┤
│                     ESPACE KERNEL                               │
│  ┌────────────────────────┴────────────────────────┐            │
│  │               Syscall Dispatcher                │            │
│  └─────┬──────────┬──────────┬──────────┬─────────┘            │
│        │          │          │          │                       │
│  ┌─────┴───┐ ┌────┴────┐ ┌───┴───┐ ┌────┴────┐                 │
│  │ sys_fs  │ │sys_proc │ │sys_mem│ │sys_misc │                 │
│  └─────────┘ └─────────┘ └───────┘ └─────────┘                 │
│        │          │          │          │                       │
│  ┌─────┴──────────┴──────────┴──────────┴─────────┐            │
│  │                    VFS                          │            │
│  │              Process Manager                    │            │
│  │              Memory Manager                     │            │
│  │                Scheduler                        │            │
│  └────────────────────────┬───────────────────────┘            │
│                           │                                     │
│  ┌────────────────────────┴───────────────────────┐            │
│  │                   Drivers                       │            │
│  │    (VGA, Keyboard, Timer, ATA, Serial)         │            │
│  └────────────────────────┬───────────────────────┘            │
│                           │                                     │
│  ┌────────────────────────┴───────────────────────┐            │
│  │              Hardware Abstraction              │            │
│  │         (GDT, IDT, Paging, Interrupts)        │            │
│  └────────────────────────────────────────────────┘            │
└─────────────────────────────────────────────────────────────────┘
```

### Organisation du Code Source

```
unix-os/
├── boot/               Bootloader assembleur
├── kernel/             Code kernel
│   ├── arch/x86/       Code spécifique x86 (GDT, IDT, paging)
│   ├── core/           Gestion processus, scheduler, signaux
│   ├── drivers/        Pilotes matériel
│   ├── fs/             Système de fichiers virtuel
│   ├── init/           Initialisation kernel
│   ├── ipc/            Communication inter-processus (pipes)
│   ├── irq/            Gestion interruptions
│   ├── lib/            Bibliothèque kernel interne
│   ├── mm/             Gestion mémoire et paging
│   ├── security/       Sécurité et capabilities
│   └── syscalls/       Implémentation syscalls
├── lib/                Bibliothèques userspace
│   ├── libc/           Implémentation libc
│   └── crt/            C runtime (crt0)
├── userspace/          Programmes utilisateur
│   ├── bin/            Sources des commandes
│   ├── include/        Headers userspace
│   └── ldso/           Runtime linker (ld.so)
├── include/            Headers partagés
│   ├── kernel/         Headers kernel
│   └── libc/           Headers libc
├── uapi/               Interface Kernel-Userspace stable
└── initramfs/          Archive CPIO pour boot
```

---

## Choix Architecturaux

### Kernel Monolithique

UnixOS utilise une architecture **monolithique** plutôt que microkernel. Ce choix a été fait pour :

- **Simplicité** : Un seul espace d'adressage pour le kernel simplifie le développement
- **Performance** : Pas de surcoût IPC entre composants kernel

Le kernel reste néanmoins modulaire dans son organisation interne, avec des sous-systèmes séparés.

### Séparation Kernel/Userspace

La frontière entre kernel et userspace est stricte :

- **Ring 0** : Code kernel, accès complet au matériel
- **Ring 3** : Code utilisateur, accès via syscalls uniquement
- **Paging** : Chaque processus a son propre espace d'adressage
- **uaccess** : Toute copie mémoire kernel↔user passe par `copy_to_user`/`copy_from_user`

### Interface UAPI

Inspiré de Linux, le dossier `uapi/` contient l'interface stable entre kernel et userspace :

- **syscalls.def** : Définition des syscalls via X-macro, partagée kernel et libc
- **types.h** : Types fondamentaux (`__kernel_pid_t`, etc.)
- **errno.h** : Codes d'erreur POSIX
- **abi_version.h** : Versioning de l'ABI pour compatibilité future

Cette approche garantit qu'un binaire compilé reste compatible même si le kernel évolue.

### Dynamic Linking en Userspace

Contrairement à certains OS éducatifs qui font le linking dans le kernel, UnixOS place le dynamic linker (`ld.so`) entièrement en userspace. Cela :

- Réduit la taille et complexité du kernel
- Permet l'évolution du linker sans modifier le kernel

Le kernel se contente de charger `ld.so` via `PT_INTERP` si le binaire est dynamique.

### Virtual Filesystem (VFS)

Le VFS abstrait les différents systèmes de fichiers derrière une interface unique :

- **Inodes** : Représentation unifiée des fichiers
- **Dentry cache** : Cache des entrées répertoire
- **File operations** : read, write, open, close...

Un filesystem RAM (ramfs) et FAT12 (lecture seule) sont implémentés. L'architecture permet d'ajouter ext2, etc.

### Architecture Boot (Void/Linux style)

```
kernel
 └── monte initramfs (CPIO)
     └── execve("/sbin/init")
         └── ld.so (userspace)
             └── libc.so
                 └── binaires userspace
```

**Principe fondamental**: Le kernel ne connaît QUE l'initramfs, jamais les binaires eux-mêmes.

### Hot Reload Support

Le système supporte le hot reload de bibliothèques via:
- **ABI versionnée**: `include/hotreload.h` définit les structures
- **dlopen/dlsym**: ld.so userspace gère le chargement dynamique
- **Swap atomique**: Remplacement thread-safe des APIs

---

## Ce Qui a Été Réalisé

### Kernel Core

- **Boot** : Bootloader assembleur charge le kernel depuis disquette
- **GDT/IDT** : Segmentation et table d'interruptions x86
- **Paging** : Pagination avec isolation processus (COW désactivé pour stabilité)
- **Scheduler** : Ordonnanceur MLFQ 4 niveaux (Multi-Level Feedback Queue)
- **Processus** : fork, exec, exit, waitpid fonctionnels
- **Threads** : clone() avec CLONE_VM, CLONE_FILES, TLS support
- **Signaux** : Gestion signaux POSIX complète avec job control (SIGTSTP/SIGCONT)
- **IPC** : Pipes, System V semaphores, shared memory, futex

### Syscalls

**78 syscalls** implémentés (vérifiés dans syscall_table.c), incluant :

- Fichiers : open, read, write, close, stat, mkdir, unlink, flock...
- Processus : fork, exec, exit, waitpid, getpid, kill, clone, gettid...
- Mémoire : brk, mmap, munmap, mprotect
- Signaux : signal, sigaction, sigprocmask, sigreturn
- Temps : time, nanosleep, alarm
- Terminal : ioctl, tcgetattr, tcsetattr
- IPC : pipe, semget, semop, shmget, shmat, futex
- Sockets : socket, bind, listen, accept, connect, send, recv
- UID/GID : getuid, setuid, geteuid, seteuid...

### Networking

- **Sockets AF_UNIX** : Communication locale inter-processus
- **Operations** : socket, bind, listen, accept, connect, send, recv, shutdown

### Filesystems

- **ramfs** : Filesystem en RAM pour initramfs
- **FAT12** : Lecture seule, support disquettes

### Userspace

- **libc** : Implémentation minimale mais fonctionnelle (stdio, stdlib, string, unistd)
- **ld.so** : Runtime linker avec support DT_NEEDED et lazy binding
- **Commandes** : init, sh, ls, cat, mkdir, rm, pwd, echo, touch, kill, ps

### Drivers

- **VGA** : Mode texte et mode graphique 320x200
- **Keyboard** : Driver PS/2 avec buffer
- **Timer** : PIT pour scheduling
- **ATA** : Accès disque (lecture/écriture secteurs)
- **Serial** : Sortie debug via COM1
- **Floppy** : Lecture disquettes FAT12

---

## Ce Qui a Été Abandonné

### Tests Kernel en Production

Des fonctions de test (`test_interrupts`, `run_doom_tests`) étaient compilées dans le kernel. Elles sont maintenant sous `#ifdef DEBUG` et **exclues** du build de production.

---

## Roadmap

### Court Terme (Prochaines Semaines)

1. **Finaliser initramfs**
   - Activer le chargement depuis archive CPIO au lieu de binaires embarqués
   - Réduire la taille du kernel à environ 50KB

2. **Compléter ld.so**
   - Implémenter le chargement récursif complet des dépendances
   - Ajouter le support du symbol versioning

3. **Améliorer le shell**
   - Ajouter les pipes shell (`ls | grep foo`)
   - Ajouter les redirections (`>`, `<`, `>>`)
   - Gérer les jobs en arrière-plan (`&`)

4. **Filesystem persistant**
   - Implémenter ext2 en lecture
   - Permettre le boot depuis un vrai disque

### Moyen Terme (Prochains Mois)

1. **Networking**
   - Driver carte réseau (e1000 ou RTL8139)
   - Stack TCP/IP minimal
   - Sockets BSD

2. **Graphiques**
   - Mode VESA haute résolution
   - Framebuffer userspace
   - Window manager simple

3. **Multithreading**
   - Threads POSIX (pthread)
   - Synchronisation (mutex, condvar)

4. **Mémoire**
   - Swap sur disque
   - Shared memory (shmem)
   - Copy-on-write amélioré

### Long Terme (Vision)

1. **Self-hosting**
   - Compiler UnixOS sur lui-même
   - Porter GCC ou un compilateur C simple

2. **SMP**
   - Support multiprocesseur
   - Scheduler multi-CPU

3. **Sécurité**
   - Capabilities complètes
   - Namespaces et cgroups simplifiés
   - ASLR

---

## Organisation de l'execution

### Initialisation Kernel (Excellent)

L'ordre de `kmain()` respecte les bonnes pratiques:
- Sécurité AVANT userspace
- Mémoire AVANT scheduler
- VFS AVANT exec
- Syscalls AVANT fork userspace

### Mémoire (Linux-like)

- **Layout précis**:
  - 0x00000000-0x00100000 (0-1MB):    BIOS/VGA/Boot
  - 0x00100000-0x00200000 (1-2MB):    Kernel code/data
  - 0x00200000-0x00A00000 (2-10MB):   Kernel heap (8MB configurable)
  - 0x00A00000-0x80000000 (10MB-2GB): User space
  - 0x80000000-0xC0000000 (2GB-3GB):  User stack
  - 0xC0000000-0xFFFFFFFF (3GB-4GB):  Kernel virtual
- **Identity mapping**: 0x00000000-0x00C00000 (0-12MB, 3 page tables)
- **Paging**: Bitmap + refcount, 4KB pages
- **COW**: Supporté (désactivé pour stabilité - eager copying)
- **mmap/mprotect**: Rollback en cas d'erreur, MAP_FIXED, MAP_PRIVATE
- **Zone mmap**: Séparée du heap (base 0x40000000)
- **Heap**: First-fit avec splitting/coalescing, spinlock protected
- **SLUB**: O(1) allocator, slab coloring, 64 objects/slab

### Processus & Scheduler

- **process_t**: Job control (pgid, sid, tty), signaux, fd table, mémoire isolée
- **MLFQ 4 niveaux**: Quantums [5,10,20,40] ticks
  - Nouveaux processus: queue 0 (haute priorité)
  - CPU-bound: dégradation sur expiration quantum
  - I/O-bound: promotion sur blocage (priority boost)
  - Round-robin dans chaque queue
- **Fork**: Copie eager de toutes les pages user, héritage signaux/fd/umask
- **Exec**: Streaming ELF loader, support PIE/ASLR, PT_INTERP

### UAPI 

- Dossier `uapi/` séparé, versionné, partagé kernel/libc
- ABI version syscall 254 pour compatibilité future
- `copy_from_user`/`copy_to_user` systématique

### Dynamic Linking (Différenciateur)

Le ld.so userspace implémente:
- DT_NEEDED, PLT/GOT
- Relocations i386 (R_386_RELATIVE, R_386_GLOB_DAT, R_386_JMP_SLOT)
- Lazy binding
- Constructeurs/destructeurs (.init/.fini)

### ELF Loader 

- **ET_EXEC**: Binaires à adresse fixe (0x08048000)
- **ET_DYN**: PIE avec ASLR (range 0x10000000-0x40000000)
- Relocations R_386_RELATIVE appliquées au chargement
- **ASLR Hardware**: Entropy via RDTSC (CPU timestamp counter)
- **Streaming loader**: Chargement par chunks de 512 bytes (pas de gros buffer)
- **PT_INTERP**: Support complet de ld.so avec passage de elf_phdr/elf_entry

### Initramfs 

**Format**: CPIO newc (magic 070701)

Trois modes de chargement (par ordre de priorité):
1. **Multiboot2**: Module passé par GRUB2 (`module2 /boot/initramfs.cpio`)
2. **Multiboot1**: Module QEMU `-kernel` boot
3. **Embarqué**: Compilé dans le kernel (`-DHAVE_INITRAMFS`)
4. **Legacy**: Binaires embarqués via headers .h

**Features**: Création automatique des répertoires parents, normalisation des chemins

### Boot 

Deux méthodes de boot supportées:

**1. GRUB ISO (recommandé)**
```bash
make iso                    # Crée build/unixos.iso
qemu-system-i386 -cdrom build/unixos.iso -m 64M
```

**2. Legacy Floppy**
```bash
make                        # Crée build/bin/os.img  
qemu-system-i386 -fda build/bin/os.img -m 64M
```

Architecture:
- Kernel à 0x100000 (1MB)
- Entry point unifié à 0x100100
- Multiboot1 + Multiboot2 headers
- Initramfs via module GRUB

### Symbol Versioning (ld.so)

Support des structures ELF pour le versioning:
- `DT_VERSYM`, `DT_VERDEF`, `DT_VERNEED`
- Permet compatibilité binaire (`foo@UNIXOS_1.0`)

---

*Document mis à jour le 30 janvier 2026*
