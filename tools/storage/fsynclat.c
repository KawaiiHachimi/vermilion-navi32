// SPDX-License-Identifier: MIT
// fsynclat: overwrite 4 KiB and sync, N times each with fsync(), F_BARRIERFSYNC and F_FULLFSYNC; print latency
// percentiles. Only fsync() is cheap on macOS because it doesn't flush the drive's write cache.
// Build: clang -O2 -o fsynclat fsynclat.c   Usage: ./fsynclat <scratch file on the volume to test> <count>
// Writes 1 MiB plus count x 3 x 4 KiB, then deletes the file.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
static int cmp(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
int main(int argc,char**argv){
  int n=atoi(argv[2]); int fd=open(argv[1],O_RDWR|O_CREAT,0600); char buf[4096]; memset(buf,0xa5,sizeof buf);
  for(int i=0;i<256;i++) pwrite(fd,buf,4096,i*4096); fcntl(fd,F_FULLFSYNC);
  const char*names[]={"fsync","F_BARRIERFSYNC","F_FULLFSYNC"};
  double *l=malloc(sizeof(double)*n);
  for(int m=0;m<3;m++){
    for(int i=0;i<n;i++){ buf[0]=i; double t=now(); pwrite(fd,buf,4096,(i%256)*4096);
      if(m==0) fsync(fd); else if(m==1) fcntl(fd,F_BARRIERFSYNC); else fcntl(fd,F_FULLFSYNC);
      l[i]=now()-t; }
    qsort(l,n,sizeof(double),cmp);
    printf("%-15s n=%d  p50 %8.1f us  p99 %8.1f us  max %8.1f us  => %6.0f syncs/s at p50\n",names[m],n,l[n/2],l[n*99/100],l[n-1],1e6/l[n/2]);
  }
  close(fd); unlink(argv[1]); return 0;
}
