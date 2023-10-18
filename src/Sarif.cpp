#include "Sarif.h"

/// Add common information for all report files.
Sarif::Sarif() {
  GenSarif = {
      {Runs,
       {{{Tool,
          {{Driver,
            {{ToolName, "name"},
             {InformationUri, "https://www..."},
             {Version, "1.1.0"},
             {Rules,
              {{{RuleId, "buffer-overflow"},
                {ShortDescription, {{Text, TextMsgBO}}},
                {Uri, UriAddrBO}},
               {{RuleId, "memory-leak"}, {ShortDescription, {{Text, TextMsgML}}}, {Uri, UriAddrML}},
               {{RuleId, "use-after-free"},
                {ShortDescription, {{Text, TextMsgUAF}}},
                {Uri, UriAddrUAF}}}}}}}},
         {Results, nlohmann::json::array()}}}},
      {Schema, SchemaURI},
      {Version, VersionValue}};
}

void Sarif::addResult(const BugReport &Result) {

  SmallVector<nlohmann::json> LocationTrace;
  for (auto TraceElem : Result.Trace) {
    LocationTrace.push_back({{Location,
                              {{Message, {{Text, "Additional information about location."}}},
                               {PhysicalLocation,
                                {{ArtifactLocation, {{FileUri, TraceElem.first}}},
                                 {Region, {{StartLine, TraceElem.second[0]}}}}}}}});
  }
  GenSarif[Runs.data()][0][Results.data()].push_back(
      {{CodeFlows, {{{ThreadFlows, {{{Locations, LocationTrace}}}}}}},
       {RuleID, Result.RuleId},
       {RuleIndex, Result.RuleIndex},
       {Message, {{Text, "Report message."}}},
       {Locations,
        {{{PhysicalLocation,
           LocationTrace.pop_back_val().at({Location}).at({PhysicalLocation})}}}}});
}

void Sarif::save() {
  std::ofstream FileSarif(FileName);
  FileSarif << GenSarif.dump(2);
}
