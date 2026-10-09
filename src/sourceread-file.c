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
#include <dirent.h>
#include <sys/stat.h>

#include "sourceread-file.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define INITIAL_BUFFER_CAPACITY 1024

/* ========================================================================== */
/*                                 data structure                             */
/* ========================================================================== */

typedef struct {
  char* data;
  size_t length;
  size_t capacity;
} sr_buffer_t;

/* ========================================================================== */
/*                                inline functions                            */
/* ========================================================================== */

/*!
** 创建并初始化一个动态字符串缓冲区结构。
**
** 分配缓冲区控制块及对应容量的字符数组，并默认写入空终止符 '\0'。
**
** @param initial_capacity 缓冲区初始分配的内存字节数。
** @return                 成功时返回新建的缓冲区指针；若堆内存分配失败则返回 NULL。
*/
static sr_buffer_t* 
sr_buffer_create(size_t initial_capacity) {
  sr_buffer_t* buf = (sr_buffer_t*)malloc(sizeof(sr_buffer_t));
  if (!buf) return NULL;
  
  buf->data = (char*)malloc(initial_capacity);
  if (!buf->data) {
    free(buf);
    return NULL;
  }
  buf->data[0] = '\0';
  buf->length = 0;
  buf->capacity = initial_capacity;
  return buf;
}

/*!
** 向动态缓冲区末尾追加单个字符，并保持字符串以 '\0' 闭合。
**
** 具备自动扩容机制：当剩余空间不足以容纳新增字符与终止符时，
** 触发倍增扩容策略 (capacity * 2)。
**
** @param buf  目标动态缓冲区指针。
** @param c    待追加的字符。
*/
static void 
sr_buffer_append(sr_buffer_t* buf, char c) {
  if (buf->length + 2 >= buf->capacity) {
    buf->capacity *= 2;
    char* new_data = (char*)realloc(buf->data, buf->capacity);
    if (!new_data) return;
    buf->data = new_data;
  }
  buf->data[buf->length++] = c;
  buf->data[buf->length] = '\0';
}

/*!
** 向动态缓冲区中追加指定数量的空格字符，用于代码层级缩进对齐。
**
** @param buf     目标动态缓冲区指针。
** @param indent  需要填充的空格总数量（绝对空格数）。
*/
static void 
sr_buffer_indent(sr_buffer_t* buf, int indent) 
{
  int total_spaces = indent;
  for (int i = 0; i < total_spaces; i++) {
    sr_buffer_append(buf, ' ');
  }
}

/*!
** 检查字符串是否以逗号分隔的多后缀列表中的任意一个结尾。
**
** 算法在原始只读字符串上以流式指针扫描推进，零堆内存分配 (Zero-Allocation)，
** 直接原地切片比对每个扩展名（例如：输入 ".c,.cpp,.hpp"）。
**
** @param str       待检测的目标字符串或文件路径（如 "main.cpp"）。
** @param suffixes  单字符串格式的逗号分隔后缀列表（如 ".c,.cpp,.hpp"）。
** @return          命中列表中任意一个后缀返回 1；全部不匹配或入参为空返回 0。
*/
int
sr_has_suffix(const char* str, const char* suffixes)
{
  if (!str || !suffixes) return 0;

  size_t str_len = strlen(str);
  const char* cur = suffixes;

  while (*cur != '\0')
  {
    /* 1. 寻找当前后缀的起始与结束位置（以 ',' 分隔） */
    const char* ext_start = cur;
    while (*cur != '\0' && *cur != ',')
    {
      cur++;
    }

    size_t ext_len = (size_t)(cur - ext_start);

    /* 2. 切片后缀长度有效时进行末尾比对 */
    if (ext_len > 0 && str_len >= ext_len)
    {
      if (strncmp(str + (str_len - ext_len), ext_start, ext_len) == 0)
      {
        return 1;
      }
    }

    /* 3. 跳过逗号分隔符继续比对下一个候选 */
    if (*cur == ',')
    {
      cur++;
    }
  }

  return 0;
}

/* ========================================================================== */
/*                                  sr_read_file                              */
/* ========================================================================== */

char*
sr_read_file(const char* filename)
{
  FILE* file = fopen(filename, "rb");
  if (!file)
  {
    perror("Failed to open file");
    return NULL;
  }

  // 1. 定位到文件末尾以获取文件大小
  if (fseek(file, 0, SEEK_END) != 0)
  {
    perror("fseek failed");
    fclose(file);
    return NULL;
  }
  
  long file_size = ftell(file);
  if (file_size < 0)
  {
    perror("ftell failed");
    fclose(file);
    return NULL;
  }
  
  // 2. 重新回到文件开头
  rewind(file);

  // 3. 为文件内容分配内存（加 1 用于存储字符串结束符 '\0'）
  char* content = (char*)malloc(file_size + 1);
  if (!content)
  {
    perror("Memory allocation failed");
    fclose(file);
    return NULL;
  }

  // 4. 将文件内容读取到缓冲区
  size_t bytes_read = fread(content, 1, file_size, file);
  content[bytes_read] = '\0'; // 确保字符串以 null 结尾

  // 5. 关闭文件并返回
  fclose(file);
  return content;
}

/* ========================================================================== */
/*                                 sr_format_code                             */
/* ========================================================================== */

char* 
sr_format_code(const char* src, int indent) 
{
  if (!src) return NULL;

  sr_buffer_t* buf = sr_buffer_create(INITIAL_BUFFER_CAPACITY);
  if (!buf) return NULL;

  int indent_level = 0;
  bool in_string = false;
  bool in_char = false;
  bool in_line_comment = false;
  bool in_block_comment = false;
  bool at_line_start = true;
  bool in_preprocessor = false;

  size_t len = strlen(src);

  for (size_t i = 0; i < len; i++) {
    char c = src[i];
    char next = (i + 1 < len) ? src[i + 1] : '\0';

    if ((in_string || in_char) && c == '\\') {
      sr_buffer_append(buf, c);
      if (next != '\0') {
        sr_buffer_append(buf, next);
        i++;
      }
      continue;
    }

    // Toggle String literals
    if (!in_line_comment && !in_block_comment && !in_char && c == '"') {
      in_string = !in_string;
      sr_buffer_append(buf, c);
      at_line_start = false;
      continue;
    }

    // Toggle Character literals
    if (!in_line_comment && !in_block_comment && !in_string && c == '\'') {
      in_char = !in_char;
      sr_buffer_append(buf, c);
      at_line_start = false;
      continue;
    }

    // Inside String/Character Literals: append verbatim
    if (in_string || in_char) {
      sr_buffer_append(buf, c);
      continue;
    }

    // Single-line comment start
    if (!in_block_comment && c == '/' && next == '/') {
      in_line_comment = true;
      if (at_line_start) {
        sr_buffer_indent(buf, indent_level * indent);
        at_line_start = false;
      }
      sr_buffer_append(buf, c);
      sr_buffer_append(buf, next);
      i++;
      continue;
    }

    // Multi-line comment start
    if (!in_line_comment && c == '/' && next == '*') {
      in_block_comment = true;
      if (at_line_start) {
        sr_buffer_indent(buf, indent_level * indent);
        at_line_start = false;
      }
      sr_buffer_append(buf, c);
      sr_buffer_append(buf, next);
      i++;
      continue;
    }

    // Multi-line comment end
    if (in_block_comment && c == '*' && next == '/') {
      in_block_comment = false;
      sr_buffer_append(buf, c);
      sr_buffer_append(buf, next);
      i++;
      continue;
    }

    // Inside any comment
    if (in_line_comment || in_block_comment) {
      sr_buffer_append(buf, c);
      if (c == '\n') {
        in_line_comment = false;
        at_line_start = true;
      }
      continue;
    }

    // Discard leading whitespace at the beginning of lines
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r')) {
      continue;
    }

    if (c == '\n') {
      sr_buffer_append(buf, '\n');
      at_line_start = true;
      in_preprocessor = false;
      continue;
    }

    // Preprocessor line (#include, #define)
    if (at_line_start && c == '#') {
      in_preprocessor = true;
      sr_buffer_append(buf, c);
      at_line_start = false;
      continue;
    }

    // Decrement indent BEFORE closing brace
    if (c == '}') {
      if (indent_level > 0) indent_level--;
      if (at_line_start) {
        sr_buffer_indent(buf, indent_level * indent);
        at_line_start = false;
      }
      sr_buffer_append(buf, c);

      // Wrap if not followed by semicolon, comma, or newline
      if (next != ';' && next != ',' && next != '\n' && next != '\0') {
        sr_buffer_append(buf, '\n');
        at_line_start = true;
      }
      continue;
    }

    // Apply indentation at line start
    if (at_line_start) {
      if (!in_preprocessor) {
        sr_buffer_indent(buf, indent_level * indent);
      }
      at_line_start = false;
    }

    sr_buffer_append(buf, c);

    // Increment indent AFTER opening brace and insert newline
    if (c == '{') {
      indent_level++;
      if (next != '\n') {
        sr_buffer_append(buf, '\n');
        at_line_start = true;
      }
      continue;
    }

    // Line break on statement semicolon
    if (c == ';') {
      if (next != '\n' && next != '\0') {
        sr_buffer_append(buf, '\n');
        at_line_start = true;
      }
      continue;
    }
  }

  // Extract the formatted string and cleanup container
  char* result = buf->data;
  free(buf);
  return result;
}

/* ========================================================================== */
/*                                  sr_walk_dir                               */
/* ========================================================================== */

void
sr_walk_dir(const char* dir_path,
            const char* ext,
            TSParser* parser,
            const char* nodetype,
            sr_file_handler_fn handler,
            void* userdata)
{
  DIR* dir = opendir(dir_path);
  if (!dir)
  {
    perror("Failed to open directory");
    return;
  }

  struct dirent* entry;
  char path_buffer[PATH_MAX];

  while ((entry = readdir(dir)) != NULL)
  {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir_path, entry->d_name);

    struct stat st;
    if (stat(path_buffer, &st) != 0)
      continue;

    if (S_ISDIR(st.st_mode))
      sr_walk_dir(path_buffer, ext, parser, nodetype, handler, userdata);
    else if (S_ISREG(st.st_mode))
    {
      // 若是常规文件，并且后缀匹配
      if (!ext || sr_has_suffix(entry->d_name, ext))
      {
        if (handler)
          handler(parser, nodetype, path_buffer, userdata);
      }
    }
  }
  closedir(dir);
}