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
#ifndef SOURCEREAD_ERROR_H
#define SOURCEREAD_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                错误码枚举定义 (XX 模块 - YY 对象 - ZZ 明细)                 */
/* ========================================================================== */

typedef enum {
  SR_SUCCESS                          = 0,

  /* ======================================================================== */
  /* [10] 通用/系统模块 (COMM)                                                */
  /* ======================================================================== */
  /* 10-01: Param 对象 */
  SR_ERR_COMM_PARAM_NULL              = 100101, /* 传入了必填的 NULL 空指针 */
  SR_ERR_COMM_PARAM_INVALID           = 100102, /* 参数格式非法或超出有效范围 */

  /* 10-02: Memory 对象 */
  SR_ERR_COMM_MEM_ALLOC               = 100201, /* malloc/calloc 内存分配失败 */
  SR_ERR_COMM_MEM_REALLOC             = 100202, /* realloc 动态扩容失败 */

  /* ======================================================================== */
  /* [20] 文件系统模块 (FILE)                                                 */
  /* ======================================================================== */
  /* 20-01: FileHandler 对象 */
  SR_ERR_FILE_HANDLER_OPEN            = 200101, /* 文件无法打开 (权限不足或不存在) */
  SR_ERR_FILE_HANDLER_READ            = 200102, /* 文件读取错误 (fread 字节不匹配) */
  SR_ERR_FILE_HANDLER_WRITE           = 200103, /* 磁盘写入失败 (fwrite 异常/空间不足) */
  SR_ERR_FILE_HANDLER_SEEK            = 200104, /* 文件偏移定位失败 (fseek 错误) */

  /* 20-02: Dir 对象 */
  SR_ERR_FILE_DIR_OPEN                = 200201, /* 无法打开或扫描指定代码目录 */

  /* ======================================================================== */
  /* [30] 语法分析模块 (PARSER / Tree-Sitter)                                 */
  /* ======================================================================== */
  /* 30-01: TreeSitter 引擎对象 */
  SR_ERR_PARSER_TS_INIT               = 300101, /* Tree-sitter 解析器初始化失败 */
  SR_ERR_PARSER_TS_PARSE              = 300102, /* AST 抽象语法树解析失败/损坏 */

  /* 30-02: Node 节点对象 */
  SR_ERR_PARSER_NODE_NULL             = 300201, /* AST 目标节点为 null */
  SR_ERR_PARSER_NODE_TYPE_MISMATCH    = 300202, /* 语法节点类型不匹配目标模式 */

  /* 30-03: Symbol 符号对象 */
  SR_ERR_PARSER_SYM_NOT_IDENTIFIER    = 300301, /* 未能穿透解包定位到 identifier */
  SR_ERR_PARSER_SYM_EXTRACT_EMPTY     = 300302, /* 提取出来的符号名称长度为 0 */

  /* ======================================================================== */
  /* [40] 索引与持久化模块 (INDEX)                                             */
  /* ======================================================================== */
  /* 40-01: Header 索引头对象 */
  SR_ERR_INDEX_HDR_MAGIC_MISMATCH     = 400101, /* 魔数不匹配 (非 "SRIX" 索引文件) */
  SR_ERR_INDEX_HDR_VERSION_UNSUPPORT  = 400102, /* 索引版本不兼容 */
  SR_ERR_INDEX_HDR_CORRUPTED          = 400103, /* 头部段偏移量异常越界 */

  /* 40-02: Dict 字典对象 */
  SR_ERR_INDEX_DICT_SORT              = 400201, /* 字典项排序过程异常 */
  SR_ERR_INDEX_DICT_READ              = 400202, /* 字典记录读取失败 */

  /* 40-03: Postings 倒排链对象 */
  SR_ERR_INDEX_POSTS_CORRUPTED        = 400301, /* 倒排链物理偏移越界或数据损坏 */

  /* 40-04: Doc 文档实体对象 */
  SR_ERR_INDEX_DOC_OFFSET_OVERFLOW    = 400401, /* 文档跳跃表记录超出物理范围 */
  SR_ERR_INDEX_DOC_NAME_OVERFLOW      = 400402, /* 文档名称超出接收缓冲区上限 */
  SR_ERR_INDEX_DOC_RANGE_INVALID      = 400403, /* 数据字节区间非法 (start_byte > end_byte) */

  /* ======================================================================== */
  /* [50] 检索与查询模块 (SEARCH)                                             */
  /* ======================================================================== */
  /* 50-01: Query 查询词对象 */
  SR_ERR_SEARCH_QUERY_TOO_SHORT       = 500101, /* 查询词过短 (Trigram 必须 >= 3 字符) */
  SR_ERR_SEARCH_QUERY_EMPTY           = 500102, /* 搜索关键词为空 */

  /* 50-02: Engine 检索执行引擎对象 */
  SR_ERR_SEARCH_ENGINE_NO_INTERSECT   = 500201, /* 倒排链交集为空 (AND 运算无重合) */
  SR_ERR_SEARCH_ENGINE_GRAM_NOT_FOUND = 500202, /* 单个三元组在字典中完全不存在 */

  /* 50-03: Match 匹配结果对象 */
  SR_ERR_SEARCH_MATCH_NOT_FOUND       = 500301  /* 未找到符合条件的符号/源码记录 */

} sr_errno_t;

/* ========================================================================== */
/*                           单头文件内联实现函数                             */
/* ========================================================================== */

/*!
** 将系统错误码转换为人类可读的格式化说明文本。
**
** 使用 static inline 修饰，保证头文件被多个编译单元包含时不会产生符号重定义错误。
**
** @param err 错误码枚举值
** @return    只读的静态错误描述英文字符串
*/
static inline const char*
sr_error_str(sr_errno_t err)
{
  switch (err)
  {
    case SR_SUCCESS:                          return "Success";

    /* [10] 通用模块 */
    case SR_ERR_COMM_PARAM_NULL:              return "[COMM-PARAM] Parameter pointer is NULL";
    case SR_ERR_COMM_PARAM_INVALID:           return "[COMM-PARAM] Invalid argument value";
    case SR_ERR_COMM_MEM_ALLOC:               return "[COMM-MEM] Memory allocation failed";
    case SR_ERR_COMM_MEM_REALLOC:             return "[COMM-MEM] Memory reallocation failed";

    /* [20] 文件系统 */
    case SR_ERR_FILE_HANDLER_OPEN:            return "[FILE-HANDLER] Failed to open file";
    case SR_ERR_FILE_HANDLER_READ:            return "[FILE-HANDLER] Failed to read expected bytes from file";
    case SR_ERR_FILE_HANDLER_WRITE:           return "[FILE-HANDLER] Failed to write data to file";
    case SR_ERR_FILE_HANDLER_SEEK:            return "[FILE-HANDLER] Failed to seek to file offset";
    case SR_ERR_FILE_DIR_OPEN:                return "[FILE-DIR] Failed to open directory";

    /* [30] 语法分析 */
    case SR_ERR_PARSER_TS_INIT:               return "[PARSER-TS] Tree-sitter engine initialization failed";
    case SR_ERR_PARSER_TS_PARSE:              return "[PARSER-TS] Tree-sitter failed to parse source code";
    case SR_ERR_PARSER_NODE_NULL:             return "[PARSER-NODE] AST node is null";
    case SR_ERR_PARSER_NODE_TYPE_MISMATCH:    return "[PARSER-NODE] AST node type mismatch";
    case SR_ERR_PARSER_SYM_NOT_IDENTIFIER:    return "[PARSER-SYM] Cannot unpack node into an identifier";
    case SR_ERR_PARSER_SYM_EXTRACT_EMPTY:     return "[PARSER-SYM] Extracted symbol name is empty";

    /* [40] 索引管理 */
    case SR_ERR_INDEX_HDR_MAGIC_MISMATCH:     return "[INDEX-HDR] Magic mismatch, invalid SRIX file";
    case SR_ERR_INDEX_HDR_VERSION_UNSUPPORT:  return "[INDEX-HDR] Unsupported index version";
    case SR_ERR_INDEX_HDR_CORRUPTED:          return "[INDEX-HDR] Header segment offsets corrupted";
    case SR_ERR_INDEX_DICT_SORT:              return "[INDEX-DICT] Dictionary sorting failed";
    case SR_ERR_INDEX_DICT_READ:              return "[INDEX-DICT] Failed to read dictionary record";
    case SR_ERR_INDEX_POSTS_CORRUPTED:        return "[INDEX-POSTS] Postings arena corrupted or out of range";
    case SR_ERR_INDEX_DOC_OFFSET_OVERFLOW:    return "[INDEX-DOC] Document offset exceeds file size";
    case SR_ERR_INDEX_DOC_NAME_OVERFLOW:      return "[INDEX-DOC] Document name exceeds buffer size";
    case SR_ERR_INDEX_DOC_RANGE_INVALID:      return "[INDEX-DOC] Invalid snippet byte boundary";

    /* [50] 检索引擎 */
    case SR_ERR_SEARCH_QUERY_TOO_SHORT:       return "[SEARCH-QUERY] Query pattern too short (< 3 chars)";
    case SR_ERR_SEARCH_QUERY_EMPTY:           return "[SEARCH-QUERY] Query pattern is empty";
    case SR_ERR_SEARCH_ENGINE_NO_INTERSECT:   return "[SEARCH-ENGINE] Postings intersection resulted in zero docs";
    case SR_ERR_SEARCH_ENGINE_GRAM_NOT_FOUND: return "[SEARCH-ENGINE] Trigram key not found in dictionary";
    case SR_ERR_SEARCH_MATCH_NOT_FOUND:       return "[SEARCH-MATCH] Target pattern not found";

    default:                                  return "Unknown error code";
  }
}

#ifdef __cplusplus
}
#endif

#endif /* SOURCEREAD_ERROR_H */