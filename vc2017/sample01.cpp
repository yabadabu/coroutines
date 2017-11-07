#define _CRT_SECURE_NO_WARNINGS
#include "json/json.hpp"
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <vector>
#include <map>
#include "coroutines/coroutines.h"

extern void dbg(const char *fmt, ...);

// ---------------------------------------------------------
using nlohmann::json;
using Coroutines::TChannel;

typedef std::vector< TChannel* > VChannels;

// ---------------------------------------------------------
struct TFlowNode {
  VChannels ins;
  VChannels outs;
  virtual bool run() = 0;
  virtual void read(const json& j) { }
};

struct TIntGenerator : public TFlowNode {
  // Runtime params
  int curr = 0;
  int num_generated = 0;

  // Config params
  int start = 0;
  int step = 1;
  int count = 5;

  bool run() override {
    assert(outs.size() == 1);
    assert(ins.size() == 0);
    dbg("TIntGenerator::writing int %d (%d/%d)\n", curr, num_generated, count );
    if (!push(outs[0], curr))
      return false;
    dbg("TIntGenerator::done\n");
    curr += step;
    num_generated++;
    return num_generated < count;
  }

  void read(const json& j) override { 
    start = j.value("start", 0);
    count = j.value("count", 5);
    step = j.value("step", 1);
    curr = start;
    num_generated = 0;
  }
};

struct TConsoleOutput : public TFlowNode {
  int nints_read = 0;
  bool run() override {
    assert(outs.size() == 0);
    assert(ins.size() == 1);
    int next_id = 0;
    bool is_ok = pull(ins[0], next_id);
    if( is_ok ) {
      dbg("[%2d] read int is %d\n", nints_read, next_id);
      ++nints_read;
    }
    return is_ok;
  }
};

// ---------------------------------------------------------
struct TNode {
  std::string id;
  std::string type;
  TFlowNode*  flow_node = nullptr;
  Coroutines::THandle co;
};
struct TConfig;

// -----------------------------------------------
struct TLink {
  TNode* src = nullptr;
  TNode* dst = nullptr;
  int    max_elems = 0;           // default
  static TConfig* curr_layout;
};

// -----------------------------------------------
typedef std::map< std::string, TNode* > MNodes;
typedef std::vector< TNode > VNodes;
typedef std::vector< TLink > VLinks;

// -----------------------------------------------
struct TConfig {
  VNodes all_nodes;
  MNodes nodes_by_id;
  VLinks all_links;
  bool read();
  bool build();
  void run();
};
TConfig* TLink::curr_layout = nullptr;

// -----------------------------------------------
void to_json(json& j, const TNode& p) {
  j = json{ { "id", p.id, "type", p.type } };
}

void from_json(const json& j, TNode& p) {
  p.id = j.at("id").get<std::string>();
  p.type = j.at("type").get<std::string>();

  if (p.type == "int_producer")
    p.flow_node = new TIntGenerator();
  else if (p.type == "console_output")
    p.flow_node = new TConsoleOutput();
  else {
    dbg("Invalid node type %s", p.type.c_str());
  }
  p.flow_node->read(j);
}

void to_json(json& j, const TLink& p) {
  j = json{ { "src", p.src->id, "dst", p.dst->id, "max_elems", p.max_elems } };
}

void from_json(const json& j, TLink& p) {
  assert(TLink::curr_layout);
  auto it_src = TLink::curr_layout->nodes_by_id.find(j.at("src").get<std::string>());
  auto it_dst = TLink::curr_layout->nodes_by_id.find(j.at("dst").get<std::string>());
  p.src = it_src->second;
  p.dst = it_dst->second;
  p.max_elems = j.at("max_elems").get<int>();
}

void to_json(json& j, const TConfig& p) {
}

void from_json(const json& j, TConfig& p) {
  p.all_nodes = j.at("nodes").get<VNodes>();
  for (auto& n : p.all_nodes)
    p.nodes_by_id[n.id] = &n;
  TLink::curr_layout = &p;
  p.all_links = j.at("links").get<VLinks>();
  TLink::curr_layout = nullptr;
}

// -----------------------------------------------
bool TConfig::read( ) {

  // read json
  std::ifstream ifs("sample01.json");
  if (!ifs.is_open())
    return false;
  json j;
  ifs >> j;

  // parse
  *this = j;

  return true;
}

// --------------------------------------------------
bool TConfig::build() {

  // Create logic nodes
  for (auto& n : all_nodes) {
  }

  // Link nodes creating a channel for each link between nodes
  for (auto& lnk : all_links) {
    int n = lnk.max_elems > 0 ? lnk.max_elems : 5;
    TChannel* c = new TChannel(lnk.max_elems, sizeof(int));
    lnk.src->flow_node->outs.push_back(c);
    lnk.dst->flow_node->ins.push_back(c);
  }

  // Create a coroutine to run on each node
  for (auto& it : all_nodes) {
    TNode* node = &it;
    node->co = Coroutines::start([node]() {
      TNode* n = node;
      assert(n->flow_node);
      while (n->flow_node->run()) {
      }
    });
  }
  return true;
}

void TConfig::run() {
  while (true) {
    Coroutines::updateCurrentTime(1);
    if (!Coroutines::executeActives())
      break;
  }
  dbg("all done\n");
}

void test_app2() {
  TConfig cfg;
  if (!cfg.read())
    return;
  if (!cfg.build())
    return;
  cfg.run();
}
