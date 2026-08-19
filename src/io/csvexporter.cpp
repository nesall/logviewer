#include "csvexporter.h"

bool io::CsvExporter::write(
  const std::string &path, 
  const core::LogSession &session, 
  const CsvExportOptions &options, 
  std::string &errorOut, 
  std::atomic<float> *progress, 
  std::atomic<bool> *stopRequested)
{
  if (session.rowCount() == 0) {
    errorOut = "Session contains no data rows.";
    return false;
  }

  std::ofstream file(path);
  if (!file.is_open()) {
    errorOut = "Could not open file for writing: " + path;
    return false;
  }

  // 1. Resolve channels
  std::vector<const core::Channel *> exportChannels;
  if (options.selectedChannelsOnly && !options.targetChannelNames.empty()) {
    if (const auto *timeCh = session.findChannel("Time")) {
      exportChannels.push_back(timeCh);
    }
    for (const auto &name : options.targetChannelNames) {
      if (name == "Time") continue;
      if (const auto *ch = session.findChannel(name)) {
        exportChannels.push_back(ch);
      }
    }
  } else {
    for (const auto &ch : session.channels()) {
      exportChannels.push_back(&ch);
    }
  }

  if (exportChannels.empty()) {
    errorOut = "No valid channels selected for export.";
    return false;
  }

  // 2. Resolve crop rows
  size_t startRow = 0;
  size_t endRow = session.rowCount() - 1;
  if (options.cropOnly && session.cropRange().active) {
    startRow = session.cropStartIndex();
    endRow = session.cropEndIndex();
  }

  const char delim = options.delimiter;
  const size_t totalRows = (endRow >= startRow) ? (endRow - startRow + 1) : 0;
  if (totalRows == 0) {
    errorOut = "No rows within selected range.";
    return false;
  }

  // 3. Header lines
  for (size_t i = 0; i < exportChannels.size(); ++i) {
    file << exportChannels[i]->name();
    if (i + 1 < exportChannels.size()) file << delim;
  }
  file << "\n";

  if (options.includeUnitsRow) {
    for (size_t i = 0; i < exportChannels.size(); ++i) {
      file << exportChannels[i]->unit();
      if (i + 1 < exportChannels.size()) file << delim;
    }
    file << "\n";
  }

  // 4. Stream rows using a fast line buffer
  std::string lineBuf;
  lineBuf.reserve(1024);
  char valBuf[32];

  for (size_t r = startRow; r <= endRow; ++r) {
    if (stopRequested && stopRequested->load()) {
      file.close();
      errorOut = "Export stopped by user.";
      return true;
    }

    lineBuf.clear();
    for (size_t i = 0; i < exportChannels.size(); ++i) {
      const auto &vals = exportChannels[i]->values();
      if (r < vals.size()) {
        double v = vals[r];
        if (!std::isnan(v)) {
          int len = std::snprintf(valBuf, sizeof(valBuf), "%.4f", v);
          lineBuf.append(valBuf, len);
        }
      }
      if (i + 1 < exportChannels.size()) lineBuf.push_back(delim);
    }
    lineBuf.push_back('\n');
    file.write(lineBuf.data(), lineBuf.size());

    if (progress && (r % 1000 == 0)) {
      progress->store(static_cast<float>(r - startRow) / static_cast<float>(totalRows));
    }
  }

  if (progress) progress->store(1.0f);
  return true;
}
