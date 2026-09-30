/*
 * The editor protocol every plugin speaks, without a host: ni::editor over a
 * recording Port, the part of ni::WebPlugin a test can link.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ni/Editor.h"

#include <clocale>
#include <string>
#include <utility>
#include <vector>

using namespace ni::editor;

namespace {

struct Recorder final : Port
{
  std::vector<double> defaults = {0.0, 7.0 / 12.0, 1.0, 0.008};
  std::vector<std::pair<int, std::string>> sent;
  std::vector<std::string> log;
  std::vector<std::pair<int, std::string>> typed;
  int height = 600;

  int PortParamCount() const override { return int(defaults.size()); }
  double PortDefault(int idx) const override { return defaults[size_t(idx)]; }
  void PortSend(int tag, const char* data, int size) override
  {
    sent.emplace_back(tag, std::string(data, size_t(size)));
    log.push_back("send " + std::to_string(tag));
  }
  void PortSendValues() override { log.push_back("values"); }
  void PortSendDisplay(int idx) override { log.push_back("display " + std::to_string(idx)); }
  void PortSetFromText(int idx, const char* text) override { typed.emplace_back(idx, text); }
  int PortHeight() const override { return height; }
  void PortResize(int h) override
  {
    height = h;
    log.push_back("resize " + std::to_string(h));
  }
  void PortReady() override { log.push_back("product"); }
};

} // namespace

TEST_CASE("the shell's tags sit past every product's")
{
  for (int t : {int(kGround), int(kDefaults), int(kReady), int(kSetText), int(kHeight)})
    CHECK(t >= 112);
  CHECK(kMaxJSString == 65536);
}

TEST_CASE("ready sends the defaults, every value, every display string, then the product's")
{
  Recorder r;
  REQUIRE(Handle(r, kReady, ""));
  const std::vector<std::string> want = {"send 113", "values",    "display 0", "display 1",
                                         "display 2", "display 3", "product"};
  CHECK(r.log == want);
  REQUIRE(r.sent.size() == 1);
  CHECK(r.sent[0].first == kDefaults);
  CHECK(r.sent[0].second == "0:0.583333333:1:0.008");
}

TEST_CASE("the defaults are a point-decimal list whatever the locale")
{
  Recorder r;
  const char* got = std::setlocale(LC_NUMERIC, "de_DE.UTF-8");
  CHECK(EncodeDefaults(r) == "0:0.583333333:1:0.008");
  CHECK(EncodeGround(0.734f) == "0.734");
  if (got)
    std::setlocale(LC_NUMERIC, "C");
}

TEST_CASE("a plugin with no parameters sends empty defaults and still its own state")
{
  Recorder r;
  r.defaults.clear();
  SendAll(r);
  REQUIRE(r.sent.size() == 1);
  CHECK(r.sent[0].second.empty());
  CHECK(r.log.back() == "product");
}

TEST_CASE("typed text reaches the parameter it names, and the readout follows")
{
  Recorder r;
  REQUIRE(Handle(r, kSetText, "2:40 ms"));
  REQUIRE(r.typed.size() == 1);
  CHECK(r.typed[0] == std::make_pair(2, std::string("40 ms")));
  CHECK(r.log.back() == "display 2");
  /* A colon inside the text stays in the text. */
  Handle(r, kSetText, "1:-24.0 dB : loud");
  CHECK(r.typed.back().second == "-24.0 dB : loud");
}

TEST_CASE("typed text for no parameter is dropped, not applied somewhere else")
{
  Recorder r;
  for (const char* bad : {"4:1", "-1:1", "x:1", "1", "", " 1:2", "1.0:2"})
  {
    CAPTURE(bad);
    CHECK(Handle(r, kSetText, bad));
  }
  CHECK(r.typed.empty());
  CHECK(r.log.empty());
}

TEST_CASE("the editor's height is honoured when plausible and new")
{
  Recorder r;
  CHECK(Handle(r, kHeight, "700"));
  CHECK(r.height == 700);
  CHECK(Handle(r, kHeight, "700"));
  CHECK(r.log.size() == 1);
  for (const char* bad : {"40000", "90", "", "abc", "700.5"})
    CHECK(Handle(r, kHeight, bad));
  CHECK(r.height == 700);
  CHECK(r.log.size() == 1);
}

TEST_CASE("a product's own tag is not the shell's")
{
  Recorder r;
  CHECK_FALSE(Handle(r, 64, "x"));
  CHECK_FALSE(Handle(r, 96, "x"));
  CHECK_FALSE(Handle(r, kGround, "x"));
  CHECK(r.log.empty());
}
