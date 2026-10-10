/*
**                                                         __
**    _________  __  _______________  ________  ____ _____/ /
**   / ___/ __ \/ / / / ___/ ___/ _ \/ ___/ _ \/ __ `/ __  / 
**  (__  ) /_/ / /_/ / /  / /__/  __/ /  /  __/ /_/ / /_/ /  
** /____/\____/\__,_/_/   \___/\___/_/   \___/\__,_/\__,_/       
**
** Copyright [yyyy] [name of copyright owner]
** 
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
** 
**     http://www.apache.org/licenses/LICENSE-2.0
** 
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.                                                   
*/
#ifndef __SOURCEREAD_UTIL_H__
#define __SOURCEREAD_UTIL_H__

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t
sr_hash(const char key[3])
{
  uint32_t h = 5381;
  h = ((h << 5) + h) + (uint8_t)key[0];
  h = ((h << 5) + h) + (uint8_t)key[1];
  h = ((h << 5) + h) + (uint8_t)key[2];
  return h % HASH_BUCKET_SIZE;
}

/*!
** 计算完整字符串的 64 位 FNV-1a 哈希值（用于符号表精准匹配）。
*/
static inline uint64_t
sr_hash64(const char* str)
{
  uint64_t h = 14695981039346656037ULL;
  while (*str)
  {
    h ^= (uint8_t)*str++;
    h *= 1099511628211ULL;
  }
  return h;
}

#ifdef __cplusplus
}
#endif

#endif /* __SOURCEREAD_UTIL_H__ */