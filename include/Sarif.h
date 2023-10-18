#ifndef GENERATE_SARIF
#define GENERATE_SARIF

#include "json_hpp/json.hpp"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <fstream>
#include <utility>

#if __has_include(<filesystem>)
#include <filesystem>
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
#else
error "Missing the <filesystem> header."
#endif

using namespace llvm;

/// Use BugReport for collecting all necessary data for result.
class BugReport {
public:
  SmallVector<std::pair<std::string, SmallVector<unsigned>>> Trace;
  StringRef RuleId;
  int RuleIndex;

  BugReport(const SmallVector<std::pair<std::string, SmallVector<unsigned>>> &Trace,
            StringRef RuleId, int RuleIndex)
      : Trace(Trace), RuleId(RuleId), RuleIndex(RuleIndex) {}
};

class Sarif {
private:
  using Key = std::string;
  using Value = std::string;

  const Key Version = "version";
  const Key Runs = "runs";
  const Key Tool = "tool";
  const Key Driver = "driver";
  const Key ToolName = "name";
  const Key Rules = "rules";
  const Key Results = "results";
  const Key RuleId = "id";
  const Key Text = "text";
  const Key ShortDescription = "shortDescription";
  const Key Uri = "helpUri";
  const Key FileUri = "uri";
  const Key PhysicalLocation = "physicalLocation";
  const Key ArtifactLocation = "artifactLocation";
  const Key RuleID = "ruleId";
  const Key RuleIndex = "ruleIndex";
  const Key FunctionName = "functionName";
  const Key Locations = "locations";
  const Key Location = "location";
  const Key Message = "message";
  const Key Filepath = "filePath";
  const Key Schema = "$schema";
  const Key CodeFlows = "codeFlows";
  const Key ThreadFlows = "threadFlows";
  const Key Region = "region";
  const Key StartLine = "startLine";
  const Key StartColumn = "startColumn";
  const Key InformationUri = "informationUri";

  const Value SchemaURI = "https://json.schemastore.org/sarif-2.1.0";
  const Value VersionValue = "2.1.0";
  const Value TextMsgBO = "report if the line may occur to a buffer overflow error";
  const Value TextMsgML = "report if the line may occur to a memory leak error";
  const Value TextMsgUAF = "report if the line may occur to a use after free";
  const Value UriAddrBO = "https://www.acunetix.com/blog/"
                          "web-security-zone/what-is-buffer-overflow/";
  const Value UriAddrML = "https://aticleworld.com/what-is-memory-leak-in-c-c-how-can-we-avoid/";
  const Value UriAddrUAF = "https://encyclopedia.kaspersky.com/glossary/use-after-free/";

  const char *FileName = "report.sarif";
  nlohmann::json GenSarif;

public:
  Sarif();
  void addResult(const BugReport &Result);
  void save();
};
#endif // GENERATE_SARIF
