#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <tree_sitter/api.h>

#include "sourceread-file.h"
#include "sourceread-treesitter.h"

extern const TSLanguage* tree_sitter_java(void);

/* ========================================================================== */
/*                              数据结构定义                                   */
/* ========================================================================== */

/* 单个 Java 方法元数据定义 */
typedef struct {
  char* class_name;   /* 类名，如 OrderServiceImpl */
  char* method_name;  /* 方法名，如 createOrder */
  char* full_name;    /* 组合全名，如 OrderServiceImpl#createOrder */
  char* code;         /* 方法源码完整文本 */
} sr_java_method_meta_t;

/* 项目全局方法仓库 */
typedef struct {
  sr_java_method_meta_t* items;
  size_t count;
  size_t capacity;
} sr_method_repo_t;

/* 已访问节点与边记录（防死循环与重复输出） */
#define MAX_EDGES 4096
#define MAX_VISITED 1024

typedef struct {
  char from[128];
  char to[128];
} sr_call_edge_t;

typedef struct {
  sr_call_edge_t edges[MAX_EDGES];
  size_t edge_count;

  char visited[MAX_VISITED][128];
  size_t visited_count;
} sr_call_graph_t;

/* ========================================================================== */
/*                           组件与 Bean 判别逻辑                             */
/* ========================================================================== */

static char*
sr_get_enclosing_class_name(TSNode method_node, const char* source_code)
{
  TSNode parent = ts_node_parent(method_node);
  while (!ts_node_is_null(parent)) {
    const char* type = ts_node_type(parent);
    if (strcmp(type, "class_declaration") == 0 ||
        strcmp(type, "interface_declaration") == 0 ||
        strcmp(type, "enum_declaration") == 0 ||
        strcmp(type, "record_declaration") == 0) {
      TSNode class_name_node = ts_node_child_by_field_name(parent, "name", 4);
      if (!ts_node_is_null(class_name_node)) {
        return sr_node_text(class_name_node, source_code);
      }
      return strdup("AnonymousClass");
    }
    parent = ts_node_parent(parent);
  }
  return strdup("TopLevel");
}

/* 判断是否是 Spring 业务/数据组件 (Mapper, Dao, Service, Repository 等) */
static bool
sr_is_component_keyword(const char* name)
{
  if (!name) return false;
  char lower[128] = {0};
  for (int i = 0; name[i] && i < 127; i++) {
    lower[i] = tolower((unsigned char)name[i]);
  }

  return (strstr(lower, "mapper") != NULL ||
          strstr(lower, "dao") != NULL ||
          strstr(lower, "repository") != NULL ||
          strstr(lower, "service") != NULL ||
          strstr(lower, "manager") != NULL ||
          strstr(lower, "client") != NULL);
}

/* 判断方法名形态是否符合 JavaBean 属性访问规范 */
static bool
sr_looks_like_property_accessor(const char* mname)
{
  if (!mname) return false;
  size_t len = strlen(mname);

  /* getXxx / setXxx */
  if (len > 3 && (strncmp(mname, "get", 3) == 0 || strncmp(mname, "set", 3) == 0)) {
    return isupper((unsigned char)mname[3]) != 0;
  }
  /* isXxx (boolean 属性) */
  if (len > 2 && strncmp(mname, "is", 2) == 0) {
    return isupper((unsigned char)mname[2]) != 0;
  }
  return false;
}

/* 过滤常见 JDK 与通用工具函数噪声 */
static bool
sr_is_builtin_or_ignored(const char* mname)
{
  if (!mname) return true;

  const char* ignored[] = {
    "equals", "hashCode", "toString", "getClass", "clone",
    "add", "put", "remove", "size", "isEmpty",
    "println", "print", "format", "builder", "build", "of",
    "orElse", "orElseGet", "orElseThrow", "map", "filter"
  };
  for (size_t i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++) {
    if (strcmp(mname, ignored[i]) == 0) return true;
  }
  return false;
}

/* 综合判断是否属于 JavaBean 的普通属性 getter/setter */
static bool
sr_is_bean_getter_or_setter(const char* callee_obj, const char* callee_method, const sr_java_method_meta_t* target)
{
  /* 1. 如果调用形态根本不像 getter/setter，直接放行 */
  if (!sr_looks_like_property_accessor(callee_method)) {
    return false;
  }

  /* 2. 【核心区分】如果调用者对象名像 Mapper/DAO/Service，绝对不是 Bean，必须保留！ */
  if (callee_obj && sr_is_component_keyword(callee_obj)) {
    return false;
  }

  /* 3. 如果已经匹配到了目标类 */
  if (target) {
    /* 目标类是 Mapper/Service 等组件，保留！ */
    if (sr_is_component_keyword(target->class_name)) {
      return false;
    }

    /* 目标类是典型实体类，或者是简单属性赋值/返回函数，判定为 Bean getter/setter */
    char lower_cls[128] = {0};
    for (int i = 0; target->class_name[i] && i < 127; i++) {
      lower_cls[i] = tolower((unsigned char)target->class_name[i]);
    }
    if (strstr(lower_cls, "entity") || strstr(lower_cls, "dto") ||
        strstr(lower_cls, "vo") || strstr(lower_cls, "model") ||
        strstr(lower_cls, "do") || strstr(lower_cls, "po") ||
        strstr(lower_cls, "bean") || strstr(lower_cls, "req") ||
        strstr(lower_cls, "resp")) {
      return true;
    }
  }

  /* 4. 未匹配到目标（如 Lombok 自动生成的 get/set）且不是组件调用的，视为 Bean */
  return true;
}

/* ========================================================================== */
/*              Phase 1: 扫描并收录工程全部方法到 Method Repo                    */
/* ========================================================================== */

static void
sr_collect_methods_visitor(TSNode node, const char* nodetype, const char* source_code, sr_method_repo_t* repo)
{
  if (ts_node_is_null(node)) return;

  const char* type = ts_node_type(node);
  if (strcmp(type, nodetype) == 0 || strcmp(type, "constructor_declaration") == 0)
  {
    TSNode method_name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(method_name_node))
    {
      char* mname = sr_node_text(method_name_node, source_code);
      char* cname = sr_get_enclosing_class_name(node, source_code);
      char* code = sr_node_text(node, source_code);

      if (mname && cname && code)
      {
        if (repo->count >= repo->capacity) {
          repo->capacity = (repo->capacity == 0) ? 64 : repo->capacity * 2;
          repo->items = (sr_java_method_meta_t*)realloc(repo->items, repo->capacity * sizeof(sr_java_method_meta_t));
        }

        size_t full_len = strlen(cname) + 1 + strlen(mname) + 1;
        char* full_name = (char*)malloc(full_len);
        snprintf(full_name, full_len, "%s#%s", cname, mname);

        repo->items[repo->count].class_name = cname;
        repo->items[repo->count].method_name = mname;
        repo->items[repo->count].full_name = full_name;
        repo->items[repo->count].code = code;
        repo->count++;
      } else {
        free(mname);
        free(cname);
        free(code);
      }
    }
  }

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    sr_collect_methods_visitor(ts_node_child(node, i), nodetype, source_code, repo);
  }
}

static void
sr_collect_file_handler(TSParser* parser, const char* nodetype, const char* filepath, void* userdata)
{
  sr_method_repo_t* repo = (sr_method_repo_t*)userdata;
  char* source_code = sr_read_file(filepath);
  if (!source_code) return;

  TSTree* tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
  if (tree) {
    TSNode root = ts_tree_root_node(tree);
    sr_collect_methods_visitor(root, nodetype, source_code, repo);
    ts_tree_delete(tree);
  }
  free(source_code);
}

/* ========================================================================== */
/*              Phase 2: 递归解析方法调用链 (Call Graph Tracing)                */
/* ========================================================================== */

/* 在全局方法库中精准/模糊匹配被调用的方法 */
static const sr_java_method_meta_t*
sr_resolve_callee(const sr_method_repo_t* repo, const char* callee_obj, const char* callee_method)
{
  /* 优先匹配：对象变量名与类名匹配（如 userMapper -> UserMapper#getById） */
  if (callee_obj) {
    for (size_t i = 0; i < repo->count; i++) {
      if (strcmp(repo->items[i].method_name, callee_method) == 0) {
        char lower_obj[64] = {0}, lower_cls[64] = {0};
        for (int k = 0; callee_obj[k] && k < 63; k++) lower_obj[k] = tolower((unsigned char)callee_obj[k]);
        for (int k = 0; repo->items[i].class_name[k] && k < 63; k++) lower_cls[k] = tolower((unsigned char)repo->items[i].class_name[k]);

        if (strstr(lower_cls, lower_obj) != NULL) {
          return &repo->items[i];
        }
      }
    }
  }

  /* 次优匹配：工程中同名方法 */
  for (size_t i = 0; i < repo->count; i++) {
    if (strcmp(repo->items[i].method_name, callee_method) == 0) {
      return &repo->items[i];
    }
  }

  return NULL;
}

static bool
sr_is_visited(const sr_call_graph_t* graph, const char* full_name)
{
  for (size_t i = 0; i < graph->visited_count; i++) {
    if (strcmp(graph->visited[i], full_name) == 0) return true;
  }
  return false;
}

static void
sr_add_edge(sr_call_graph_t* graph, const char* from, const char* to)
{
  for (size_t i = 0; i < graph->edge_count; i++) {
    if (strcmp(graph->edges[i].from, from) == 0 && strcmp(graph->edges[i].to, to) == 0) {
      return;
    }
  }
  if (graph->edge_count < MAX_EDGES) {
    strncpy(graph->edges[graph->edge_count].from, from, 127);
    graph->edges[graph->edge_count].from[127] = '\0';
    strncpy(graph->edges[graph->edge_count].to, to, 127);
    graph->edges[graph->edge_count].to[127] = '\0';
    graph->edge_count++;
  }
}

static void
sr_trace_calls(TSParser* parser,
               const sr_method_repo_t* repo,
               const sr_java_method_meta_t* current,
               sr_call_graph_t* graph);

static void
sr_find_invocations_dfs(TSNode node,
                        const char* source_code,
                        TSParser* parser,
                        const sr_method_repo_t* repo,
                        const sr_java_method_meta_t* current,
                        sr_call_graph_t* graph)
{
  if (ts_node_is_null(node)) return;

  const char* type = ts_node_type(node);

  /* 捕获 Java 函数调用表达式 (method_invocation) */
  if (strcmp(type, "method_invocation") == 0)
  {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    TSNode obj_node = ts_node_child_by_field_name(node, "object", 6);

    if (!ts_node_is_null(name_node)) {
      char* callee_name = sr_node_text(name_node, source_code);
      char* callee_obj = !ts_node_is_null(obj_node) ? sr_node_text(obj_node, source_code) : NULL;

      if (callee_name && !sr_is_builtin_or_ignored(callee_name))
      {
        const sr_java_method_meta_t* target = sr_resolve_callee(repo, callee_obj, callee_name);

        /* 【关键过滤】智能排除 Bean 的 getter/setter，但保留 Mapper/Service 的调用 */
        if (!sr_is_bean_getter_or_setter(callee_obj, callee_name, target))
        {
          if (target && strcmp(target->full_name, current->full_name) != 0)
          {
            sr_add_edge(graph, current->full_name, target->full_name);
            sr_trace_calls(parser, repo, target, graph);
          }
        }
      }

      free(callee_name);
      free(callee_obj);
    }
  }

  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; i++) {
    sr_find_invocations_dfs(ts_node_child(node, i), source_code, parser, repo, current, graph);
  }
}

static void
sr_trace_calls(TSParser* parser,
               const sr_method_repo_t* repo,
               const sr_java_method_meta_t* current,
               sr_call_graph_t* graph)
{
  if (sr_is_visited(graph, current->full_name)) return;

  if (graph->visited_count < MAX_VISITED) {
    strncpy(graph->visited[graph->visited_count], current->full_name, 127);
    graph->visited[graph->visited_count][127] = '\0';
    graph->visited_count++;
  }

  TSTree* tree = ts_parser_parse_string(parser, NULL, current->code, strlen(current->code));
  if (tree) {
    TSNode root = ts_tree_root_node(tree);
    sr_find_invocations_dfs(root, current->code, parser, repo, current, graph);
    ts_tree_delete(tree);
  }
}

/* ========================================================================== */
/*              Phase 3: 渲染并输出 Graphviz DOT 脚本                           */
/* ========================================================================== */

void
sr_generate_controller_callgraph_dot(const char* proj_path, const char* target_controller_method)
{
  TSParser* parser = ts_parser_new();
  if (!ts_parser_set_language(parser, tree_sitter_java())) {
    fprintf(stderr, "Failed to load Java grammar\n");
    ts_parser_delete(parser);
    return;
  }

  /* 1. 扫描整个项目的所有方法到 repo */
  sr_method_repo_t repo = {0};
  sr_walk_dir(proj_path, ".java", parser, "method_declaration", sr_collect_file_handler, &repo);

  /* 2. 找到指定的 Controller 入口方法 */
  const sr_java_method_meta_t* root_method = NULL;
  for (size_t i = 0; i < repo.count; i++) {
    if (strcmp(repo.items[i].full_name, target_controller_method) == 0) {
      root_method = &repo.items[i];
      break;
    }
  }

  if (!root_method) {
    fprintf(stderr, "[Error] Method '%s' not found in project!\n", target_controller_method);
    goto cleanup;
  }

  /* 3. 递归构建调用栈有向图 */
  sr_call_graph_t graph = {0};
  sr_trace_calls(parser, &repo, root_method, &graph);

  /* 4. 打印 DOT 语言输出 */
  printf("digraph CallGraph {\n");
  printf("  rankdir = LR;\n");
  printf("  node [shape = box, style = \"rounded,filled\", fillcolor = \"#f1f5f9\", fontname = \"Menlo,Consolas,Courier\"];\n");
  printf("  edge [color = \"#475569\", arrowhead = vee, fontname = \"Menlo,Consolas,Courier\"];\n\n");

  /* 高亮突出显示 Controller 根节点 */
  printf("  /* Root Controller Method (Entrance) */\n");
  printf("  \"%s\" [fillcolor = \"#bae6fd\", color = \"#0284c7\", penwidth = 2.0, style = \"rounded,filled,bold\"];\n\n",
         target_controller_method);

  printf("  /* Call Stack Edges */\n");
  for (size_t i = 0; i < graph.edge_count; i++) {
    printf("  \"%s\" -> \"%s\";\n", graph.edges[i].from, graph.edges[i].to);
  }
  printf("}\n");

cleanup:
  for (size_t i = 0; i < repo.count; i++) {
    free(repo.items[i].class_name);
    free(repo.items[i].method_name);
    free(repo.items[i].full_name);
    free(repo.items[i].code);
  }
  free(repo.items);
  ts_parser_delete(parser);
}

/* ========================================================================== */
/*                                   main                                     */
/* ========================================================================== */

int main(void)
{
  const char* project_dir = "/Users/christian/export/local/works/koron.com/abpms/03.Development/abpms-java";
  
  /* 指定你要分析的 Controller 方法: "类名#方法名" */
  const char* controller_method = "ScenarioBillController#generate";

  sr_generate_controller_callgraph_dot(project_dir, controller_method);
  return 0;
}