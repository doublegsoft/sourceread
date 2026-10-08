/*
**                                                         __
**    _________  __  _______________  ________  ____ _____/ /
**   / ___/ __ \/ / / / ___/ ___/ _ \/ ___/ _ \/ __ `/ __  / 
**  (__  ) /_/ / /_/ / /  / /__/  __/ /  /  __/ /_/ / /_/ /  
** /____/\____/\__,_/_/   \___/\___/_/   \___/\__,_/\__,_/                                                          
*/
#include <dirent.h>
#include <sys/stat.h>

#include "sourceread.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

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

void
sr_print_ast(TSNode node, const char* node_type, const char* source_code, int indent)
{
  if (ts_node_is_null(node)) return;

  const char* type = ts_node_type(node);
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  uint32_t child_count = ts_node_child_count(node);

  if (strcmp(type, node_type) == 0)
  {
    printf("%.*s\n", (int)(end - start), source_code + start);
  }
  else 
  {
    for (uint32_t i = 0; i < child_count; i++) 
    {
      TSNode child = ts_node_child(node, i);
      sr_print_ast(child, node_type, source_code, indent + 1);
    }
  }
}

int
sr_has_suffix(const char* str, const char* suffix)
{
  if (!str || !suffix) return 0;

  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);

  if (str_len < suffix_len) return 0;

  return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

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

char*
sr_node_text(TSNode node, const char* source_code)
{
  if (ts_node_is_null(node) || !source_code)
  {
    return NULL;
  }

  uint32_t start = ts_node_start_byte(node);
  uint32_t end   = ts_node_end_byte(node);

  if (end < start)
  {
    return NULL;
  }

  size_t len = end - start;
  char* text = (char*)malloc(len + 1);
  if (!text)
  {
    perror("malloc failed");
    return NULL;
  }

  memcpy(text, source_code + start, len);
  text[len] = '\0';

  return text;
}

void
sr_extract_methods(TSNode node, const char* source_code, const char* method_node_type)
{
  if (ts_node_is_null(node) || !source_code || !method_node_type)
  {
    return;
  }

  const char* type = ts_node_type(node);

  // 1. 匹配传入的目标节点类型
  if (strcmp(type, method_node_type) == 0)
  {
    // 尝试提取方法名：
    // 大部分语言使用 "name" 字段（如 Java、Python、Go、Rust），
    // C/C++ 等语言通常使用 "declarator" 字段。
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node))
    {
      name_node = ts_node_child_by_field_name(node, "declarator", 10);
    }

    char* method_name = sr_node_text(name_node, source_code);
    char* method_body = sr_node_text(node, source_code);
    if (method_body == NULL || strlen(method_body) == 0)
      return;

    printf("========================================\n");
    printf("[Type]: %s\n", method_node_type);
    printf("[Name]: %s\n", method_name ? method_name : "<anonymous>");
    printf("[Source Code]:\n%s\n", method_body ? method_body : "");
    printf("========================================\n\n");

    free(method_name);
    free(method_body);

    // 命中目标方法后，若不需要继续提取其内部嵌套的方法/闭包，直接返回；
    // 若需要解析内部嵌套函数，去掉 return 即可。
    return;
  }

  // 2. 递归遍历子节点
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++)
  {
    TSNode child = ts_node_child(node, i);
    sr_extract_methods(child, source_code, method_node_type);
  }
}