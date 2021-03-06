#pragma once
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

#include "../include/libxafp.h"

#include "AFPProto.h"
#include "AFPClient.h"
#include "SessionManager.h"

#define MAX_SERVER_NAME 32
#define MAX_SERVER_TYPE 32
#define MAX_USER_NAME 512 // Up to 255 Unicode chars
#define MAX_PASSWORD 16

struct _client_context
{
  char* server_dns_name; // Could also be IP-address string
  unsigned int port;
  char* username;
  char* password;

  CAFPSession* session;
};

struct _context_pool
{
  CSessionManager* manager;
};

struct _fs_node_iterator
{
  CAFPNodeList* list;
  CAFPNodeList::Iterator* iter;
};

int xafp_get_volume(xafp_client_handle hnd, const char* pVolumeName);
