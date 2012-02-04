/*
 *      Copyright (C) 2011 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "Common.h"

#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/md2.h>

#include "AFPClient.h"
#include "AFPProto.h"

// TODO: List
// - Improve string/path handling. There is a lot of pointless copying going on...

// AFP Session Handling
/////////////////////////////////////////////////////////////////////////////////
CAFPSession::CAFPSession() :
  m_LoggedIn(false),
  m_pAuthInfo(NULL)
{
  
}

CAFPSession::~CAFPSession()
{
  Logout();
  Close();
}

bool CAFPSession::Login(const char* pUsername, const char* pPassword)
{
  if (m_pAuthInfo)
    delete m_pAuthInfo;
  
  m_pAuthInfo = new CAFPCleartextAuthInfo(pUsername, pPassword);
  m_LoggedIn = LoginClearText((CAFPCleartextAuthInfo*)m_pAuthInfo);
  
  return m_LoggedIn;
}   

bool CAFPSession::LoginClearText(CAFPCleartextAuthInfo* pAuthInfo)
{
  CDSIBuffer reqBuf(strlen(kAFPVersion_3_1) + strlen(kClearTextUAMStr) + strlen(pAuthInfo->GetPassword()) + 8 + 5);
  reqBuf.Write((uint8_t)FPLogin); // Command
  reqBuf.Write((char*)kAFPVersion_3_1); // AFP Version
  reqBuf.Write((char*)kClearTextUAMStr); // UAM
  reqBuf.Write((char*)pAuthInfo->GetUserName());
  reqBuf.Write((uint8_t)0); // Pad
  char pwd[10];
  memset(pwd,0,8);
  int len = strlen(pAuthInfo->GetPassword());
  memcpy(pwd, pAuthInfo->GetPassword(), len > 8 ? 8 : len);
  reqBuf.Write((void*)pwd, 8); // Password parameter is 8 bytes long

  uint32_t err = SendCommand(reqBuf);
  return (err == kNoError);
}

void CAFPSession::Logout()
{
  if (m_pAuthInfo)
  {
    delete m_pAuthInfo;
    m_pAuthInfo = NULL;
  }
  
  if (!IsLoggedIn())
    return;
  
  m_LoggedIn = false;
  
  CDSIBuffer reqBuf(2);
  reqBuf.Write((uint8_t)FPLogout); // Command
  reqBuf.Write((uint8_t)0); // Pad
  SendCommand(reqBuf);
}

int CAFPSession::OpenVolume(const char* pVolName)
{
  if (!IsLoggedIn())
    return kFPUserNotAuth;
 
  CDSIBuffer reqBuf(4 + 1 + strlen(pVolName));
  reqBuf.Write((uint8_t)FPOpenVol); // Command
  reqBuf.Write((uint8_t)0); // Pad
  uint16_t bitmap = kFPVolIDBit;
  reqBuf.Write((uint16_t)bitmap); // Bitmap
  reqBuf.Write((char*)pVolName);
  
  CDSIBuffer replyBuf;
  uint32_t err = SendCommand(reqBuf, &replyBuf);
  if (err == kNoError)
  {
    bitmap = replyBuf.Read16();
    if (bitmap & kFPVolIDBit)
    {
      uint16_t volID = replyBuf.Read16();
      return volID;      
    }
  }
  return kNoError;  
}

void CAFPSession::CloseVolume(int volId)
{
  CDSIBuffer reqBuf(4);
  reqBuf.Write((uint8_t)FPCloseVol); // Command
  reqBuf.Write((uint8_t)0); // Pad
  reqBuf.Write((uint16_t)volId); // Bitmap
  
  SendCommand(reqBuf);
}

int CAFPSession::GetDirectoryId(int volumeId, const char* pPathSpec, int refId /*=2*/)
{  
  int len = strlen(pPathSpec);
  CDSIBuffer reqBuf(13 + 4 + 1 + len + 1);
  reqBuf.Write((uint8_t)FPGetFileDirParms); // Command
  reqBuf.Write((uint8_t)0); // Pad
  reqBuf.Write((uint16_t)volumeId);
  reqBuf.Write((uint32_t)refId); // Point from which to start the search
  reqBuf.Write((uint16_t)NULL); // File Bitmap
  uint16_t bitmap = kFPNodeIDBit;
  reqBuf.Write((uint16_t)bitmap); // Directory Bitmap
  reqBuf.WritePathSpec(pPathSpec, len);
  
  CDSIBuffer replyBuf;
  uint32_t err = SendCommand(reqBuf, &replyBuf);
  if (err == kNoError)
  {
    replyBuf.Skip(4); // Skip File Bitmap and Directory Bitmap (we already specified)
    bool isDir = replyBuf.Read8() & 0x80; // Directory bit + pad(7bits)
    replyBuf.Skip(1); // Pad
    if (isDir)
    {
      CDirParams params(bitmap, (uint8_t*)replyBuf.GetData() + 6, replyBuf.GetDataLen() - 6);
      return params.GetInfo()->nodeId;
    }
  }  
  return 0; // TODO: Need error codes for callers...  
}

int CAFPSession::GetNodeList(CAFPNodeList** ppList, int volumeId, const char* pPathSpec, int refId /*=2*/)
{
  if (!IsLoggedIn())
    return kFPUserNotAuth;

  if (!ppList)
    return kParamError;
  
  int len = strlen(pPathSpec);
  
  // TODO: handle instances that require multiple calls (too many entries for one block)
  CDSIBuffer reqBuf(29); // TODO: This method of sizing the request block is WAAAY too error-prone...
  // TODO: FPEnumerateExt2 requires AFPv3.1 - need to add support for FPEnumerateExt for AFPv3.0
  reqBuf.Write((uint8_t)FPEnumerateExt2); // Command
  reqBuf.Write((uint8_t)0); // Pad
  reqBuf.Write((uint16_t)volumeId);
  reqBuf.Write((uint32_t)refId);
  uint16_t fileBitmap = kFPAttributeBit | kFPUTF8NameBit | kFPUnixPrivsBit | kFPModDateBit | kFPExtDataForkLenBit;
  reqBuf.Write((uint16_t)fileBitmap); // File Bitmap
  uint16_t dirBitmap = kFPAttributeBit | kFPUTF8NameBit | kFPUnixPrivsBit | kFPModDateBit | kFPOffspringCountBit;
  reqBuf.Write((uint16_t)dirBitmap); // Directory Bitmap
  reqBuf.Write((uint16_t)20); // Max Results
  reqBuf.Write((uint32_t)1); // Start Index
  reqBuf.Write((uint32_t)5280); // Max Reply Size
  reqBuf.WritePathSpec(pPathSpec, len);

  CDSIBuffer* pReplyBuf = new CDSIBuffer();
  uint32_t err = SendCommand(reqBuf, pReplyBuf);
  if (err == kNoError)
  {
    // TODO: Validate returned bitmap vs provided one
    pReplyBuf->Read16(); // Skip File Bitmap (we already specified)
    pReplyBuf->Read16(); // Skip Directory Bitmap (we already specified)
    int count = pReplyBuf->Read16(); // Results Returned
    *ppList = new CAFPNodeList(pReplyBuf, dirBitmap, fileBitmap, count);
    return kNoError;
  }
  else
  {
    delete pReplyBuf;
    return err;
  }
}

int CAFPSession::Stat(int volumeId, const char* pPathSpec, CNodeParams** pParams, int refId /*=2*/)
{
  if (!IsLoggedIn())
    return kFPUserNotAuth;

  int len = strlen(pPathSpec);
  CDSIBuffer reqBuf(13 + 4 + 1 + len);
  reqBuf.Write((uint8_t)FPGetFileDirParms); // Command
  reqBuf.Write((uint8_t)0); // Pad
  reqBuf.Write((uint16_t)volumeId);
  reqBuf.Write((uint32_t)refId);
  
  uint16_t fileBitmap = NULL;
  uint16_t dirBitmap = NULL;
  if (pParams)
  {
    fileBitmap = kFPNodeIDBit | kFPAttributeBit | kFPUTF8NameBit | kFPUnixPrivsBit | kFPModDateBit | kFPExtDataForkLenBit;;
    dirBitmap = kFPNodeIDBit | kFPAttributeBit | kFPUTF8NameBit | kFPUnixPrivsBit | kFPModDateBit | kFPOffspringCountBit;
  }
  reqBuf.Write((uint16_t)fileBitmap);
  reqBuf.Write((uint16_t)dirBitmap);
  reqBuf.WritePathSpec(pPathSpec, len);
    
  CDSIBuffer replyBuf;
  uint32_t err = SendCommand(reqBuf, &replyBuf);
  if (err == kNoError)
  {
    if (pParams)
    {
      // TODO: Validate returned bitmap vs provided one
      replyBuf.Read16(); // Skip File Bitmap (we already specified)
      replyBuf.Read16(); // Skip Directory Bitmap (we already specified)
      bool isDir = replyBuf.Read8() & 0x80; // Directory bit + pad(7bits)
      replyBuf.Read8(); // Pad
      if (isDir)
      {
        *pParams = new CDirParams();      
        ((CDirParams*)(*pParams))->Parse(dirBitmap, (uint8_t*)replyBuf.GetData() + 6, replyBuf.GetDataLen() - 6);
      }
      else
      {
        *pParams = new CFileParams();
        ((CFileParams*)(*pParams))->Parse(fileBitmap, (uint8_t*)replyBuf.GetData() + 6, replyBuf.GetDataLen() - 6);
      }
    }
    return 0; // TODO: Need error codes for callers...  
  }
  return -1;
}

int CAFPSession::OpenFile(int volumeId, const char* pPathSpec, uint16_t mode /*=kFPForkRead*/, int refId /*=2*/)
{
  if (!IsLoggedIn())
    return -1;

  uint16_t len = strlen(pPathSpec);
  CDSIBuffer reqBuf(17 + len + 1);
  
  reqBuf.Write((uint8_t)FPOpenFork); // Command
  reqBuf.Write((uint8_t)0); // Flag (File Fork)
  reqBuf.Write((uint16_t)volumeId);
  reqBuf.Write((uint32_t)refId);
  reqBuf.Write((uint16_t)0); // No need to return any parameters
  reqBuf.Write((uint16_t)mode);
  reqBuf.WritePathSpec(pPathSpec, len);
  
  CDSIBuffer replyBuf;
  uint32_t err = SendCommand(reqBuf, &replyBuf);
  if (err == kNoError)
  {
    replyBuf.Skip(2); // Skip bitmap. We didn't request any params
    uint16_t forkId = replyBuf.Read16();
    return forkId;
  } 
  return 0;
}

int CAFPSession::Create(int volumeId, const char* pPathSpec, bool dir /*=false*/, int refId /*=2*/)
{
  if (!IsLoggedIn())
    return -1;
  
  uint16_t len = strlen(pPathSpec);
  CDSIBuffer reqBuf(15 + len + 1);
  
  reqBuf.Write((uint8_t)(dir ? FPCreateDir : FPCreateFile)); // Command
  reqBuf.Write((uint8_t)(dir ? 0 : 1)); // Pad/Flag
  reqBuf.Write((uint16_t)volumeId);
  reqBuf.Write((uint32_t)refId);
  reqBuf.WritePathSpec(pPathSpec, len);
  
  CDSIBuffer replyBuf;
  int32_t err = SendCommand(reqBuf, dir ? &replyBuf : NULL);
  if (err == kNoError)
  {
    if (dir)
      return replyBuf.Read32();
    else
      return 0;
  }
  return err;
}

int CAFPSession::Delete(int volumeId, const char* pPathSpec, int refId /*=2*/)
{
  if (!IsLoggedIn())
    return -1;
  
  uint16_t len = strlen(pPathSpec);
  CDSIBuffer reqBuf(15 + len + 1);
  
  reqBuf.Write((uint8_t)FPDelete); // Command
  reqBuf.Write((uint8_t)0); // Pad
  reqBuf.Write((uint16_t)volumeId);
  reqBuf.Write((uint32_t)refId);
  reqBuf.WritePathSpec(pPathSpec, len);
  
  int32_t err = SendCommand(reqBuf);
  return err;
}

int CAFPSession::Move(int volumeId, const char* pPathSpec, const char* pNewPathSpec)
{
  if (!IsLoggedIn())
    return -1;
  
  std::string src = pPathSpec;
  std::string dest = pNewPathSpec;

  // Make sure the source exists, and get some information about it
  CNodeParams* pParams = NULL;
  if (Stat(volumeId, pPathSpec, &pParams) < 0)
    return -2;
  
  int newPos = dest.rfind('/');
    
  // TODO: Add more robust error checking/string validation...
  CDSIBuffer reqBuf(22 + src.length() + dest.length() + 2);
  reqBuf.Write((uint8_t)FPMoveAndRename); // Command
  reqBuf.Write((uint8_t)0); // Pad
  reqBuf.Write((uint16_t)volumeId);
  reqBuf.Write((uint32_t)2); // Source Directory Id (root)
  reqBuf.Write((uint32_t)2); // Dest Directory Id (root)
  reqBuf.WritePathSpec(pPathSpec, src.length());
  reqBuf.WritePathSpec(dest.substr(0, newPos).c_str(), newPos); // Trim filename from path
  reqBuf.WritePathSpec(dest.substr(newPos + 1).c_str(), dest.length() - newPos - 1); // Just the destination filename
  
  CDSIBuffer replyBuf;
  uint32_t err = SendCommand(reqBuf, &replyBuf);
  if (err == kNoError)
    return 0;
    
  return -3;
}

void CAFPSession::CloseFile(int forkId)
{
  if (!IsLoggedIn())
    return;
  
  CDSIBuffer reqBuf(4);
  reqBuf.Write((uint8_t)FPCloseFork); // Command
  reqBuf.Write((uint8_t)0); // Flag (File Fork)
  reqBuf.Write((uint16_t)forkId);

  SendCommand(reqBuf, NULL);
}

int CAFPSession::ReadFile(int forkId, uint64_t offset, void* pBuf, uint32_t len)
{
  if (!IsLoggedIn())
    return -1;
  
  CDSIBuffer reqBuf(20);
  reqBuf.Write((uint8_t)FPReadExt); // Command
  reqBuf.Write((uint8_t)0); // Pad
  reqBuf.Write((uint16_t)forkId);
  reqBuf.Write((uint64_t)offset); // Offset
  reqBuf.Write((uint64_t)len); // Bytes to Read

  CDSIBuffer replyBuf;
  uint32_t err = SendCommand(reqBuf, &replyBuf);
  if (err == kNoError)
  {
    // TODO: This is a useless copy...come up with a better way
    memcpy(pBuf, replyBuf.GetData(), len);
    return len;
  }
  
  return 0;
}

int CAFPSession::WriteFile(int forkId, uint64_t offset, void* pBuf, uint32_t len)
{
  // TODO: Handle requests in chunks the size of the server quantum (provided in OpenSession)
  
  if (!IsLoggedIn())
    return -1;
  
  CDSIBuffer reqBuf(20 + len);
  reqBuf.Write((uint8_t)FPWriteExt); // Command
  reqBuf.Write((uint8_t)0); // Flag (Offset from start of file)
  reqBuf.Write((uint16_t)forkId);
  reqBuf.Write((uint64_t)offset); // Offset
  reqBuf.Write((uint64_t)len); // Bytes to Write
  reqBuf.Write((void*)pBuf, len); // TODO: This is a useless copy...come up with a better way

  // TODO: Lock the range to be written before beginning the write
  CDSIBuffer replyBuf;
  uint32_t err = SendCommand(reqBuf, &replyBuf, 20);
  if (err == kNoError)
    return len;

  return 0;
}

int CAFPSession::FlushFile(int forkId)
{
  if (!IsLoggedIn())
    return -1;
  
  CDSIBuffer reqBuf(4);
  reqBuf.Write((uint8_t)FPFlushFork); // Command
  reqBuf.Write((uint8_t)0); // Pad
  reqBuf.Write((uint16_t)forkId);

  uint32_t err = SendCommand(reqBuf);
  if (err == kNoError)
    return 0;
  return -2;
}


/*
 // http://developer.apple.com/library/mac/#documentation/Networking/Conceptual/AFP/AFPSecurity/AFPSecurity.html
 // DHX2 Initialization Vectors (IV)
 uint8_t C2SIV[] = { 0x4c, 0x57, 0x61, 0x6c, 0x6c, 0x61, 0x63, 0x65};
 uint8_t S2CIV[] = { 0x43, 0x4a, 0x61, 0x6c, 0x62, 0x65, 0x72, 0x74};
 
 bool CAFPSession::LoginDHX2(const char* pUsername, const char* pPassword)
 {
 CDSIPacket pkt(DSICommand, GetNewRequestId(), strlen(kAFPVersion_3_1) + strlen(kDHX2UAMStr) + strlen(pUsername) + 4);
 pkt.Write((uint8_t)FPLogin); // Command
 pkt.Write((char*)kAFPVersion_3_1); // AFP Version
 pkt.Write((char*)kDHX2UAMStr); // UAM
 pkt.Write((char*)pUsername);
 uint32_t err;
 CDSIPacket reply;
 SendReceiveAFPPacket(pkt, reply, *this, err);
 if (err)
 if (err != (uint32_t)kFPAuthContinue)
 return false;
 
 //Reply block will contain:
 // *   Transaction ID (2 bytes, MSB)
 // *   g: primitive mod p sent by the server to the client(4 bytes, MSB)
 // *   length of large values in bytes (2 bytes, MSB)
 // *   p (minimum 64 bytes, indicated by length value, MSB)
 // *   Mb (minimum 64 bytes, indicated by length value, MSB)
 
 // Populate a Diffie Hellman key structure
 DH* pDH = DH_new();
 
 uint16_t id = reply.Read16();
 
 uint8_t g[4];
 reply.Read(g, 4);
 pDH->g = BN_bin2bn((unsigned char*)g, 4, NULL);
 
 uint16_t len = reply.Read16();
 
 uint8_t* p = (uint8_t*)malloc(len);
 reply.Read(p, len);
 pDH->p = BN_bin2bn(p, len, NULL);
 free(p);
 
 uint8_t* Mb = (uint8_t*)malloc(len);
 reply.Read(Mb, len); // Server's public key
 BIGNUM* bn_Mb = BN_bin2bn(Mb, len, NULL);
 free(Mb);
 
 // Generate our 'public' key (Ma)
 if (DH_generate_key(pDH))
 {
 // Compute the shared key value, K (sesion key)
 uint8_t* K = (uint8_t*)malloc(DH_size(pDH));
 int keySize = DH_compute_key(K, bn_Mb, pDH);
 
 // Calculate the MD5 hash of K
 EVP_MD_CTX mdctx;
 uint8_t K_md5[EVP_MAX_MD_SIZE];
 uint32_t md_len;
 EVP_MD_CTX_init(&mdctx);
 EVP_DigestInit_ex(&mdctx, EVP_md5(), NULL);
 EVP_DigestUpdate(&mdctx, K, keySize);
 EVP_DigestFinal_ex(&mdctx, K_md5, &md_len);
 EVP_MD_CTX_cleanup(&mdctx);    
 
 // Generate nonce bytes
 uint8_t nonce[16];
 RAND_bytes(nonce, 16);
 
 // Encrypt nonce using CAST 128 CBC cipher and key K_md5
 EVP_CIPHER_CTX ctx;
 EVP_CIPHER_CTX_init(&ctx);
 EVP_EncryptInit_ex(&ctx, EVP_cast5_cbc(), NULL, K_md5, C2SIV);
 EVP_CIPHER_CTX_set_key_length(&ctx, md_len);
 uint8_t* ai = (uint8_t*)malloc(len + 16);
 int ai_len = 0;
 int ret = EVP_EncryptUpdate(&ctx, ai, &ai_len, nonce, 16);
 int padLen = 0;
 ret = EVP_EncryptFinal_ex(&ctx, ai + ai_len, &padLen);
 //ai_len += padLen;
 EVP_CIPHER_CTX_cleanup(&ctx);
 
 // Write Sequence Packet 3 (Key Exchange)
 uint8_t* Ma = (uint8_t*)malloc(len);
 ret = BN_bn2bin(pDH->pub_key, Ma);
 pkt.Init(DSICommand, GetNewRequestId(), 4 + len + ai_len);
 pkt.Write((uint8_t)FPLoginCont);
 pkt.Write((uint16_t)id);
 pkt.Write(Ma, len);
 pkt.Write(ai, ai_len);
 free(Ma);
 free(ai);
 
 SendReceiveAFPPacket(pkt, reply, *this, err);
 if (err == (uint32_t)kFPAuthContinue)
 {
 
 }
 
 free(K);
 }
 DH_free(pDH); // Also frees component BIGNUMs
 free(Mb);
 
 return true;  
 }
 */