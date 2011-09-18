#include <iostream>

#include "../include/libxafp.h"
#include "../src/AFPClient.h"
#include "../src/AFPProto.h"

void cp(const char* path, const char* dest, const char* username, const char* pass)
{
  xafp_client_handle ctx = xafp_create_context("kennel", username, pass);
  
  // Authenticate and open the desired volume
  // TODO: Strip out volume name
  if (!xafp_mount(ctx, "Media", xafp_mount_flag_none))
  {
    xafp_file_handle fileHandle = xafp_open_file(ctx, path, xafp_open_flag_read);
    if (fileHandle)
    {
      uint64_t bytesWritten = 0;
      uint64_t lastUpdate = 0;
      FILE* fp = fopen("/Users/chris/test.mp4", "w");
      uint64_t bufLen = 262144;
      void* pBuf = malloc(bufLen);
      time_t lastTime = time(NULL);
      time_t startTime = lastTime;
      while (xafp_read_file(ctx, fileHandle, bytesWritten, pBuf, bufLen) > 0)
      {
        fwrite(pBuf, 1, bufLen, fp);
        bytesWritten += bufLen; 
        uint64_t deltaBytes = bytesWritten - lastUpdate;
        if (deltaBytes > 10485760) // 10MB
        {
          time_t deltaTime = time(NULL) - lastTime;
          printf("%lu: Wrote %llu bytes in %lu seconds (%0.2f KB/sec)\n", clock(), deltaBytes, deltaTime, (double)(deltaBytes/1024) / (double)deltaTime);
          lastTime = time(NULL);
          lastUpdate = bytesWritten;
        }
      }
      time_t deltaTime = time(NULL) - startTime;
      printf("%lu: Wrote %llu bytes in %lu seconds (%0.2f KB/sec)\n", clock(), bytesWritten, deltaTime, (double)(bytesWritten/1024) / (double)deltaTime);
      free(pBuf);
      fclose(fp);
      xafp_close_file(ctx, fileHandle);
    }
    xafp_unmount(ctx, "Media");
  }
  
  // Clean-up the session
  xafp_destroy_context(ctx);
}

void ls(const char* path, const char* username, const char* pass)
{
  xafp_client_handle ctx = xafp_create_context("kennel", username, pass);
  
  // Authenticate and open the desired volume
  // TODO: Strip out volume name
  if (!xafp_mount(ctx, "Media", xafp_mount_flag_none))
  {
    xafp_node_iterator iter = xafp_get_dir_iter(ctx, path);
    xafp_node_handle node = xafp_next(iter);
    while(node)
    {
      CNodeParams* pParams = (CNodeParams*)node;
      if (!(pParams->GetAttributes() & kFPInvisibleBit))
      {
        uint32_t perms = pParams->GetPermissions();
        time_t modTime = pParams->GetModDate();
        char timeString[32];
        strncpy(timeString, ctime(&modTime), 32);
        timeString[strlen(timeString) - 1] = '\0';
        printf ("%s%s%s%s%s%s%s%s%s%s %s %d %d %d %s %s\n", 
                pParams->IsDirectory() ? "d" : "-",
                (perms & kRPOwner) ? "r" : "-",
                (perms & kWROwner) ? "w" : "-",
                (perms & kSPGroup) ? "x" : "-",
                (perms & kRPGroup) ? "r" : "-",
                (perms & kWRGroup) ? "w" : "-",
                (perms & kSPGroup) ? "x" : "-",
                (perms & kRPOther) ? "r" : "-",
                (perms & kWROther) ? "w" : "-",
                (perms & kSPOther) ? "x" : "-",
                "-",
                pParams->GetUserId(),
                pParams->GetGroupId(),
                pParams->IsDirectory() ? 0 : 0,
                timeString,
                pParams->GetName()
                );
      }
      node = xafp_next(iter);
    }
    xafp_free_iter(iter);
    xafp_unmount(ctx, "Media");
  }
  
  // Clean-up the session
  xafp_destroy_context(ctx);
}

int main (int argc, char * const argv[]) 
{
  XafpSetLogLevel(XAFP_LOG_LEVEL_INFO);
  char secret[32];
  FILE* fsecret = fopen(argv[1], "r");
  fread(secret, 1, sizeof(secret), fsecret);

  ls("/Media/video/Movies/BRRip","chris",secret);
  
  cp("/Media/video/Movies/BRRip/Aeon Flux.mp4","/Users/chris/test.mp4","chris",secret);
  
  return 0;
}
