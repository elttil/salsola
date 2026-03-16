CC="x86_64-salsola-gcc"
AR="x86_64-salsola-ar"
AS="x86_64-salsola-as"
#CFLAGS = -ggdb -nostdlib -ffreestanding -fsanitize=vla-bound,shift-exponent,pointer-overflow,shift,signed-integer-overflow,bounds -ffreestanding -Wall -Wextra -pedantic -Werror -I./include/ -static -I../../include/
CFLAGS = -ggdb -nostdlib -ffreestanding -O2 -ffreestanding -Wall -Wextra -pedantic -Werror -I./include/ -static -I../../include/
STDIO_OBJ=stdio/dprintf.o stdio/fgets.o stdio/fseek.o stdio/printf.o stdio/sprintf.o stdio/fclose.o stdio/fileno.o stdio/fsetpos.o stdio/putc.o stdio/stdin.o stdio/fdopen.o stdio/fopen.o stdio/ftell.o stdio/putchar.o stdio/tmpfile.o stdio/feof.o stdio/fprintf.o stdio/fwrite.o stdio/puts.o stdio/tmpnam.o stdio/ferror.o stdio/fputc.o stdio/getchar.o stdio/remove.o stdio/ungetc.o stdio/fflush.o stdio/fputs.o stdio/getdelim.o stdio/rename.o stdio/vdprintf.o stdio/fread.o stdio/getline.o stdio/setvbuf.o stdio/vfprintf.o stdio/fgetpos.o stdio/fscanf.o stdio/open_memstream.o stdio/snprintf.o stdio/vprintf.o stdio/perror.o stdio/fgetc.o
STDLIB_OBJ=stdlib/setenv.o stdlib/getenv.o stdlib/exit.o stdlib/atoi.o stdlib/strtol.o stdlib/strtoll.o stdlib/strtoul.o stdlib/atof.o stdlib/strtod.o stdlib/system.o stdlib/rand.o stdlib/srand.o
STRING_OBJ=string/memchr.o string/memcmp.o string/memcpy.o string/memmove.o string/memset.o string/sscanf.o string/strcasecmp.o string/strcat.o string/strchr.o string/strcmp.o string/strcpy.o string/strcspn.o string/strdup.o string/strlcpy.o string/strlen.o string/strncasecmp.o string/strncmp.o string/strncpy.o string/strndup.o string/strnlen.o string/strpbrk.o string/strrchr.o string/strspn.o string/strstr.o string/strtok.o string/strerror.o string/memcpy_v.o string/memset_v.o
ERRNO_OBJ=errno/error_to_errno.o errno/errno.o
FCNTL_OBJ=fcntl/fcntl.o fcntl/open.o
DIRENT_OBJ=dirent/opendir.o dirent/readdir.o dirent/scandir.o dirent/closedir.o
ASSERT_OBJ=assert/assert.o
UNISTD_OBJ=unistd/write.o unistd/close.o unistd/lseek.o unistd/sbrk.o unistd/read.o unistd/fork.o unistd/execvp.o unistd/sa_exec.o unistd/dup2.o unistd/execv.o unistd/pipe.o unistd/sa_pipe.o unistd/getpid.o unistd/sa_chdir.o unistd/chdir.o unistd/getcwd.o unistd/getopt.o unistd/sa_exec_env.o unistd/ftruncate.o unistd/sa_bindfd.o unistd/msleep.o unistd/pwrite.o unistd/pread.o unistd/sa_pwrite.o unistd/sa_pread.o unistd/recvfd.o
MALLOC_OBJ=malloc/malloc.o
MATH_OBJ=math/fmin.o math/ldexp.o math/sqrt.o math/fabs.o math/cos.o math/ceil.o math/acos.o math/pow.o math/fmod.o math/floor.o math/__math_oflow.o math/exp_data.o math/pow_data.o math/__cos.o math/__sin.o math/__rem_pio2.o math/__rem_pio2_large.o math/scalbn.o math/__math_uflow.o math/__math_xflow.o math/__math_invalid.o
TB_OBJ=tb/sv.o tb/sb.o tb/sha1.o
SYS_OBJ=sys/random/randomfill.o sys/random/sarandom.o sys/mman/mmap.o sys/stat/stat.o sys/stat/fstat.o sys/kpoll.o sys/waitfd.o sys/wait.o sys/stat/mkdir.o

CTYPE_OBJ=ctype/isalnum.o ctype/isblank.o ctype/isgraph.o ctype/ispunct.o ctype/isxdigit.o ctype/isalpha.o ctype/iscntrl.o ctype/islower.o ctype/isspace.o ctype/tolower.o ctype/isascii.o  ctype/isdigit.o  ctype/isprint.o  ctype/isupper.o  ctype/toupper.o

OBJ=crti.o crt0.o crt1.o crtn.o systemcall.o setup.o ubsan.o $(STDIO_OBJ) $(STRING_OBJ) $(UNISTD_OBJ) $(ERRNO_OBJ) $(FCNTL_OBJ) $(ASSERT_OBJ) $(MALLOC_OBJ) $(SYS_OBJ) $(MATH_OBJ) $(TB_OBJ) $(CTYPE_OBJ) $(STDLIB_OBJ) $(DIRENT_OBJ)
all: libc.a crt0.o

%.o: %.c
#	clang-format -i $<
	$(CC) $(CFLAGS) -I. -o $@ -c $<

%.o: %.s
	$(AS) $< -o $@

libc.a: $(OBJ)
	$(AR) rcs libc.a $(OBJ)

install:
	mkdir -p ../../sysroot/usr/
	mkdir -p ../../sysroot/lib/
	cp -r ../../include ../../sysroot/usr/
	cp -r include ../../sysroot/usr/
	cp crt*.o ../../sysroot/lib/
	cp libc.a ../../sysroot/lib/

clean:
	rm libc.a $(OBJ)
